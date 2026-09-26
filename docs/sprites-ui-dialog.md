# Sprites, menus, and timed dialogue

What the retail UI loads, how its sprite pixels reach the screen, which menu
loops are separate, and how a `DIALOG.DRD` entry becomes voice plus timed text.
Evidence is from both retail discs and static decompilation of `WINDREAM.EXE`
(image base `0x400000`). The sprite and dialogue paths below are verified
unless explicitly marked open.

## UI asset path

During initialization, `FUN_004341eb` loads `DATA\ICONE\ICONES.BF` and binds
the menu assets. `ICONES.BF` is a `UBIK` container, not an image sheet itself.
Its members are sprite banks with a shared `TABLE` descriptor layout. Disc 1
ships `MAGIE.ALP`, `ANIM.ALP`, `PYRAM.ALP`, `TOUCHES.SPR`, and `INTERF.ALP`;
disc 2 adds `TITRES.SPR`. The executable's five-file bank list names only the
first five. The boot menu draws its four labels through `FUN_00426073`, so it
does not use `TITRES.SPR` for those labels; no other runtime use has been found.

Names also live in the executable. `FUN_00427217` searches a 72-name table at
`0x49DB12` and returns a `(bank, slot)` pair from `0x49DD9A`; bank filenames
come from `0x49DACC`. The filenames and sprite slots are therefore separate
from the pixel records in each bank.

| Bank | Contents decoded from the shipping members |
|---|---|
| `MAGIE` | Inventory and ability icons; the first 30 name-table IDs follow `[OBJECT]` order, mapped indirectly to non-linear pixel slots |
| `ANIM` | Small animation/status icons |
| `PYRAM` | Spell-selector pyramid pieces, cursor, and replay/record elements |
| `TOUCHES` | Direction, keyboard, and joypad caps shown on the controls screen |
| `INTERF` | Main-menu active/inactive corner brackets, ribbons, and description panels |
| `TITRES` | Twelve indexed title images on disc 2; not used for boot-menu labels and no other runtime use found |
| `SOUR.ALP` | Two cursor frames, loaded separately from `DATA\OBJET` |

See [file-formats.md](file-formats.md) for the bundle and record layouts and
the full executable name map.

## Correct pixel interpretation

Each member starts with a 256-entry RGB555 palette. `FUN_00426C46` binds that
palette to each sprite descriptor, converts palette words from 5:5:5 to 5:6:5
when the display is 16-bit, and copies the pixel bytes without converting
them. `FUN_004274B0` selects a bank and slot; `FUN_00401935` performs the
screen blit.

There are two pixel layouts:

| Stored size | Retail interpretation | Files |
|---|---|---|
| 1 byte per pixel | Palette index; index 0 is transparent | `TOUCHES.SPR`, `TITRES.SPR` |
| 2 bytes per pixel | Byte 0 is palette index; byte 1 is blend/opacity input; index 0 is transparent | `.ALP` banks and `SOUR.ALP`; `PYRAM` also has compositor markers |

`FUN_004274B0` puts flag `0x10` in the draw descriptor. `FUN_00401935` maps
that to `DAT_0049D126=1`; the branch at `0x401C04` jumps to `0x401DAD`, which
reads the two-byte texel and uses the second byte directly. It skips coverage
0, copies coverage `>=63` as opaque, and blends values 1–62 through
`FUN_00401524`. That helper uses `weight = coverage >> 1` (0–31) and blends
each packed channel as `((31 - weight) * destination + weight * source) >> 5`.
`FUN_00424F7E` initializes the multiplication lookup table used by
`FUN_004014D0`, confirming the weights. The divide at `0x401EBF` belongs to a
different flag branch (`DAT_0049D12A=1`), which the `0x10` menu wrapper does
not select. The PNG preview maps the raw coverage through the same blend
weight. Straight alpha cannot reproduce the exact per-channel sum, whose two
weights total 31 before division by 32, so the preview can differ by a small
amount in blended edge pixels.

`PYRAM.ALP` has a second special case: in slots 0 (`pyrambo`) and 5
(`exprbor`), coverage values `0xff`, `0xfe`, and `0xfd` are compositor
commands, not opacity. `FUN_0040368B`, called from `FUN_00427859`, resolves
`0xff` through linked descriptor 3 and `0xfe` through descriptor 4, subject
to animation bounds. `0xfd` can read linked descriptor 5; in another state it
is transparent or selects one of two hard-coded fills. These layer references
depend on menu state and animation frame, so a standalone sheet cannot be the
final in-game pyramid image. The extractor shows them in diagnostic colors
(magenta/cyan/yellow) rather than as opaque palette pixels.

