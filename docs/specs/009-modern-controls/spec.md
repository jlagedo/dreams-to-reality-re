# 009 — Modern controls and combat: camera-relative movement, a free camera, soft lock-on and hit feedback

Status: **Planned, 2026-10-13. Nothing implemented.**
Date: 2026-10-13
Depends on: [000 the recomp](../000-the-recomp/spec.md),
[ai-animation-runtime.md](../../research/ai-animation-runtime.md) (player
controls, action events, attack objects),
[engine.md](../../research/engine.md) (camera, physics, the fixed step)

## In short

Retail Dreams to Reality uses tank controls. Up walks the way Duncan faces,
left and right rotate him, and the camera stays fixed behind his back. Combat
gives the player no help: a swing hits only if Duncan happens to face the
enemy at the right frame, and nothing on screen or in the hands says that it
hit.

This spec adds an optional **modern** scheme as found in current third-person
action games:

- **Movement** relative to the camera: push left and Duncan turns left
  relative to the view and walks there.
- **Camera** on the right stick, orbiting Duncan independently of where he
  faces.
- **Soft lock-on**: an attack turns Duncan toward the best enemy in front of
  him, and projectiles leave toward it.
- **Hit feedback**: hit-stop, rumble, camera shake and a target marker.

The original code keeps every gameplay decision: walk speed, animations,
jumps, collision, damage and the hit test itself. The host only changes which
way Duncan faces, which direction words the controller sees, and where the
chase camera sits.

## Goal and boundary

Goal: with the setting on, a player who has never seen the game can walk,
look around and win a fight with a gamepad, as in a current third-person
game. With it off, the game behaves exactly as retail.

Out of scope:

- **Strafing and a strafing lock-on.** The game has no sidestep clips (owner
  decision, 2026-10-13); walking sideways with the forward walk clip would
  look wrong. A lock-on in this spec only turns Duncan toward the target and
  frames it.
- New animations, new moves, AI changes, difficulty and damage values.
- The DOS builds and the web build. The web build may follow later; it has its
  own input layer.
- Demo playback (see Decisions).

