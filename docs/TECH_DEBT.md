# Technical Debt Tracker

Known shortcuts, deferred work, and things that need revisiting.
Updated as items are created or resolved.

---

## Active debt

### Ghost still T-poses (animation)
**Severity:** High — visible to players
**Context:** Three fix attempts (EnableAI removal, IsMoving+SpeedSampled, atomic ActorState bitmask write). Position tracks correctly. Weapon equip visible (Lydia's default sword). Locomotion graph still won't transition.
**Likely root cause:** SetPosition teleports the actor, so the character controller doesn't register motion. The behavior graph's locomotion branch may gate on actual character-controller velocity, not just the `Speed` graph variable.
**Next step:** Try `NotifyAnimationGraph("moveStart")` / event-driven transitions instead of variable-driven. Or use `TranslateTo` via REL offset lookup to make the engine animate the ghost's motion.

### Ghost appears as Lydia clone
**Severity:** Medium — cosmetic, everyone knows it's a placeholder
**Context:** VanillaCloneSpawner uses Lydia's base (0x000A2C94). Custom ActorBase via ESP is the proper fix — gives each ghost a clean, package-free NPC base.
**Next step:** Create a minimal ESP with an empty-package ActorBase. Ship with the plugin.

### Character data gather crashes on early saves
**Severity:** Medium — only affects tutorial-exit saves
**Context:** `GetActorValue()` crashes when called at kPostLoadGame on very early saves where ActorValue arrays aren't allocated. Deferred all character reads — Hello sends defaults.
**Next step:** Use a timer (5-10s after connect) or a new `CharacterUpdate` message pushed once the player is stable. Gate on a stronger signal than `parentCell != null`.

### Zeus-spawned NPCs not synced across clients
**Severity:** Low — Zeus Phase 1 design compromise
**Context:** `spawn` broadcasts PlaceObjectAtMe to all clients, but each client creates an independent NPC with independent AI. They'll diverge immediately.
**Next step:** Phase 4 NPC replication will sync NPC state across clients. Until then, Zeus spawns are visual set dressing only.

### No death/respawn state
**Severity:** Low — HP drops to 0 but nothing happens visually
**Context:** Health component tracks HP, server sends DamageApply, but when HP reaches 0 there's no death animation, ragdoll, or respawn.
**Next step:** Add a `PlayerDeath` message, trigger ragdoll on the ghost, respawn after N seconds (configurable).

### PvP disabled UX feedback
**Severity:** Low — attacker gets no feedback when PvP is off
**Context:** CombatEvents are silently dropped. Attacker sees no stagger, may think network is broken.
**Next step:** `ServerNotice { text }` message type, displayed as console toast. Reusable for kick reasons, chat, announcements.

### Spell damage is a flat sentinel (25)
**Severity:** Low — spells all do the same damage
**Context:** Walking SpellItem → effect → magnitude chain adds complexity. Sentinel value works for Phase 2.5.
**Next step:** Read `MagicItem::GetCostliestEffectItem()->GetMagnitude()` when wiring Phase 3+ magic sync.

### No reconnect logic
**Severity:** Low — player must restart Skyrim after disconnect
**Context:** Phase 1 design. `rl connect` from console does reconnect, but auto-reconnect on network drop isn't implemented.
**Next step:** Net thread detects disconnect, waits N seconds, retries. Reset ghost state on reconnect.

### Anti-cheat deferred (Phase 2.4)
**Severity:** Low for friend-trust; would be high for public servers
**Context:** Server trusts client position entirely. Deliberately deferred — friend-trust mod doesn't need it. Future opt-in "strict mode" config.
**Next step:** Only when someone wants to host for untrusted players.

---

## Resolved debt

| Item | Resolved in | How |
|------|-------------|-----|
| Exterior cell crash (adjacent grid squares) | `825960c` | Send cell_form_id=0 for exteriors; only interiors use real FormID |
| EnableAI(false) caused T-pose | `598c595` | Removed EnableAI(false), kept AI on |
| AI overwrites graph vars | `b71f255` | InitiateDoNothingPackage + ActorState stomping |
| ActorState bitmask tearing | `94bd3fd` | Single 32-bit word write instead of per-bit |
| Demo ghost flicker | earlier commit | Aligned synthetic tick with real tick counter |
| Self-rescheduling AddTask infinite loop | earlier commit | Background thread pump at 50ms cadence |
