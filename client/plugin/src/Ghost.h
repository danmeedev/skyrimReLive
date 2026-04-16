#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

#include <RE/Skyrim.h>

namespace relive::ghost {

    struct Snapshot {
        std::uint64_t server_tick = 0;
        std::uint64_t server_time_ms = 0;
        float x = 0;
        float y = 0;
        float z = 0;
        float yaw = 0;
    };

    // Pluggable spawner. Phase 1 ships VanillaCloneSpawner (clones a known
    // vanilla NPC base, e.g. Lydia). Future variants: RuntimeNpcSpawner
    // (TESDataHandler::CreateForm<TESNPC>()) and EspActorBaseSpawner (custom
    // ActorBase in a ships-with-plugin ESP).
    class Spawner {
    public:
        virtual ~Spawner() = default;
        virtual RE::NiPointer<RE::Actor> spawn_near_player() = 0;
        virtual void despawn(RE::NiPointer<RE::Actor>& actor) = 0;
    };

    [[nodiscard]] std::unique_ptr<Spawner> make_default_spawner();

    // Thread-safe bridge between the net-thread (producer, decoded snapshots)
    // and the main-thread (consumer, touches RE/Actor).
    class Manager {
    public:
        struct PlayerUpdate {
            std::uint32_t player_id;
            Snapshot snap;
        };

        Manager();
        ~Manager();
        Manager(const Manager&) = delete;
        Manager(Manager&&) = delete;
        Manager& operator=(const Manager&) = delete;
        Manager& operator=(Manager&&) = delete;

        // The plugin calls this once the Welcome lands so we know not to
        // spawn a ghost for ourselves in the crowd.
        void set_self_id(std::uint32_t id) noexcept;

        // Producer: called from the net thread on each WorldSnapshot.
        // The flatbuffer vector is decoded into PlayerUpdates and queued.
        void ingest(std::uint64_t server_tick, std::uint64_t server_time_ms,
                    std::span<const PlayerUpdate> players);

        // Demo hook: inject a synthetic snapshot for a single player_id as
        // if it came from the wire. Used by `rl demo` to validate ghost
        // rendering without needing a second client.
        void inject_synthetic(std::uint32_t player_id, float x, float y,
                              float z, float yaw);

        // Immediately despawn all synthetic ghosts (id >= kSyntheticIdBase).
        // Must be called on the main thread (via AddTask).
        void clear_synthetic_ghosts();

        // Consumer: called from the main thread (SKSE TaskInterface). Drains
        // the queue, spawns new ghosts, updates transforms, despawns stale.
        void tick_main_thread();

        [[nodiscard]] std::size_t ghost_count() const;

    private:
        struct StampedSnapshot {
            Snapshot snap;
            // Wall-clock arrival time on the main thread; used as the
            // interpolation time axis so we don't have to reason about
            // server/client clock skew for Phase 1.
            std::chrono::steady_clock::time_point arrived_at;
        };
        struct Ghost {
            RE::NiPointer<RE::Actor> actor;
            std::deque<StampedSnapshot> history;  // oldest first, capped
            std::uint64_t last_seen_tick = 0;
        };

        struct QueuedSnapshot {
            std::uint64_t server_tick;
            std::uint64_t server_time_ms;
            std::vector<PlayerUpdate> players;
        };

        // Demo mode: when active, a background thread in Plugin.cpp pumps
        // synthetic snapshots through `inject_synthetic`. Synthetic player
        // IDs are above this sentinel so they never collide with real ones.
        static constexpr std::uint32_t kSyntheticIdBase = 0xFFFF0000;

        std::atomic<std::uint32_t> self_id_{0};
        std::unique_ptr<Spawner> spawner_;

        // Net-thread ↔ main-thread handoff.
        mutable std::mutex queue_mu_;
        std::deque<QueuedSnapshot> pending_;

        // Main-thread only (ghosts_). last_applied_tick_ is atomic so
        // inject_synthetic (demo thread) can read it for tick alignment.
        std::unordered_map<std::uint32_t, Ghost> ghosts_;
        std::atomic<std::uint64_t> last_applied_tick_{0};
    };

    // One global manager per plugin instance. Access via this.
    Manager& instance();

}
