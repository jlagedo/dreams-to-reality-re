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

## Coverage — 4 of 95, and why

Three conditions make the mapping provable: one contiguous stride-40 run,
starting at 221, whose length matches the pool. That holds for **E01GROTT,
L03_REQI, L16_BOMB and O01EAU01**.

The other 91 split their references across several runs with unrelated bases.
`E10_PIEC` has nine, starting 221, 22,281, 24,281, 29,685, 32,301, 34,917,
35,157, 35,237 and 35,397. Stride is 40 inside every run; only the bases are
unrelated, and nothing yet maps a run to its position in the pool.

Ruled out, so they are not retried:

- ordering runs by pointer value, or by the object that first references them —
  both leave floor planarity at 8/39, no better than doing nothing
- `index = (ref - base) / 20` — overruns the pool in every scene
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