The composition uses a procedural mask field as well as the `.ALP` file.
`FUN_00403B60` treats BSS storage at `0x5DFB8C` as an 84×64 byte field. Each
call writes 16 PRNG words to row 83 (`+0x14C0`), masks them with
`0xAFAFAFAF`, then updates the 82×62 interior (rows 1–82, columns 1–62) as
the average of itself, the next byte in its row, and two bytes in the next row
(`+0x3f`, `+0x40`). It walks upward from the seeded bottom row, so each pass
diffuses that noise through the field. The PRNG state begins at the
initialized executable value `0xFE9A735C`. Startup runs eight passes after a
DirectDraw `Unlock`; the smoother receives the unlock HRESULT in EAX, which is
zero on success. During UI updates, `FUN_00434596` calls it after the viewport
layout helper leaves the screen-height quotient in EAX (1 for the common
400/480-line modes).

`FUN_00427859` updates the `pyrafvi`/`pyrafma` descriptor pointers, then
`FUN_00427AE9` copies 84 rows ×32 bytes from two horizontal windows of that
field into the opacity bytes of their 64×84 sprite texels. One window starts
15 bytes to the right of the other. `FUN_0040368B` composes those dynamically
masked layers with the marker-bearing base image; `FUN_004274B0` draws the
cursor separately. A faithful static composite therefore needs the live
field, animation frame, and current state parameters, not just the raw
`PYRAM.ALP` bytes. The source field is generated at runtime rather than read
from an asset.

The previous decoder treated the two bytes as a byte-swapped RGB555 word.
That produced green and purple artifacts. Some `*_sheet.png` files under
`out/boot/icones/` were left over from that experiment, so they did not reflect
the current decoder. They have now been regenerated through the indexed/blend
path. The retail renderer contradicts the byte-swapped reading: its indexed
lookup and blend operations prove the two-byte layout. The corrected sheet
renderer and test are in
[`image.py`](../src/dreams/formats/image.py) and
[`test_menu_sprites.py`](../tests/test_menu_sprites.py). Corrected contact
sheets were visually inspected for `MAGIE`, `ANIM`, `PYRAM`, `TOUCHES`,
`INTERF`, and `TITRES`; `PYRAM` command pixels are diagnostic overlays, not a
composite preview. The browser's exported `INTERF` corner-bracket sprites now
use the same decoder through `tools/export_menu_sprites.py`; before this fix,
that exporter independently treated each two-byte texel as a swapped color
word. Regenerate these web assets with `uv run python tools/export_menu_sprites.py`.
The inspection contact sheets are scratch output under `out/boot/icones/`; the
font previews are under `out/boot/fonts/decoded/`. To regenerate the individual
extractable sprites and glyphs from the configured disc paths, run
`uv run dreams extract --only sprites,icons --force`; outputs go to the
configured extraction root.

## Player pyramid HUD

`FUN_00427315` binds `pyrambo`, `pyramvi`, `pyramma`, `pyramox`, `pyrafvi`,
`pyrafma`, and `pyrcurs`. The render tick `FUN_00434596` calls
`FUN_00427859` with the primary frame's top-left at
`x = 20 / sx`, `y = (H - 90) / sy`, where `H` is the logical height computed
by `FUN_004344AE`. The 64×84 frame is drawn at native size when `sx=sy=1` and
at 32×42 when both scale factors are 2. For 320×240, the helper uses logical
size 640×400 and `(sx,sy)=(2,2)`, giving `(10,155)`; at 640×480 it uses
640×480 and `(1,1)`, giving `(20,390)`.
At the executable's initialized 640×400 size, it uses `(1,1)`, giving
`(20,310)`.

The active 40×40 `MAGIE.ALP` icon is placed at
`((W - 230) / sx, (H - 50) / sy)`. Three quick-slot icons use the same y and
x positions `((W - 160 + 50*n) / sx)`, for `n=0..2`. The 320×240 placement is
`(205,175)` with 20×20 output; the 640×400 default is `(410,350)`, and at
640×480 it is `(410,430)` at 40×40. Here
`W` is 640 when the screen width is a multiple of 320 or equals the executable's
1024 reference width, and otherwise 800. `H` is 480 when the height is 480,
600 when divisible by 300, and otherwise 400. The code chooses `sx=2` below
401 pixels wide and `sy=2` below 400 pixels high; other sizes use 1. These
branches, rather than a three-entry resolution table, are the renderer's scale
rules.

