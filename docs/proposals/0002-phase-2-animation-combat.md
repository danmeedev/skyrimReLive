# 0002 — Phase 2: animation graph sync + combat

- **Status:** accepted
- **Author(s):** SkyrimReLive contributors
- **Created:** 2026-04-15
- **Supersedes:** —

## Summary

Extend Phase 1's position replication with animation-state sync so remote
players visibly walk, run, draw weapons, attack, block, and take hits.
Combat damage passes through the server for authority (tightening the H1
caveat from Phase 1). End state: two players in one cell can see each other
moving naturally and engage in basic melee combat.

## Motivation

Phase 1 ghosts teleport/slide across the ground with no animation — they
look like mannequins on rails. To feel like multiplayer, remote players
need to:

1. Play the correct locomotion animation (idle → walk → run → sprint).
2. Show weapon draw/sheath transitions.
3. Play attack, block, and stagger animations.
4. Take damage from other players (server-validated).

Without animation, "multiplayer" is just two dots on a map. With it, you
can actually play together.

## Design

### Scope

**In:**
- Locomotion state: idle, walk, run, sprint, sneak, jump, fall, land.
- Weapon state: sheathed, drawing, drawn, sheathing, two-hand vs one-hand
  vs bow vs magic (stance detection).
- Basic melee combat: attack (light, power), block, hit reaction, stagger.
- Damage authority: client reports "I swung", server validates range + timing,
  server applies damage to target, target client plays hit reaction.
- Death / ragdoll (simplified: ghost goes limp, respawns after N seconds).

**Out (deferred):**
- Ranged combat (bow projectile sync — Phase 2.5 or 3).
- Magic / spell effects (Phase 3+).
- Shouts (Phase 3+).
- Mounted combat (Phase 4).
- NPC combat AI (Phase 4).
- Full equipment sync / visual appearance matching (Phase 3).
- Stealth meter / detection sync (Phase 3).

### Animation graph variables

Skyrim uses Havok Behavior for animation. The animation state machine is
driven by a set of graph variables (bools, floats, ints) that the engine
reads and writes each frame. Key variables for Phase 2:

**Locomotion:**
- `Speed` (float) — current movement speed.
- `Direction` (float) — movement direction relative to facing (radians).
- `IsRunning` (bool) — walk vs run.
- `IsSprinting` (bool).
- `IsSneaking` (bool).
- `IsInAir` (bool) — jump/fall.
- `Jumping` (bool) — ascending phase of a jump.

**Weapon state:**
- `IsEquipping` / `IsUnequipping` (bool) — draw/sheath transition.
- `iState` (int) — overall combat state (0=unarmed, 1-8 various stances).
- `IsAttacking` (bool).
- `IsBlocking` (bool).
- `IsPowerAttacking` (bool).
- `IsStaggered` (bool).
- `iLeftHandType` / `iRightHandType` (int) — weapon/spell category.

