# AI, movement, and animation runtime

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `tools/check_names.py`.

Retail-binary trace of how a project actor becomes an animated character, how
the actor AI chooses actions, and how action numbers reach `.DAN` clips.
Addresses are virtual addresses from Disc 1 `WINDREAM.EXE`, image base
`0x400000`. Claims marked **[verified]** are static observations in the shipped
executable or game data. Action names remain **[unverified]** where the files
and code store numbers rather than labels.

## Runtime shape

```mermaid
flowchart LR
    DAT["DREAMS.DAT project + OBJET records"] --> Spawn["SCENE_LoadLevel / ENT_InstantiateFromObjet"]
    Spawn --> Actor["Runtime actor and AI fields"]
    Actor --> Schedule["AI_TickSquads AI scheduler"]
    Schedule --> Decide["target, range, and random action choice"]
    Decide --> Request["actor +0x160 requested action"]
    Request --> Resolve["ANIM_ApplyPendingState state-to-clip lookup"]
    Resolve --> Clip["model-family slot AN###"]
    Clip --> Pose["skeletal pose, blend, and root translation"]
    Pose --> Physics["movement and collision update"]
    Pose --> Event["animation-time effect event"]
    Event --> Shot["effect actor class 0x14"]
    Shot --> Hit["collision and damage"]
```

The game has a central actor updater and a separate AI scheduler. AI requests a
numbered action through the same actor state fields used by player actions; the
animation system resolves that number in the actor's loaded model family. The
executable contains transition tables for AI modes and a separate per-model
action-to-clip table. **[verified]**

## Direct answers

- **Movement-to-animation table:** yes. `0x004f7728` is a 16-family × 64-action
  table mapping requested action numbers to loaded `AN###` clips, with per-slot
  playback flags.
- **AI chain/schedule:** yes. `AI_BuildSquads` (`0x4115a1`) groups eligible actors into
  runtime controllers, and `AI_ApplySquadOrderTable` (`0x41208f`) applies a project-selected mode
  transition list.
- **AI layer:** in `WINDREAM.EXE`, the per-frame dispatcher is `AI_TickSquads` (`0x415109`);
  target collection and decisions run in `AI_ScanNearbyActors` (`0x4131a4`), `AI_UpdateMemberStatus` (`0x414d61`), and
  `AI_TickCombat` (`0x414646`).
- **NPC firing:** Project54's `IBI.DAN` is a concrete retail match: its target
  AI requests actions that resolve to `IBIAN016`; an animation-timed handler
  launches a moving effect whose collision path applies damage.

## AI schedule and actor chains

### Per-frame scheduler

`GAME_Tick` (`0x4240ba`) calls `AI_TickSquads` (`0x415109`). The scheduler walks several active actor
lists in a fixed order: **[verified]**

| List | Count | Pass | Observed role |
|---|---|---|---|
| `0x005df3ec` | `0x005df474` | `AI_ApplySquadOrderTable` (`0x41208f`) | Apply project-selected actor mode transitions. |
| `0x005df3ac` | `0x005df470` | `AI_DispatchSquadOrders` (`0x413b3b`) | Update linked or grouped AI actors. |
| `0x005df42c` | `0x005df46c` | `AI_RunMemberOrders` (`0x414b24`) | Run per-actor steering and action decisions. |
| `0x005df42c` | `0x005df46c` | `AI_UpdateMemberStatus` (`0x414d61`) | Refresh actor candidate and target state. |
| `0x005df3ac` | `0x005df470` | `AI_AggregateSquadStatus` (`0x414f6d`) | Aggregate status from linked actors. |

AI group-controller records store a linked-member count at `+0x19c` and member
pointers beginning at `+0x1a0`. `AI_AddSquadMember` (`0x410f03`) increments that count, appends a
member pointer, and stores the controller back-pointer on the member at
`+0x1ac`. The controller's mode is at `+0x1b4`, previous mode at `+0x1bc`,
status bits at `+0x1b0`, and current target at `+0x1d0`.
**[verified]**

`AI_BuildSquads` (`0x4115a1`) builds the lists on scene load, and it is re-run
from `GAME_Tick` (`0x4240ba`), `0x41e5af`, the trigger tick `SCENE_TickTriggers`
(`0x429061`), and the hit resolver `ENT_ApplyAttackHit` (`0x4440aa`). It scans the 32 project-actor
slots, filters active/eligible actors, separates them by actor flag `+0xa9`
bit `0x20`, and groups each class in batches of up to three. The
`0x005df3ec` and `0x005df3ac` arrays hold controller records; `0x005df42c`
holds member actors. `AI_ResetSquadNode` (`0x410e5d`) initializes both record types. **[verified]**