The gauge is a layer compositor, not a rectangular crop. `FUN_0040368B`
composites `pyrambo`'s `0xff`, `0xfe`, and `0xfd` texel commands from linked
sprites, clipping substitutions by row thresholds. The main HUD path sends
actor `+0x38` (vitality), `+0x3c` (magic), and `+0x40` (oxygen); the fallback
globals are `0x004fbab0` (100), `0x004fbab4` (20), and `0x004fbab8` (0).
The compositor maps vitality and magic through 48 rows and oxygen through 76
rows, each against a 100-point scale. `FUN_00423399` drains actor `+0x40` when
the actor is at least 150 units below the water surface:
`oxygen -= scene[+0x118] * frameDelta * 0.01`. At zero it sets the meter to 40
and runs the actor transition handler. `FUN_004231F0` restores it to 100 in
the safe water-height band, and `FUN_00423399` caps it at 100. Actor `+0x50`
(fallback `0x004fbac8`) is a separate
HUD state/blink input; `FUN_0042CE4A` adjusts it with state-dependent
frame-time rates and a 200-unit bound.

`FUN_00427859` normally selects `pyramvi` and `pyramma`. When vitality drops
by more than one point it selects `pyrafvi` for that composite pass; when
magic rises by more than one point it selects `pyrafma`. It restores the
normal descriptors after drawing, so these are brief resource-change flashes,
not low-resource warnings. `FUN_00403B60` generates an 84×64 noise field and
`FUN_00427AE9` copies two 84×32 windows into the mask sprites' texel coverage
bytes.
`pyrcurs` is a 4×4 sprite drawn separately at `x + 30/sx` and
`y + (77-random)/sy`; it floats along the center of the 2D pyramid art. There
is no 3D pyramid rotation or 14-face spell map. The 14 abilities are the first
14 entries of the pause overlay's 4×4 `MAGIE` grid. `FUN_00430494` lays them
out column-major at `x=(W/2-30)/sx + 80*column/sx`,
`y=41/sy + 50*row/sy`, with `column=id/4` and `row=id%4`:

| ID | Ability | Column | Row |
|---:|---|---:|---:|
| 0 | feu | 0 | 0 |
| 1 | arc | 0 | 1 |
| 2 | epee | 0 | 2 |
| 3 | guerison | 0 | 3 |
| 4 | bouclier | 1 | 0 |
| 5 | connaiss | 1 | 1 |
| 6 | temps | 1 | 2 |
| 7 | spirit | 1 | 3 |
| 8 | holo | 2 | 0 |
| 9 | resurec | 2 | 1 |
| 10 | invivib | 2 | 2 |
| 11 | mine | 2 | 3 |
| 12 | shaman | 3 | 0 |
| 13 | vitesse | 3 | 1 |

The active actor pointer used by this HUD is `DAT_004fbb48`. Its current
vitality is read at `+0x38` (the damage handler `FUN_00443619` also changes
this field), current magic is read at `+0x3c`, and current oxygen at `+0x40`.
`FUN_0042D0AE` initializes the actor's current vitality at `+0x38` from the
100-point global and current magic at `+0x3c` from the 20-point starting-magic
global. No separate maximum-vitality actor field is confirmed; `+0x1c` remains
unknown. The fallback HP global is reset to 100 on game over. The engine caps
current magic at 100; no separate maximum-magic actor offset was found.
Oxygen's current and maximum are `+0x40` and 100. No live experience value or
`exprlev` draw was found.

`exprbor` and `exprlev` exist in the `PYRAM.ALP` name table, but no code xref
to either name and no draw of their slots appears in the HUD path. Their
runtime placement and a live experience/level variable therefore remain
unverified; they may be unused assets in this build.

## Separate menu loops

