# Character and prop models — `.DAN`

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `tools/check_names.py`.

**Solved.** 159/159 models on the discs decode to glTF: **1,886 parts, 41,614
triangles, 131 collision proxies**, with texture pages. **[verified]**

`.DAN` was catalogued for two passes as *"Animation. Same container and LZ as
`.DSN`; payload meanings open."* That was wrong in an expensive way — **tag 1
is the model**, and it is built from the same scene-graph node as `.DSN` tag 1.
Solving level geometry solved the models for free; nobody had to decode a
second format.

Implemented in [`src/dreams/formats/node.py`](../src/dreams/formats/node.py),
exported by `dreams.gltf.from_model`, wired into the pipeline as the `models`
group.

## The node

One C struct, `fwrite`'d live by Cryo's exporter, shared by scenes and models:

```
+0x24  u32       parent node address        0 at a root
+0x28  u32       first child
+0x2c  u32       next sibling
+0x30  i32[3]    translation, RELATIVE to the parent
+0x3c  i32[3][3] rotation, Q15 row-major (32768 = 1.0)
+0x60  i32[3]    world translation   always 0
+0x6c  i32[3][3] world rotation      a copy of +0x3c
+0x90  u32       vertex count
+0x94  u32       pointer to its own vertex array
+0x9c  u32       == +0x94 + 40*count
+0xc4  i32       bounding-sphere radius
+0xc8  i32[3]    sphere centre == centroid of the vertices
+0xd4  u32       40, the vertex record stride, stored literally
+0xf0  ----      vertex array; position at +4 of each 40-byte record
```

Nodes are found by **signature**, not by a directory: `+0xd4 == 40` together
with the `+0x9c == +0x94 + 40*count` identity. Both hold for **2,248 nodes
across 95 scenes** and **1,886 across 159 models**, and in every one the
bounding sphere at `+0xc4` agrees with the decoded vertices — three
independent checks.

### Why `+0x60` and `+0x6c` are dead

They are the *world* transform, and on disk they are always zero / a copy of
the local one. The exporter dumped the scene graph **before any frame composed
world transforms**, so those fields were never filled in. That explains the
duplicate matrix that sat unexplained through two passes.

### Addresses, and the 220

The header is the **240 bytes before** the vertex array, so a node's own
address is `base - 240`. But the runtime node the engine passes around starts
`0x14` further in, and **that** is what child and sibling pointers hold:

```
node address = base - 220
```

This is why searching for `base` found nothing: 2,239 structs across 95 scenes
were scanned at every offset and every delta in −32..+64, with zero hits.
`base - 244` also found nothing. The sweep that worked returned `S = 220` with
no spread at all — 549 hits at `+0x24`, 330 at `+0x28`, one single value.

The `0x14` was pinned from the binary, not guessed: `REND_TransformClipVertices` (`0x478c2c`) reads the
vertex count at node `+0x7c` and the array at node `+0x80` with stride `0x28`,
which are exactly `+0x90` and `+0x94` of this header.

## World transforms

```
R_world = (R_parent @ R_child) >> 15
T_world = ((R_parent @ T_child) >> 15) + T_parent
```

Order and rounding both come from `WINDREAM.EXE` — the multiply in
`MATH_MulMat3` (`0x45b86c`) and the parent add in `REND_DrawObject` (`0x47e498`). The shift is `sar 0xf`:
sum the three products, then **one** arithmetic shift, with no `+0x4000` bias.
Python's `>>` on ints floors, which is what `sar` does, so it transcribes
directly.

Composition walks **parent** links. Both directions are present, but the
forward links are sparse in these files while the parent link resolves for
every non-root node — giving **at most 2 roots per model across all 159**.

