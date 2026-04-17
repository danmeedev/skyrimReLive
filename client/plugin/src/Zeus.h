#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <RE/Skyrim.h>

namespace relive::zeus {

    struct SpawnedNpc {
        std::uint32_t zeus_id;
        RE::FormID base_form_id;
        RE::NiPointer<RE::Actor> actor;
    };

    // Register a freshly-spawned NPC. Returns the assigned zeus_id.
    std::uint32_t register_npc(RE::FormID base_id, RE::NiPointer<RE::Actor> actor);

    // Look up a spawned NPC by zeus_id.
    RE::Actor* get_npc(std::uint32_t zeus_id);

    // List all tracked NPCs (for `rl cmd npcs`).
    std::vector<SpawnedNpc> list_npcs();

    // Execute an NPC order. Called on the main thread.
    // Returns a human-readable result string.
    std::string execute_npc_order(std::uint32_t zeus_id, const std::string& order,
                                  const std::string& args);

    // Execute a spawn command. Called on the main thread.
    // Returns the spawned actor (null on failure).
    RE::NiPointer<RE::Actor> execute_spawn(RE::FormID base_form_id, float x, float y, float z);

    // Execute a give command on the local player. Called on the main thread.
    void execute_give(RE::FormID item_form_id, std::uint32_t count);

}
