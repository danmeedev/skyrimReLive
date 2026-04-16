#include "PCH.h"

#include "Cell.h"

#include <RE/Skyrim.h>

namespace relive::cell {

    void Watcher::poll_main_thread() noexcept {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->parentCell) {
            current_.store(0, std::memory_order_release);
            return;
        }
        current_.store(player->parentCell->formID, std::memory_order_release);
    }

    Watcher& instance() {
        static Watcher w;
        return w;
    }

}