> **Correction.** That second root was read as "most `.DAN` files carry two
> copies of the model". They do not. The two names are **two texture groups
> over one mesh** — see [Two pages, not two copies](#two-pages-not-two-copies).

`CH0.DAN` decodes as a textbook humanoid:

```
0  pelvis ─┬─ 2  torso ─┬─ 3  head
           │            ├─ 5 ─ 6 ─ 7    arm, z = -15, -45, -31
           │            └─ 8 ─ 9 ─ 10   arm, z = +15, +46, +31
           ├─ 11 ─ 12 ─ 13   leg, z = -13, -5, -6
           └─ 14 ─ 15 ─ 16   leg, z = +13, +5, +6
```

## Faces — two rules that cost visible geometry

A face block is located by `u32 68` at `+0x20`, count at `+0x10`, records from
`+0x2c`. Within a 68-byte record, words 1, 4, 7 are vertex references and
words 12, 13, 14 are UV references. A reference is a stale pointer:
`slot = (ref - base) / 40`.

**1. A face may span two nodes.** Resolve each of the three references
*independently*, in its own node's world space. These bridging triangles are
the skin over a joint and always join a **parent-child** pair — pelvis↔torso,
torso↔head, torso↔arms, hip↔thigh. **12,373 of 41,614 faces (30%)** are
bridges. Requiring one owner for all three corners drops them and the model
falls apart into floating segments.

This is the opposite of `.DSN` levels, where **all 71,689 faces sit in a
single node** — so the level rule does *not* carry over, and assuming it does
is what produced a character with a missing foot and gaps at every joint.

**2. The last block may overrun.** In **86 of 159 models** the final face
block declares four bytes more than the decompressed record holds. Only the
unused tail is missing; all three vertex references are present. Bounds are
therefore checked **per record**, over the 32 bytes a record actually needs.

Each bug alone leaves parts empty, and they are independent: fixing only the
bridging gives 1,208 parts with geometry, only the tail gives fewer still,
both gives **1,755 of 1,886**.

### The 131 parts with no faces

**129 have exactly 8 vertices** and 2 have a single vertex. The 8-vertex ones
are axis-aligned boxes with a bounding-sphere radius of `side * √3 / 2` —
collision proxies. The single-vertex ones are attachment points. These
correctly carry no render geometry, so the decode is complete.

## Textures

`.DAN` tag 2 is a fixed **98,324 bytes in 191/191 files**:

```
+0x00     20-byte header
+0x14     0x8000   32 palettes x 256 entries x 4 bytes
+0x8014   0x10000  one 256x256 indexed page
```

The palette entry is a `u16` at `+2` of each 4-byte slot, **RGB565** — the same
packing as level textures and the save thumbnail. One colour format across the
whole game.

**The 32 "palettes" are one palette in 32 brightness steps** — a light ramp, as
a 1997 software rasteriser would carry. It is how the engine shaded a face
without per-pixel lighting, and it is why nothing in the face record holds a
colour. The row is chosen at **runtime**: `WINDREAM.EXE` selects `31 - shade`,
the shade coming from lighting in `REND_LightObject` (`0x47b7e0`). **[verified]**

### The packing is RGB565, and that is measured

Each 4-byte slot is `u16 zero, u16 colour`; `+0` is zero in all 8,192 slots.
The colour is **RGB565**, not the 555 that a 1997 Windows title might equally
have used. The ramp proves it: 32 brightness steps of one palette must stay
proportional, and the spread of the per-channel ratio is **0.15 under 565
against 0.39 under 555**. Only 565 is consistent with the file's own contents.
A dead top bit would also have settled it, and there isn't one — bit 15 is set
in 5,161 of 8,192 slots. **[verified]**

### Row 0 is not the artwork

The obvious reading — row 0 is the unlit palette — is wrong, and it is what
made the player's navy shorts come out **teal**.

Row 0 is **over-brightened and clipped**: 93 of its 256 entries have a channel
at maximum, and 19 distinct colours collapse onto a shared value.

The proof is not the clipping count, which a saturated palette would also
show. It is that **darkening can only ever merge colours**. If row 0 were the
artwork, every darker row would be a contraction of it and could hold at most
as many distinct colours. Instead row 20 holds **all 256** where row 0 holds
221 — impossible unless row 0 has already lost information.

So the row to export is the brightest one that still resolves every entry.
Such a row exists in **252 of 261 banks**; it is 20 in 117 of them, and 12, 17
or 19 in most of the rest, depending on how saturated the artwork is — which
is why `neutral_row()` measures it per bank instead of fixing it. Entry 200 of
`XH_` reads `(0, 93, 131)` at row 0 and `(0, 32, 70)` at row 20: teal against
navy, and navy is what the game shows. **[verified]**

There are **126 distinct pages across 191 files**, shared exactly where you
would expect: 13 `MCHAPO` files share one page, 10 `CAISSE` files another —
crates share a crate texture.

Rendered, the pages are unambiguous. `XH_` is a bare-chested man with a carved
mask panel; `MHE` is a red-haired woman with a white and teal top and trainers
with treaded soles.

### UVs — solved **[verified]**

A UV reference is **exact**. Add the record's relocation delta and it lands on
an 8-byte record holding `u32 u` then `u32 v`, both **16.16 fixed point in
texels**, so `value / 65536` is a pixel in 0..255 and dividing again by 256
normalises it for glTF.

The records sit in a dedicated pool, three per face, mostly consecutive at a
stride of 8.

Two errors had to be undone, and each one alone is enough to destroy the
mapping:

**The reference was read five bytes early.** `mesh.py` subtracts 5 for `.DSN`
and that is correct *there* — but it is not a field offset, it is `.DSN`'s own
relocation delta, which happens to be −5. A model's delta is positive (267 in
`XH_.DAN`), so subtracting 5 on top of it lands mid-record, five bytes into the
previous pair. Every corner still fell inside the pool, so the bounds check
that was used as proof passed at 100%.