Phase 2 replicates a curated **whitelist** of these variables — not the
entire graph (which has hundreds of internal variables that are
engine-driven and shouldn't be overridden remotely). The whitelist approach
is what Skyrim Together Reborn uses; it's the known-safe pattern.

### Wire-format additions

New fields in `PlayerInput` (client → server):

```
table PlayerInput {
    transform: Transform;         // existing
    client_time_ms: uint64;       // existing
    // Phase 2 additions:
    anim_floats: [AnimFloat];     // named float variables
    anim_bools: [AnimBool];       // named bool variables
    anim_ints: [AnimInt];         // named int variables
    weapon_drawn: bool;           // convenience: is weapon currently drawn
}

struct AnimFloat { name_hash: uint32; value: float; }
struct AnimBool  { name_hash: uint32; value: bool; }
struct AnimInt   { name_hash: uint32; value: int32; }
```

Using name hashes (CRC32 of the variable name string) instead of strings
saves ~20 bytes per variable per packet. The whitelist is compiled into
both client and server so the hash→name mapping is known at build time.

New fields in `PlayerState` within `WorldSnapshot` (server → client):

```
struct PlayerState {
    player_id: uint32;            // existing
    transform: Transform;         // existing
    // Phase 2 additions: same anim fields as PlayerInput
    anim_floats: [AnimFloat];
    anim_bools: [AnimBool];
    anim_ints: [AnimInt];
    weapon_drawn: bool;
}
```

**Important:** `PlayerState` is currently a Flatbuffers `struct` (inline,
fixed-size). Adding variable-length arrays (`[AnimFloat]`) requires
converting it to a `table` (heap-allocated, pointer-based). This is a
**breaking wire-format change** requiring a protocol version bump to `2`.
Phase 1 clients reject v2 packets via the header check; clean failure.

### Combat event messages

New message types (added to `types.fbs` MessageType enum):

```
CombatEvent = 32,   // C → S: "I swung my weapon"
DamageApply = 33,   // S → C: "you were hit for X damage"
```

```
table CombatEvent {
    attack_type: ubyte;    // 0=light, 1=power, 2=bash
    target_id: uint32;     // player_id of intended target (0=no target)
    weapon_reach: float;   // from weapon record, for server-side range check
}

table DamageApply {
    attacker_id: uint32;
    damage: float;
    stagger: bool;         // should target play stagger animation
}
```

### Authority model (tightens H1)

Phase 1 trusted client transforms entirely. Phase 2 tightens:

1. **Transforms:** server validates plausibility — rejects teleport jumps
   (> 500 units/tick), clamps speed to sprint max. If a client violates,
   server snaps them back to last-valid position and logs a warning.

2. **Combat:** client reports "I attacked" + target + weapon reach. Server
   checks: is attacker within reach of target? Is attack timing plausible
   (not spamming faster than animation allows)? If valid, server computes
   damage from weapon stats (stored server-side or reported+validated) and
   sends `DamageApply` to target. Target client plays hit reaction.

3. **Health:** server tracks health as a component. Death is server-decided.

### Client implementation

**Reading animation variables** from the local player:

```cpp
auto* player = RE::PlayerCharacter::GetSingleton();
auto* controller = player->GetActorRuntimeData().behaviorGraphComponent;
// Read variable by name:
float speed;
controller->GetGraphVariableFloat("Speed", speed);
```

CommonLibSSE-NG exposes `GetGraphVariableFloat`, `GetGraphVariableBool`,
`GetGraphVariableInt` on the behavior component. We read the whitelisted
variables each tick and include them in `PlayerInput`.

**Applying animation variables** to a ghost actor:

```cpp
ghost->SetGraphVariableFloat("Speed", remoteSpeeed);
ghost->SetGraphVariableBool("IsRunning", remoteIsRunning);
// etc.
```

The ghost's animation graph responds to these variable changes the same way
the player's does — it transitions between idle, walk, run, etc. based on
the variable values. This is the proven approach from Skyrim Together.

### Pitch replication

Phase 2 adds pitch (aim angle) to the `Transform` struct:

```
struct Transform {
    pos: Vec3;
    yaw: float;
    pitch: float;   // Phase 2: weapon aim angle (radians)
}
```

This changes the struct size (breaking wire change — covered by the v2
version bump). Pitch is needed for:
- Bow aiming direction (Phase 2.5)
- Visual head tracking on ghosts
- Combat range validation (vertical component)

### Tick rate changes

Animation variable reads happen at 60 Hz (same as PlayerInput). But not
all variables change every tick. **Delta compression:** only include
variables whose value changed since last send. For a standing player, this
reduces the anim payload from ~20 variables to 0 most ticks.

Snapshot rate stays at 20 Hz. The server interpolates the 60 Hz input
stream to 20 Hz output — or just forwards the latest values (simpler,
Phase 2 default).

## Alternatives considered

### Full animation graph replication

Instead of a variable whitelist, replicate the entire Havok behavior tree
state. Rejected: hundreds of variables, many engine-internal, setting wrong
values causes crashes or T-poses. The whitelist approach is battle-tested
by STR.

### Client-authoritative combat

Let the attacking client decide damage. Rejected: trivially exploitable;
violates H1 completely. Server authority on damage is non-negotiable for
any multiplayer mod that might be self-hosted publicly.

### Animation events instead of variables

Use Skyrim's animation event system (`SendAnimationEvent`) instead of
graph variables. Rejected for locomotion (events are discrete, locomotion
is continuous). Useful for one-shot events (attack, block start/end) —
Phase 2 may use a hybrid: variables for continuous state, events for
discrete transitions.

## Open questions

1. **Variable whitelist composition.** Exact set of whitelisted variable
   names needs testing — some variables are version-specific or have
   surprising side effects when set on non-player actors. Build the list
   incrementally: start with Speed + Direction + IsRunning, add more
   as we test.

2. **Weapon stat source.** Where does the server get weapon base damage
   for validation? Options: client reports it (server validates against a
   known weapon FormID table), or server loads ESM data at startup (heavy
   but authoritative). Defer decision to implementation.

3. **Hit detection geometry.** Server-side range check uses Euclidean
   distance + weapon reach. Is that good enough, or do we need facing-
   angle checks too? Start with distance-only, add angle if exploitable.

4. **Ragdoll sync.** Ragdoll on death is physics-driven per client. Do
   we replicate ragdoll bone positions, or just play a canned death
   animation? Canned is simpler and good enough for Phase 2.

5. **Equipment appearance.** Ghosts currently look like the player base
   (or Lydia). Phase 2 doesn't sync visual equipment — the ghost's
   appearance doesn't change when the remote player equips different armor.
   Phase 3 addresses this with an equipment-sync message.

## Migration / rollout

- **Wire-format version bump: v1 → v2.** PlayerState becomes a table (was
  struct). Transform gains pitch. New message types CombatEvent/DamageApply
  added. v1 clients get Disconnect(VersionMismatch) from a v2 server.
- **Backward compatibility: none.** Both client and server must be rebuilt
  from the same commit. Phase 1 is alpha; no deployed userbase to migrate.
- **Incremental implementation:** locomotion anim sync first (sub-step 2.1),
  then weapon draw/sheath (2.2), then combat events (2.3). Each sub-step
  is independently testable with the demo ghost.

## Implementation steps

1. **Step 2.1: locomotion anim sync.** Read Speed/Direction/IsRunning/
   IsSprinting/IsSneaking from local player, add to PlayerInput, apply
   to ghost via SetGraphVariable. Demo ghost gets a `--walk` mode that
   simulates locomotion variables. Test: ghost walks when you walk.

2. **Step 2.2: weapon state sync.** Read iState/weapon drawn/equip
   transitions. Apply to ghost. Test: ghost draws weapon when you do.

3. **Step 2.3: combat events + damage authority.** New message types.
   Server validates range, applies damage, sends DamageApply. Ghost plays
   attack/hit/stagger animations. Test: hit demo ghost, see it react.

4. **Step 2.4: transform validation.** Server rejects teleport/speedhack.
   Test: modify echo-client to send impossible positions, verify server
   clamps.

5. **Step 2.5: pitch + ranged prep.** Add pitch to Transform, replicate
   head tracking. Lay groundwork for bow combat (Phase 2.5 or Phase 3).

## Changelog

- 2026-04-15: initial draft.
- 2026-04-16: accepted as-is. Step 2.1 implementation simplification noted:
  use fixed flatbuffer fields (5 variables) instead of hash-keyed arrays
  for the locomotion sub-step. Hash arrays revisit when variable count
  exceeds ~10 in step 2.2/2.3.
