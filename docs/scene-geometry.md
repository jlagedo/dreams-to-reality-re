# Scene geometry — `.DSN` from container to glTF

How a level goes from 1.8 MB of packed bytes to a mesh in Blender. Everything
here is **[verified]** on this machine unless marked otherwise.

`uv run dreams mesh` lists every scene; `--gltf DIR` exports; `--preview DIR`
writes a three-view PNG.

## The pipeline

```
DSNF header  ->  body = chain of tagged records
                   tag 1  LZ  ->  material table + per-object face records
                   tag 2  LZ  ->  vertex pool
                   tag 3      ->  256-entry RGB565 palette per object
                   tag 4 x64  ->  32x32 planes, interleaved to 256x256
```

Header layout and the record chain are in [file-formats.md](file-formats.md).
The codec is in [`dreams.formats.lz`](../src/dreams/formats/lz.py) and decodes
**610/610** tag 1 and tag 2 records across every scene and animation on both
discs, with zero failures.

## Tag 2 — the vertex pool

```
+0x14  u32          pool length
+0x30  i32[3] x N   x, y, z in signed integer scene units
```

Not float and not 16.16 fixed point — plain signed integers. A room is a couple
of thousand units across; `E01GROTT` spans 2464 x 2140 x 2166.

The rest of tag 2 is unaccounted for. `E10_PIEC` carries 12,480 bytes for at
most ~94 vertices, so something else lives past the array.

## Tag 1 — materials and faces

```
+0x18  u32            1200
+0x1c  u32            material count (objects + 1 for DEFAULT)
+0x20  44 bytes each  char[16] MATERIAL name
                      char[16] texture name, the same word lowercased
                      u32      DEFAULT holds 0x3DEF3DEF, an RGB555 mid-grey
                      u32      0x3F800000 (1.0f) on textured entries
                      u32
```

Then one block per object, located by its name and confirmed by the `68` at
`+32`:

```
name-8   u32       3
name+0   char[16]  object name
name+16  u32       face-record count
name+20  u32       pointer to this object's face records
name+32  u32       68, the record size
name+44  ...       the face records
```

### The 68-byte face record

Seventeen little-endian words. Four reference classes, three of each — one per
triangle corner:

| Words | Offsets | Class | Congruence | Role |
|---|---|---|---|---|
| 1, 4, 7 | `+4,+16,+28` | vertex | `1 mod 20` | index into the tag 2 pool |
| 2, 5, 8 | `+8,+20,+32` | parallel | `13 mod 16` | sorted order matches the vertex refs |
| 3, 6, 9 | `+12,+24,+36` | edge | `77 mod 100` | 100-byte records: a self-pointer stepping by 100 and two more vertex refs, so edge or adjacency data |
| 12, 13, 14 | `+48,+52,+56` | UV | `5 mod 8` | 8-byte records, two 16.16 values |

Word 0 is a self-pointer stepping by 68. Word 15 is zero in every record.

## References are stale pointers

This is the part that matters. A reference is **not an index** — it is a raw
address left over from the exporter's memory, so its absolute value is
meaningless. Resolving one against tag 1 lands inside the material-name table:
reference 221 sits in the middle of the string `e01_sol4`.

What is usable is the *spacing*. Vertex references sit on **40-byte slots**, so
when a scene's distinct references form one contiguous run from 221,
`ref_i == 221 + 40*i` and sorting them gives the index by construction.

**A reference record does not start at the reference.** Each class points some
bytes into its own record, and the offset differs per class — UV references are
`5 mod 8`, so their record starts at `p - 5`. Reading at `p - 1`, which is
right for the other classes, straddles two entries and textures render as
diagonal streaks.

## The arena directory

**[verified] 95/95.** Tag 1 records where each source vertex array began. The
directory is reached through a bias any object block reveals, since a block
stores both its own address and its own offset:

```
D       = name_offset - u32(name_offset + 20)
bias    = D - 200
n       = u32(tag1 + 0x14)            arena count
O[j]    = u32(tag1 + 0x18 + 4*j)      descriptor offsets
base[j] = O[j] - bias                 base in reference space

at tag1 + O[j]:
    +0x90  u32  count
    +0x94  u32  base[j]               confirms the bias
    +0x9c  u32  base[j] + 40*count    confirms the extent
```

