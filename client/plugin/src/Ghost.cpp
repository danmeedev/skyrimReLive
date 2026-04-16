#include "Ghost.h"

#include <SKSE/Logger.h>

#include "Cell.h"

namespace relive::ghost {

    namespace {
        // Lydia (0x000A2C94) is a well-known vanilla NPC that exists in every
        // install and isn't scripted into critical quests. Good stand-in for
        // a ghost template until a custom ActorBase lands.
        constexpr RE::FormID kTemplateFormID = 0x000A2C94;

        // A ghost is despawned locally if we haven't seen an update for it
        // in this many server ticks. At 60 Hz tick / 20 Hz broadcast,
        // 180 ticks ~ 3 s — well past the server's own timeout sweep.
        constexpr std::uint64_t kGhostStaleTicks = 180;

        // Snapshot history retention. Render target is 100 ms behind the
        // latest arrival, so we need at least two history entries bracketing
        // that time. 10 gives us 500 ms of runway for bad network jitter.
        constexpr std::size_t kHistoryMax = 10;

        // Maximum distance (Skyrim world units) at which we'll spawn or
        // keep a ghost. ~5000 units is roughly one exterior cell + slack.
        // Beyond this, SetPosition would warp the ghost across unloaded
        // cells, which crashes the engine. Phase 3's proper cell-aware
        // AoI replaces this; for Phase 2 it's a hard distance cap.
        constexpr float kMaxGhostDist = 5000.0F;

        // Client renders this far behind the most-recently-arrived snapshot
        // so we can always linearly interpolate between bracketing pairs
        // instead of extrapolating past the latest.
        constexpr auto kRenderDelay = std::chrono::milliseconds(100);

        class VanillaCloneSpawner final : public Spawner {
            static RE::TESNPC* resolve_base() {
                if (auto* b = RE::TESForm::LookupByID<RE::TESNPC>(kTemplateFormID)) {
                    return b;
                }
                if (auto* ref = RE::TESForm::LookupByID<RE::Actor>(kTemplateFormID)) {
                    if (auto* b = ref->GetActorBase()) return b;
                }
                if (auto* p = RE::PlayerCharacter::GetSingleton()) {
                    if (auto* b = p->GetActorBase()) {
                        SKSE::log::warn(
                            "ghost template 0x{:08x} unresolvable; using player base",
                            kTemplateFormID);
                        return b;
                    }
                }
                return nullptr;
            }

        public:
            RE::NiPointer<RE::Actor> spawn_near_player() override {
                auto* base = resolve_base();
                if (!base) {
                    SKSE::log::error("no ghost base form available");
                    return nullptr;
                }
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player || !player->parentCell) {
                    return nullptr;
                }
                auto ref = player->PlaceObjectAtMe(base, false);
                if (!ref) {
                    SKSE::log::error("PlaceObjectAtMe returned null");
                    return nullptr;
                }
                RE::NiPointer<RE::Actor> actor(static_cast<RE::Actor*>(ref.get()));
                if (!actor) {
                    return nullptr;
                }
                // PlaceObjectAtMe can leave the clone disabled/unloaded,
                // especially when cloning the player's own base. Explicitly
                // enable and force 3D load so the ghost is visible immediately.
                actor->Enable(false);
                actor->Load3D(false);
                // AI must stay on so the behavior-graph keeps processing
                // (EnableAI(false) turns it into a T-pose statue). But the
                // template's default AI package (Lydia's follow/sandbox)
                // keeps overwriting our Speed/forceRun writes every frame,
                // so lock the ghost into a do-nothing package first — the
                // package has no movement commands, leaving the locomotion
                // state machine free for us to drive.
                actor->InitiateDoNothingPackage();
                actor->EvaluatePackage(true, true);
                actor->AllowBleedoutDialogue(false);
                return actor;
            }

            void despawn(RE::NiPointer<RE::Actor>& actor) override {
                if (!actor) return;
                actor->Disable();
                actor->SetDelete(true);
                actor.reset();
            }
        };

