#include "Config.h"

#include <filesystem>

#include <toml++/toml.hpp>

#include <SKSE/Logger.h>

namespace relive::config {

    namespace {
        // Skyrim's cwd when launched via skse64_loader.exe is the install
        // directory, so this relative path resolves to the same folder our
        // own DLL lives in.
        constexpr auto kConfigRelPath = "Data/SKSE/Plugins/SkyrimReLive.toml";
    }

    Config load() {
        Config cfg;

        std::filesystem::path path = kConfigRelPath;
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            SKSE::log::warn("config file not found at {}; using defaults",
                            path.string());
            return cfg;
        }

        try {
            auto tbl = toml::parse_file(path.string());
            if (auto v = tbl["server_host"].value<std::string>()) {
                cfg.server_host = *v;
            }
            if (auto v = tbl["server_port"].value<std::int64_t>()) {
                if (*v > 0 && *v < 65536) {
                    cfg.server_port = static_cast<std::uint16_t>(*v);
                } else {
                    SKSE::log::warn("server_port {} out of range; using default {}",
                                    *v, cfg.server_port);
                }
            }
            if (auto v = tbl["player_name"].value<std::string>()) {
                cfg.player_name = *v;
            }
            if (auto v = tbl["auto_connect"].value<bool>()) {
                cfg.auto_connect = *v;
            }
            // `target_cell` accepted as either a string hex literal
            // ("0x1A27A") or a bare integer (107658).
            if (auto v = tbl["target_cell"].value<std::string>()) {
                try {
                    cfg.target_cell_form_id = std::stoul(*v, nullptr, 0);
                } catch (...) {
                    SKSE::log::warn("target_cell {} unparseable; ignoring", *v);
                }
            } else if (auto v = tbl["target_cell"].value<std::int64_t>()) {
                cfg.target_cell_form_id = static_cast<std::uint32_t>(*v);
            }
            SKSE::log::info(
                "config loaded from {}: host={} port={} name={} auto={} target_cell=0x{:x}",
                path.string(), cfg.server_host, cfg.server_port,
                cfg.player_name, cfg.auto_connect, cfg.target_cell_form_id);
        } catch (const toml::parse_error& e) {
            SKSE::log::warn("config parse error ({}); using defaults",
                            e.what());
        }

        return cfg;
    }

}