Evidence tags follow [the docs index](../../README.md#evidence-tags).

## Decisions

| Question | Decision | Why |
|---|---|---|
| Default | Retail controls; modern is a setting (`WD_CONTROLS=retail\|modern`, a launcher option, `dreams.ini`) | The verification tools (Unicorn oracles, `collision_replay.py`, the collision walks) are only meaningful against retail behaviour; the fixed step was introduced the same way |
| Where the code lives | `recomp/windream/host/gameplay/`, host-only, entries in `HOST_ENTRIES` (`lift/replacements.py`) | Keeps retail-derived behaviour separate from host-only structure (AGENTS.md) |
| How it changes the game | Wrap retail functions: adjust the inputs (heading, direction words), call the original, restore. No retail function is rewritten | The original code keeps deciding animation, speed, jumps, collision and damage |
| Strafing | None (owner decision, 2026-10-13) | No sidestep clips |
| Demo playback | Always retail controls | Demos store the raw direction words; replaying them through the modern scheme would steer differently |
| Combat assist and feedback | Each part its own setting under the modern scheme (assist strength, hit-stop, rumble, shake, marker) | Players disagree on how much help they want; each part must also be testable alone |

## Part A — What we know

### A1. The ground controller reads four direction words **[verified]**

`ENT_TickPlayerControl` (`0x423767`, once per frame from `GAME_Tick`) runs one
controller by movement mode `+0x34`: ground `ENT_TickPlayerGround`
(`0x421717`), swimming `ENT_TickPlayerSwimming` (`0x422af2`), flying
`ENT_TickPlayerFlying` (`0x422cd7`). Decompiled 2026-10-13 (`ghidra_headless.py
... Decompile.java 00421717 00409de2 0040a866 00423767`).

The ground controller never reads the keyboard. It reads the per-frame action
words that `INPUT_UpdateActions` fills: ↑ `0x49d2fe`, ↓ `0x49d302`, ←
`0x49d306`, → `0x49d30a`, Alt `0x49d316`, Ctrl `0x49d31a`, Space `0x49d30e`,
Esc `0x49d312`, 1/2/3 `0x49d31e`/`322`/`326`. Bit 1 is held. A press is
tested as `& 1 && & 4` and a slot-key release (the cast) as `& 2 && & 4`, so
bit 4 reads as "changed this frame" and bit 2 as "released". **[verified
tests; bit meanings inferred from their use]**

What the direction words drive, outside combat stance:

- ← / → request the turn states `0x3d` / `0x3c` (twice when `0x5e5474` is set;
  see A4);
- ↑ requests walk `4` (`0x2c`/`0x2d` with the sword or bow drawn), and turns a
  Ctrl jump from a forward walk into the running jump `0xb`;
- ↓ requests step back `0x1a`; Ctrl+↓ `0x2e`; Alt+Ctrl+↓ `0x30`;
- take-off into flight (Alt+Ctrl with ↑) and the bow and gun fire paths test ↑.

In combat stance (`+0xac & 0x20`), ↑ is `0x17` with Ctrl and walk without it,
↓ is `0x2e` with Ctrl and `0x2b` alone.

### A2. Turning and speed **[verified]**

From ai-animation-runtime.md, *Player controls*: a turn request adds to the
yaw rate `+0x60` (capped at ±100·Δt) and `ENT_TickEntity` (`0x407b51`) adds the
rate to the heading `+0x5c` each frame; 4096 units are one turn. Speed comes
from the clip's root motion, blended into the velocity with weight `+0x104`/128.
Writing the heading directly is therefore enough to point Duncan; the walk
clip then carries him that way.

### A3. The chase camera reads the heading once **[verified]**

`CAM_UpdateFollowPos` (`0x40a866`) calls `CAM_ComputeChasePos` (`0x409de2`)
for a ground actor outside combat stance, and `CAM_ComputeOrbitPos`
(`0x40a4a5`) for swimming, flying, combat stance, action `0x2e` and falls.
`CAM_ComputeChasePos` reads the actor's heading `+0x5c` in exactly one place
to compute its yaw; the eye goes behind along that yaw and the target ahead.
Everything after that (look pitch `0x52c879`, eye collision
`CAM_CollideEye`, easing, the height band, the dead zone) works on whatever
yaw it got.

Retail already uses an independent yaw: while the debug pause (P, `0x49da20`)
is on, both follow cameras take their yaw from `0x49da24` instead. It starts
from `0x4fbad4` when P is pressed and grows by `10·Δt` per frame, so the camera
circles the frozen player. Nothing else writes `0x49da24`. **[verified]**
What `0x4fbad4` holds is open: demo playback also writes it from its records,
which suggests a copy of the player's heading. **[unverified]**

The modern camera does not reuse `0x49da24`, which belongs to the pause; it
uses the same idea with a host-owned yaw.

### A4. How a hit happens **[verified]**

From ai-animation-runtime.md, *Action events* and *Firing animation*: when an
attack clip passes a fixed fraction of its frames, `ENT_TickEntity` calls
`ENT_SpawnAttackObject` (`0x442944`). `ENT_PlaceAtFacingOffset` (`0x442786`)
places the attack object along the owner's facing vector at distance `+0x10c`
and gives it that vector as its velocity; `ENT_MoveAttackObject` (`0x442e0d`)
moves it; `ENT_TickAttackObject` (`0x444b8f`) tests its collision and
`ENT_ApplyAttackHit` (`0x4440aa`) / `ENT_ApplyDamage` (`0x443619`) take health
and request the target's hit or death state. Aim is therefore the facing at
the spawn frame.

Retail's only assists:

- `AI_ScanNearbyActors` (`0x4131a4`) keeps up to five candidates sorted by
  distance (`+0x1dc`, count `+0x204`), within a facing-angle window and the
  actor's range; `ENT_TickPlayerControl` stores the first live one at
  `+0x1d0`.
- When that enemy is close and outside an angle window
  (`AI_ComputeYawToTarget` (`0x411da4`)), `0x5e5474` is set and the ground
  controller requests each turn twice.
- The head turns toward the target (`ENT_TrackTarget` (`0x4071bb`), ±45°).

Open: the attack object's collision radius and the distance `+0x10c` for the
player's sword, bow and gun; whether the player's melee attack object moves.
B6 measures them before anything is tuned.

### A5. Host input **[verified]**

The host already reads SDL gamepads (`host/sdl/winmm.c`): both sticks, the
triggers and the d-pad, with a radial dead zone; `WD_PAD=winmm|keys|off` and
`WD_PADMAP` map them to WinMM joysticks or to keys
(`host/sdl/input_map.h`). The modern scheme reads the same SDL gamepads
directly.

## Part B — Plan

Each phase is usable on its own and keeps retail as the default.

### B1. Settings and the gameplay module

`host/gameplay/` with one entry point per wrapped retail function, the
setting (`WD_CONTROLS`, launcher, `dreams.ini`), and the stick and key state
read once per frame from the host's input. Forced to retail during demo
playback and in the editor (Develop with the editor flag on).

### B2. The camera on the right stick

Host state: camera yaw and pitch. The right stick (or the keyboard binding,
open question Q1) moves them at a set angular speed.

Wrap `CAM_ComputeChasePos`: save the actor's heading, write the camera yaw in
heading units, call the original, restore the heading. Pitch goes through the
retail look pitch `0x52c879`, which the original clamps to [−88.5°, +45°]; the
wrapper sets the look flag `0x52c844` so the original applies it and the
height band is lifted as in retail look mode. In look mode the original also
moves the look yaw `0x52c875` and pitch with ← → ↑ ↓, so the wrapper clears the
four direction words for the call (restoring them after) and keeps
`0x52c875` at 0.

Behaviour to settle by playing:

- The eye eases toward its goal in a straight line (¼ of the gap per frame),
  so a fast turn cuts inside the circle. If that feels wrong, rotate the
  current eye about Duncan by the frame's yaw change before the call.
- Without right-stick input the camera keeps its yaw. A slow recentre behind
  the walking direction is an option (setting).
- At level start, after a scripted shot and on respawn, the camera yaw is set
  from Duncan's heading.

### B3. Camera-relative movement on the left stick

Wrap `ENT_TickPlayerGround`. When the stick is out of its dead zone:

1. target heading = camera yaw + stick angle;
2. turn `+0x5c` toward it at a fast set rate (a full turn in about 0.3 s, a
   setting) and zero the yaw rate `+0x60`;
3. set ↑ held in `0x49d2fe`, clear ↓ ← → for the call, call the original,
   restore the four words.

Jumps, the running jump, pick-up, spells and attacks still come from the
original controller. Pulling the stick toward the camera turns Duncan around;
the step back `0x1a` is no longer reachable.

The ↓ combinations (Ctrl+↓ `0x2e`, Alt+Ctrl+↓ `0x30`, ↓ in combat stance)
need their own buttons; the wrapper sets ↓ for the call when that button is
held (open question Q2).

Below the walk threshold the stick only turns Duncan in place.

### B4. Swimming and flying

The same wrappers on `ENT_TickPlayerSwimming`, `ENT_TickPlayerFlying` and
`CAM_ComputeOrbitPos`. Flying also has pitch and bank (`+0x6c`, `+0x64`); the
vertical controls are to be read from the flying controller first.

### B5. Scripted cameras

In camera modes other than follow (fixed, track, pair, ride; engine.md), the
movement basis is the camera on screen, read from node 0. When the shot
changes while the stick is held, the old basis is kept until the stick is
released or returns to the dead zone.

### B6. Measure combat first

A host log of every player attack: action, weapon, the spawn frame, the
target `+0x1d0` and every candidate's distance and angle off the facing, and
whether `ENT_ApplyAttackHit` was reached. A scripted fight through the control
channel (projects with near enemies) gives the miss causes: angle, range or
timing. The open values of A4 (radius, `+0x10c`) are read in the same pass.

### B7. Soft lock-on

When an attack state starts, pick a target: the live candidates of
`AI_ScanNearbyActors` (and `+0x1d0` first), within a cone around the stick
direction (or the facing with the stick centred) and a range per weapon,
scored by angle and distance. Turn Duncan toward it over the wind-up frames,
reaching it by the spawn frame. For projectiles (bow, gun, spells), wrap
`ENT_PlaceAtFacingOffset` for player-owned objects so the object leaves
toward the target, its height included.

Only if B6 shows the reach is the cause: a modest increase of the player's
attack-object distance or radius, as its own setting.

### B8. Lock-on without strafing

A held button (left trigger) keeps the current target: Duncan faces it while
attacking and standing, and the camera yaw follows the line from Duncan to the
target so both stay in view. Walking still turns him to the stick direction
(no strafe, see Goal and boundary). It ends on release, on the target's death
or out of range.

### B9. Hit feedback

All triggered from a wrapper on `ENT_ApplyAttackHit` when the attacker is the
player or the target is the player.

| Feedback | How |
|---|---|
| Hit-stop | Hold the game for a few 30 Hz steps (`host/sdl/pacing.c`); display interpolation keeps drawing |
| Rumble | `SDL_RumbleGamepad`, stronger for a kill and for hits taken |
| Camera shake | A short decaying offset on the chase camera's result, display-only if possible |
| Target marker | A marker over the B7/B8 target, drawn by the host over the frame (the direct renderer's overlay path, as the Develop tools draw) |
| Hit flash | Optional: a brief tint on the hit model in the direct renderer; the software renderer may skip it |

## Checks

| Check | How |
|---|---|
| Retail unchanged | With `WD_CONTROLS=retail` the existing tests and `motion_trace.py` give the same results as before |
| Movement goes where pushed | Control-channel test: set the camera yaw, push the stick in eight directions, Duncan's step direction is within a few degrees of camera yaw + stick angle after the turn |
| Camera orbits | Right stick held: the eye circles Duncan at the set rate, distance and height within the preset band, eye collision still active (`camera_trace.py`) |
| Collision intact | `collision_walk.py` and `collision_regress.py` with modern controls report nothing new |
| Moves still reachable | Jump, running jump, pick-up, the three ↓ moves through their new buttons, take-off, casting: each reached in a scripted run |
| Combat assist | The B6 log over the same scripted fights: share of attacks that hit, retail against modern |
| Feedback | Hit-stop length in steps, rumble calls and marker position read from the log and a screenshot |
| Demo playback | A demo plays back identically with the setting on |

## Open questions

| | Question |
|---|---|
| Q1 | Keyboard scheme: WASD with mouse camera, or the arrows camera-relative with two keys to turn the camera |
| Q2 | Buttons for the ↓ moves (Ctrl+↓, Alt+Ctrl+↓, ↓ in combat stance) |
| Q3 | What `0x4fbad4` holds (A3) |
| Q4 | The player's attack-object reach and radius per weapon (A4, B6) |
| Q5 | Whether the recentring camera (B2) is on by default |
