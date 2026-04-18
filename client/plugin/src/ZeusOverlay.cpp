// Zeus overlay — ImGui D3D11 integration.
// SKIP_PRECOMPILE_HEADERS ON: this TU includes d3d11.h + dxgi.h directly,
// which conflict with CommonLib's REX/W32 surface. No CommonLib headers
// are included here; we talk to the rest of the plugin through the opaque
// ZeusOverlay.h interface and a minimal set of callbacks.

#include "ZeusOverlay.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

// Forward declare the Win32 ImGui WndProc handler.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace relive::zeus_overlay {

    namespace {
        using InputToggleFn = void(*)(bool);
        using CommandFn = void(*)(const char*);
        using OverlayPlayer = relive::zeus_overlay::OverlayPlayer;
        using OverlayNpc = relive::zeus_overlay::OverlayNpc;
        using FormEntry = relive::zeus_overlay::FormEntry;

        std::atomic<bool> g_installed{false};
        std::atomic<bool> g_active{false};
        std::atomic<bool> g_free_cam{false};
        bool g_imgui_initialized = false;
        InputToggleFn g_input_toggle = nullptr;
        CommandFn g_command_fn = nullptr;

        std::mutex g_data_mu;
        std::vector<OverlayPlayer> g_players;
        std::vector<OverlayNpc> g_npcs;
        std::vector<FormEntry> g_forms;

        // Case-insensitive substring match.
        bool icontains(const char* haystack, const char* needle) {
            if (!needle[0]) return true;
            for (const char* h = haystack; *h; ++h) {
                const char* a = h;
                const char* b = needle;
                while (*a && *b && (tolower(static_cast<unsigned char>(*a)) ==
                                    tolower(static_cast<unsigned char>(*b)))) {
                    ++a; ++b;
                }
                if (!*b) return true;
            }
            return false;
        }

        void send_cmd(const char* fmt_cmd) {
            if (g_command_fn) g_command_fn(fmt_cmd);
        }

        ID3D11Device* g_device = nullptr;
        ID3D11DeviceContext* g_context = nullptr;
        ID3D11RenderTargetView* g_rtv = nullptr;
        HWND g_hwnd = nullptr;
        WNDPROC g_original_wndproc = nullptr;

        // Original Present function pointer (from vtable swap).
        using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
        PresentFn g_original_present = nullptr;

        void create_rtv(IDXGISwapChain* swap) {
            ID3D11Texture2D* back_buffer = nullptr;
            swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                            reinterpret_cast<void**>(&back_buffer));
            if (back_buffer) {
                g_device->CreateRenderTargetView(back_buffer, nullptr, &g_rtv);
                back_buffer->Release();
            }
        }

        void init_imgui(IDXGISwapChain* swap) {
            DXGI_SWAP_CHAIN_DESC desc{};
            swap->GetDesc(&desc);
            g_hwnd = desc.OutputWindow;

            swap->GetDevice(__uuidof(ID3D11Device),
                            reinterpret_cast<void**>(&g_device));
            g_device->GetImmediateContext(&g_context);

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            auto& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            ImGui::StyleColorsDark();

            ImGui_ImplWin32_Init(g_hwnd);
            ImGui_ImplDX11_Init(g_device, g_context);

            create_rtv(swap);
            g_imgui_initialized = true;
        }

        void render_zeus_panel() {
            ImGui::SetNextWindowSize(ImVec2(440, 600), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Zeus Mode", nullptr,
                             ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "SkyrimReLive Zeus Mode");
                ImGui::Separator();

                // ---- Free Camera ----
                {
                    bool fc = g_free_cam.load(std::memory_order_relaxed);
                    if (ImGui::Checkbox("Free Camera (Fly Mode)", &fc)) {
                        g_free_cam.store(fc, std::memory_order_relaxed);
                        if (g_input_toggle) g_input_toggle(g_active.load(std::memory_order_relaxed));
                    }
                    if (fc) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("WASD to fly, mouse to look (click off panel)");
                    }
                }
                ImGui::Separator();

                // ---- Time & Weather ----
                if (ImGui::CollapsingHeader("Time & Weather",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    static float s_hour = 12.0f;
                    ImGui::SliderFloat("Time", &s_hour, 0.0f, 24.0f, "%.0f:00");
                    ImGui::SameLine();
                    if (ImGui::Button("Set##time")) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "time %.0f", s_hour);
                        send_cmd(buf);
                    }

                    static int s_weather = 0;
                    const char* weather_names[] = {
                        "Clear", "Rain", "Snow", "Storm", "Fog"};
                    const char* weather_cmds[] = {
                        "weather clear", "weather rain", "weather snow",
                        "weather storm", "weather fog"};
                    ImGui::Combo("Weather", &s_weather, weather_names, 5);
                    ImGui::SameLine();
                    if (ImGui::Button("Set##weather")) {
                        send_cmd(weather_cmds[s_weather]);
                    }
                }

                // ---- Spawn / Item Browser ----
                if (ImGui::CollapsingHeader("Spawn / Browse",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    static char s_search[128] = "";
                    static int s_cat_filter = 0;
                    const char* categories[] = {
                        "All", "NPC", "Weapon", "Armor", "Potion",
                        "Misc", "Ammo", "Book", "Ingredient", "Key", "Scroll",
                        "Static", "Tree", "Door", "Activator", "Light",
                        "Furniture", "Flora", "MovableStatic"};
                    ImGui::InputText("Search", s_search, sizeof(s_search));
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120);
                    ImGui::Combo("##cat", &s_cat_filter, categories, 19);

                    {
                        std::lock_guard lock(g_data_mu);
                        const char* cat_str = s_cat_filter > 0 ? categories[s_cat_filter] : nullptr;
                        int shown = 0;
                        ImGui::BeginChild("form_list", ImVec2(0, 200), true);
                        for (const auto& f : g_forms) {
                            if (f.name[0] == '\0') continue;
                            if (cat_str && strcmp(f.category, cat_str) != 0) continue;
                            if (s_search[0] && !icontains(f.name, s_search)) continue;
                            if (++shown > 200) {
                                ImGui::TextDisabled("... too many results, refine search");
                                break;
                            }
                            ImGui::PushID(static_cast<int>(f.form_id));
                            char label[192];
                            snprintf(label, sizeof(label), "[%s] %s (0x%x)",
                                     f.category, f.name, f.form_id);
                            if (ImGui::Selectable(label, false)) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "spawn 0x%x", f.form_id);
                                send_cmd(buf);
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::BeginTooltip();
                                ImGui::Text("Left-click: Spawn at your position");
                                ImGui::Text("FormID: 0x%x", f.form_id);
                                ImGui::EndTooltip();
                            }
                            ImGui::PopID();
                        }
                        if (shown == 0) {
                            ImGui::TextDisabled(g_forms.empty()
                                ? "Form library loading..."
                                : "No results. Try a different search.");
                        }
                        ImGui::EndChild();
                    }

                    // Manual FormID fallback
                    static char s_formid[32] = "0x";
                    ImGui::InputText("Manual FormID", s_formid, sizeof(s_formid));
                    ImGui::SameLine();
                    if (ImGui::Button("Spawn##manual")) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "spawn %s", s_formid);
                        send_cmd(buf);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Give##manual")) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "give 1 %s 1", s_formid);
                        send_cmd(buf);
                    }
                }

                // ---- Players ----
                if (ImGui::CollapsingHeader("Players")) {
                    std::lock_guard lock(g_data_mu);
                    if (g_players.empty()) {
                        ImGui::TextDisabled("No players (waiting for server broadcast)");
                    } else {
                        ImGui::Columns(4, "player_cols", true);
                        ImGui::Text("ID"); ImGui::NextColumn();
                        ImGui::Text("Name"); ImGui::NextColumn();
                        ImGui::Text("Level"); ImGui::NextColumn();
                        ImGui::Text("HP"); ImGui::NextColumn();
                        ImGui::Separator();
                        for (const auto& p : g_players) {
                            ImGui::Text("%u", p.player_id); ImGui::NextColumn();
                            ImGui::Text("%s", p.character_name[0] ? p.character_name : p.name);
                            ImGui::NextColumn();
                            ImGui::Text("%u", static_cast<unsigned>(p.level)); ImGui::NextColumn();
                            ImGui::Text("%.0f/%.0f", p.hp, p.hp_max); ImGui::NextColumn();
                        }
                        ImGui::Columns(1);
                    }
                }

                // ---- Spawned NPCs ----
                if (ImGui::CollapsingHeader("Spawned NPCs")) {
                    std::lock_guard lock(g_data_mu);
                    if (g_npcs.empty()) {
                        ImGui::TextDisabled("No spawned NPCs");
                    } else {
                        for (const auto& n : g_npcs) {
                            ImGui::PushID(static_cast<int>(n.zeus_id));
                            ImGui::Text("Zeus #%u (0x%x)", n.zeus_id, n.base_form_id);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Follow")) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "npc %u follow", n.zeus_id);
                                send_cmd(buf);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Wait")) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "npc %u wait", n.zeus_id);
                                send_cmd(buf);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Combat")) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "npc %u combat", n.zeus_id);
                                send_cmd(buf);
                            }
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Delete")) {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "npc %u delete", n.zeus_id);
                                send_cmd(buf);
                            }
                            ImGui::PopID();
                        }
                    }
                }

                // ---- Spawned Objects ----
                if (ImGui::CollapsingHeader("Spawned Objects")) {
                    // Objects are tracked client-side in the zeus
                    // registry but we don't push them to the overlay
                    // yet. For now, show a note about using console.
                    ImGui::TextDisabled("Use `rl cmd obj <id> delete` or");
                    ImGui::TextDisabled("`rl cmd obj <id> moveto <x y z>`");
                    ImGui::TextDisabled("Object IDs shown in console on spawn.");
                }

                // ---- Admin ----
                if (ImGui::CollapsingHeader("Admin",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    static bool s_pvp = false;
                    if (ImGui::Checkbox("PvP Enabled", &s_pvp)) {
                        send_cmd(s_pvp ? "pvp on" : "pvp off");
                    }

                    // Kick player
                    static int s_kick_id = 1;
                    ImGui::InputInt("Kick ID", &s_kick_id);
                    ImGui::SameLine();
                    if (ImGui::Button("Kick")) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "kick %d", s_kick_id);
                        send_cmd(buf);
                    }
                }

                ImGui::Separator();
                ImGui::TextDisabled("Press F8 to close");
            }
            ImGui::End();
        }

        bool g_f8_was_down = false;

        HRESULT __stdcall hooked_present(IDXGISwapChain* swap, UINT sync,
                                          UINT flags) {
            if (!g_imgui_initialized) {
                init_imgui(swap);
            }

            // Poll F8 directly — Skyrim uses DirectInput so WM_KEYDOWN
            // doesn't reliably reach our WndProc during gameplay.
            const bool f8_down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
            if (f8_down && !g_f8_was_down) {
                const bool now_active = !g_active.load(std::memory_order_relaxed);
                g_active.store(now_active, std::memory_order_relaxed);

                if (now_active) {
                    // Show cursor + unclip mouse so ImGui can receive clicks.
                    ClipCursor(nullptr);
                    while (ShowCursor(TRUE) < 0) {}
                    auto& io = ImGui::GetIO();
                    io.MouseDrawCursor = true;
                } else {
                    // Hide cursor + let game reclaim mouse.
                    while (ShowCursor(FALSE) >= 0) {}
                    auto& io = ImGui::GetIO();
                    io.MouseDrawCursor = false;
                }
                if (g_input_toggle) g_input_toggle(now_active);
            }
            g_f8_was_down = f8_down;

            if (g_active.load(std::memory_order_relaxed)) {
                // Manually feed mouse state — Skyrim captures mouse via
                // DirectInput so WndProc never sees WM_LBUTTONDOWN etc.
                auto& io = ImGui::GetIO();
                io.MouseDown[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                io.MouseDown[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
                io.MouseDown[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                render_zeus_panel();

                ImGui::Render();
                if (g_rtv) {
                    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
                    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
                }
            }

            return g_original_present(swap, sync, flags);
        }

        LRESULT CALLBACK hooked_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
            if (g_active.load(std::memory_order_relaxed)) {
                if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) {
                    return 0;
                }
                // When ImGui wants the input (hovering a panel), block it
                // from the game. Otherwise let it through for free-cam.
                auto& io = ImGui::GetIO();
                if (io.WantCaptureMouse) {
                    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
                        return 0;
                }
                if (io.WantCaptureKeyboard || io.WantTextInput) {
                    if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
                        msg == WM_CHAR || msg == WM_SYSKEYDOWN ||
                        msg == WM_SYSKEYUP)
                        return 0;
                }
            }

            return CallWindowProcW(g_original_wndproc, hwnd, msg, wp, lp);
        }
    }

    void install_hooks() {
        // No-op at kDataLoaded. The real setup happens in set_swap_chain
        // when we have the renderer's swap chain pointer.
    }

    void set_swap_chain(void* swap_chain_ptr) {
        if (!swap_chain_ptr) return;
        if (g_installed.exchange(true)) return;

        auto* swap = static_cast<IDXGISwapChain*>(swap_chain_ptr);

        // Get the HWND from the swap chain descriptor — avoids fragile
        // FindWindowA with guessed class names.
        DXGI_SWAP_CHAIN_DESC desc{};
        swap->GetDesc(&desc);
        g_hwnd = desc.OutputWindow;

        // Hook WndProc for input forwarding (F8 toggle + ImGui mouse/kb).
        if (g_hwnd) {
            g_original_wndproc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                                  reinterpret_cast<LONG_PTR>(hooked_wndproc)));
        }

        // Vtable hook: replace Present (slot 8) with our hook.
        auto** vtable = *reinterpret_cast<void***>(swap);
        DWORD old_protect;
        VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE,
                        &old_protect);
        g_original_present = reinterpret_cast<PresentFn>(vtable[8]);
        vtable[8] = reinterpret_cast<void*>(&hooked_present);
        VirtualProtect(&vtable[8], sizeof(void*), old_protect, &old_protect);
    }

    void set_input_toggle(void(*fn)(bool)) {
        g_input_toggle = fn;
    }

    void set_command_callback(void(*fn)(const char*)) {
        g_command_fn = fn;
    }

    void push_player_list(const OverlayPlayer* players, unsigned int count) {
        std::lock_guard lock(g_data_mu);
        g_players.assign(players, players + count);
    }

    void push_npc_list(const OverlayNpc* npcs, unsigned int count) {
        std::lock_guard lock(g_data_mu);
        g_npcs.assign(npcs, npcs + count);
    }

    void push_form_library(const FormEntry* entries, unsigned int count) {
        std::lock_guard lock(g_data_mu);
        g_forms.assign(entries, entries + count);
    }

    void toggle() {
        g_active.store(!g_active.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    }

    bool is_active() {
        return g_active.load(std::memory_order_relaxed);
    }

    bool is_free_cam() {
        return g_free_cam.load(std::memory_order_relaxed);
    }

    void set_free_cam(bool enabled) {
        g_free_cam.store(enabled, std::memory_order_relaxed);
    }

}
