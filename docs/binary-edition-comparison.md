# Game binary comparison across retail editions

Four canonical editions were compared from the ISO file trees under
`E:\dev_game\DREAMS_ISOS_EXTRACTED`: the European English reference, Dutch,
Spanish and Turkish. The alternate European and Dutch rips and the Spanish
portable ISOs contain byte-identical game files to their corresponding
canonical sources. This page concerns the four game executables on Disc 1.

## Inventory and chronology

| Edition | `WINDREAM` / `GDIDREAM` bytes | `DREAMS.EXE` bytes | `DREAMSFX.EXE` bytes | Windows PE timestamp (UTC) |
|---|---:|---:|---:|---|
| European reference | 864,768 | 1,176,454 | 1,025,154 | 1997-10-29 14:25:41 |
| Dutch | 864,768 | 1,176,678 | 1,025,362 | 1997-11-04 16:42:18 |
| Spanish | 878,080 | 1,191,698 | 1,029,954 | 1997-12-05 15:31:39 |
| Turkish | 877,568 | 1,191,602 | 1,029,874 | 1997-12-11 19:20:46 |

All four editions contain distinct SHA-256 hashes for each game executable.
`SETUP.EXE` and Disc 2's `DEMOS2/CRYO.DLL` are byte-identical across all
editions. The PE timestamps are linker-header values, not independently
verified release dates. Full hashes are in
`E:\dev_game\DREAMS_ISOS_EXTRACTED\game_binary_inventory.csv`.
**[verified]**

## Windows builds

All `WINDREAM.EXE` and `GDIDREAM.EXE` variants are 32-bit Watcom PE files
with the same six section types (`AUTO`, `.idata`, `DGROUP`, `.bss`, `.reloc`,
`.rsrc`), image base `0x400000` and no PE debug directory or COFF symbols.

| Edition | `AUTO` code bytes | `DGROUP` raw bytes | Imported functions | Extra imports versus Europe |
|---|---:|---:|---:|---|
| Europe | 631,808 | 172,032 | 109 | — |
| Dutch | 631,808 | 172,032 | 109 | — |
| Spanish | 642,560 | 172,544 | 111 | `USER32.GetCursorPos`, `USER32.ScreenToClient` |
| Turkish | 642,560 | 172,544 | 109 | — |

The Spanish-only APIs appear in import thunks, but a static scan found no
direct calls or absolute references to them elsewhere in `AUTO`. Their use
is therefore unverified. Resource payload bytes from `.rsrc + 0x200` onward
are identical across all four editions; localized UI text is in the linked
data rather than distinct PE resource payloads. **[verified]**

Within **each** edition, `WINDREAM.EXE` and `GDIDREAM.EXE` have the same
code except one byte: an immediate `0` versus `1` written to the video
backend selector during `VID_Init`. The other file differences are header or
resource-directory fields (10 bytes overall in Europe, Dutch and Turkish;
19 in Spanish). The one code-byte switch is at reference VA `0044603c`,
Dutch `0044608d`, and Spanish/Turkish `00446583`. **[verified]**

Spanish and Turkish have **the same `AUTO` section length and code layout**.
Their 642,560-byte sections differ at 2,279 raw byte positions: 2,266 are
paired PE relocation operands, three are relative branch/call operands, and
ten are the two Spanish-only import thunks. No remaining opcode/data byte
differences were found by this static classification. Their linked data
differs substantially, so equal instruction layouts do not imply identical
runtime text or behavior. **[verified for byte classification]**

## Function-level transfer from the European Windows build

The reference Ghidra feature dump contains 1,192 functions of at least
64 bytes within the `AUTO` section. A conservative matcher used unique
48-byte normalized code anchors, masked paired base relocations and relative
branch/call operands, then required the whole aligned reference function to
match. This found:

| Target edition | Normalized whole-body matches | Instruction-confirmed matches | Aligned but changed | No unique anchor / ambiguous |
|---|---:|---:|---:|---:|
| Dutch | 891 / 1,192 | 884 | 85 | 216 |
| Spanish | 785 / 1,192 | 779 | 158 | 249 |
| Turkish | 780 / 1,192 | 774 | 163 | 249 |

The instruction-confirmed counts are **lower bounds** on unchanged code; a
second pass used Capstone to check instruction sequence and unmasked bytes.
Seven Dutch and six Spanish/Turkish normalized matches were inconclusive or
failed this stricter pass. An unmatched function may have
relocated, been recompiled, changed constants or escaped the anchor search;
it is not automatically a behavior change. See
`E:\dev_game\DREAMS_ISOS_EXTRACTED\windows_function_similarity_validated.csv`
for each candidate address and status. The Dutch and Spanish OMF residue
gives original renderer names independently of this broad matcher; see
[localized-build-symbols.md](localized-build-symbols.md). **[verified for
the matching method and results]**

The Spanish OMF `PUBDEF` functions occur at the **same virtual addresses** in
the Turkish Windows build. The corresponding code regions have the same
instruction bytes after the relocation/branch normalization above, even
though Turkish does not retain the OMF records. **[verified]**

### Which function differences look real?

