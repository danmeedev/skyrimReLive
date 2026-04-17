#pragma once

// Opaque interface to the ImGui-based Zeus overlay. The implementation
// (ZeusOverlay.cpp) includes d3d11.h + imgui.h directly, isolated from
// CommonLib's REX/W32 headers via SKIP_PRECOMPILE_HEADERS. Callers only
// see these free functions — no D3D11 types leak.

namespace relive::zeus_overlay {

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
