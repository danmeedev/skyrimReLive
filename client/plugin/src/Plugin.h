#pragma once

// Pure plugin-control API. The SKSE console command layer (Commands.cpp)
// and any future UI (ImGui overlay, MCM menu, external IPC) call through
// these functions — this header deliberately exposes no SKSE or RE types
// so consumers can be unit-tested in isolation.

#include <cstdint>
#include <string>

namespace relive::plugin {

    enum class ConnState : std::uint8_t {
        Idle = 0,       // not yet connected
        Connected = 1,  // Hello accepted, ticker armed
        Failed = 2,     // Hello failed; remains until disconnect() clears
    };

    [[nodiscard]] ConnState state() noexcept;
    [[nodiscard]] std::uint32_t player_id() noexcept;
    [[nodiscard]] std::size_t ghost_count() noexcept;
    [[nodiscard]] std::uint64_t snapshots_received() noexcept;
    [[nodiscard]] std::uint64_t player_inputs_sent() noexcept;
    // Most recent transform read from the local PlayerCharacter; zero if we
    // haven't sampled yet (e.g., on main menu).
    struct LocalPos { float x, y, z, yaw; };
    [[nodiscard]] LocalPos last_local_pos() noexcept;

    // Current effective server target (last connect attempt, or config default).
    [[nodiscard]] std::string current_server() noexcept;

    // Connect to the given host/port as the given display name. Returns a
    // human-readable status string (for command output). Idempotent: if
    // already connected, returns "already connected as <id>".
    std::string start_connection(std::string host, std::uint16_t port,
                                 std::string name);

    // Disconnects if connected; no-op otherwise. Returns a status string.
    std::string stop_connection();

    // Called by the SKSE lifecycle layer on kPostLoadGame/kNewGame. Obeys
    // the `auto_connect` config flag.
    void on_world_loaded();

    // Cell-gating helpers used by the `rl cell` subcommand.
    [[nodiscard]] std::uint32_t current_cell_form_id() noexcept;
    [[nodiscard]] std::uint32_t target_cell_form_id() noexcept;
    void set_target_cell_form_id(std::uint32_t form_id) noexcept;

    // Demo ghost: spawns a synthetic remote player that orbits the local
    // player, used to validate ghost rendering without a second client.
    // Returns human-readable status.
    std::string demo_start();
    std::string demo_stop();
    [[nodiscard]] bool demo_running() noexcept;

    // Phase 2.3b: forward a CombatEvent through the active Net::Client.
    // No-op when not connected. Called from the TESHitEvent sink.
    void send_combat_event(std::uint32_t target_player_id,
                           std::uint8_t attack_type, float weapon_reach,
                           float weapon_base_damage);

}
