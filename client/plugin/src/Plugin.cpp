#include "PCH.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <SKSE/SKSE.h>
#include <SKSE/Logger.h>
#include <SKSE/Interfaces.h>

#include <RE/Skyrim.h>

#include "Cell.h"
#include "Combat.h"
#include "Commands.h"
#include "Config.h"
#include "Ghost.h"
#include "Net.h"
#include "Plugin.h"
#include "ZeusOverlay.h"

namespace {
    relive::net::Client g_client;
    std::atomic<relive::plugin::ConnState> g_state{relive::plugin::ConnState::Idle};
    std::atomic<bool> g_demo_active{false};

    // Last-attempted target so commands can report it in `rl status`.
    std::mutex g_target_mu;
    std::string g_target_host;
    std::uint16_t g_target_port = 0;

    // Zeus: latest player roster from server.
    std::mutex g_pl_mu;
    std::vector<relive::plugin::PlayerEntry> g_player_list;

    void Toast(const std::string& msg) {
        SKSE::log::info("{}", msg);
        // ConsoleLog is always available; press ~ to see. Proper HUD
        // notifications (top-left toast) need the Papyrus Debug.Notification
        // native — deferring to a follow-up until we have a Papyrus bridge.
        if (auto* log = RE::ConsoleLog::GetSingleton()) {
            log->Print("%s", msg.c_str());
        }
    }

    void GhostTickPump() {
        using namespace std::chrono_literals;
        while (g_state.load(std::memory_order_acquire) ==
               relive::plugin::ConnState::Connected) {
            if (auto* task = SKSE::GetTaskInterface()) {
                task->AddTask([]() { relive::ghost::instance().tick_main_thread(); });
            }
            std::this_thread::sleep_for(50ms);
        }
    }

    // Orbits a synthetic ghost around the local player every 3 seconds at
    // radius 150. Enough motion to visually verify that spawn, interp, and
    // despawn all work without a second connected client.
    void DemoOrbit() {
        using namespace std::chrono_literals;
        constexpr std::uint32_t kDemoId = 0xFFFF0001;
        constexpr float kRadius = 150.0F;
        constexpr float kTwoPi = 6.28318530717958647692F;
        constexpr float kPeriodSeconds = 3.0F;
        const auto start = std::chrono::steady_clock::now();

        while (g_demo_active.load(std::memory_order_acquire)) {
            const float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - start).count();
            const float phase = (kTwoPi / kPeriodSeconds) * elapsed;
            // Orbit around wherever the local player currently is.
            const auto center = relive::plugin::last_local_pos();
            const float x = center.x + kRadius * std::cos(phase);
            const float y = center.y + kRadius * std::sin(phase);
            // Face tangent to circle (yaw leading by +90 degrees).
            const float yaw = phase + (kTwoPi / 4.0F);
            // Pretend the demo ghost is running so it plays a run animation
            // instead of standing in idle pose. Speed 350 ~= player run speed.
            constexpr float kRunSpeed = 350.0F;
            relive::ghost::instance().inject_synthetic(
                kDemoId, x, y, center.z, yaw, kRunSpeed, /*is_running=*/true);
            std::this_thread::sleep_for(50ms);
        }
    }
}

namespace relive::plugin {

    ConnState state() noexcept { return g_state.load(std::memory_order_acquire); }
    std::uint32_t player_id() noexcept { return g_client.player_id(); }
    std::size_t ghost_count() noexcept { return ghost::instance().ghost_count(); }
    std::uint64_t snapshots_received() noexcept { return g_client.snapshots_received(); }
    std::uint64_t player_inputs_sent() noexcept { return g_client.player_inputs_sent(); }
    LocalPos last_local_pos() noexcept {
        return LocalPos{
            g_client.last_local_x(),
            g_client.last_local_y(),
            g_client.last_local_z(),
            g_client.last_local_yaw(),
        };
    }

    std::uint32_t current_cell_form_id() noexcept { return cell::instance().current(); }
    std::uint32_t target_cell_form_id() noexcept { return cell::instance().target(); }
    void set_target_cell_form_id(std::uint32_t form_id) noexcept {
        cell::instance().set_target(form_id);
    }

    bool demo_running() noexcept {
        return g_demo_active.load(std::memory_order_acquire);
    }

    std::string demo_start() {
        if (state() != ConnState::Connected) {
            return "not connected; run `rl connect` first so the ghost ticker is armed";
        }
        bool expected = false;
        if (!g_demo_active.compare_exchange_strong(expected, true)) {
            return "demo already running";
        }
        std::thread(DemoOrbit).detach();
        Toast("[SkyrimReLive] demo ghost orbiting (Lydia clone)");
        return "demo started";
    }