**The scale carried a spurious ×255.** `UV_SCALE` was `65536 * 255`, which
divides a texel by 255 a second time and collapses every coordinate to roughly
0.004. That is what produced "flat colour blocks" and "no value exceeds 128 of
256" — both were measurements of the bug, not of the format.

With both fixed, `XH_.DAN`'s 1,512 corners give `u` 0..254 over 166 distinct
values and `v` 1..255 over 168, and the model renders as a bare-chested man in
teal shorts. `dreams model FILE --preview` writes that picture, and the
`models` group writes one per model to `models/preview/`.

**The engine agrees.** `WINDREAM.EXE`'s texture inner loop at `0x00471a67`
reads

```
SAR EBX,0x8        ; v, 16.16
SAR EAX,0x10       ; u, 16.16 -> texel
AND EBX,0xff00
OR  EAX,EBX        ; addr = (u & 0xff) | ((v & 0xff) << 8)
ADD EAX,page_base
MOV AL,byte ptr [EAX]
```

`SAR 0x10` is the division by 65536, and the `OR` builds a 256-byte-pitch index
into the `0x10000`-byte page allocated at `DSN_Create3DM` (`0x417a07`) — so `page[v*256 + u]`,
exactly. **[verified]**

**The proof is `CAISSE`, the crate.** Its texture carries the French words
*HAUT* and *BAS* - top and bottom. They render **legibly and the right way up**
in `models/preview/cai.png`. Readable text out of an atlas is a check no
statistic can fake: u, v, the axis order and the orientation all have to be
right at once for letters to come out as letters.

The lesson is recorded in [research-log.md](research-log.md): a reference that
resolves in range is **not** evidence that it resolves correctly. Only
rasterising the result separated the two.

**One small gap.** Over the whole corpus **126,819 of 126,819** UV references
decode, and both coordinates land in range in **125,059 — 98.6%**. The
remainder are confined to `F37.DAN`, `H14.DAN` and `L14.DAN`. At most 0.35% of
a model's corners, so nothing shows, but it is unexplained. **[unverified]**

## The rest of the 68-byte face record

Records begin at `off + 40`, which is what the block's `+0x14` points at and
what makes the next-pointer chain close — it validates **2,768 of 2,768**
blocks there and 134 of 2,768 one word later. The table below indexes from
`off + 44`, one word in, which is where the decoder reads:

| word | what it is |
|---|---|
| `w0` | address of the **next** record — the block is a linked list |
| `w1` `w4` `w7` | the three corner vertices, `slot = (ref − base) / 40` |
| `w2` `w5` `w8` | into a 16-byte-stride array, one entry per vertex |
| `w3` `w6` `w9` | into an 88-byte-stride array, one entry per corner |
| `w10` | into a 16-byte-stride array, one entry per **face** |
| `w11` | a small **signed** int, −14..+70 in `XH_`; carried through clipping, and *not* the shade selector |
| `w12` `w13` `w14` | the three UV records |
| `w15` `w16` | `w15` is **zero in every record on the discs**; `w16` takes 535 values, 8 in 23,174 of 42,174 |

`w0` chaining by exactly 68 is what confirms the record stride independently of
the `68` stored at the block's `+0x20`. The block header carries the object
name in its first 8 bytes, the face count at `+0x10`, the address of the first
record at `+0x14`, and at `+0x24` a pointer that is the same for every block in
a file — in 174 of 175 files, `F74.DAN` being the exception.

**[unverified]** — `w2`/`w5`/`w8`, `w3`/`w6`/`w9`, `w10`, `w11`, `w15`, `w16`
and the block header's `+0x1c` are named by their stride and their arity, not
by anything found in the binary.

