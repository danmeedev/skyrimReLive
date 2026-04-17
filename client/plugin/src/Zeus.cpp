#include "Zeus.h"

#include <SKSE/Logger.h>

namespace relive::zeus {

    namespace {
        std::uint32_t g_next_zeus_id = 1;
        std::vector<SpawnedNpc> g_npcs;

        // Vanilla follower faction. Adding an NPC to this at rank 0 makes
        // them eligible for the follower AI package (follow player, assist
        // in combat, obey wait/follow commands).
        constexpr RE::FormID kCurrentFollowerFaction = 0x5C84E;
    }

    std::uint32_t register_npc(RE::FormID base_id, RE::NiPointer<RE::Actor> actor) {
        const auto id = g_next_zeus_id++;
        g_npcs.push_back({id, base_id, actor});
        return id;
    }

    RE::Actor* get_npc(std::uint32_t zeus_id) {
        for (auto& n : g_npcs) {
            if (n.zeus_id == zeus_id && n.actor) return n.actor.get();
        }
        return nullptr;
    }

    std::vector<SpawnedNpc> list_npcs() {
        // Prune dead/deleted refs.
        std::erase_if(g_npcs, [](const SpawnedNpc& n) {
            return !n.actor || n.actor->IsDeleted();
        });
        return g_npcs;
    }

    RE::NiPointer<RE::Actor> execute_spawn(RE::FormID base_form_id,
                                            float x, float y, float z) {
        auto* base = RE::TESForm::LookupByID<RE::TESBoundObject>(base_form_id);
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!base || !player || !player->parentCell) return nullptr;

        auto ref = player->PlaceObjectAtMe(base, false);
        if (!ref) return nullptr;

        auto* actor = ref->As<RE::Actor>();
        if (!actor) {
            // Not an NPC — just a placed object (weapon, item, etc.)
            ref->SetPosition({x, y, z});
            return nullptr;
        }

        RE::NiPointer<RE::Actor> actor_ptr(actor);
        actor->Enable(false);
        actor->Load3D(false);
        // Spawn with the NPC's default AI — don't force follower mode
        // on every spawn. The "follow" order opts in to the follower
        // faction when the admin explicitly requests it. This avoids
        // behavior-graph conflicts that crash with multiple spawns.
        actor->SetPosition({x, y, z}, true);

        const auto zid = register_npc(base_form_id, actor_ptr);
        SKSE::log::info("zeus spawn: id={} base=0x{:x} at ({:.0f},{:.0f},{:.0f})",
                        zid, base_form_id, x, y, z);
        return actor_ptr;
    }

    void execute_give(RE::FormID item_form_id, std::uint32_t count) {
        auto* item = RE::TESForm::LookupByID(item_form_id);
        auto* bound = item ? item->As<RE::TESBoundObject>() : nullptr;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (bound && player) {
            player->AddObjectToContainer(bound, nullptr,
                                         static_cast<std::int32_t>(count), nullptr);
        }
    }

    std::string execute_npc_order(std::uint32_t zeus_id, const std::string& order,
                                  const std::string& args) {
        auto* actor = get_npc(zeus_id);
        if (!actor) {
            return "zeus_id " + std::to_string(zeus_id) + " not found (use `rl cmd npcs`)";
        }

        if (order == "follow") {
            // Add to follower faction + set teammate on demand.
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(kCurrentFollowerFaction);
            if (faction) {
                actor->AddToFaction(faction, 0);
            }
            actor->GetActorRuntimeData().boolBits.set(
                RE::Actor::BOOL_BITS::kPlayerTeammate);
            actor->EvaluatePackage(true, true);
            return "following";
        }
        if (order == "wait" || order == "stay") {
            actor->InitiateDoNothingPackage();
            return "waiting";
        }
        if (order == "moveto") {
            float x = 0, y = 0, z = 0;
            std::istringstream iss(args);
            iss >> x >> y >> z;
            actor->SetPosition({x, y, z}, true);
            return "moved";
        }
        if (order == "aggro" || order == "aggression") {
            float level = 0;
            try { level = std::stof(args); } catch (...) {}
            // 0=unaggressive, 1=aggressive, 2=very aggressive, 3=frenzied
            actor->SetActorValue(RE::ActorValue::kAggression, level);
            return "aggression set to " + std::to_string(static_cast<int>(level));
        }
        if (order == "confidence") {
            float level = 4;
            try { level = std::stof(args); } catch (...) {}
            // 0=cowardly, 1=cautious, 2=average, 3=brave, 4=foolhardy
            actor->SetActorValue(RE::ActorValue::kConfidence, level);
            return "confidence set to " + std::to_string(static_cast<int>(level));
        }
        if (order == "combat" || order == "attack") {
            // Make aggressive + confident, then point at the player's
            // current combat target or crosshair ref.
            actor->SetActorValue(RE::ActorValue::kAggression, 2.0F);
            actor->SetActorValue(RE::ActorValue::kConfidence, 4.0F);
            actor->EvaluatePackage(true, true);
            return "combat mode — aggressive + foolhardy";
        }
        if (order == "passive" || order == "pacify") {
            actor->SetActorValue(RE::ActorValue::kAggression, 0.0F);
            actor->SetActorValue(RE::ActorValue::kConfidence, 2.0F);
            actor->EvaluatePackage(true, true);
            return "pacified — unaggressive + average confidence";
        }
        if (order == "delete" || order == "remove") {
            actor->Disable();
            actor->SetDelete(true);
            return "deleted";
        }

        return "unknown order '" + order +
               "'; try: follow, wait, moveto, aggro, confidence, combat, passive, delete";
    }

}