`AI_RunMemberOrders` (`0x414b24`) selects work from bits in the mode word: bit `0x10` runs the
look/steering path, bit `0x02` runs the general behavior path, and bit `0x04`
runs the target action selector `AI_TickCombat` (`0x414646`). `AI_DispatchSquadOrders` (`0x413b3b`) applies mode
changes across an actor's linked members. **[verified]**

### Executable transition lists

`AI_ApplySquadOrderTable` (`0x41208f`) selects a transition list from a pointer array at `0x0049d5a0`,
using the project header value at `+0xd0`. It compares the controller's current
mode and status bits, then changes that mode. Each row is three integers:

```text
current AI mode, required status-bit mask, next AI mode
```

The current retail `DREAMS.DAT` uses selector 0 in 143 projects and selector 1
in 7 projects: 28, 57, 78, 90, 117, 130, and 146. The executable also contains
a third list (selector 2), which no retail project selects. **[verified]**

| Selector | Transition rows (`from`, required `+0x1b0` bits, `to`) |
|---:|---|
| 0 | `(0x50, 0x000, 0x40)` |
| 1 | `(0x00, 0x000, 0x01)`, `(0x01, 0x800, 0x20)`, `(0x20, 0x040, 0x01)`, `(0x01, 0x004, 0x10)`, `(0x10, 0x400, 0x00)` |
| 2 | `(0x00, 0x000, 0x01)`, `(0x01, 0x800, 0x20)`, `(0x20, 0x040, 0x02)`, `(0x02, 0x004, 0x10)`, `(0x10, 0x400, 0x00)` |

The selector is a project-level schedule choice, while mode and status live on
each runtime actor. This is the recovered AI transition list; it is compiled
into the executable rather than stored as a named script file. **[verified]**

### Target selection and attack-like actions

`AI_ScanNearbyActors` (`0x4131a4`) builds candidate lists from active actors. It sorts them by a
distance metric and keeps at most five. It uses actor group flags, a
facing-angle window, and the actor's configured range. Candidate pointers are
stored at actor `+0x1dc` and `+0x1f0`, with counts at `+0x204` and `+0x208`.
`AI_UpdateMemberStatus` (`0x414d61`) checks the candidate set and assigns the selected actor pointer
to `+0x1d0`; one path requires a helper result below 600. The helper's unit and
full meaning remain unresolved. **[verified condition; helper interpretation open]**

`AI_TickCombat` (`0x414646`) uses the selected target, distance, facing, and a random
threshold to request numbered actions. Its attack-like branch chooses among
state IDs `0x10`, `0x14`, `0x12`, and `0x16`. It also requests state `0x39` when
its collision test succeeds and state `0x1c` on a separate timed path.
`ANIM_RequestState` (`0x405118`) places these requests in actor `+0x160`; the ordinary actor
update later resolves and applies them. **[verified]**

The source object fields feed those checks: `OBJET +0x78` is copied to runtime
actor `+0x118`, which the decision code squares for a target-distance limit;
`OBJET +0x88` is copied to `+0x194`, used as a random-action threshold.
**[verified]**

### Project54 `IBI.DAN`: firing path

Project54 is *Exterieur Armee*. Its `OBJET1` is `IBI.DAN`, the only retail
`OBJET` with behavior selector 5. Its record sets `+0x70 = 585`, `+0x78 = 6250`,
and `+0x88 = 20`. Its flags word is `0x1247` (byte `+0x35 = 0x12`); that byte's
`0x10` bit maps to actor `+0xae & 0x02`, which gates the target-action path.
`ENT_InstantiateFromObjet` (`0x41deb8`) remaps selector 5 to runtime class 1 and sets actor flag
`+0xad & 0x20`; it also copies the three parameters to actor `+0x10c`, `+0x118`,
and `+0x194`. **[verified from Disc 2 `IBI.DAN` and retail `DREAMS.DAT`]**

IBI has clips `AN000`, `AN002`, `AN016`, `AN050`, and `AN057`. The AI branch
requests states 16, 20, 18, or 22. `ANIM_LoadEntitySet` (`0x404d98`) backfills missing slots from
the nearest earlier loaded clip, so all four requests resolve to `IBIAN016`
(82 frames). The separate collision branch requests state 57, which resolves
to `IBIAN057` (82 frames). **[verified]**

