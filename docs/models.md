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
a 1997 software rasteriser would carry. Row 0 is full brightness and row 31 is
49% of it, with hue preserved the whole way down: entry 200 runs `(0, 93, 131)`
at row 0 to `(0, 0, 32)` at row 31. So **row 0 is the correct unlit palette**,
which is the one `texture_page()` returns and the one the exported PNGs use.
The ramp is how the engine shaded a face without per-pixel lighting, and it is
why nothing in the face record needs to hold a colour. **[verified]** on
`XH_.DAN`.

The row is chosen at **runtime**, not from the file: `WINDREAM.EXE` selects
`31 - shade`, with the shade computed from lighting in `FUN_0047b7e0`. So row 0
is the fully lit one, `w11` is not the selector, and an exporter has nothing to
decide. **[verified]**

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

**One small gap.** 168 of 175 exported models keep every corner inside 0..1.
Seven do not: `f37` reaches 1.77, and `cg1`, `e_p`, `f24`, `gg1` and two others
have a handful of corners at 115.38, which is a whole word read as a texel and
therefore a reference that did not resolve. It is 1 to 9 faces per model and at
most **0.35%** of corners, so it does not show, but it is unexplained.
**[unverified]**

## The rest of the 68-byte face record

Indexing words from the start of a record:

| word | what it is |
|---|---|
| `w0` | address of the **next** record — the block is a linked list |
| `w1` `w4` `w7` | the three corner vertices, `slot = (ref − base) / 40` |
| `w2` `w5` `w8` | into a 16-byte-stride array, one entry per vertex |
| `w3` `w6` `w9` | into an 88-byte-stride array, one entry per corner |
| `w10` | into a 16-byte-stride array, one entry per **face** |
| `w11` | a small **signed** int, −14..+70 in `XH_`; carried through clipping, and *not* the shade selector |
| `w12` `w13` `w14` | the three UV records |
| `w15` `w16` | unknown; 0 and 8 in the sample read |

`w0` chaining by exactly 68 is what confirms the record stride independently of
the `68` stored at the block's `+0x20`. The block header carries the object
name in its first 8 bytes, the face count at `+0x10`, the address of the first
record at `+0x14`, and at `+0x24` a pointer that is the same for every block in
a file.

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
- **Animation** — partially decoded. Tag 3 is one clip per record: `MI0.DAN`
  has 7 `frame_refs` and 7 tag-3 records, `AR0.DAN` 5 and 5. The layout is a
  count `N` at `+0x14`, a span `4*(N+1)` at `+0x18`, then `N-1` record offsets
  from `+0x1c` with a final word that is a frame/time value rather than an
  offset.

  **The rotations are keyframed unit quaternions, not matrices** — which is why
  searching tag 3 for orthonormal Q15 matrices found none. Each record holds a
  Q15 quaternion in `[x, y, z, w]` order at `+0x2c`, with `(0,0,0,32768)` as
  identity, followed by `K` 20-byte entries that appear to be
  `{qx, qy, qz, qw, u32 time}` keys.

  **[verified]** the quaternion slot: across **14,304 records in all 159
  models** the norm is 32768 to within integer rounding in **100%** of cases,
  and 14,104 are exactly identity. A field that is a unit quaternion 14,304
  times out of 14,304 is not a coincidence.

  **[unverified]** everything else in the record — the endpoint words, the
  meaning of the type field at `+0x1c`, and how a frame composes onto the
  node's rest transform. The worker that decoded it reports several codec
  variants reusing the same space differently. Not implemented in the exporter.
- Which bank a face group samples is assigned **by order**, not read from the
  file. Swapping it is visibly wrong, so the order is right, but the field that
  states it has not been found. **[unverified]**
