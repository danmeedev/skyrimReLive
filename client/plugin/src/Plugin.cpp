#include "PCH.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <SKSE/SKSE.h>
#include <SKSE/Logger.h>
#include <SKSE/Interfaces.h>

#include <RE/Skyrim.h>

#include "Cell.h"
#include "Commands.h"
#include "Config.h"
#include "Ghost.h"
#include "Net.h"
#include "Plugin.h"

namespace {
    relive::net::Client g_client;
    std::atomic<relive::plugin::ConnState> g_state{relive::plugin::ConnState::Idle};
    std::atomic<bool> g_demo_active{false};

    // Last-attempted target so commands can report it in `rl status`.
    std::mutex g_target_mu;
    std::string g_target_host;
    std::uint16_t g_target_port = 0;

    void Toast(const std::string& msg) {
        SKSE::log::info("{}", msg);
        // ConsoleLog is always available; press ~ to see. Proper HUD
        // notifications (top-left toast) need the Papyrus Debug.Notification
        // native — deferring to a follow-up until we have a Papyrus bridge.
        if (auto* log = RE::ConsoleLog::GetSingleton()) {
            log->Print("%s", msg.c_str());
        }
    }

    void GhostTickPump() {
        using namespace std::chrono_literals;
        while (g_state.load(std::memory_order_acquire) ==
               relive::plugin::ConnState::Connected) {
            if (auto* task = SKSE::GetTaskInterface()) {
                task->AddTask([]() { relive::ghost::instance().tick_main_thread(); });
            }
            std::this_thread::sleep_for(50ms);
        }
    }

    // Orbits a synthetic ghost around the local player every 3 seconds at
    // radius 150. Enough motion to visually verify that spawn, interp, and
    // despawn all work without a second connected client.
    void DemoOrbit() {
        using namespace std::chrono_literals;
        constexpr std::uint32_t kDemoId = 0xFFFF0001;
        constexpr float kRadius = 150.0F;
        constexpr float kTwoPi = 6.28318530717958647692F;
        constexpr float kPeriodSeconds = 3.0F;
        const auto start = std::chrono::steady_clock::now();

        while (g_demo_active.load(std::memory_order_acquire)) {
            const float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - start).count();
            const float phase = (kTwoPi / kPeriodSeconds) * elapsed;
            // Orbit around wherever the local player currently is.
            const auto center = relive::plugin::last_local_pos();
            const float x = center.x + kRadius * std::cos(phase);
            const float y = center.y + kRadius * std::sin(phase);
            // Face tangent to circle (yaw leading by +90 degrees).
            const float yaw = phase + (kTwoPi / 4.0F);
            relive::ghost::instance().inject_synthetic(kDemoId, x, y, center.z, yaw);
            std::this_thread::sleep_for(50ms);
        }
    }
}

namespace relive::plugin {

    ConnState state() noexcept { return g_state.load(std::memory_order_acquire); }
    std::uint32_t player_id() noexcept { return g_client.player_id(); }
    std::size_t ghost_count() noexcept { return ghost::instance().ghost_count(); }
    std::uint64_t snapshots_received() noexcept { return g_client.snapshots_received(); }
    std::uint64_t player_inputs_sent() noexcept { return g_client.player_inputs_sent(); }
    LocalPos last_local_pos() noexcept {
        return LocalPos{
            g_client.last_local_x(),
            g_client.last_local_y(),
            g_client.last_local_z(),
            g_client.last_local_yaw(),
        };
    }

    std::uint32_t current_cell_form_id() noexcept { return cell::instance().current(); }
    std::uint32_t target_cell_form_id() noexcept { return cell::instance().target(); }
    void set_target_cell_form_id(std::uint32_t form_id) noexcept {
        cell::instance().set_target(form_id);
    }

    bool demo_running() noexcept {
        return g_demo_active.load(std::memory_order_acquire);
    }

