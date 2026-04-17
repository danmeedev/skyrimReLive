#pragma once

// Opaque interface to the ImGui-based Zeus overlay. The implementation
// (ZeusOverlay.cpp) includes d3d11.h + imgui.h directly, isolated from
// CommonLib's REX/W32 headers via SKIP_PRECOMPILE_HEADERS. Callers only
// see these free functions — no D3D11 types leak.

namespace relive::zeus_overlay {

    // Callback invoked when the overlay toggles. The plugin uses this to
    // disable/enable game controls via CommonLib (which this module can't
    // include). `active = true` means overlay just opened.
    using InputToggleFn = void(*)(bool active);
    void set_input_toggle(InputToggleFn fn);

    // Callback for sending admin commands to the server. The overlay calls
    // this with strings like "time 14", "weather clear", "pvp on".
    using CommandFn = void(*)(const char* cmd);
    void set_command_callback(CommandFn fn);

    // Push player list data into the overlay for rendering. Call from the
    // main plugin whenever the player list updates. Simple C-compatible
    // struct so no CommonLib types cross the boundary.
    struct OverlayPlayer {
        unsigned int player_id;
        char name[64];
        char character_name[64];
        unsigned short level;
        float hp, hp_max;
    };
    void push_player_list(const OverlayPlayer* players, unsigned int count);

    // Push spawned NPC list for the NPC panel.
    struct OverlayNpc {
        unsigned int zeus_id;
        unsigned int base_form_id;
    };
    void push_npc_list(const OverlayNpc* npcs, unsigned int count);

    struct FormEntry {
        unsigned int form_id;
        char name[128];
        char category[16]; // "NPC", "Weapon", "Armor", "Potion", "Misc", etc.
    };
    void push_form_library(const FormEntry* entries, unsigned int count);

    // Install WndProc hook for input. Call at kDataLoaded. Idempotent.
    void install_hooks();

    // Pass the real IDXGISwapChain* obtained via CommonLib's
    // RE::BSGraphics::Renderer. Call at kPostLoadGame (renderer is alive).
    // This file can't include CommonLib so the pointer is void*.
    void set_swap_chain(void* swap_chain_ptr);

    // Toggle the overlay on/off. Called from a hotkey handler.
    void toggle();

    // Query whether the overlay is currently visible (consuming input).
    [[nodiscard]] bool is_active();

}
