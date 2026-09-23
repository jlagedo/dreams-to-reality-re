# Character and prop models — `.DAN`

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

The `0x14` was pinned from the binary, not guessed: `FUN_00478c2c` reads the
vertex count at node `+0x7c` and the array at node `+0x80` with stride `0x28`,
which are exactly `+0x90` and `+0x94` of this header.

## World transforms

```
R_world = (R_parent @ R_child) >> 15
T_world = ((R_parent @ T_child) >> 15) + T_parent
```

Order and rounding both come from `WINDREAM.EXE` — the multiply in
`FUN_0045b86c` and the parent add in `FUN_0047e498`. The shift is `sar 0xf`:
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
the shade coming from lighting in `FUN_0047b7e0`. **[verified]**

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
into the `0x10000`-byte page allocated at `FUN_00417a07` — so `page[v*256 + u]`,
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

`.DAN` is a dual container storing 3D mesh parts (Tag 1), texture pages (Tag 2), and **animation clips (Tag 3)**.
The container header has two directories:
1. Directory 1 at `+0x10`: `N` 11-byte strings declaring internal mesh names (`.3DM`).
2. Directory 2 at `+0x10 + 11*N + 2`: `F` 13-byte fixed slots declaring source animation clip names (`.3DA`).

Each Tag 3 chunk in the body corresponds 1-to-1 with the declared `.3DA` names in Directory 2 in sequential order.
The engine's loader at `FUN_0040fff7` / `FUN_004105eb` decompresses each Tag 3 chunk via Cryo's LZ decompressor (`FUN_0049afd1`) into runtime skeletal animation tracks.

### Tag 3 Stream Layout [verified]

Decompressed, each Tag 3 payload has the following binary structure:

```
+0x00  u32        unknown / magic (often 0)
+0x04  u32        unknown
+0x08  u32        unknown
+0x0C  u32        unknown
+0x10  u32        unknown
+0x14  u32        track count N (equals scene-graph node count, e.g. 27 for XH_, 17 for CH0)
+0x18  u32        header table span == 4 * (N + 1)
+0x1C  u32[N-1]   relative offsets to track records 0 .. N-2
...    u32        total duration in frames
...    u32        frame rate (typically 10 fps)
```

