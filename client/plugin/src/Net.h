#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace relive::net {

    struct CharacterData {
        std::string character_name;
        std::uint16_t level = 1;
        struct Skill { std::string name; float value; };
        std::vector<Skill> top_skills;
    };

    // Phase 1 client. Owns one UDP socket and one background thread that:
    //   - sends Hello and waits for Welcome on start()
    //   - polls the local PlayerCharacter at 60 Hz, ships PlayerInput
    //   - drains incoming WorldSnapshot packets (counted, not yet rendered)
    //
    // Reading PlayerCharacter from a non-game thread is a known race; the
    // values are simple float fields and a torn read at worst causes one
    // frame of position jitter on remote clients. Phase 2 moves to a proper
    // main-thread hook when combat sync needs frame-perfect reads.
    class Client {
    public:
        Client() = default;
        ~Client();

        Client(const Client&) = delete;
        Client(Client&&) = delete;
        Client& operator=(const Client&) = delete;
        Client& operator=(Client&&) = delete;

        // Connect synchronously, then spin up the background tick loop.
        // Returns false if Hello/Welcome handshake fails; logs the reason.
        // char_data is gathered on the main thread before this call.
        bool start(std::string_view host, uint16_t port,
                   std::string_view player_name, const CharacterData& char_data);
        void stop();

        // Phase 2.3b: ship a CombatEvent for a hit the local player just
        // landed on a known ghost. Thread-safe with the tick loop because
        // the underlying socket send is one-shot. Drops silently when not
        // connected.
        void send_combat_event(uint32_t target_player_id, uint8_t attack_type,
                               float weapon_reach, float weapon_base_damage,
                               uint8_t attack_class = 0);

        void send_chat(std::string_view text);

        [[nodiscard]] uint32_t player_id() const noexcept { return player_id_; }
        [[nodiscard]] uint64_t snapshots_received() const noexcept {
            return snapshots_received_.load(std::memory_order_relaxed);
        }
        [[nodiscard]] uint64_t player_inputs_sent() const noexcept {
            return player_inputs_sent_.load(std::memory_order_relaxed);
        }
        // Last transform read from PlayerCharacter and shipped as PlayerInput.
        // Read with best-effort atomic load; four separate loads so this can
        // tear slightly, acceptable for diagnostic display.
        [[nodiscard]] float last_local_x() const noexcept { return last_x_.load(std::memory_order_relaxed); }
        [[nodiscard]] float last_local_y() const noexcept { return last_y_.load(std::memory_order_relaxed); }
        [[nodiscard]] float last_local_z() const noexcept { return last_z_.load(std::memory_order_relaxed); }
        [[nodiscard]] float last_local_yaw() const noexcept { return last_yaw_.load(std::memory_order_relaxed); }

    private:
        void run();
        bool send_hello(std::string_view name, const CharacterData& cd);
        bool wait_for_welcome();
        void send_player_input();
        void drain_incoming();

        // Opaque socket handle; backed by relive::sock::Handle in Socket.cpp.
        std::uintptr_t socket_ = ~std::uintptr_t{0};
        uint32_t player_id_ = 0;
        std::atomic<bool> running_{false};
        std::atomic<uint64_t> snapshots_received_{0};
        std::atomic<uint64_t> player_inputs_sent_{0};
        std::atomic<float> last_x_{0.0F};
        std::atomic<float> last_y_{0.0F};
        std::atomic<float> last_z_{0.0F};
        std::atomic<float> last_yaw_{0.0F};
        std::thread thread_;
    };

}