    std::string demo_stop() {
        const bool was = g_demo_active.exchange(false, std::memory_order_acq_rel);
        if (!was) return "demo not running";
        // Explicitly despawn on the main thread instead of waiting for
        // the 3-second staleness timer.
        if (auto* task = SKSE::GetTaskInterface()) {
            task->AddTask([]() { ghost::instance().clear_synthetic_ghosts(); });
        }
        Toast("[SkyrimReLive] demo ghost stopped");
        return "demo stopped";
    }

    std::string current_server() noexcept {
        const std::lock_guard lock(g_target_mu);
        if (g_target_port == 0) return "<none>";
        return g_target_host + ":" + std::to_string(g_target_port);
    }

    std::string start_connection(std::string host, std::uint16_t port,
                                 std::string name,
                                 const net::CharacterData& char_data) {
        auto expected = ConnState::Idle;
        if (!g_state.compare_exchange_strong(expected, ConnState::Connected)) {
            if (expected == ConnState::Connected) {
                return "already connected as player_id=" +
                       std::to_string(g_client.player_id());
            }
            // Failed state: clear it and retry.
            g_state.store(ConnState::Idle, std::memory_order_release);
            expected = ConnState::Idle;
            if (!g_state.compare_exchange_strong(expected, ConnState::Connected)) {
                return "busy (state transitioning)";
            }
        }

        {
            const std::lock_guard lock(g_target_mu);
            g_target_host = host;
            g_target_port = port;
        }

        SKSE::log::info("SkyrimReLive: connecting to {}:{} as {}", host, port, name);
        if (!g_client.start(host, port, name, char_data)) {
            g_state.store(ConnState::Failed, std::memory_order_release);
            Toast("[SkyrimReLive] connect failed");
            return "connect failed (see SkyrimReLive.log)";
        }
        ghost::instance().set_self_id(g_client.player_id());
        std::thread(GhostTickPump).detach();
        const auto id = g_client.player_id();
        Toast("[SkyrimReLive] connected as player_id=" + std::to_string(id));
        return "connected as player_id=" + std::to_string(id);
    }

    std::string stop_connection() {
        auto prev = g_state.exchange(ConnState::Idle, std::memory_order_acq_rel);
        if (prev != ConnState::Connected) {
            return "not connected";
        }
        g_client.stop();
        Toast("[SkyrimReLive] disconnected");
        return "disconnected";
    }

    std::vector<PlayerEntry> get_player_list() noexcept {
        const std::lock_guard lock(g_pl_mu);
        return g_player_list;
    }

    void update_player_list(std::vector<PlayerEntry> list) {
        const std::lock_guard lock(g_pl_mu);
        g_player_list = std::move(list);
    }

    void send_chat(std::string_view text) {
        if (g_state.load(std::memory_order_acquire) != ConnState::Connected) return;
        g_client.send_chat(text);
    }

    void send_admin_auth(std::string_view password) {
        if (g_state.load(std::memory_order_acquire) != ConnState::Connected) return;
        g_client.send_admin_auth(password);
    }

    void send_admin_command(std::string_view command) {
        if (g_state.load(std::memory_order_acquire) != ConnState::Connected) return;
        g_client.send_admin_command(command);
    }

    void send_combat_event(std::uint32_t target_player_id,
                           std::uint8_t attack_type, float weapon_reach,
                           float weapon_base_damage,
                           std::uint8_t attack_class) {
        if (g_state.load(std::memory_order_acquire) != ConnState::Connected) {
            return;
        }
        g_client.send_combat_event(target_player_id, attack_type, weapon_reach,
                                   weapon_base_damage, attack_class);
    }