Both identities hold in every scene, and every vertex reference falls inside
some arena. A reference belongs to the arena with the greatest `base <= ref`,
at local slot `(ref - base) // 40`.

This **classifies** a reference without **resolving** it. Arena counts sum to
the pool size in only 15 of 95 scenes, and even there, searching one index
offset per arena matches 26-57% of faces. The remaining step is a shared-vertex
merge that is not affine: `E10_PIEC` has 94 distinct references for 64 pool
entries, with zero duplicate coordinates in the pool, so several references
resolve to the same vertex.

Tag 1 carries no coordinates of its own — searching all 100,496 bytes of
`E01GROTT`'s tag 1 at every alignment for any of the pool's 193 triples returns
zero hits — so the information to undo that merge is not in the file.

### Why it is not in the file

**[verified]** in `WINDREAM.EXE`: the engine never converts a reference to an
index. It does **pointer relocation**. `FUN_00456e24` decodes a record into an
arena and hands it to `FUN_00456368`, which walks the structure through
`FUN_00455eb4` / `FUN_00455e48` / `FUN_00455d6c` down to `FUN_00455700`. That
function strides the 68-byte records with an explicit `+= 0x44` and adds one
relocation delta to every pointer field:

```c
for (i = 0; i < object->count; ++i) {
    p = object->records + i * 0x44;
    if (*(u32 *)(p + 4) != 0) *(u32 *)(p + 4) += delta;
    *(u32 *)(p + 8) += delta;   /* ... through +0x3c */
}
```

There is no subtract-and-divide by 40, no sorted-rank table, no hash, and no
tag 2 lookup anywhere in that chain. A stored value simply becomes a valid
pointer.

So the mapping we want **never existed**. The arena directory's bias makes
`ref + bias` a tag 1 offset, and that lands on 40-byte records holding Q15
values (`32768` = 1.0) — per-vertex attributes, not positions. Positions live
only in tag 2, and the two are parallel arrays that happen to coincide in order
when a scene has a single arena. That is exactly the set of scenes that verify.

`FUN_00473014` installs the render callback and walks the object list, so the
face records **are** rendered; tag 2's triangle array with its normals, edge
half-spaces and bounding boxes is the separate collision or spatial structure.

## Coverage — 5 of 95, and why

The gate is **proof, not a heuristic**: a tag 1 decode is accepted only when
*every* face it produces is also a triangle in tag 2, which is known-correct
geometry. Five scenes pass — **E01GROTT, E98ARAI1, L03_REQI, L16_BOMB and
O01EAU01** — and they are exactly the single-arena scenes. Those carry object
names, materials and UVs. The other 90 fall back to
:func:`read_tri_mesh`: correct geometry, no materials.

Ruled out, so they are not retried:

- ordering arenas by pointer value, or by the object that first references them
  — both leave floor planarity at 8/39, no better than doing nothing
- one index offset per arena, searched against tag 2's triangles — 26-57%
- directory-order concatenation, even on the 15 scenes where arena counts sum
  to the pool size exactly — only the single-arena ones reach 100%
- `index = (ref - base) / 20` — overruns the pool in every scene
- constraint propagation over faces against tag 2's triangle set — no solution
  inside a 3-second budget, ~10k nodes
- connected components as objects — `E01GROTT` is one welded component for 26
  named objects
- references indexing tag 1 directly — lands in the material-name table

## Checking a decode

The numeric screens **do not work**, and this cost real time. Four scenes with a
single clean run but a length short of the pool render as debris while scoring
under 6% degenerate triangles, passing edge sanity, and carrying unremarkable
bounding boxes. `L14_PETI` is a twisted ribbon at 0.8% slivers. Geometry can be
locally plausible and globally wrong.

What does work:

1. **Render it.** `--preview` draws three z-buffered axis views. `E01GROTT` is a
   closed room with a doorway; `L03_REQI` is a submarine hull with a pointed
   nose and tapered tail. Debris is obvious against those.