## Two pages, not two copies

A `.DAN` normally carries **two** tag-2 banks, and **in 0 of the 57 models that
have two are the two identical**. Each face block is named for the bank it
samples, and the name says so out loud: `XH_IMG_A` and `XH_IMG_B` — image A and
image B.

They are one mesh, not two. In **51 of 54** two-group models the groups share
vertex positions — 18 of 26 nodes in `XH_`, 13 of 15 in `F01`. Exported as one
primitive with one page, every `_B` face reads the wrong atlas: the character's
chest lands on his back and a trainer appears under his arm.

So a model exports as **one material and one page per group**, mapped in sorted
name order — `_A` before `_B`, `MHEROI1` before `MHEROI2`. The order is
**[unverified]** in the sense that nothing in the file states it, but swapping
it deliberately reproduces the chest-on-the-back artefact exactly, and the
correct order gives shoulder blades and a spine.

Three models — `E_P`, `E66` and one other — have groups that share **no**
vertices. For those the two groups are disjoint geometry, so whether they are
parts or variants is still open; the texture-group reading holds either way,
because the block still names its page. This also retires the old question
about `E_POULP2` being a level-of-detail copy: it is the octopus's second
texture group.

## Face blocks must vouch for themselves

`u32 68` at `+0x20` plus a plausible count is **not** enough. **17 models**
contain a byte run that satisfies both and is not a face block — `E_P`, `CG1`,
`H14`, `MI0`, `F24`, `F27`, `GG1`, `F01` among them.

The block's own pointer at `+0x14` settles it: relocated it must equal
`off + 40`, where the records actually begin, and in every real block it does
exactly. In the impostors it misses by hundreds or thousands of bytes.

Without the check `F01` gains 204 faces whose vertex references *do* resolve to
real geometry — so the usual validity test passes — while their UV references
are small negative numbers. They render as **black holes** punched through the
model.

## Who is who

`LISTL0.TXT` is the always-loaded list: `CH0.DAN`, `HOLO.DAN`, `XH_.dan`,
`MHE.dan`.

| file | internal name | what it is |
|---|---|---|
| `XH_.DAN` | `XH_IMG_A` / `XH_IMG_B` | **the player character** — 27 parts, 504 faces |
| `MHE.DAN` | `MHEROI1` / `MHEROI2` | a red-haired woman — 26 parts, 467 faces |
| `CH0.DAN` | `F14_M01` / `F14_M02` | a level-14 creature, not a hero |
| `HOLO.DAN` | `MCHAPO` | 1 part, 98 faces |

`XH_` is the player on the evidence of **animation breadth**: it appears in 8
`.DAN` files with 170 frames against `MHEROI`'s 2 files and 80, it has the most
frames of any model (49), and it turns up inside `MOT.DAN` alongside
`EMOTO1`/`EMOTO2` — the hero on the motorbike. Picking by "referenced in every
manifest" instead gave `CH0`, which is a monster; the internal names are the
thing to read.

**Authoring coordinate axes & rest pose [verified]:** In the raw exported mesh
of `XH_.DAN`, the model rests in 3D Studio's authoring T-pose:
- **Face / Nose**: points along $+X$ (positive X maximum at $(+0.24, 2.90, 0)$).
- **Ponytail**: extends horizontally backwards along $-X$ (tip at $(-0.88, 3.13, 0)$).
- **Arms**: outstretched along $\pm Z$ (total wingspan $2.83$, from $z = -1.42$ to $+1.41$).
In standard glTF/engine conventions where $+Z$ is forward, aligning Duncan's face
forward requires a $+90^\circ$ rotation around the vertical axis ($Y$).

Names elsewhere are French and legible: `E_POULP` octopus, `M14MINO` minotaur,
`CAISSE` crate, `GRILLE` grate, `M01GUN`, `L08_TANK`, `Patte` (paw).

### Shipped file descriptions

