#pragma once

#include <cstdint>

namespace relive::combat {

    // Install a TESHitEvent sink so local-player melee hits get shipped
    // to the server as CombatEvent packets. Idempotent — extra calls are
    // no-ops. Call after kPostLoadGame so the script event source exists.
    void register_sink();

    // Net-thread callback: a DamageApply packet from the server addressed
    // to us. Schedules a main-thread task that plays a stagger animation
    // (when the server flagged it) and logs the hit to console.
    void on_damage_apply(std::uint32_t attacker_pid, float damage, bool stagger,
                         float new_hp);

}
