# Render placement and model exceptions

Reviewed against WINDREAM's loaders and draw path, 2026-09-26. The source
reviews are recorded in [renderer-gaps review](../../re/reviews/renderer-gaps-20260926.md).

## Why all 95 scenes use tag 1

`RES_Relocate` (`0x456368`) routes type 1 through `MDL_RelocNodeTree`
(`0x455e48`) and type 5 through `MDL_RelocCollision` (`0x455fb4`). The entity
loader attaches the visual `.3DC` resource and separately registers `.3DI`
collision. `REND_DrawScene` / `REND_DrawObject` (`0x47e700` / `0x47e498`)
compose the visual node hierarchy and submit its own faces. No collision
placement vote approves or adjusts that render transform.

The Python selector previously let `check_nodes` reject tag 1 when another
offset matched more collision points. That gate is removed. **All 95 scenes
now select the tag-1 graph**, retaining names, materials, signed UVs and stored
transforms. `check_nodes` remains a diagnostic; no voted offsets are applied.
Legacy fallback routes remain available when a graph cannot be decoded.

The corpus has 2,248 geometric nodes, 157,433 tag-1 faces and 472,299 resolved
face-vertex pointers, versus 152,536 collision triangles. These are distinct
representations; unequal positions or counts do not imply a broken render decode.

## The eleven former fallbacks

Offsets below are diagnostic votes from composed render positions toward
collision positions, with the existing tolerance. They are **not corrections**.

| Scene / node | Groups | Voted offset | What the evidence supports |
|---|---|---|---|
| E04ARAI2 / 4 | E04_B2 | (0,-305,1), (1,-305,1) tied | Only 6/12 distinct points match; repeated-shape ambiguity |
| E11_ANGK / 1 | E11_B01 | (0,284,0) | Four points match another collision placement; mover identity unproved |
| E12_ANGK / 3 | E12_EAU | (0,-408,0) | Improves 15/17 already matching points to 17/17; earlier “moving block” label was not established |
| E15_RIDE / 0–5 | Water, neutral and ride groups | Y offsets 7–87, small X/Z offsets | Alternate collision placements; no proven link from the project's F30 entity to these scene nodes |
| E99ARAI2 / 4 | E04_B2 | (-263,-152,1) | Only 6/12 points match |
| F15SOUFF / 50 | F15SDES | Four tied X offsets, Y=1 | Repeated-shape ambiguity; only 6/12 points match |
| F33BATMO / 1 | H06PRO03/04 | (0,286,0) | Alternate placement; project has no platform-flagged OBJET |
| L08_CANY / 2 | L08_CIE1–4 | (0,-31,94) | Alternate placement, no established entity/node link |
| L13_USI2 / 18 | H02VENT1–4 | (13,1,-12) | Project's L14 platform groups have different names; no proven linkage |
| M03DORM / 15 | M03MUR1 | (-1,-33,1), (-1,-35,1) tied | Both match within tolerance; no distinct pose established |
| M07LACM / 24 | M07EAU1 | (0,32,0) | Improves 26/44 to 27/44; insufficient evidence of motion |

OBJET flag 0x0800 becomes actor `+0xad & 8`. `PHYS_InitEntity` (`0x43d62c`)
then invokes `PHYS_BuildPlatforms` (`0x43df1f`) on that entity's model children.
`PHYS_UpdatePlatforms` (`0x43e115`) updates their positions, deltas and bounds;
`PHYS_CollidePlatforms` (`0x43e32e`) uses them for walker contact. This is a
real movement path, but it does not identify an arbitrary displaced DSN group.
Specific mover identities remain a gameplay research question, not an export gate.

## UVs and open surfaces

The 159-model scan found zero invalid UV-record pointers. F37/H14 use face
type 2, which the faithful Glide hook clamps; L14 uses type 9, which wraps.
Both consume the signed stored coordinates. The old decoder's `max(value,0)`
discarded authored information. The reader now preserves signed normalized
UVs and block types; glTF emits the corresponding samplers and material extras.
See [glide-renderer.md](glide-renderer.md) for the independent hardware evidence.
This does not assert the same addressing or interpolation in the software spans.

`ARC.3DC` has 49 vertex records and **75 faces in blocks of 72 and 3**. Every
face resolves. Its four components have 33, 6, 33 and 3 faces; 15 source edges
have one incident face, with no degenerate or duplicate geometric faces.
The executable does not synthesize caps. Preserve that open source topology;
artistic intent or an original asset defect requires visual comparison.

The current production reader and raw block census accept **41,608 DAN faces**
(124,824 corners). Older docs reported 41,614 faces and 126,819 UV references;
those are not a reproducible current baseline. The six-face historical count
difference remains untraced, so it is not evidence for adding six triangles.

## Validation boundary

Tests cover all 95 render routes, every checked scene face pointer, the E12_EAU
regression, signed UV examples and distinct clamp/repeat material exports.
These establish faithful data selection and preservation. They do not prove
every animated mover pose, the entire Glide state contract in glTF, or visual
equivalence to the running original.
