// Zeus overlay — ImGui D3D11 integration.
// SKIP_PRECOMPILE_HEADERS ON: this TU includes d3d11.h + dxgi.h directly,
// which conflict with CommonLib's REX/W32 surface. No CommonLib headers
// are included here; we talk to the rest of the plugin through the opaque
// ZeusOverlay.h interface and a minimal set of callbacks.

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
        std::atomic<bool> g_installed{false};
        std::atomic<bool> g_active{false};
        bool g_imgui_initialized = false;

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
            ImGui::SetNextWindowSize(ImVec2(420, 500), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Zeus Mode", nullptr,
                             ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                                   "SkyrimReLive Zeus Mode");
                ImGui::Separator();

                if (ImGui::CollapsingHeader("Time & Weather",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    static float s_hour = 12.0f;
                    ImGui::SliderFloat("Time", &s_hour, 0.0f, 24.0f, "%.0f:00");
                    ImGui::SameLine();
                    if (ImGui::Button("Set##time")) {
                        // TODO: send time command to server
                    }

                    static int s_weather = 0;
                    const char* weather_names[] = {
                        "Clear", "Rain", "Snow", "Storm", "Fog"};
                    ImGui::Combo("Weather", &s_weather, weather_names, 5);
                    ImGui::SameLine();
                    if (ImGui::Button("Set##weather")) {
                        // TODO: send weather command to server
                    }
                }

                if (ImGui::CollapsingHeader("Spawn",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    static char s_search[128] = "";
                    ImGui::InputText("Search", s_search, sizeof(s_search));
                    ImGui::Text("(Form browser coming in Phase B)");

                    static char s_formid[32] = "";
                    ImGui::InputText("FormID", s_formid, sizeof(s_formid));
                    ImGui::SameLine();
                    if (ImGui::Button("Spawn")) {
                        // TODO: send spawn command
                    }
                }

                if (ImGui::CollapsingHeader("Players")) {
                    ImGui::Text("(Player list coming — wired to rl players)");
                }

                if (ImGui::CollapsingHeader("Spawned NPCs")) {
                    ImGui::Text("(NPC list coming — wired to zeus registry)");
                }

                if (ImGui::CollapsingHeader("Admin")) {
                    static bool s_pvp = false;
                    if (ImGui::Checkbox("PvP Enabled", &s_pvp)) {
                        // TODO: send pvp toggle
                    }
                }
            }
            ImGui::End();
        }

        HRESULT __stdcall hooked_present(IDXGISwapChain* swap, UINT sync,
                                          UINT flags) {
            if (!g_imgui_initialized) {
                init_imgui(swap);
            }

            if (g_active.load(std::memory_order_relaxed)) {
                ImGui_ImplDX11_NewFrame();
                ImGui_ImplWin32_NewFrame();
                ImGui::NewFrame();

                render_zeus_panel();

                ImGui::Render();
                g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            }

            return g_original_present(swap, sync, flags);
        }

        LRESULT CALLBACK hooked_wndproc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
            // F8 toggles the overlay.
            if (msg == WM_KEYDOWN && wp == VK_F8) {
                g_active.store(!g_active.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
                return 0;
            }

            // When overlay is active, feed input to ImGui.
            if (g_active.load(std::memory_order_relaxed)) {
                if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) {
                    return 0;
                }
                // Block game input while overlay is up (mouse + keyboard).
                if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) return 0;
                if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
                    msg == WM_CHAR || msg == WM_SYSKEYDOWN ||
                    msg == WM_SYSKEYUP) {
                    return 0;
                }
            }

            return CallWindowProcW(g_original_wndproc, hwnd, msg, wp, lp);
        }
    }

    void install_hooks() {
        if (g_installed.exchange(true)) return;

        // We can't install the Present hook here because the swap chain
        // doesn't exist yet at plugin load time. Instead, we defer to the
        // first call to toggle() or to a kPostLoadGame callback. The actual
        // vtable swap happens in hooked_present's lazy init path.
        //
        // For now: use a polling approach — the first time hooked_present
        // is called, it inits ImGui. We just need to GET the swap chain
        // and hook its Present vtable slot.
        //
        // Accessing the swap chain: Skyrim's renderer is initialized by
        // the time kDataLoaded fires. We'll try to grab it here.

        // The Renderer singleton and its swap chain live behind CommonLib
        // types we can't include here. Instead, we'll use a workaround:
        // scan the DXGI factory for the game's swap chain via a known
        // window class, or hook Present at a known game address.
        //
        // Simplest reliable approach: create a dummy D3D device + swap
        // chain to harvest the IDXGISwapChain vtable, then hook the real
        // one via the vtable pointer.

        // Step 1: Find the game window.
        HWND hwnd = FindWindowA("Skyrim Special Edition", nullptr);
        if (!hwnd) {
            hwnd = FindWindowA(nullptr, "Skyrim Special Edition");
        }
        if (!hwnd) return;

        // Step 2: Create a dummy swap chain to get the Present vtable offset.
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL feat;
        ID3D11Device* dummy_dev = nullptr;
        ID3D11DeviceContext* dummy_ctx = nullptr;
        IDXGISwapChain* dummy_swap = nullptr;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &dummy_swap, &dummy_dev, &feat,
            &dummy_ctx);
        if (FAILED(hr) || !dummy_swap) {
            if (dummy_ctx) dummy_ctx->Release();
            if (dummy_dev) dummy_dev->Release();
            return;
        }

        // Step 3: Read the vtable from the dummy swap chain.
        auto** vtable = *reinterpret_cast<void***>(dummy_swap);
        auto* present_addr = vtable[8]; // IDXGISwapChain::Present is slot 8

        dummy_swap->Release();
        dummy_ctx->Release();
        dummy_dev->Release();

        // Step 4: Now find the REAL swap chain by scanning the game's DXGI
        // usage. Actually, the vtable pointer is shared across all swap
        // chains created by the same DXGI factory — so we can patch the
        // vtable entry directly and it applies to the game's swap chain too.
        //
        // BUT vtable patching is fragile. A more robust approach: use
        // MinHook or a trampoline to hook the function at `present_addr`.
        // For now, we'll do a direct vtable overwrite on the game's swap
        // chain. We need to find it first.
        //
        // Re-create a dummy to get the vtable base, then find the game's
        // real swap chain via DXGI enumeration... this is getting complex.
        //
        // SIMPLER: enumerate all windows and find the game's, then use
        // the GetDevice hack... no.
        //
        // SIMPLEST: Hook via kDataLoaded when we have access to CommonLib.
        // But this file can't include CommonLib.
        //
        // PRAGMATIC: export a function that accepts the raw swap chain
        // pointer from Plugin.cpp (which CAN access CommonLib), and hook
        // it here.

        // For now, store the vtable address. The actual hook will be
        // installed when set_swap_chain() is called from Plugin.cpp with
        // the real pointer obtained via CommonLib.
        (void)present_addr;

        // Hook WndProc for input forwarding.
        g_hwnd = hwnd;
        g_original_wndproc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(hooked_wndproc)));
    }

    // Called from Plugin.cpp with the raw swap chain pointer obtained via
    // RE::BSGraphics::Renderer. This file can't include CommonLib headers.
    void set_swap_chain(void* swap_chain_ptr) {
        if (!swap_chain_ptr) return;
        auto* swap = static_cast<IDXGISwapChain*>(swap_chain_ptr);

        // Vtable hook: replace Present (slot 8) with our hook.
        auto** vtable = *reinterpret_cast<void***>(swap);

        DWORD old_protect;
        VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE,
                        &old_protect);
        g_original_present = reinterpret_cast<PresentFn>(vtable[8]);
        vtable[8] = reinterpret_cast<void*>(&hooked_present);
        VirtualProtect(&vtable[8], sizeof(void*), old_protect, &old_protect);
    }

    void toggle() {
        g_active.store(!g_active.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    }

    bool is_active() {
        return g_active.load(std::memory_order_relaxed);
    }

}