The boot menu is a 2×2 grid over the looping `GENERIC.HNM` video. It uses four
text labels and `INTERF` corner sprites. `FUN_00435C2B` resolves the active and
inactive corner names and places them at resolution-scaled positions;
`FUN_00435EA0` draws the four corners and menu text; `FUN_00435FAE` handles
navigation and confirm. Selection state 0–3 maps to New Game, Load, Options,
and Quit. Up/down moves around the grid as `0→2→3→1` and `1→0→2→3`;
left/right use the complementary order. New Game exits toward project loading,
Load opens the save browser, Options opens a separate loop, and Quit ends the
boot controller. The full flow is in [boot-sequence.md](boot-sequence.md).

The in-game inventory/pause overlay is a separate controller
(`FUN_004337C0` → `FUN_00432B45`), with its own item and options states. On entry,
`FUN_00430E46` resolves the four active/inactive corner markers, four
description panels, and two ribbons from `INTERF`. `FUN_004308F2` and
`FUN_0042FB74` build two item lists by filtering the executable's icon-name
table through the category byte at `0x49DFDA`. `FUN_00432B45` draws four
top-level states:

| Overlay state | Category | Contents and renderer |
|---:|---:|---|
| 0 | flag 1, 14 entries | Spells and abilities; 4×4 icon grid in `FUN_00430494` |
| 1 | flag 2, 16 entries | Inventory/quest objects; 4×4 icon grid in `FUN_0042F72E` |
| 2 | — | Action list shows `Load` / `Options` / `Quit`; nested pages use the save-slot helper and the four settings toggles |
| 3 | — | Close the overlay and return to play |

The two category counts match the 30 `[OBJECT]` entries in `DREAMS.INI`:
14 ability names (`feu`, `arc`, `epee`, `guerison`, `bouclier`, `connaiss`,
`temps`, `spirit`, `holo`, `resurec`, `invivib`, `mine`, `shaman`, `vitesse`)
and 16 key/quest-object names (`cleeau` through `surfplan`). The table labels
each name and sprite slot; the category byte supplies the page.
The name-table IDs follow the `[OBJECT]` enumeration, but their `MAGIE.ALP`
pixel slots are a separate, non-linear mapping.
`FUN_004318D0` handles directional and confirm input; `FUN_0042FE75` and
`FUN_0042F38B` navigate the two item grids. The selected icon and its three
description lines go through `FUN_0042F246` or `FUN_0042FD30`.

The gameplay controller checks mapped input flag `0x006308e1` and calls
`FUN_0043141C` to open the spell page. In the column-major grid, left/right
move by `-4/+4` and up/down by `-1/+1`; Space is the keyboard confirm action
and Escape cancels the overlay. Three configured button flags
(`0x00630909/0x0063090a/0x0063090b`) assign the selected spell to quick slot
0/1/2. The binary identifies the spell-page action flag, but not which
physical keyboard key or joystick button is bound to it on this install.

In state 2, `DAT_004A155B` selects four actions: 0 calls
`FUN_00437AA2(0)` to load a slot; 1 calls `FUN_00437AA2(1)` to save (it
filters for slots whose status field is clear); 2 enters Options; 3 sets the
game-exit flag. The save/load view is drawn by `FUN_00437C01`. Options exposes
four toggles: real/2D shadow, manual/automatic fight, volume max/min, and
cinemascope/full screen. The static English labels found in this overlay are
`Load` (`0x004c519e`), `Options` (`0x004c51a3`), and `Quit` (`0x004c51ab`).
For action 1 (Save), `FUN_0043126A(1)` indexes the text block at
`0x004a102c + 1*0x63 = 0x004a108f`, beginning `Save the game`, followed by
`in progress...`. This is the selected action's description, not a separate
short label. There is no standalone `Save` literal in the executable, so the
runtime source of the short Save label remains unresolved. `DREAMS.INI`
supplies additional localized system strings and inventory descriptions.

The save/load browser and boot Options screen remain separate controllers.
Another cyclic UI-message handler, `FUN_00435896`, consumes 12-byte events;
event `0x40` selects the voice/caption entry described below. The per-frame
task table is built by `FUN_004288C6`, called during scene setup. It copies
`LINKADVENT +0x1C` into task `+0x10`; for opcode `0x40`, that field is the
one-based dialogue entry ID. When its proximity/interaction conditions pass,
`FUN_00429061` queues `0x40` with the value minus one as the zero-based entry
index. Of the 80 `DREAMS.DAT` records with opcode `0x40`, 73 have a nonzero
dialogue ID in the observed 1–174 range. Thus the speech ID comes from `LINKADVENT`, not
directly from NPC `OBJET +0x70` (a behavior-specific parameter in the actor
loader). A nonempty `LINKADVENT +0x2C` filename is also copied into the task
and opened through the video-event path. `FUN_00420B60` is a separate
level-trigger handler; it queues UI event `0x42` with a target name and can
start a configured cutscene video.
`FUN_00427315` resolves seven named `PYRAM` assets during UI initialization.

