#pragma once

#include <cstdint>
#include <string>

namespace relive::config {

    struct Config {
        std::string server_host = "127.0.0.1";
        std::uint16_t server_port = 27015;
        std::string player_name = "dovahkiin";
        // If true, connect automatically on save-load / new-game. If false,
        // user must run `rl connect` in the console.
        bool auto_connect = true;

        // Hex form ID of the cell where replication is active. 0 means
        // "any cell counts as active" (dev / solo test default). Nonzero
        // restricts Phase 1's one-cell design to a specific Whiterun cell
        // or similar.
        std::uint32_t target_cell_form_id = 0;
    };

    // Load from `<Skyrim>/Data/SKSE/Plugins/SkyrimReLive.toml`. Missing file
    // or parse error returns defaults and logs a warning — the plugin must
    // not hard-fail on a fresh install.
    Config load();

}
