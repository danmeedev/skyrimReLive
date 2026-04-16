# Playtest Checklist

A step-by-step session script for two players to validate every shipped
feature. Run through this end-to-end after each major update. Takes
~15-20 minutes.

**Players:** Host (runs the server + Skyrim) and Friend (runs Skyrim only).

**Before you start:**
- Host: server binary built and ready
- Both: latest `SkyrimReLive.dll` installed via friend bundle or manual copy
- Both: Tailscale connected (or on same LAN)
- Both: a save in **Whiterun exterior** (near the main gate works well)

---

## Phase A — Connection (2 min)

### A1. Server starts clean
- **Host:** start the server (`cargo run` from `server/`)
- [ ] Server log shows `skyrim-relive-server listening ... pvp=false`

### A2. Host connects
- **Host:** launch Skyrim via `skse64_loader.exe`, load save
- [ ] `SkyrimReLive.log` shows `Welcome: player_id=1`
- [ ] Console `rl status` → `state=connected server=127.0.0.1:27015 player_id=1`

### A3. Friend connects
- **Friend:** launch Skyrim, load save
- [ ] Friend's log shows `Welcome: player_id=2`
- [ ] Friend's `rl status` → `state=connected`
- [ ] Server log shows two `Hello accepted` lines

### A4. Both see ghost count
- **Both:** `rl status`
- [ ] Host sees `ghosts=1`
- [ ] Friend sees `ghosts=1`

---

## Phase B — Position & Interpolation (2 min)

### B1. Ghost appears at correct location
- **Both in Whiterun exterior, near each other**
- [ ] Host sees Friend's ghost (Lydia clone) near Friend's actual position
- [ ] Friend sees Host's ghost near Host's actual position

### B2. Position tracks in real time
- **Friend:** walk forward slowly
- [ ] Host sees Friend's ghost move in the same direction, smoothly (no teleporting)

### B3. Rotation tracks
- **Friend:** spin in place (turn 360 degrees)
- [ ] Host sees Friend's ghost rotate to match

### B4. Interpolation is smooth
- **Friend:** run back and forth quickly
- [ ] Host sees smooth motion, no violent teleports, slight ~100ms delay is normal

---

## Phase C — Locomotion Animation (3 min)

### C1. Walk animation
- **Friend:** walk slowly (push stick gently / tap W)
- [ ] Host sees ghost play walk animation (slow gait, arms relaxed)
- **Result:** PASS / FAIL / ghost slides / ghost T-poses / ___________

### C2. Run animation
- **Friend:** run (full forward)
- [ ] Host sees ghost play run animation (faster gait, arms pumping)
- **Result:** PASS / FAIL / ___________

### C3. Sprint animation
- **Friend:** sprint (hold sprint key while running)
- [ ] Host sees ghost sprint (fastest gait)
- **Result:** PASS / FAIL / ___________

### C4. Sneak animation
- **Friend:** enter sneak (press crouch)
- [ ] Host sees ghost crouch into sneak posture
- **Result:** PASS / FAIL / ___________

### C5. Idle (standing still)
- **Friend:** stand still for 5 seconds
- [ ] Host sees ghost standing idle (not walking in place, not T-posing)
- **Result:** PASS / FAIL / ___________

---

## Phase D — Weapon State (2 min)

### D1. Draw weapon
- **Friend:** draw a one-handed weapon
- [ ] Host sees ghost play draw animation, end in weapon-out stance
- **Result:** PASS / FAIL / ___________

### D2. Sheath weapon
- **Friend:** sheath the weapon
- [ ] Host sees ghost play sheath animation, return to unarmed stance
- **Result:** PASS / FAIL / ___________

### D3. Two-handed weapon
- **Friend:** equip and draw a two-handed weapon (greatsword, battleaxe)
- [ ] Host sees ghost hold two-handed stance
- **Result:** PASS / FAIL / ___________

### D4. Bow draw
- **Friend:** equip a bow, draw it (hold attack)
- [ ] Host sees ghost in bow-draw pose
- **Result:** PASS / FAIL / ___________

---

## Phase E — Pitch / Aiming (1 min)

### E1. Look up
- **Friend:** aim straight up at the sky
- [ ] Host sees ghost actor tilted upward
- **Result:** PASS / FAIL / no visible tilt / ___________

### E2. Look down
- **Friend:** aim at the ground
- [ ] Host sees ghost actor tilted downward
- **Result:** PASS / FAIL / ___________

---

## Phase F — Combat: PvP OFF (2 min)

> Server should be running with default config (`pvp_enabled = false`).

### F1. Melee swing does nothing
- **Host:** draw sword, walk up to Friend's ghost, swing
- [ ] No stagger on Friend's screen
- [ ] No `[SkyrimReLive] hit by...` message in Friend's console
- [ ] Server log shows `CombatEvent dropped: PvP disabled`
- **Result:** PASS / FAIL / ___________

---

## Phase G — Combat: PvP ON (3 min)

> **Host:** stop the server. Edit `server.toml`: set `pvp_enabled = true`.
> Restart the server. Both players reconnect (restart Skyrim or `rl connect`).