2. **Use the French names as ground truth.** `ME`/`MN`/`MO`/`MS` are Mur
   Est/Nord/Ouest/Sud, `SOL` floor, `P`/`PLAF` plafond, `COL` colonne. On
   `E01GROTT` the east walls centre at x=+1111 against west at x=-1161, north
   z=+1081 against south z=-827, floor y=-8, ceiling y=-2026 — the compass
   names reproduced by the geometry, independently.
3. **Floor planarity**, but only on scenes with several `SOL` objects. Tunnels
   and sculpted caves have genuinely non-planar floors.

## What `E01GROTT` turned out to be

193 vertices, 338 faces, 26 objects. A closed chamber roughly 24 m across with
**one doorway**: the south wall splits at `x = -207..209`, 416 units wide and
open floor to ceiling, while every other wall meets exactly — `MN1`/`MN2` at
`x = -25`, `ME1`/`ME2` and `MO1`/`MO2` at `z = 135`. A floor strip runs south
through the gap. `E01_SOL5` is the one non-planar floor: a raised mound in the
centre of the shaman's cave.

There is **no missing transform** — objects are already in world space.

## Export

`dreams/gltf.py` is a dependency-free glTF 2.0 writer: `.gltf` plus `.bin`, Y
negated because the engine puts the floor at 0 and the ceiling at large
negative Y, geometry unindexed because UVs are per corner. Textures are the
interleaved 256x256 surfaces, one PNG per object.

## The scene-graph node — and why it solves models but not levels

The struct behind `.DSN` tag 1 is the engine's **scene-graph node**, documented
in full in [models.md](models.md) and implemented in
[`src/dreams/formats/node.py`](../src/dreams/formats/node.py). It is found by
signature — `u32 40` at `+0xd4` plus `+0x9c == +0x94 + 40*count` — and holds a
parent/child/sibling triple at `+0x24`/`+0x28`/`+0x2c`, a local transform at
`+0x30`/`+0x3c`, and its own vertex array 240 bytes later. **2,248 nodes across
95 scenes** satisfy the signature. **[verified]**

That decode gives `.DAN` models completely: 159/159, 1,886 parts, 41,614
triangles.

### It gives levels too — the old score was measuring rounding

This was recorded as *"it does not give levels"*: composing world transforms
through the parent links and comparing against tag 2's pool reached ≥99% in
only **8 of 95 scenes**, median **11.4%**, which was read as proof that the
graph contains **group nodes carrying a transform but no geometry** that
`find_nodes` cannot see.

**That score compared integers for exact equality.** World positions are
composed with an integer `>> 15` at every level of the tree, so a node a few
levels down lands a unit or two from where the engine put it. Allowing that:

| tolerance | scenes at ≥99% | median |
|---|---:|---:|
| exact | 8 / 95 | 11.4% |
| **±2 units** | **58 / 95** | **100%** |
| ±8 units | unchanged | unchanged |
| ±32 units | unchanged | unchanged |

±2 on a scene spanning ~100,000 units is **one part in 50,000**. And the
flatness is the argument: this is not loosening until it passes. A sharp step
at 2 and then nothing is the signature of a **fixed rounding offset**, which
is what a flooring shift produces against an engine that rounds differently.
A genuinely misplaced object does not come back at ±2 and stay put at ±32.

Corroborating, on the same decode: the face count equals tag 2's in **57 of 95**
scenes, the bounds agree in 54 of those, and `H03PAQUE` yields **24 nodes for
its 24 named objects with 23 of 24 parents resolving and none dangling** — no
missing group nodes in that scene at all. **[verified]**

So `dreams.formats.mesh.read_scene` now prefers the node decode, and **58 of 95
scenes export with per-object names and UVs** where 5 did before. That is what
makes a level *textured*: tag 2 is one nameless merged triangle array, so a
scene exported from it draws as a blank hull — `H18ANGKR` came out a smooth
sphere instead of a grass plateau carrying a temple and its roots.

The remaining 37 scenes still fall back to tag 2, and the group-node reading may
yet be right for some of them; `E15_RIDE` reaches only 8.3% at ±2. Ruled out
already, so not retried: ordering arenas by pointer value or first use, one
index offset per arena, directory-order concatenation, `(ref-base)/20`, CSP over
faces, connected components as objects, and a pivot at the bounding-sphere
centre.