        // Interpolate `a` -> `b` at fraction `t` in [0, 1], choosing the
        // shortest yaw arc. Yaw is the only angle Phase 1 replicates.
        Snapshot lerp_snap(const Snapshot& a, const Snapshot& b, float t) {
            const float lerp_f = [&]() {
                float d = b.yaw - a.yaw;
                constexpr float pi = 3.14159265358979323846F;
                while (d > pi)  d -= 2 * pi;
                while (d < -pi) d += 2 * pi;
                return a.yaw + d * t;
            }();
            return Snapshot{
                .server_tick = b.server_tick,
                .server_time_ms = b.server_time_ms,
                .x = a.x + (b.x - a.x) * t,
                .y = a.y + (b.y - a.y) * t,
                .z = a.z + (b.z - a.z) * t,
                .yaw = lerp_f,
            };
        }

    }

    std::unique_ptr<Spawner> make_default_spawner() {
        return std::make_unique<VanillaCloneSpawner>();
    }

    Manager::Manager() : spawner_(make_default_spawner()) {}
    Manager::~Manager() = default;

    void Manager::set_self_id(std::uint32_t id) noexcept {
        self_id_.store(id, std::memory_order_release);
    }

    void Manager::ingest(std::uint64_t server_tick, std::uint64_t server_time_ms,
                         std::span<const PlayerUpdate> players) {
        QueuedSnapshot qs;
        qs.server_tick = server_tick;
        qs.server_time_ms = server_time_ms;
        qs.players.assign(players.begin(), players.end());

        const std::lock_guard lock(queue_mu_);
        if (pending_.size() > 16) {
            pending_.pop_front();
        }
        pending_.emplace_back(std::move(qs));
    }

    void Manager::inject_synthetic(std::uint32_t player_id, float x, float y,
                                   float z, float yaw, float anim_speed,
                                   bool anim_is_running) {
        PlayerUpdate u;
        u.player_id = player_id;
        u.snap.x = x;
        u.snap.y = y;
        u.snap.z = z;
        u.snap.yaw = yaw;
        u.snap.speed = anim_speed;
        u.snap.is_running = anim_is_running;
        // Align synthetic tick with the real server tick so unsigned
        // staleness math (last_applied_tick_ - last_seen_tick) doesn't
        // underflow. last_applied_tick_ is atomic for this read.
        const auto tick = last_applied_tick_.load(std::memory_order_relaxed) + 1;
        const auto ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        u.snap.server_tick = tick;
        u.snap.server_time_ms = ms;
        ingest(tick, ms, std::span(&u, 1));
    }

    void Manager::clear_synthetic_ghosts() {
        for (auto it = ghosts_.begin(); it != ghosts_.end();) {
            if (it->first >= kSyntheticIdBase) {
                SKSE::log::info("despawning demo ghost player_id=0x{:x}", it->first);
                spawner_->despawn(it->second.actor);
                it = ghosts_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::size_t Manager::ghost_count() const {
        const std::lock_guard lock(queue_mu_);
        return ghosts_.size();
    }

    void Manager::tick_main_thread() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->parentCell) {
            return;
        }

        cell::instance().poll_main_thread();
        if (!cell::instance().is_active()) {
            return;
        }

        // Capture local player position once per tick for the per-ghost
        // distance gate (see kMaxGhostDist below).
        const auto local_pos = player->GetPosition();

        std::deque<QueuedSnapshot> local_pending;
        {
            const std::lock_guard lock(queue_mu_);
            local_pending.swap(pending_);
        }

        const auto self_id = self_id_.load(std::memory_order_acquire);
        const auto now = std::chrono::steady_clock::now();

        for (auto& qs : local_pending) {
            last_applied_tick_ = qs.server_tick;
            for (const auto& upd : qs.players) {
                if (upd.player_id == self_id) {
                    continue;
                }
                auto& g = ghosts_[upd.player_id];
                g.history.push_back({upd.snap, now});
                while (g.history.size() > kHistoryMax) {
                    g.history.pop_front();
                }
                g.last_seen_tick = qs.server_tick;
            }
        }

        if (last_applied_tick_ == 0) {
            return;
        }

        for (auto it = ghosts_.begin(); it != ghosts_.end();) {
            auto& [pid, g] = *it;
            const bool stale =
                last_applied_tick_ - g.last_seen_tick > kGhostStaleTicks;
            if (stale) {
                SKSE::log::info("despawning ghost player_id={}", pid);
                spawner_->despawn(g.actor);
                it = ghosts_.erase(it);
                continue;
            }
            if (g.history.empty()) {
                ++it;
                continue;
            }

            // Distance gate: if remote is far away (different cell or
            // wildly invalid position), skip spawn / despawn existing.
            // Prevents cross-cell SetPosition warps that crash the engine.
            const auto& latest_for_dist = g.history.back().snap;
            const float dx = latest_for_dist.x - local_pos.x;
            const float dy = latest_for_dist.y - local_pos.y;
            const float dz = latest_for_dist.z - local_pos.z;
            const float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq > kMaxGhostDist * kMaxGhostDist) {
                if (g.actor) {
                    SKSE::log::info(
                        "ghost player_id={} out of range ({:.0f}u); despawning",
                        pid, std::sqrt(dist_sq));
                    spawner_->despawn(g.actor);
                }
                ++it;
                continue;
            }

            if (!g.actor) {
                g.actor = spawner_->spawn_near_player();
                if (g.actor) {
                    SKSE::log::info("spawned ghost for player_id={}", pid);
                } else {
                    ++it;
                    continue;
                }
            }

            // Render-time interpolation. Target = now - 100 ms. Find
            // bracketing pair in history; lerp between them. If the target
            // is outside the history range, clamp to the nearest endpoint.
            const auto target = now - kRenderDelay;
            Snapshot render;
            if (g.history.size() == 1 || target >= g.history.back().arrived_at) {
                render = g.history.back().snap;
            } else if (target <= g.history.front().arrived_at) {
                render = g.history.front().snap;
            } else {
                render = g.history.back().snap;  // sentinel; replaced below
                for (std::size_t i = 1; i < g.history.size(); ++i) {
                    if (g.history[i].arrived_at >= target) {
                        const auto& s1 = g.history[i - 1];
                        const auto& s2 = g.history[i];
                        const double span_ms =
                            std::chrono::duration<double, std::milli>(
                                s2.arrived_at - s1.arrived_at)
                                .count();
                        const double elapsed_ms =
                            std::chrono::duration<double, std::milli>(
                                target - s1.arrived_at)
                                .count();
                        const float t = span_ms > 0.0
                            ? static_cast<float>(elapsed_ms / span_ms)
                            : 0.0F;
                        render = lerp_snap(s1.snap, s2.snap, t);
                        break;
                    }
                }
            }

            const RE::NiPoint3 pos{render.x, render.y, render.z};
            g.actor->SetPosition(pos, true);
            g.actor->SetAngle(RE::NiPoint3{0.0F, 0.0F, render.yaw});

            // Phase 2.1: drive locomotion via animation graph variables.
            // Latest snapshot values (no interp on bools); the graph state
            // machine reads these and transitions between idle/walk/run/
            // sneak. Best-effort — failures (e.g., variable not found) are
            // silent so missing-graph cases don't spam logs.
            const auto& latest = g.history.back().snap;
            g.actor->SetGraphVariableFloat("Speed", latest.speed);
            g.actor->SetGraphVariableFloat("Direction", latest.direction);
            g.actor->SetGraphVariableBool("IsRunning", latest.is_running);
            g.actor->SetGraphVariableBool("IsSprinting", latest.is_sprinting);
            g.actor->SetGraphVariableBool("IsSneaking", latest.is_sneaking);
            // Phase 2.2: weapon state. Bool transitions trigger draw/sheath
            // animations; iState pins the stance once drawn.
            g.actor->SetGraphVariableBool("IsEquipping", latest.is_equipping);
            g.actor->SetGraphVariableBool("IsUnequipping", latest.is_unequipping);
            g.actor->SetGraphVariableInt("iState", latest.weapon_state);
            // Belt-and-suspenders: stomp the ActorState bits the locomotion
            // graph samples. InitiateDoNothingPackage stops the AI from
            // fighting us, but we still need to tell the state machine what
            // gait to use. Speed > ~1 means "moving"; we pick walk vs run
            // from is_running, and force the movingForward bit so the graph
            // leaves the idle branch.
            auto* st = g.actor->AsActorState();
            if (st) {
                const bool moving = latest.speed > 1.0F;
                st->actorState1.movingForward = moving ? 1 : 0;
                st->actorState1.movingBack    = 0;
                st->actorState1.movingLeft    = 0;
                st->actorState1.movingRight   = 0;
                st->actorState1.walking  = (moving && !latest.is_running) ? 1 : 0;
                st->actorState1.running  = (moving && latest.is_running) ? 1 : 0;
                st->actorState1.sprinting = latest.is_sprinting ? 1 : 0;
                st->actorState1.sneaking = latest.is_sneaking ? 1 : 0;
                st->actorState2.forceRun = latest.is_running ? 1 : 0;
                st->actorState2.forceSneak = latest.is_sneaking ? 1 : 0;
            }
            ++it;
        }
    }

    Manager& instance() {
        static Manager m;
        return m;
    }

}