The broad matcher initially flagged 85 aligned Dutch functions and 158/163
Spanish/Turkish functions as changed. An instruction-aware second pass
reclassified most of these as the same instructions with different addresses:

| Edition | Address operands / branch targets only | Inconclusive decode length | Different instruction layout | Changed non-address operand |
|---|---:|---:|---:|---:|
| Dutch | 78 | 5 | 1 | 1 |
| Spanish | 126 | 30 | 1 | 1 |
| Turkish | 133 | 28 | 1 | 1 |

Thus a `changed_after_alignment` row is **not** automatically a gameplay
change. The full classification and first differing instruction are in
`E:\dev_game\DREAMS_ISOS_EXTRACTED\windows_function_change_analysis.csv`.
The 30 distinct functions with a decode-length mismatch, their established
purposes and review notes are in `windows_inconclusive_functions.csv`.

Among same-layout functions, one clear changed operand is shared across all
three later editions:
`MENJ_PlayVoiceCaptions` subtracts `0x7d` (125) where Europe subtracts
`0x69` (105) before scaling a UI coordinate for `TEXT_PrintFaded`. The same
`SUB EDX, imm8` change occurs in the Windows, DOS and Glide game binaries.
This is a 20-unit adjustment in the voice-caption UI path; its exact visual
effect depends on the scaling variables. **[verified]**

Two formerly inconclusive Windows functions contain confirmed call-path
changes in every later edition:

- `INPUT_UpdateActions`: in input mode **1**, Europe reads the cached
  **keyboard** Space (`VK 0x20`) and Esc (`VK 0x1b`) states. Dutch/Spanish/Turkish
  instead call the existing `JOY_Poll` routine and map its button bits
  `0x4` and `0x200` into the Space and Esc action words. Mode 1 is not the
  `J` hotkey's joystick mode (that is mode 3). The game initializes mode 0,
  and traced direct setters select only modes 0, 3, 4 and 5, so there is no
  confirmed retail path into mode 1. This may be an unused controller-profile
  enhancement rather than an improvement in normal play. **[verified for
  code and traced setters; inferred for intent]**
- `CD_PromptSwap`: after finding the replacement disc, Europe sends MGM
  message `0x23` (`CD_ResumeAudio`). Later builds call `CD_SetTimeFormat`
  directly; that routine stops playback, selects the MCI TMSF format and
  refreshes the CD track count. This may address stale audio state after a
  swap, but author intent is unknown. **[verified for calls; inferred for
  purpose]**

Other decode-length mismatches are not yet proof of changed behavior.
`CTRL_Dispatcher`'s inline dispatch data stops linear disassembly early;
the heuristic candidate for `GAME_TickFrame` begins in the middle of an
instruction. The heuristic also placed Spanish `REND_DrawObject` at
`0047f4cf`, whereas its surviving OMF `PUBDEF` names the true entry
`0047f304`. Among the remaining entries are level/player control, menus,
animation, collision and rendering functions. Their boundaries need to be
established before comparing semantics. **[verified for these examples]**

The Spanish/Turkish `DSOUND_CreateChannels` layout difference is an
instruction reorder: `WAVEFORMATEX.wBitsPerSample = 8` is written before
computing the average byte rate, while Europe writes it afterward. Both
versions present the same initialized format to DirectSound. The Dutch
`MENU_DrawGameMenu` linear-disassembly difference falls in apparent inline
data; it is not yet evidence of a behavior change. **[verified for instruction
order; inferred for the inline-data classification]**

No game executable references the extra `DIALOG16.DRD` filename directly;
all four editions contain `DIALOG.DRD` as the runtime path. That extra bank
does not by itself imply a new voice-loading code path. **[verified by string
scan]**

## DOS and Glide builds

`DREAMS.EXE` and `DREAMSFX.EXE` are Watcom LE files, not PE files. All four
editions' LE headers have zero debug offset/length and no named import
modules. The DOS software build has three LE objects; the Glide build has
six. Their executable object sizes change by edition:

| Edition | `DREAMS.EXE` main executable object | `DREAMSFX.EXE` main executable object |
|---|---:|---:|
| Europe | 716,880 | 626,208 |
| Dutch | 716,896 | 626,240 |
| Spanish | 726,772 | 627,104 |
| Turkish | 726,772 | 627,104 |

Spanish and Turkish share the same LE object layouts, but the code-object
bytes are not identical: 40,974 same-position bytes differ in `DREAMS.EXE`
and 32,920 in `DREAMSFX.EXE`. Those raw counts include fixup operands and
do not establish how many instructions changed. Unlike the PE result above,
the localized LE builds have not been matched function by function.
**[verified for headers and raw bytes]**

## Practical consequence

The current Ghidra addresses and name registry target the European English
build. Direct address copying to Dutch or the later Spanish/Turkish group is
unsafe; use code matches. The Spanish and Turkish Windows code layouts are
especially suitable for transferring the Spanish OMF names to Turkish,
while the Dutch OMF definitions and normalized code match recover their
English counterparts. Data and voice differences are described in
[edition-comparison.md](edition-comparison.md).
