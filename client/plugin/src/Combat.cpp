#include "Combat.h"

#include <atomic>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <SKSE/Logger.h>

#include "Ghost.h"
#include "Plugin.h"

namespace relive::combat {

    namespace {
        // Resolve the source TESForm into a melee weapon and pull (reach,
        // base damage). Returns sentinel defaults when the source isn't a
        // weapon (unarmed, magic, projectile) — server applies its own
        // clamps either way.
        struct WeaponStats {
            float reach;
            float damage;
        };
        WeaponStats stats_for_source(RE::FormID source_form_id) {
            constexpr WeaponStats kUnarmed{100.0F, 5.0F};
            if (source_form_id == 0) return kUnarmed;
            auto* form = RE::TESForm::LookupByID(source_form_id);
            if (!form) return kUnarmed;
            auto* weap = form->As<RE::TESObjectWEAP>();
            if (!weap) return kUnarmed;
            return WeaponStats{weap->GetReach(),
                               static_cast<float>(weap->GetAttackDamage())};
        }

        std::uint8_t classify_attack(REX::EnumSet<RE::TESHitEvent::Flag, std::uint8_t> flags) {
            if (flags.any(RE::TESHitEvent::Flag::kBashAttack))  return 2;
            if (flags.any(RE::TESHitEvent::Flag::kPowerAttack)) return 1;
            return 0;
        }

        class HitEventSink final : public RE::BSTEventSink<RE::TESHitEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(
                const RE::TESHitEvent* event,
                RE::BSTEventSource<RE::TESHitEvent>* /*source*/) override {
                if (!event) return RE::BSEventNotifyControl::kContinue;

                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!player || event->cause.get() != player) {
                    // Phase 2.3b only ships hits the local player landed.
                    // NPC-on-NPC or remote-on-remote stays local for now.
                    return RE::BSEventNotifyControl::kContinue;
                }

                auto* target_ref = event->target.get();
                if (!target_ref) return RE::BSEventNotifyControl::kContinue;
                auto* target_actor = target_ref->As<RE::Actor>();
                if (!target_actor) return RE::BSEventNotifyControl::kContinue;

                // Map the engine actor back to a server-side player_id.
                // Skip if the target isn't one of our network ghosts —
                // hitting a bandit shouldn't generate combat traffic.
                const auto target_pid =
                    ghost::instance().player_id_for_actor(target_actor);
                if (!target_pid.has_value()) {
                    return RE::BSEventNotifyControl::kContinue;
                }

                const auto stats = stats_for_source(event->source);
                const auto attack_type = classify_attack(event->flags);
                plugin::send_combat_event(*target_pid, attack_type,
                                          stats.reach, stats.damage);
                SKSE::log::info(
                    "shipped CombatEvent: target_pid={} type={} reach={:.0f} dmg={:.1f}",
                    *target_pid, attack_type, stats.reach, stats.damage);
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        std::atomic<bool> g_registered{false};
        HitEventSink g_sink{};
    }

    void register_sink() {
        if (g_registered.exchange(true)) return;
        auto* src = RE::ScriptEventSourceHolder::GetSingleton();
        if (!src) {
            SKSE::log::error("ScriptEventSourceHolder unavailable; combat hits won't be sent");
            g_registered.store(false);
            return;
        }
        src->AddEventSink<RE::TESHitEvent>(&g_sink);
        SKSE::log::info("TESHitEvent sink registered");
    }

    void on_damage_apply(std::uint32_t attacker_pid, float damage, bool stagger,
                         float new_hp) {
        // Net-thread entry. Touching RE::Actor / animation graphs is main-
        // thread only — punt onto the SKSE task queue.
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        task->AddTask([attacker_pid, damage, stagger, new_hp]() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return;
            if (stagger) {
                // staggerStart is the vanilla event the locomotion graph
                // listens for. Engine handles the rest of the animation.
                player->NotifyAnimationGraph("staggerStart");
            }
            if (auto* console = RE::ConsoleLog::GetSingleton()) {
                console->Print(
                    "[SkyrimReLive] hit by player_id=%u for %.1f dmg (HP %.1f)%s",
                    attacker_pid, damage, new_hp,
                    stagger ? " [STAGGER]" : "");
            }
            SKSE::log::info(
                "DamageApply: attacker={} dmg={:.1f} stagger={} hp={:.1f}",
                attacker_pid, damage, stagger, new_hp);
        });
    }

}