The shot effect is synchronized to the attack animation. `ENT_TickEntity` (`0x407b51`) calls
`ENT_SpawnAttackObject` (`0x442944`) when the attack state's frame threshold is crossed. The effect
builder allocates a transient actor; IBI's `+0xad & 0x20` flag makes it effect
class `0x14`. `ENT_PlaceAtFacingOffset` (`0x442786`) writes the owner's facing
vector scaled by `+0x10c` (IBI's value 585) into the effect's `+0x18`/`+0x1c`/`+0x20`
and sets the effect's position to the owner's position plus that vector (with a
constant Y offset from `0x4c585c`); it is a general helper that `0x419a3e` and
`SCENE_TickTriggers` (`0x429061`) also call. `ENT_MoveAttackObject` (`0x442e0d`) advances effect classes other than `0x10` by their
velocity. `ENT_TickAttackObject` (`0x444b8f`) is the per-tick update of an attack
object, called from `ENT_TickAll` (`0x407089`), and includes its collision test; for class `0x14`,
`ENT_ApplyAttackHit` (`0x4440aa`) calls `ENT_ApplyDamage` (`0x443619`) to reduce the target's health at `+0x38` by
10 and request its hit/death state. **[verified]** This confirms the target-
driven animation emits a damaging moving projectile effect.

This is the recovered NPC firing sequence: the IBI controller requests one of
four numeric attack states, each resolves to `IBIAN016`, and the animation
event launches a damaging moving effect. The engine contains no clip label or
effect name that calls this a fireball; the exact visual/effect resource
identity is still open. **[verified sequence; effect label unverified]**

### `OBJET` fields: verified runtime use and limits

`ENT_InstantiateFromObjet` (`0x41deb8`) creates an actor from a project's `OBJET` record. **[verified]**

| `OBJET` offset | Runtime use observed |
|---:|---|
| `+0x34` | Flag word. Its low bits control active/character behavior; it is not an entity-kind enum. |
| `+0x64` | Copied to actor `+0x34` as a behavior/class selector. Values 5 and 6 are remapped to runtime classes 1 and 3, with extra flags. |
| `+0x68` | Copied as a movement scale to actor `+0x104`. |
| `+0x6c` | Copied to actor `+0x108`, the **turn step**: `ANIM_RequestState` (`0x405118`) turns by it (default `0x30` of 4096 per turn, ¾ of it unless flying). Not a `BOX` index. **[verified]** 2026-09-26 |
| `+0x70` | Copied to actor `+0x10c`; used as the facing-offset magnitude for attack effects by `ENT_PlaceAtFacingOffset` (`0x442786`). |
| `+0x78` | Target-distance parameter copied to actor `+0x118`. |
| `+0x88` | Random-action threshold copied to actor `+0x194`. |

This corrects the earlier `project.py` comment that treated `+0x6c` as a local
`BOX` route index. Retail values such as 63, 113, and 128 exceed the 0–11 `BOX`
slot range. The records do include `BOX` point geometry, but a direct actor-to-
`BOX` route link has not been found. **[verified field mapping; unresolved use]**

## Action state to animation clip

### Per-model action table

The runtime table at `0x004f7728` has 16 model-family banks × 64 action slots.
Each 16-byte slot contains a loaded clip handle and state flags. **[verified]**

- `ANIM_InitStateTable` (`0x40484d`) initializes the slots and assigns shared per-action flags.
- `ANIM_LoadEntitySet` (`0x404d98`) loads a model's `.DAN` directory, parses each `AN###` suffix,
  and installs the clip handle at that numeric slot in the model's family bank.
- Missing slots are filled from the preceding available slot. The family is
  stored on the actor at `+0x158`.
- `ANIM_ApplyPendingState` (`0x4058d5`) resolves the requested action in `+0x160` as a slot within the
  actor's family, commits it as the current action at `+0x15c`, and starts a
  transition if the resolved clip changes.

There is no textual “walk → clip 18” dictionary. The executable uses numeric
action states and the matching numeric `AN###` clip slot. The shared flag byte
at each slot affects playback and transitions; not every flag bit has been
given a reliable semantic name.

### Player movement

`SCENE_InitLevel` (`0x41f42e`), which `SCENE_LoadLevel` (`0x41f9db`) runs on
every level load, loads the player bank from `XH_.DAN` (or `MHE.DAN` for the
alternate character) through `ENT_LoadObject` (`0x41d624`) and `ANIM_LoadEntitySet`,
requests state 0, and enters the same resolver. **[verified]**

For runtime class 1, `ANIM_ApplyPendingState` (`0x4058d5`) selects an action from actor `+0x240`
divided by frame delta. Physics code `PHYS_IntegrateMotion` (`0x43d360`) updates `+0x238`, `+0x240`,
and `+0x248` as a three-component velocity vector, so `+0x240` is one velocity
component rather than a named “run speed.” Under the relevant actor flags, its
thresholds are: **[verified]**

| Actor `+0x240` / frame delta | Requested state | `XH_.DAN` slot |
|---:|---:|---|
| below 1, or movement flags inactive | 0 | `AN000` |
| 1 to below 20 | `0x29` (41) | `AN041` |
| 20 to below 45 | `0x19` (25) | `AN025` |
| 45 or above | `0x2a` (42) | `AN042` |

The executable therefore has a real movement-state-to-animation selector.
These state IDs are certain; names such as idle, walk, run, and sprint still
need to be matched to the stored poses before assigning them. Other action
requests come from controls and AI through `ANIM_RequestState` (`0x405118`).

The generic actor updater `ENT_TickEntity` (`0x407b51`) advances animation and movement
together. The engine's nominal animation base is 30 frames per second. The
shared clip evaluator blends two channels and feeds root translation into the
movement/collision path; see [animation timing](animation-timing.md),
[root movement and blending](animation-root-blending.md), and the
[animation subsystem](animation-subsystem.md). **[verified]**

## Firing animation: IBI sequence and remaining effect identity

The AI firing/attack path now has concrete stages:

1. `AI_TickSquads` (`0x415109`) schedules the NPC's mode and target passes.
2. `AI_UpdateMemberStatus` (`0x414d61`) chooses an eligible nearby target.
3. `AI_TickCombat` (`0x414646`) checks target distance, facing, and a random threshold.
4. It requests one of action slots 16, 20, 18, or 22 through `ANIM_RequestState` (`0x405118`).
5. The common resolver maps that action number to the NPC model's `AN016`,
   `AN020`, `AN018`, or `AN022` clip slot.

**[verified control flow]** For the IBI actor in Project54, all four requested
states resolve to `IBIAN016`, then `ENT_TickEntity` (`0x407b51`) triggers the moving effect
through `ENT_SpawnAttackObject` (`0x442944`) / `ENT_PlaceAtFacingOffset` (`0x442786`) / `ENT_MoveAttackObject` (`0x442e0d`); the collision path
applies damage through `ENT_TickAttackObject` (`0x444b8f`) / `ENT_ApplyAttackHit` (`0x4440aa`) / `ENT_ApplyDamage` (`0x443619`). The
remaining check is the effect's visible resource and how collision rules vary
between actor classes; those tables do not supply human-readable names.

## Reproduce the binary trace

```powershell
. .\tools\dreams-env.ps1
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') `
  ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly `
  -scriptPath ghidra_scripts `
  -postScript Decompile.java 0040484d 00404d98 00405118 004058d5 `
  00407089 00407b51 00410e5d 00410f03 004115a1 0041208f 004131a4 `
  00413b3b 00414646 00414b24 00414d61 00414f6d 00415109 `
  00442786 00442944 00442e0d 00444b8f 004440aa 00443619
```

The project transition selector is at decompressed `DREAMS.DAT` record `+0xd0`.
The pointer table and rules are at `WINDREAM.EXE:0x0049d5a0`; the action table
is initialized at `ANIM_InitStateTable` (`0x40484d`) and populated at `ANIM_LoadEntitySet` (`0x404d98`).

Selector census, using the parser field added for this finding:

```powershell
. .\tools\dreams-env.ps1
$dat = Join-Path (Get-DreamsSetting DREAMS_DISC1) 'DREAMS.DAT'
uv run python -c 'import sys; from dreams.formats.project import read; print([(p.index, p.ai_schedule_selector) for p in read(sys.argv[1]) if p.ai_schedule_selector])' $dat
```

## Open questions

- Which other ranged actors use the moving class-`0x14` effect, and which
  visible effect asset or damage/collision routine gives it its gameplay name?
- The IBI clip is functionally the attack/launch pose; its authored pose label
  is still absent from the retail clip directory.
- What are the human-readable labels for player movement slots 25, 41, and 42?
- What is the runtime meaning of `OBJET +0x6c`, and which project `BOX` records
  define NPC navigation or patrol paths?
- Which actor mode/status bit meanings can be named from additional retail
  data and call-site tracing?