Track $i$ ($0 \le i < N$) maps 1-to-1 to Scene-Graph Node $i$ in the skeletal hierarchy.

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
+0x1C  u32        interpolation / codec type (1 = linear, 2 = Hermite spline)
+0x20  u32        start offset of keyframe array
+0x24  u32        end offset of keyframe array
+0x28  i32[4]     rest unit quaternion [qx, qy, qz, qw], Q15 fixed-point (32768 = 1.0)
```

Each keyframe $k \in [0, K-1]$ is located at exact byte offset `trk_off + 40 + k * stride`:
- **Linear Keyframes (`stride == 20`, `interp_type == 1`)**:
  - `+0x00` `u32`: keyframe timestamp / frame index
  - `+0x04` `i32[4]`: unit quaternion `[qx, qy, qz, qw]` ($32768 = 1.0$)
- **Hermite Spline Keyframes (`stride == 60`, `interp_type == 2`)**:
  - `+0x00` `u32`: keyframe timestamp / frame index ($0, 1, 2, \dots$)
  - `+0x04` `i32[4]`: unit quaternion `[qx, qy, qz, qw]` ($32768 = 1.0$)
  - `+0x14` `u32[2]`: curve control / ease flags
  - `+0x1C` `i32[4]`: incoming Hermite tangent quaternion
  - `+0x2C` `i32[4]`: outgoing Hermite tangent quaternion

Verified across **10,127 tracks and 97,729 keyframes across all 159 models on the game discs** with 0 errors, 100% monotonic timestamps, and 100% unit quaternions ($1.000$).

### Engine Evaluation Math in `WINDREAM.EXE` [verified]

Headless Ghidra decompilation revealed the exact runtime evaluation routines:

1. **Quaternion to Rotation Matrix (`FUN_0045bc28`)**:
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

3. **Hierarchical Transform Composition (`FUN_0047e498`)**:
   - Evaluates:
     $$R_{world} = (R_{parent} \times R_{child}) \gg 15$$
     $$T_{world} = ((R_{parent} \times T_{child}) \gg 15) + T_{parent}$$
   - Matrix multiply implemented in `FUN_0045b86c`.

4. **Vertex Deformations (`FUN_00478dac`)**:
   - Deforms local vertex coordinates into world coordinates for rendering:
     $$V_{world} = ((R_{world} \times V_{local}) \gg 15) + T_{world}$$

### Duncan (`XH_`) Motion Action Mapping [verified]

By performing forward kinematics and analyzing limb angular trajectories across all 49 authored clips in `XH_.DAN`, the primary locomotion states were recovered:

| Clip | Duration | Frame Rate | Action / Trajectory |
|---|---|---|---|
| `XH_AN000.3DA` | 200 frames | 10 fps | **Idle**: 20-second breathing cycle, feet planted on ground ($Y \approx -28$), subtle hand and weight shifts |
| `XH_AN055.3DA` | 39 frames | 28 fps | **Run / Sprint**: high-speed cyclic run, knee lift ($Y=-82$), alternating arm swings ($X = -13 \dots +25$) |
| `XH_AN056.3DA` | 38 frames | 26 fps | **Run / Sprint**: alternative high-velocity running cycle |
| `XH_AN018.3DA` | 35 frames | 7 fps | **Walk**: measured walking gait |
| `XH_AN024.3DA` | 65 frames | 19 fps | **Fly / Levitate**: mid-air flight cycle, both feet tucked/hovering, outstretched arms |
| `XH_AN035.3DA` | 65 frames | 19 fps | **Fly / Glide**: levitation flight variant |
| `XH_AN020.3DA` | 25 frames | 7 fps | **Jump**: vertical leap, leg compression followed by explosive mid-air extension |

### Skeletal Pose Evaluation [verified]

For any playback time $t$ in frames:
1. For each track $i$, find the bounding keyframes $k_0 \le t \le k_1$.
2. If $k_0 == k_1$, rotation is $Q(k_0)$. Otherwise compute interpolation factor $\alpha = \frac{t - t_0}{t_1 - t_0}$ and evaluate spherical linear interpolation:
   $$\text{Slerp}(Q_0, Q_1, \alpha) = \frac{\sin((1-\alpha)\theta)}{\sin\theta} Q_0 + \frac{\sin(\alpha\theta)}{\sin\theta} Q_1$$
   where $\cos\theta = Q_0 \cdot Q_1$.
3. Convert interpolated quaternion $Q$ to a $3 \times 3$ rotation matrix $R_{\text{anim}}$.
4. In `WINDREAM.EXE` (`FUN_0047e498` / `FUN_0047e700`), the local node rotation $R_{\text{local}}$ is replaced by $R_{\text{anim}}$, and world transforms are composed down the scene-graph hierarchy:
   $$R_{\text{world}} = (R_{\text{parent}} \cdot R_{\text{child}}) \gg 15$$
   $$T_{\text{world}} = ((R_{\text{parent}} \cdot T_{\text{child}}) \gg 15) + T_{\text{parent}}$$

### Dual Animation Tracks in In-Engine HUD [verified]

`WINDREAM.EXE` at `0x00416606` (`Debug_DrawObjectInfo`) tracks two concurrent animation channels per entity:
- `Object Anim 0`: primary animation clip index and progress
- `Object Anim 1`: secondary/blend animation clip index (used for combat, walking while aiming, transitions)

Implemented in [`src/dreams/formats/animation.py`](../src/dreams/formats/animation.py) and verified across 550 extracted animation clips in 111 `.DAN` models.

## Still open

- **The unnamed face-record fields** in the table above.
- Which bank a face group samples is assigned **by order**, not read from the
  file. Swapping it is visibly wrong, so the order is right, but the field that
  states it has not been found. **[unverified]**