    std::string demo_start() {
        if (state() != ConnState::Connected) {
            return "not connected; run `rl connect` first so the ghost ticker is armed";
        }
        bool expected = false;
        if (!g_demo_active.compare_exchange_strong(expected, true)) {
            return "demo already running";
        }
        std::thread(DemoOrbit).detach();
        Toast("[SkyrimReLive] demo ghost orbiting (Lydia clone)");
        return "demo started";
    }

    std::string demo_stop() {
        const bool was = g_demo_active.exchange(false, std::memory_order_acq_rel);
        if (!was) return "demo not running";
        // Explicitly despawn on the main thread instead of waiting for
        // the 3-second staleness timer.
        if (auto* task = SKSE::GetTaskInterface()) {
            task->AddTask([]() { ghost::instance().clear_synthetic_ghosts(); });
        }
        Toast("[SkyrimReLive] demo ghost stopped");
        return "demo stopped";
    }

    std::string current_server() noexcept {
        const std::lock_guard lock(g_target_mu);
        if (g_target_port == 0) return "<none>";
        return g_target_host + ":" + std::to_string(g_target_port);
    }

    std::string start_connection(std::string host, std::uint16_t port,
                                 std::string name) {
        auto expected = ConnState::Idle;
        if (!g_state.compare_exchange_strong(expected, ConnState::Connected)) {
            if (expected == ConnState::Connected) {
                return "already connected as player_id=" +
                       std::to_string(g_client.player_id());
            }
            // Failed state: clear it and retry.
            g_state.store(ConnState::Idle, std::memory_order_release);
            expected = ConnState::Idle;
            if (!g_state.compare_exchange_strong(expected, ConnState::Connected)) {
                return "busy (state transitioning)";
            }
        }

        {
            const std::lock_guard lock(g_target_mu);
            g_target_host = host;
            g_target_port = port;
        }

        SKSE::log::info("SkyrimReLive: connecting to {}:{} as {}", host, port, name);
        if (!g_client.start(host, port, name)) {
            g_state.store(ConnState::Failed, std::memory_order_release);
            Toast("[SkyrimReLive] connect failed");
            return "connect failed (see SkyrimReLive.log)";
        }
        ghost::instance().set_self_id(g_client.player_id());
        std::thread(GhostTickPump).detach();
        const auto id = g_client.player_id();
        Toast("[SkyrimReLive] connected as player_id=" + std::to_string(id));
        return "connected as player_id=" + std::to_string(id);
    }

    std::string stop_connection() {
        auto prev = g_state.exchange(ConnState::Idle, std::memory_order_acq_rel);
        if (prev != ConnState::Connected) {
            return "not connected";
        }
        g_client.stop();
        Toast("[SkyrimReLive] disconnected");
        return "disconnected";
    }

    void on_world_loaded() {
        const auto cfg = config::load();
        // Apply cell target from config. 0 means "any cell" so solo testing
        // works everywhere by default.
        cell::instance().set_target(cfg.target_cell_form_id);

        if (!cfg.auto_connect) {
            SKSE::log::info("SkyrimReLive: auto_connect=false; use `rl connect` in console");
            Toast("[SkyrimReLive] ready — type `rl connect` to join");
            return;
        }
        std::thread([cfg]() {
            start_connection(cfg.server_host, cfg.server_port, cfg.player_name);
        }).detach();
    }

}

namespace {
    void OnMessage(SKSE::MessagingInterface::Message* msg) {
        if (!msg) return;
        switch (msg->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                SKSE::log::info(
                    "SkyrimReLive: data loaded; waiting for kPostLoadGame/kNewGame");
                relive::commands::register_console_command();
                break;
            case SKSE::MessagingInterface::kPostLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                relive::plugin::on_world_loaded();
                break;
            default:
                break;
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);
    SKSE::log::init();
    SKSE::log::info("SkyrimReLive plugin loaded");

    const auto messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        SKSE::log::error("SkyrimReLive: failed to register messaging listener");
        return false;
    }

    return true;
}