### G1. Melee hit registers
- **Host:** draw sword, swing at Friend's ghost
- [ ] Friend sees console message `[SkyrimReLive] hit by player_id=1 for X dmg (HP Y)`
- [ ] Server log: `combat hit applied`
- **Result:** PASS / FAIL / ___________

### G2. Stagger on heavy hit
- **Host:** do a power attack (hold attack button) on Friend's ghost
- [ ] Friend staggers visibly (character pushed back / stumbles)
- [ ] Console message includes `[STAGGER]`
- **Result:** PASS / FAIL / ___________

### G3. Bow hit registers
- **Host:** equip bow, stand ~30 feet away, shoot Friend's ghost
- [ ] Friend sees hit message
- [ ] Host log: `shipped CombatEvent: ... class=1`
- **Result:** PASS / FAIL / server rejects "out of range" / ___________

### G4. Spell hit registers
- **Host:** equip a destruction spell (Flames, Firebolt), cast at Friend
- [ ] Friend sees hit message (damage ~25 = spell sentinel)
- [ ] Host log: `shipped CombatEvent: ... class=2`
- **Result:** PASS / FAIL / ___________

### G5. Reverse direction — Friend hits Host
- **Friend:** swing melee at Host's ghost
- [ ] Host sees hit message + stagger
- **Result:** PASS / FAIL / ___________

---

## Phase H — Cell Transitions (3 min)

### H1. Enter interior together
- **Both:** go to Dragonsreach door (Whiterun exterior)
- **Host:** enter Dragonsreach
- [ ] Friend's ghost of Host despawns (different cells now)
- **Friend:** enter Dragonsreach
- [ ] Host sees Friend's ghost spawn inside Dragonsreach
- [ ] Friend sees Host's ghost inside Dragonsreach
- **Result:** PASS / FAIL / ghost visible in wrong cell / crash on load / ___________

### H2. Exit interior
- **Friend:** exit Dragonsreach back to exterior
- [ ] Host (still inside) sees Friend's ghost despawn
- **Host:** exit Dragonsreach
- [ ] Both see each other's ghosts in Whiterun exterior again
- **Result:** PASS / FAIL / ___________

### H3. Fast travel separation
- **Host:** fast travel to Riften (or any distant location)
- [ ] Friend stops seeing Host's ghost (cell mismatch)
- [ ] Host stops seeing Friend's ghost
- [ ] Both `rl status` still show `state=connected`
- [ ] No crash during fast travel load screen
- **Result:** PASS / FAIL / ___________

### H4. Rejoin after fast travel
- **Host:** fast travel back to Whiterun
- [ ] Both see each other's ghosts again
- **Result:** PASS / FAIL / ___________

---

## Phase I — Disconnect & Reconnect (2 min)

### I1. Clean disconnect
- **Friend:** open console, type `rl disconnect`
- [ ] Host sees Friend's ghost despawn within ~5 seconds
- [ ] Friend `rl status` → `state=idle`
- **Result:** PASS / FAIL / ghost stays / ___________

### I2. Reconnect
- **Friend:** type `rl connect`
- [ ] Friend `rl status` → `state=connected` with new player_id
- [ ] Host sees Friend's ghost respawn
- **Result:** PASS / FAIL / ___________

### I3. Hard exit
- **Friend:** Alt-F4 Skyrim (hard close, no clean disconnect)
- [ ] Host sees Friend's ghost despawn after ~5 seconds (timeout)
- **Result:** PASS / FAIL / ghost never despawns / ___________

---

## Phase J — Edge Cases (optional, 2 min)

### J1. Console teleport (coc)
- **Friend:** open console, type `coc WhiterunDragonsreach`
- [ ] Host sees Friend's ghost despawn from exterior
- [ ] If Host also `coc WhiterunDragonsreach`, ghost reappears inside
- **Result:** PASS / FAIL / ___________

### J2. Demo ghost while connected
- **Host:** `rl demo start`
- [ ] Demo ghost orbits Host, separate from Friend's real ghost
- **Host:** `rl demo stop`
- [ ] Demo ghost despawns, Friend's ghost unaffected
- **Result:** PASS / FAIL / ___________

### J3. Load a different save mid-session
- **Friend:** save current game, load a different save
- [ ] No crash during load
- [ ] After loading, `rl status` shows connected (auto-reconnect on load)
- [ ] Ghost reappears if both in same cell
- **Result:** PASS / FAIL / ___________

---

## Results Summary

**Date:** ___________
**Build:** ___________  (commit hash from `git log --oneline -1`)
**Host:** ___________  **Friend:** ___________
**Connection:** Tailscale / LAN / WAN (circle one)

| Phase | Pass | Fail | Notes |
|-------|------|------|-------|
| A — Connection       | /4 | | |
| B — Position         | /4 | | |
| C — Locomotion       | /5 | | |
| D — Weapon state     | /4 | | |
| E — Pitch            | /2 | | |
| F — Combat PvP OFF   | /1 | | |
| G — Combat PvP ON    | /5 | | |
| H — Cell transitions | /4 | | |
| I — Disconnect       | /3 | | |
| J — Edge cases       | /3 | | |
| **Total**            | **/35** | | |

**Blocking issues (must fix before next session):**

1. ___________
2. ___________
3. ___________

**Non-blocking issues (note and fix later):**

1. ___________
2. ___________
3. ___________
