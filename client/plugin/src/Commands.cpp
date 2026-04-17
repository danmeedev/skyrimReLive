#include "PCH.h"

#include "Commands.h"

#include <sstream>
#include <string>
#include <vector>

#include <RE/Skyrim.h>
#include <SKSE/Logger.h>

#include "Plugin.h"

namespace relive::commands {

    namespace {
        std::vector<std::string> split(std::string_view s) {
            std::vector<std::string> out;
            std::string cur;
            for (char c : s) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!cur.empty()) {
                        out.push_back(std::move(cur));
                        cur.clear();
                    }
                } else {
                    cur.push_back(c);
                }
            }
            if (!cur.empty()) out.push_back(std::move(cur));
            return out;
        }

        std::string fmt_state(plugin::ConnState s) {
            switch (s) {
                case plugin::ConnState::Idle:      return "idle";
                case plugin::ConnState::Connected: return "connected";
                case plugin::ConnState::Failed:    return "failed";
                default:                           return "?";
            }
        }

        std::string help() {
            return "SkyrimReLive console commands:\n"
                   "  rl status                  show connection + stats\n"
                   "  rl connect [host] [port]   connect (defaults to config)\n"
                   "  rl disconnect              close the connection\n"
                   "  rl cell                    show current + target cell\n"
                   "  rl cell set [hex]          pin target to current cell, or to hex form ID\n"
                   "  rl cell clear              target any cell\n"
                   "  rl players                 show connected players\n"
                   "  rl chat <message>          send a chat message to all players\n"
                   "  rl admin <password>        authenticate as server admin (Zeus)\n"
                   "  rl cmd <command>           send admin command (pvp on|off, kick <id>, help)\n"
                   "  rl demo start              spawn a synthetic orbiting ghost (solo test)\n"
                   "  rl demo stop               despawn the demo ghost\n"
                   "  rl help                    this message";
        }

        // Defined later in this file — forward declare for do_players.
        std::string fmt_hex(std::uint32_t v);

        std::string do_players() {
            const auto list = plugin::get_player_list();
            if (list.empty()) {
                return "no player list received yet (server broadcasts every ~60s)";
            }
            std::string r = std::to_string(list.size()) + " player(s):\n";
            for (const auto& e : list) {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "  [%u] %s (%s) Lv%u HP %.0f/%.0f",
                              e.player_id, e.character_name.c_str(),
                              e.display_name.c_str(),
                              static_cast<unsigned>(e.level),
                              e.hp, e.hp_max);
                r += buf;
                r += " cell=" + fmt_hex(e.cell_form_id);
                if (!e.top_skills.empty()) {
                    r += " skills:";
                    for (const auto& [sn, sl] : e.top_skills) {
                        char sbuf[64];
                        std::snprintf(sbuf, sizeof(sbuf), " %s(%.0f)", sn.c_str(), sl);
                        r += sbuf;
                    }
                }
                r += "\n";
            }
            return r;
        }

        std::string fmt_hex(std::uint32_t v) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%08X", v);
            return buf;
        }

        std::string do_cell(const std::vector<std::string>& args) {
            if (args.size() == 1) {
                const auto cur = plugin::current_cell_form_id();
                const auto tgt = plugin::target_cell_form_id();
                std::string r = "current=" + fmt_hex(cur);
                r += " target=" + (tgt == 0 ? std::string("any") : fmt_hex(tgt));
                r += " active=";
                r += (tgt == 0 ? (cur != 0) : (cur == tgt)) ? "yes" : "no";
                return r;
            }
            if (args[1] == "clear") {
                plugin::set_target_cell_form_id(0);
                return "target cleared (any cell active)";
            }
            if (args[1] == "set") {
                std::uint32_t new_tgt = 0;
                if (args.size() >= 3) {
                    try {
                        new_tgt = std::stoul(args[2], nullptr, 0);
                    } catch (...) {
                        return "bad hex form id; try e.g. 0x1A27A";
                    }
                } else {
                    new_tgt = plugin::current_cell_form_id();
                    if (new_tgt == 0) {
                        return "no current cell — load a save first";
                    }
                }
                plugin::set_target_cell_form_id(new_tgt);
                return "target set to " + fmt_hex(new_tgt);
            }
            return "usage: rl cell [set [hex] | clear]";
        }

        std::string status() {
            const auto s = plugin::state();
            std::string r = "state=" + fmt_state(s);
            r += " server=" + plugin::current_server();
            if (s == plugin::ConnState::Connected) {
                r += " player_id=" + std::to_string(plugin::player_id());
                r += " ghosts=" + std::to_string(plugin::ghost_count());
                r += " inputs_sent=" +
                     std::to_string(plugin::player_inputs_sent());
                r += " snapshots_received=" +
                     std::to_string(plugin::snapshots_received());
                const auto p = plugin::last_local_pos();
                char buf[96];
                std::snprintf(buf, sizeof(buf),
                              " pos=(%.1f, %.1f, %.1f) yaw=%.2f",
                              p.x, p.y, p.z, p.yaw);
                r += buf;
            }
            return r;
        }

        std::string do_connect(const std::vector<std::string>& args) {
            // Pull defaults from the current TOML so the user can change
            // config and reconnect without restarting.
            // Commands.cpp deliberately doesn't include Config.h to keep
            // the API surface small; Plugin::on_world_loaded() already
            // loads config. Here we parse optional overrides only.
            if (args.size() < 2) {
                // No args → kick off the normal auto-connect path, which
                // re-reads config for fresh values.
                plugin::on_world_loaded();
                return "triggered auto_connect flow (see status)";
            }
            const std::string host = args[1];
            std::uint16_t port = 27015;
            if (args.size() >= 3) {
                try {
                    const int parsed = std::stoi(args[2]);
                    if (parsed <= 0 || parsed > 65535) {
                        return "port out of range";
                    }
                    port = static_cast<std::uint16_t>(parsed);
                } catch (...) {
                    return "bad port";
                }
            }
            return plugin::start_connection(host, port, "dovahkiin");
        }

    }

    std::string execute(std::string_view cmdline) {
        const auto args = split(cmdline);
        if (args.empty() || args[0] == "help" || args[0] == "?") {
            return help();
        }
        if (args[0] == "status")      return status();
        if (args[0] == "connect")     return do_connect(args);
        if (args[0] == "disconnect")  return plugin::stop_connection();
        if (args[0] == "cell")        return do_cell(args);
        if (args[0] == "players")     return do_players();
        if (args[0] == "chat") {
            if (args.size() < 2) return "usage: rl chat <message>";
            // Rejoin everything after "chat" as the message text.
            auto sv = cmdline;
            const auto pos = sv.find("chat");
            if (pos != std::string_view::npos) {
                sv = sv.substr(pos + 4);
                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                    sv.remove_prefix(1);
            }
            if (sv.empty()) return "usage: rl chat <message>";
            plugin::send_chat(sv);
            return "";
        }
        if (args[0] == "admin") {
            const auto pw = args.size() >= 2 ? args[1] : "";
            plugin::send_admin_auth(pw);
            return "auth request sent";
        }
        if (args[0] == "cmd") {
            if (args.size() < 2) return "usage: rl cmd <command> (try: rl cmd help)";
            auto sv = cmdline;
            const auto pos = sv.find("cmd");
            if (pos != std::string_view::npos) {
                sv = sv.substr(pos + 3);
                while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
                    sv.remove_prefix(1);
            }
            if (sv.empty()) return "usage: rl cmd <command>";
            plugin::send_admin_command(sv);
            return "";
        }
        if (args[0] == "demo") {
            if (args.size() < 2 || args[1] == "status") {
                return plugin::demo_running() ? "demo running" : "demo idle";
            }
            if (args[1] == "start") return plugin::demo_start();
            if (args[1] == "stop")  return plugin::demo_stop();
            return "usage: rl demo [start|stop|status]";
        }
        return "unknown subcommand '" + args[0] + "'; try `rl help`";
    }

    namespace {
        // SCRIPT_FUNCTION::Execute_t signature per CommonLibSSE-NG.
        bool ConsoleExecute(
            const RE::SCRIPT_PARAMETER* /*paramInfo*/,
            RE::SCRIPT_FUNCTION::ScriptData* /*scriptData*/,
            RE::TESObjectREFR* /*thisObj*/,
            RE::TESObjectREFR* /*containingObj*/,
            RE::Script* scriptObj,
            RE::ScriptLocals* /*locals*/,
            double& /*result*/,
            std::uint32_t& /*opcodeOffsetPtr*/)
        {
            // `Script::GetCommand()` returns the full line the user typed at
            // the console, e.g. "rl status". Strip the command name prefix.
            std::string raw;
            if (scriptObj) {
                raw = scriptObj->GetCommand();
            }
            std::string_view cmdline{raw};
            const auto sp = cmdline.find(' ');
            cmdline = (sp == std::string_view::npos) ? std::string_view{} : cmdline.substr(sp + 1);

            const auto output = execute(cmdline);
            if (auto* log = RE::ConsoleLog::GetSingleton()) {
                log->Print("%s", output.c_str());
            }
            return true;
        }
    }

    void register_console_command() {
        // SKSE plugins register custom console commands by repurposing an
        // existing slot. The exact set of victims varies by game version,
        // so we try a list. None of these are used in normal play.
        static const char* const kCandidates[] = {
            "TestLocalizedString",
            "TestAllMessages",
            "PickRefByID",
            "ShowMods",
            "HairTint",
            "Transfer",
        };

        RE::SCRIPT_FUNCTION* cmd = nullptr;
        const char* victim = nullptr;
        for (auto* name : kCandidates) {
            cmd = RE::SCRIPT_FUNCTION::LocateConsoleCommand(name);
            if (cmd) {
                victim = name;
                break;
            }
        }

        if (!cmd) {
            SKSE::log::error(
                "could not locate any console slot; dumping first 30 commands for diagnostics");
            if (auto* first = RE::SCRIPT_FUNCTION::GetFirstConsoleCommand()) {
                for (int i = 0; i < 30; ++i) {
                    if (first[i].functionName) {
                        SKSE::log::info("  cmd[{}] = \"{}\"", i, first[i].functionName);
                    }
                }
            }
            return;
        }

        static const char* kName = "SkyrimReLive";
        static const char* kShort = "rl";
        static const char* kHelp =
            "SkyrimReLive control — try `rl help` for subcommands.";
        cmd->functionName = kName;
        cmd->shortName = kShort;
        cmd->helpString = kHelp;
        cmd->executeFunction = &ConsoleExecute;
        cmd->conditionFunction = nullptr;
        cmd->referenceFunction = false;
        cmd->numParams = 0;
        cmd->params = nullptr;
        SKSE::log::info("registered console command 'rl' (took over '{}')", victim);
    }

}
