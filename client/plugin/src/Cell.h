#pragma once

#include <atomic>
#include <cstdint>

namespace relive::cell {

    // Tracks which cell the local player is in and whether that matches the
    // configured target cell. Polled on the main thread (piggybacked onto
    // the ghost tick pump) — see proposal 0001 for why we picked polling
    // over the TESCellAttachDetachEvent sink for Phase 1.
    class Watcher {
    public:
        // Main-thread only: read PlayerCharacter's current parentCell and
        // cache its form ID. Zero means no cell (main menu / load screen).
        void poll_main_thread() noexcept;

        // `target == 0` means "any cell counts as active" (dev/solo default).
        // Nonzero means "only this specific cell form ID is active".
        void set_target(std::uint32_t form_id) noexcept {
            target_.store(form_id, std::memory_order_release);
        }
        [[nodiscard]] std::uint32_t target() const noexcept {
            return target_.load(std::memory_order_acquire);
        }
        [[nodiscard]] std::uint32_t current() const noexcept {
            return current_.load(std::memory_order_acquire);
        }

        // True when the local player is in a cell AND (no target set OR
        // current matches target). Safe to call from any thread.
        [[nodiscard]] bool is_active() const noexcept {
            const auto cur = current();
            if (cur == 0) return false;
            const auto tgt = target();
            return tgt == 0 || cur == tgt;
        }

    private:
        std::atomic<std::uint32_t> current_{0};
        std::atomic<std::uint32_t> target_{0};
    };

    Watcher& instance();

}
