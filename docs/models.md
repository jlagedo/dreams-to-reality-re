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
every non-root node — giving **at most 2 roots per model across all 159**. Two
rather than one because most `.DAN` files carry two copies of the model.

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

There are **126 distinct pages across 191 files**, shared exactly where you
would expect: 13 `MCHAPO` files share one page, 10 `CAISSE` files another —
crates share a crate texture.

Rendered, the pages are unambiguous. `XH_` is a bare-chested man with a carved
mask panel; `MHE` is a red-haired woman with a white and teal top and trainers
with treaded soles.

### UVs — located, not solved **[unverified]**

The references resolve. Words 12, 13, 14 are UV references, the 8-byte record
starts at `ref - 5`, and — the step an earlier pass missed — the value is an
**address**, so the record is at `ref - 5 + delta` where
`delta = node.offset + 0xf0 - node.base`. Reading without the delta yields
near-zero garbage, which is why the rule was first reported as not applying.

With it: **100% of 26,827 corners land inside the record**, 85 distinct `u`
and 78 distinct `v`, and the values are `pixel * 255` in 16.16.

But **rasterising gives flat colour blocks**, not the detail on the page, and
no value exceeds 128 of a 256-wide atlas. So the scale — and probably the
per-corner pairing — is still wrong. `node.UV_SCALE` is provisional and the
exporter writes UVs that should not yet be trusted.

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

## Still open

- **UV mapping**, above.
- **Animation.** Tag 3 is one record per animation — `MI0.DAN` has 7
  `frame_refs` and 7 tag-3 records, `AR0.DAN` 5 and 5. A record opens with a
  count at `+0x14` (19 where the model has 18 parts) and a table of `u32`
  offsets from `+0x1c`. The per-part payload at those offsets is not decoded.
  No orthonormal Q15 matrices occur in tag 3, so the pose is not stored the way
  the header transforms are.
- Whether `E_POULP2` is a level-of-detail copy or a separate variant. It is a
  different decomposition with many small `Patte` structs, and nothing in the
  file distinguishes the two cases. **[unverified]**
