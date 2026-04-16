#pragma once

// Text-command interface. All user-facing control flows through here:
// the in-game console (~), and any future UI (ImGui overlay, MCM, IPC).
// The rule for future UIs is: translate button clicks into one of these
// commands, don't call Plugin.h directly. Keeps behavior uniform and the
// command set becomes the "API".

#include <string>
#include <string_view>

namespace relive::commands {

    // Parse and run a command line. Returns human-readable output.
    // Empty input or "help" prints the command list.
    std::string execute(std::string_view cmdline);

    // Hijack the vanilla "Transfer" console command slot so the player can
    // type `rl <subcommand>` at the `~` prompt. Called once from the SKSE
    // kDataLoaded handler.
    void register_console_command();

}