    net::CharacterData gather_character_data() {
        net::CharacterData cd;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            SKSE::log::warn("gather_character_data: player singleton null");
            return cd;
        }
        if (!player->Is3DLoaded()) {
            SKSE::log::warn("gather_character_data: player 3D not loaded, using defaults");
            return cd;
        }
        if (auto* base = player->GetActorBase()) {
            if (const char* n = base->GetName(); n && n[0]) {
                cd.character_name = n;
            }
        }
        cd.level = static_cast<std::uint16_t>(player->GetLevel());
        // Skill reads can crash on very early saves where the actor value
        // owner isn't fully initialized. Gate on parentCell as a proxy for
        // "the world is actually loaded and the player is placed."
        if (!player->parentCell) {
            SKSE::log::warn("gather_character_data: no parentCell, skipping skills");
            return cd;
        }
        struct SkillPair { const char* name; RE::ActorValue av; };
        static constexpr SkillPair kSkills[] = {
            {"OneHanded",    RE::ActorValue::kOneHanded},
            {"TwoHanded",    RE::ActorValue::kTwoHanded},
            {"Archery",      RE::ActorValue::kArchery},
            {"Block",        RE::ActorValue::kBlock},
            {"Smithing",     RE::ActorValue::kSmithing},
            {"HeavyArmor",   RE::ActorValue::kHeavyArmor},
            {"LightArmor",   RE::ActorValue::kLightArmor},
            {"Pickpocket",   RE::ActorValue::kPickpocket},
            {"Lockpicking",  RE::ActorValue::kLockpicking},
            {"Sneak",        RE::ActorValue::kSneak},
            {"Alchemy",      RE::ActorValue::kAlchemy},
            {"Speech",       RE::ActorValue::kSpeech},
            {"Alteration",   RE::ActorValue::kAlteration},
            {"Conjuration",  RE::ActorValue::kConjuration},
            {"Destruction",  RE::ActorValue::kDestruction},
            {"Illusion",     RE::ActorValue::kIllusion},
            {"Restoration",  RE::ActorValue::kRestoration},
            {"Enchanting",   RE::ActorValue::kEnchanting},
        };
        std::vector<net::CharacterData::Skill> all;
        for (const auto& [sname, av] : kSkills) {
            all.push_back({sname, player->GetActorValue(av)});
        }
        std::sort(all.begin(), all.end(),
                  [](const auto& a, const auto& b) { return a.value > b.value; });
        if (all.size() > 3) all.resize(3);
        cd.top_skills = static_cast<decltype(all)&&>(all);
        SKSE::log::info("gather_character_data: char='{}' level={} skills={}",
                        cd.character_name, cd.level, cd.top_skills.size());
        return cd;
    }

    void on_world_loaded() {
        const auto cfg = config::load();
        cell::instance().set_target(cfg.target_cell_form_id);
        combat::register_sink();

        if (!cfg.auto_connect) {
            SKSE::log::info("SkyrimReLive: auto_connect=false; use `rl connect` in console");
            Toast("[SkyrimReLive] ready — type `rl connect` to join");
            return;
        }
        // Send defaults in Hello — character data is populated lazily once
        // the player is fully stable. Reading GetLevel/GetActorValue at
        // kPostLoadGame (even deferred by one frame) crashes on early saves
        // where the player's ActorValue arrays aren't allocated yet.
        std::thread([cfg]() {
            start_connection(cfg.server_host, cfg.server_port,
                             cfg.player_name);
        }).detach();
    }

}

