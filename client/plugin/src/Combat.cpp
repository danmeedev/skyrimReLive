#include "Combat.h"

#include <atomic>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <SKSE/Logger.h>

#include "Ghost.h"
#include "Plugin.h"

namespace relive::combat {

    namespace {
        // AttackClass mirrors the FlatBuffer enum (Melee=0, BowArrow=1, Spell=2).
        constexpr std::uint8_t kClassMelee = 0;
        constexpr std::uint8_t kClassBow   = 1;
        constexpr std::uint8_t kClassSpell = 2;
        constexpr float kSpellDamageSentinel = 25.0F;

        struct HitStats {
            float reach;
            float damage;
            std::uint8_t attack_class;
        };
        HitStats stats_for_source(RE::FormID source_form_id, RE::FormID projectile_id) {
            constexpr HitStats kUnarmed{100.0F, 5.0F, kClassMelee};
            if (source_form_id == 0) return kUnarmed;
            auto* form = RE::TESForm::LookupByID(source_form_id);
            if (!form) return kUnarmed;
            if (auto* weap = form->As<RE::TESObjectWEAP>()) {
                const float dmg = static_cast<float>(weap->GetAttackDamage());
                if (weap->IsBow() || weap->IsCrossbow() || projectile_id != 0) {
                    return HitStats{0.0F, dmg, kClassBow};
                }
                if (weap->IsStaff()) {
                    return HitStats{0.0F, kSpellDamageSentinel, kClassSpell};
                }
                return HitStats{weap->GetReach(), dmg, kClassMelee};
            }
            if (form->As<RE::SpellItem>() || form->As<RE::MagicItem>()) {
                return HitStats{0.0F, kSpellDamageSentinel, kClassSpell};
            }
            return kUnarmed;
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

                const auto stats = stats_for_source(event->source, event->projectile);
                const auto attack_type = classify_attack(event->flags);
                plugin::send_combat_event(*target_pid, attack_type,
                                          stats.reach, stats.damage,
                                          stats.attack_class);
                SKSE::log::info(
                    "shipped CombatEvent: target_pid={} class={} type={} reach={:.0f} dmg={:.1f}",
                    *target_pid, stats.attack_class, attack_type,
                    stats.reach, stats.damage);
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