## Fonts and text

The game loads one of `HI320.SPR`, `HI480.SPR`, and `HI640.SPR` for its chosen
resolution (plus a duplicate `HI320` slot). Each file has a 256-word RGB555
palette, one-byte palette-index glyph images, 256 fixed 28-byte descriptors,
and a trailing `u32` count of 256. `FUN_00425C61` builds a 256-entry advance
table from `width - s32(descriptor[+0x0c])`; the space entry takes the advance
of `'0'`. `FUN_00425F07` formats and lays out the string, and the glyph path
blits nonzero palette indices. The decoder and `extract sprites` group now
write one PNG per renderable glyph. `HI320` codepoint 37 (`%`) has nonsensical
dimensions and is skipped; it is valid in the other two files. Whether this
slot was intended as a formatting control remains open.

## Other indexed OBJET sheets

The separate `DATA\OBJET\*.SPR` family uses the 6-bit VGA palette and
pointer-table format described in [file-formats.md](file-formats.md). The
shipping files decode to 64 `ALPHABET` glyphs, 64 `ALPHABE2` glyphs, 64
`PARTICL2` particles, 32 `PARTICLE` sprites, and 8 `OBJET0` icons. Paired
contact sheets in `out/boot/obj-sprites/` were inspected with palette index 0
transparent and with every index opaque. The transparent view reads as clean
glyphs, particles, and icons, but this family has no identified retail draw
call. Ghidra finds filename strings
for `OBJET0.SPR`, `PARTICLE.SPR`, and `ALPHABE2.SPR` but no direct code xrefs to
them. So index-0 transparency for these OBJET sheets remains an explicit
extractor assumption, separate from the confirmed UI/font color key.

## `DIALOG.DRD`: entry event to voice and captions

`DIALOG.DRD` contains 178 entries, each with a mono 11,025 Hz, 8-bit WAVE and
timed English text. The parser recovers 589 non-empty text lines. The file is
not all loaded into memory at startup: `FUN_0041072C` opens it, keeps the entry
offset table, and allocates a reusable entry buffer. `FUN_00410928` seeks and
reads the requested entry into that buffer.

The runtime presentation path is:

```text
UI event 0x40, payload = dialogue entry index
  -> FUN_00435896
  -> FUN_00410CD6 / FUN_00410928: load the entry's WAVE and text blocks
  -> FUN_00410D19 / FUN_00446E01: submit the voice bytes to the sound buffer
  -> FUN_00436AB6: display timed lines through FUN_00425F07
```

Each line record stores a raw `u32` timing, a `u8` string length, and the
NUL-terminated text. `FUN_00410928` scales the timing by `15/100`; the
presentation loop uses those values to advance the displayed lines. The voice
and text share an entry ID, so their association is in the archive itself,
not a separate subtitle file. Tag 4 is an indexed portrait sprite, not facial
keyframes or camera directives: 169 records contain a `TABLE` payload with a
512-byte RGB555 palette, one 2-byte-per-pixel 124×124 or 128×128 image,
capacity for 256 descriptors, and a trailing count of 1. The engine's
`FUN_00427020` copies the palette and first descriptor into a runtime sprite;
`FUN_00427432` draws it at `(16/sx,64/sy)`.

Captions are left-aligned. `FUN_00436AB6` places timed text at
`(150/sx, 90/sy)`, with 20/sy vertical spacing for successive rows. Each
tag-3 record is already one display line; `FUN_00425F07` advances glyphs but
does not wrap them to a box width. No dark backing rectangle or second shadow
pass is drawn on this path. The only separate UI sprite here is the `joy_swi`
prompt at `((W-40)/sx,165/sy)`; it is not a subtitle scrim.

The runtime trace proves the timed text presentation path and portrait role.
`TITRES.SPR` remains absent from the five-bank loader and has no runtime
references found outside extraction; the saved title images are unused by the
retail UI. The exact runtime field that feeds event `0x40` before it reaches
the action task, the separate `exprbor`/`exprlev` draws, and the physical key
bound to the spell-page action remain open.