Both discs also contain `DATA/3DC/DESCRIPT.ION`, a directory-level file
description sidecar rather than a game animation table. Together they cover
40 filenames. Examples: `F01.DAN` = `enfant` (child), `F09.DAN` =
`capitaine araignee` (spider captain), `F10.DAN` = `scribe ok`, `F13.DAN` =
`projectioniste`, `F14.DAN` = `Chaman`, and `Y_Z.DAN` = `grand monstre bleu`.
Several unrelated filenames share generic descriptions such as `tortue`
(turtle), so treat these as authoring hints rather than verified character
identifications. The files contain no `XH_` entry or individual clip labels.
The sidecar convention is documented in [JP Software's DESCRIBE help](https://jpsoft.com/help/describe.htm).

## `.3DC` props and weapons

`F3DC` is **not** a tagged record chain and is **not** LZ-packed — it is a raw
blob holding the same node, so it decodes by signature straight off the file
bytes. **165 nodes across the 16 unique `.3DC` files**, exported by the same
`models` group.

| model | nodes | vertices | faces | boundary edges |
|---|---:|---:|---:|---:|
| `BOULE` | 1 | 98 | 192 | **0** — was 18 |
| `EPEE` | 1 | 28 | 52 | **0** |
| `GUN` | 2 | 19 | 56 | **0** |
| `CARRE` | 1 | 4 | 2 | 4 — correct, see below |
| `ARC` | 2 | 49 | 75 | 15 — still open |

`CARRE` was listed for two passes as "fails to decode as a box". It is a quad,
and *carré* is French for **square**. The decode was right; the expectation was
the bug.

## Still open

- **The unnamed face-record fields** in the table above.
## Animation — `.DAN` Tag 3 Decoded [verified]

For the executable hypothesis harness, named node directory, and the correction
to track binding, see [animation-validation.md](animation-validation.md).

`.DAN` is a dual container storing 3D mesh parts (Tag 1), texture pages (Tag 2), and **animation clips (Tag 3)**.
The container header has two directories:
1. Directory 1 at `+0x10`: `N` 11-byte strings declaring internal mesh names (`.3DM`).
2. Directory 2 at `+0x10 + 11*N + 2`: `F` 13-byte fixed slots declaring source animation clip names (`.3DA`).

Each Tag 3 chunk in the body corresponds 1-to-1 with the declared `.3DA` names in Directory 2 in sequential order.

### Animation names and runtime IDs

The 780 clips from 159 distinct `.DAN` files have only source-style names such as
`XH_AN000.3DA`; every name follows the `AN` plus three-digit pattern. There is no
embedded label such as “idle” or “walk” in the clip directory. The French
`DATA/LANG/FRANCAIS/DREAMS.INI` resource names objects and projects, not clips.

`WINDREAM.EXE` at `ANIM_LoadEntitySet` (`0x404d98`) reads the `.DAN` clip directory (or searches
`an???.3da` files when there is no container), parses each numeric suffix via
`atoi_` (`0x4551b5`), and installs the loaded clip in that numbered runtime slot.
`ANIM_GetTrackEntry` (`0x455278`) retrieves a track entry from a clip slot: the
slot table at `0x661ee0` holds clip records whose `+0x18` array holds the
per-node tracks. Its helper
`ANIM_GetTrackByHandle` (`0x4553f8`) splits a packed `(clip slot << 16) | track slot` value; that is a
track lookup encoding. Gameplay action selectors at actor `+0x15c`/`+0x160`
are separate direct 0..63 IDs, resolved through the model-family action table
documented in [ai-animation-runtime.md](ai-animation-runtime.md). The executable
contains control labels including “Run”, “Walk”, “Jump”, “Take-off”, and
“Flight or Swim”, but no textual action-to-clip name lookup was found. The
human-readable action labels below remain inferences.
The engine's loader at `DAN_OpenArchive` (`0x40fff7`) / `DAN_Load3DA` (`0x4105eb`) opens the DANF archive, finds each `.3DA` name in its 13-byte directory and decompresses the Tag 3 chunk via Cryo's LZ decompressor (`LZ_Unpack` (`0x49afd1`)) into runtime skeletal animation tracks.

### Tag 3 Stream Layout [verified]

Decompressed, each Tag 3 payload has the following binary structure:

```
+0x00  u32        unknown / magic (often 0)
+0x04  u32        unknown
+0x08  u32        unknown
+0x0C  u32        unknown
+0x10  u32        unknown
+0x14  u32        track count N (27 for XH_, 18 for CH0)
+0x18  u32[N]     offsets to track records 0 .. N-1
```

Track $i$ maps to slot $i$ of the **Tag 1 node directory**, whose order differs
from the geometry scanner's physical record order. In `XH_`, slot 2 is
`avbras-d` (geometry index 10), slot 25 is `tete` (geometry index 4), and slots
17–20 are `nat01`–`nat04` (geometry indices 5–8). `CH0`'s eighteenth slot is
the named zero-vertex node `bassin01`, omitted by the geometry scanner.

There is **no established FPS field after this table**. In `XH_` the next
words belong to the root track: duration, rotation-key count, translation-key
count. Earlier reported rates such as 28 fps were actually key counts. The
viewer now uses the **30 frames/second executable base**, recovered separately
in [animation-timing.md](animation-timing.md). Actor/state modifiers can change
effective speed. Clip duration is taken from track durations.

### Track Record Layout [verified]

Each track record contains a 40-byte header followed immediately by $K$ uniform keyframes:

```
+0x00  u32        unknown flags / ID
+0x04  u32        unknown
+0x08  u32        unknown
+0x0C  u32        unknown
+0x10  u32        unknown
+0x14  u32        track duration in frames
+0x18  u32        keyframe count K
+0x1C  u32        translation key count
+0x20  u32        start offset of keyframe array
+0x24  u32        start offset of translation array (also ends rotation array)
+0x28             first keyframe starts here; there is no separate rest quaternion
```

Each keyframe $k \in [0, K-1]$ is located at exact byte offset `trk_off + 40 + k * stride`:
- **20-byte keyframes (`stride == 20`)**:
  - `+0x00` `u32`: keyframe timestamp / frame index
  - `+0x04` `i32[4]`: unit quaternion `[qx, qy, qz, qw]` ($32768 = 1.0$)
- **60-byte keyframes (`stride == 60`)**:
  - `+0x00` `u32`: keyframe timestamp / frame index ($0, 1, 2, \dots$)
  - `+0x04` `i32[4]`: unit quaternion `[qx, qy, qz, qw]` ($32768 = 1.0$)
  - `+0x14` `u32[2]`: curve control / ease fields
  - `+0x1C` `i32[4]`: outgoing control quaternion (used on the left key)
  - `+0x2C` `i32[4]`: incoming control quaternion (used on the right key)

Verified across **15,084 tracks and 142,806 keyframes in 780 clips from 159 distinct models** with 0 invalid strides or nonmonotonic timestamps.

The pointers at `+0x20` and `+0x24` are relative to decompressed payload
`+0x14`. Translation records have timestamp plus XYZ at their start, with
16-byte records accompanying 20-byte rotation keys, and 48-byte records
accompanying 60-byte rotation keys. These pairings agree with evaluators
`ANIM_EvalTrackLinear` (`0x459808`) and `ANIM_EvalTrackSpline` (`0x45a03c`). The validation harness reads the translation
keys as independent evidence of track binding. The shared runtime now evaluates
these position curves and blends them; see [animation-root-blending.md](animation-root-blending.md).

### Engine Evaluation Math in `WINDREAM.EXE` [verified]

Headless Ghidra decompilation revealed the exact runtime evaluation routines:

1. **Quaternion to Rotation Matrix (`MATH_QuatToMatrix` (`0x45bc28`))**:
   Takes a Q15 quaternion `param_1` and computes a $3 \times 3$ row-major fixed-point rotation matrix at `param_2`:
   ```c
   // Diagonal terms:
   param_2[0] = 0x8000 - ((qy*qy + qz*qz) >> 14);
   param_2[4] = 0x8000 - ((qx*qx + qz*qz) >> 14);
   param_2[8] = 0x8000 - ((qx*qx + qy*qy) >> 14);
   // Off-diagonal terms:
   param_2[1] = (qx*qy - qz*qw) >> 14;
   param_2[3] = (qx*qy + qz*qw) >> 14;
   param_2[2] = (qx*qz + qy*qw) >> 14;
   param_2[6] = (qx*qz - qy*qw) >> 14;
   param_2[5] = (qy*qz - qx*qw) >> 14;
   param_2[7] = (qy*qz + qx*qw) >> 14;
   ```
   Note that right shift by 14 on Q15 squares ($32768^2 = 2^{30}$) effectively computes $2 \times Q_a Q_b / 32768$.

2. **Runtime Scene-Graph Node Struct (`base - 220`)**:
   - `+0x10`: parent node pointer
   - `+0x14`: first child pointer
   - `+0x18`: next sibling pointer
   - `+0x1c`: local translation `[x, y, z]`
   - `+0x28`: local rotation matrix ($3 \times 3$ row-major, 9 dwords)
   - `+0x4c`: world translation `[x, y, z]`
   - `+0x58`: world rotation matrix ($3 \times 3$ row-major, 9 dwords)
   - `+0x7c`: vertex count
   - `+0x80`: vertex array pointer

3. **Hierarchical Transform Composition (`REND_DrawObject` (`0x47e498`))**:
   - Evaluates:
     $$R_{world} = (R_{parent} \times R_{child}) \gg 15$$
     $$T_{world} = ((R_{parent} \times T_{child}) \gg 15) + T_{parent}$$
   - Matrix multiply implemented in `MATH_MulMat3` (`0x45b86c`).

4. **Vertex Projection (`REND_ProjectVertices` (`0x478dac`))**:
   - Rigidly transforms the node's flagged vertices by the node matrix (there is no skinning or deformation), then projects them to screen:
     $$V_{world} = ((R_{world} \times V_{local}) \gg 15) + T_{world}$$

### Duncan (`XH_`) Motion Action Mapping [inferred]

The viewer currently uses the following clip assignments. They were inferred before the missing track 0 was recovered and have not yet been matched to the game's action-to-clip assignments; recheck each action against the corrected poses:

| Clip | Duration | Frame Rate | Action / Trajectory |
|---|---|---|---|
| `XH_AN000.3DA` | 200 frames | 30 fps base | Stationary / idle candidate |
| `XH_AN055.3DA` | 39 frames | 30 fps base | Run / sprint candidate |
| `XH_AN056.3DA` | 38 frames | 30 fps base | Alternative run candidate |
| `XH_AN018.3DA` | 35 frames | 30 fps base | Walk candidate |
| `XH_AN024.3DA` | 65 frames | 30 fps base | Fly candidate |
| `XH_AN035.3DA` | 65 frames | 30 fps base | Alternative fly candidate |
| `XH_AN020.3DA` | 25 frames | 30 fps base | Jump candidate |

### Skeletal Pose Evaluation [partially verified]

For any playback time $t$ in frames:
1. For each track $i$, find the bounding keyframes $k_0 \le t \le k_1$.
2. If $k_0 == k_1$, rotation is $Q(k_0)$. Otherwise compute interpolation factor $\alpha = \frac{t - t_0}{t_1 - t_0}$ and evaluate spherical linear interpolation:
   $$\text{Slerp}(Q_0, Q_1, \alpha) = \frac{\sin((1-\alpha)\theta)}{\sin\theta} Q_0 + \frac{\sin(\alpha\theta)}{\sin\theta} Q_1$$
   where $\cos\theta = Q_0 \cdot Q_1$.
3. Convert interpolated quaternion $Q$ to a $3 \times 3$ rotation matrix $R_{\text{anim}}$.
4. In `WINDREAM.EXE` (`REND_DrawObject` (`0x47e498`), called for each node by the scene walk `REND_DrawScene` (`0x47e700`)), the local node rotation $R_{\text{local}}$ is replaced by $R_{\text{anim}}$, and world transforms are composed down the scene-graph hierarchy:
   $$R_{\text{world}} = (R_{\text{parent}} \cdot R_{\text{child}}) \gg 15$$
   $$T_{\text{world}} = ((R_{\text{parent}} \cdot T_{\text{child}}) \gg 15) + T_{\text{parent}}$$

The original spline evaluator `ANIM_EvalTrackSpline` (`0x45a03c`) additionally interpolates control
quaternions and blends again (SQUAD), and evaluates translation curves. The
viewer now implements the rotation spline and easing structure in floating
point where controls are valid, with SLERP for absent/zero controls. See
[animation-smoothing.md](animation-smoothing.md) for precision differences,
missing controls, and the correction that excludes frame zero from loops.

### Dual Animation Tracks in In-Engine HUD [verified]

`WINDREAM.EXE` at `0x00416606` (`DBG_DrawObjectInfo`) tracks two concurrent animation channels per entity:
- `Object Anim 0`: primary animation clip index and progress
- `Object Anim 1`: secondary/blend animation clip index (used for combat, walking while aiming, transitions)

Implemented in [`src/dreams/formats/animation.py`](../src/dreams/formats/animation.py) and verified across 550 extracted animation clips in 111 `.DAN` models.

## Still open

- **The unnamed face-record fields** in the table above.
- Which bank a face group samples is assigned **by order**, not read from the
  file. Swapping it is visibly wrong, so the order is right, but the field that
  states it has not been found. **[unverified]**