namespace {
    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        if (!msg) return;
        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                SKSE::log::info(
                    "SkyrimReLive: data loaded; waiting for kPostLoadGame/kNewGame");
                relive::commands::register_console_command();
                relive::zeus_overlay::set_command_callback([](const char* cmd) {
                    // Auto-admin: authenticate silently if not already,
                    // then send the command.
                    if (g_state.load(std::memory_order_acquire) ==
                        relive::plugin::ConnState::Connected) {
                        g_client.send_admin_auth("");
                        g_client.send_admin_command(cmd);
                    }
                });
                relive::zeus_overlay::set_input_toggle([](bool active) {
                    auto* controls = RE::ControlMap::GetSingleton();
                    if (!controls) return;

                    bool free_cam = relive::zeus_overlay::is_free_cam();

                    if (active) {
                        if (free_cam) {
                            // Free cam: disable gameplay but keep movement + looking
                            controls->ToggleControls(RE::ControlMap::UEFlag::kFighting, false, true);
                            controls->ToggleControls(RE::ControlMap::UEFlag::kActivate, false, true);
                            controls->ToggleControls(RE::ControlMap::UEFlag::kSneaking, false, true);
                            // Enable free camera
                            auto* cam = RE::PlayerCamera::GetSingleton();
                            if (cam) cam->ToggleFreeCameraMode(false);
                            auto* player = RE::PlayerCharacter::GetSingleton();
                            if (player) player->SetGodMode(true);
                        } else {
                            controls->ToggleControls(RE::ControlMap::UEFlag::kAll, false, true);
                        }
                    } else {
                        controls->ToggleControls(RE::ControlMap::UEFlag::kAll, true, true);
                        // Disable free camera if it was on
                        auto* cam = RE::PlayerCamera::GetSingleton();
                        if (cam && cam->IsInFreeCameraMode()) {
                            cam->ToggleFreeCameraMode(false);
                        }
                        auto* player = RE::PlayerCharacter::GetSingleton();
                        if (player) player->SetGodMode(false);
                        relive::zeus_overlay::set_free_cam(false);
                    }
                    controls->AllowTextInput(active);
                    SKSE::log::info("zeus overlay: controls {} free_cam={}",
                                    active ? "disabled" : "re-enabled", free_cam);
                });
                relive::zeus_overlay::install_hooks();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
            case SKSE::MessagingInterface::kNewGame: {
                relive::plugin::on_world_loaded();
                // Build the form library for the Zeus spawn browser.
                // Deferred to a task so we don't block the load callback.
                // Defer form scan — kPostLoadGame is too early on some saves.
                // Use a detached thread with a short delay so the data
                // handler is fully populated before we iterate it.
                std::thread([]() {
                    std::this_thread::sleep_for(std::chrono::seconds(3));
                    if (auto* task = SKSE::GetTaskInterface()) {
                        task->AddTask([]() {
                            auto* dh = RE::TESDataHandler::GetSingleton();
                            if (!dh) {
                                SKSE::log::warn("form scan: data handler null");
                                return;
                            }
                            std::vector<relive::zeus_overlay::FormEntry> forms;
                            auto add = [&](const char* category, auto& arr) {
                                for (const auto* form : arr) {
                                    if (!form) continue;
                                    const char* name = nullptr;
                                    __try {
                                        name = form->GetName();
                                    } __except(1) {
                                        continue;
                                    }
                                    if (!name || !name[0]) continue;
                                    relive::zeus_overlay::FormEntry e{};
                                    e.form_id = form->GetFormID();
                                    strncpy(e.name, name, sizeof(e.name) - 1);
                                    strncpy(e.category, category,
                                            sizeof(e.category) - 1);
                                    forms.push_back(e);
                                }
                                SKSE::log::info("form scan: {} = {} entries",
                                                category, arr.size());
                            };
                            add("NPC", dh->GetFormArray<RE::TESNPC>());
                            add("Weapon", dh->GetFormArray<RE::TESObjectWEAP>());
                            add("Armor", dh->GetFormArray<RE::TESObjectARMO>());
                            add("Potion", dh->GetFormArray<RE::AlchemyItem>());
                            add("Misc", dh->GetFormArray<RE::TESObjectMISC>());
                            add("Ammo", dh->GetFormArray<RE::TESAmmo>());
                            add("Book", dh->GetFormArray<RE::TESObjectBOOK>());
                            add("Ingredient", dh->GetFormArray<RE::IngredientItem>());
                            add("Key", dh->GetFormArray<RE::TESKey>());
                            add("Scroll", dh->GetFormArray<RE::ScrollItem>());
                            add("Static", dh->GetFormArray<RE::TESObjectSTAT>());
                            add("Tree", dh->GetFormArray<RE::TESObjectTREE>());
                            add("Door", dh->GetFormArray<RE::TESObjectDOOR>());
                            add("Activator", dh->GetFormArray<RE::TESObjectACTI>());
                            add("Light", dh->GetFormArray<RE::TESObjectLIGH>());
                            add("Furniture", dh->GetFormArray<RE::TESFurniture>());
                            add("Flora", dh->GetFormArray<RE::TESFlora>());
                            add("MovableStatic", dh->GetFormArray<RE::BGSMovableStatic>());
                            SKSE::log::info("zeus form browser: indexed {} named forms",
                                            forms.size());
                            relive::zeus_overlay::push_form_library(
                                forms.data(),
                                static_cast<unsigned>(forms.size()));
                        });
                    }
                }).detach();
                // Pass the game's swap chain to the overlay for the Present
                // hook. Renderer is alive by kPostLoadGame.
                if (auto* renderer = RE::BSGraphics::Renderer::GetSingleton()) {
                    auto* rw = renderer->GetCurrentRenderWindow();
                    if (rw && rw->swapChain) {
                        SKSE::log::info("zeus overlay: passing swap chain to overlay");
                        relive::zeus_overlay::set_swap_chain(rw->swapChain);
                    } else {
                        SKSE::log::warn("zeus overlay: render window or swap chain is null");
                    }
                } else {
                    SKSE::log::warn("zeus overlay: renderer singleton is null");
                }
            }
                break;
            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::log::init();
    SKSE::log::info("SkyrimReLive plugin loaded");

    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        SKSE::log::error("SkyrimReLive: failed to register messaging listener");
        return false;
    }

    return true;
}
