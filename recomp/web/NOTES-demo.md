# DEMO stream notes: the browser demo pack

Owner: DEMO stream (`recomp/web/demo/`, `tests/recomp/test_web_demo.py`). Everything
below was checked by booting the cut with the native Windows recomp (retail
`GDIDREAM.EXE` lifted code, direct renderer, headless, control channel).

## Commands

```sh
uv run python recomp/web/demo/make_demo.py                   # default: profile wip, 34.2 MB raw
uv run python recomp/web/demo/make_demo.py --profile first   # the first level only, 6.65 MB raw
uv run python recomp/web/demo/make_demo.py --movies none     # no movies (see "Movies")
uv run pytest tests/recomp/test_web_demo.py                  # manifest + native boot (20 s)
DREAMS_DEMO_SWEEP=1 uv run pytest tests/recomp/test_web_demo.py   # every project and exit (~8 min)
uv run python recomp/web/demo/verify_cut.py --transitions    # same sweep, printed
```

Outputs, all under `out/recomp/web/` (game-derived, never committed):

| Path | What |
|---|---|
| `demo/manifest.json`, `demo/<sha10>-<name>.NNN` | the pack, per CONTRACT (extra manifest keys: `profile`, `projects`, `movies`); `demo/files.json` says what each file is for |
| `demo-root/` | the same files loose, under their `/dreams` paths (and the EXE directory of a native run) |
| `demo-native/CRYO/DREAMS/` | hard links of `demo-root`, for `WD_READ_ROOTS` of a native run (see "Two path spaces") |
| `demo-rec/` | recordings of which files each retail project opens (`record_opens.py`, `collate_opens.py`) |

Tools in `recomp/web/demo/`: `make_demo.py` (the pack), `demo_common.py` (sources, trees,
bank and DRD writers), `record_opens.py` + `collate_opens.py` (what the retail game opens),
`probe_drd.py` (which DIALOG.DRD entry a level asks for), `verify_cut.py` (boot + exits).

## What the pack is

Retail data only, from disc 1 (`DREAMS_DISC1`); `DREAMS_INSTALL_ROOT` and disc 2 are also searched.
It starts a new game straight in the first level (Ile d'Angkor, `H18ANGKR.DSN`, Project0).

Profile `wip` (default): 68 files, 34,205,907 bytes raw (gzip -9 26.9 MB, brotli 25.8 MB: the
levels and HNM movies are already compressed, only the dialogue bank and some DSNs shrink).
Largest file 2.4 MB, so one chunk per file (chunking at 20 MiB is implemented and tested).

| Part | Bytes | Content |
|---|--:|---|
| 10 levels (`DATA/3DC/*.DSN`, `*.DAN`, `*.3DC`) | 20.2 MB | Projects 0, 62, 39, 30, 108, 46, 31, 99, 48, 76 and the models they place |
| movies (`DATA/HNM/`) | 9.6 MB | `ED1` (P62), `ED2` (P39), `ED0` (P30), `AR_G_COL` (P31), `TUNNEL` (P46), `ED_07` (P76), `ANGKOR` (P39 event cutscene) |
| `DATA/3DC/DIALOG.DRD` | 1.6 MB | 14 of the 178 voice entries (every `condition_stage` of the levels' LINKADVENT records that can be an entry number) |
| `GDIDREAM.EXE` | 0.86 MB | the lifted program's image |
| `DATA/SOUND/FSB.DAT` | 0.74 MB | the sound-effect bank (needed) |
| fonts, icons, sprites, shared models, markers | 1.0 MB | loaded by every level (list in `make_demo.CORE`) |
| `DREAMS.DAT` | 0.14 MB | retail bank, only the pack's projects reachable |

Profile `first`: 36 files, 6,651,677 bytes raw (brotli 4.2 MB): the same core, `H18ANGKR` and its
five models, the dialogue entries of that level, no movies.

### How `DREAMS.DAT` was cut

Retail records stay (150, same layout); only links change. A link to a project outside the pack
is cleared (`LINK0` of Project0 to Project134, `LINK0` of Project62 to Project70). A project left
with no exit gets its main link sent to Project0: Project76 (end of the Angkor chain, its retail
exit goes to Project41) now returns to the start; in profile `first` Project0's exit does the
same. Everything else is the retail record.

## What was cut, and what the game does without it

Each of these was run natively with the file absent:

| Absent | Game behaviour | Needed from the host |
|---|---|---|
| `DATA/HNM/INTRO.HNM` (38 MB) | `BOOT_Run` waits for a key at the intro: **ESC continues** | send ESC |
| `DATA/HNM/GENERIC.HNM` (menu background) | black menu with the corner sprites; **RETURN on "New game"** starts | send RETURN |
| `DATA/HNM/TETE_E~1.HNM` (new-game talking head) | skipped at once, the level loads | nothing |
| CD audio (track 9 for Project0) | `[cd] 0 tracks`; MCI play fails quietly, the game goes on without music | `mciSendCommand` for the CD device must fail or no-op, never block or raise a message box (the native host with no `WD_CD_DIR` does) |
| `magie.alp anim.alp pyram.alp touches.spr interf.alp` | absent in the retail install too | nothing |
| `DATA/GAME/*` (saves) | none exist at start; the game writes `game.dat`, `game0.dat`, `game0.ico` at each level entry | writes must create directories (`CRYO\DREAMS\data\game\...`); the page mounts IDBFS at `/dreams/DATA/GAME` |
| `.DSN` or `.DAN` twin of an asset | the loader tries `X.DSN`, `X.DAN`, then the asset's own name: the failed opens in the log are normal | nothing |
| level animated textures `DATA/ANIM/*.HNM` | level runs without them | nothing (`--movies all` adds `E11_EAU`, `E12_EAU` etc.) |
| other levels, disc 2, DOS and installer files | not reachable from the pack's projects | nothing |

Movies. A level's own movie (`anim_video`) plays when the level is entered by an exit. With the
movie file absent the game sits on a black screen waiting for a key, the same wait as the missing
intro. That was seen with the pack at `--movies none`: Project0 to Project62 never loaded. So the
pack keeps those movies (`--movies project`, default) and `--movies none` is only for experiments.
The movies play through the host's HNM path (decoder and presentation), which therefore has to work
in the browser build. The events' cutscenes (`ANGKOR.HNM`) are the same.

DIALOG.DRD entries the pack lacks point at one shared silent entry (50 ms, no captions). A
missing or unreadable entry otherwise crashes the game in `DSOUND_LoadWav` (seen with an entry cut
to end of file), so a wrong request costs a silent line, not the process. Project0 asks for
entry 170 (zero-based 169) about 6 s into the level; entries 1, 151, 152, 170 are kept for it.

## Requests to the WASM host (it owns `recomp/windream/**`)

1. **Two path spaces, one tree.** The retail game opens files in two namespaces: CD-root paths
   (`dreams.dat`, `data\objet\particle.spr`, `DATA\FONT\HI640.SPR`, `data\icone\icones.bf`,
   `data\sound\fsb.dat`, `data\hnm\*.hnm`, `DATA\1CD.ID`) and install-root paths
   (`CRYO\DREAMS\DATA\HD.ID`, `...\FULL.ID`, `...\LEVEL.ID`, `...\data\3dc\*`, `...\data\game\*`).
   Natively the first resolve in the EXE's directory, the second under `WD_READ_ROOTS`'s
   `CRYO\DREAMS`. The pack is one union tree (`DREAMS.DAT` at the root, `DATA/...` below), so
   in the browser a guest path, drive letter dropped, must be tried with and without a leading
   `CRYO\DREAMS\` below `/dreams`. Nothing in the pack exists under two names.
2. **Case.** The game spells one file several ways (`data\3dc\xh_.DAN`, `DATA\3DC\GR00.3DC`,
   `dreams.dat` for `DREAMS.DAT`, `data\hnm\ED1.HNM`). The pack uses the disc's spelling, so the
   host must resolve case-insensitively (the `files.c` per-segment walk does when the root is
   case-sensitive).
3. **`GDIDREAM.EXE`** is in the pack at `/dreams/GDIDREAM.EXE` (retail disc 1, 864,768 bytes),
   because the host loads the image for its data sections. If the wasm embeds it, drop it with
   `--exclude GDIDREAM.EXE` (or remove the `plan.files["GDIDREAM.EXE"]` line).
4. **Boot input.** After the host starts: press ESC until the guest has opened `generic.hnm`
   (that open fails; it comes right after the intro open fails), then RETURN until it opens
   `dreams.dat` the second time (new game); about 4 s native, longer in wasm. The native run with
   `WD_KEYS=2500:ESC,4000:RETURN` reached the level and its first dialogue. Acting on the file
   opens (as `game_nav.boot_into` does) is more robust than times. An unattended page needs this
   in the host or the page; the pack cannot avoid it (the menu is code).
5. **Writes** (autosave on every level entry) create `CRYO\DREAMS\data\game\game0.dat`,
   `game.dat`, `game0.ico` (8192-byte thumbnail from a 64x64 explicit readback of the frame: the
   direct renderer's `reason=thumbnail` readback must work, or fail without stopping the game).
6. CD device: see the table; `[cd] 0 tracks` and no audio. Sound effects and dialogue are DirectSound.

## WIP demo to retail mapping

The July 1997 demo (`E:\dev_game\DREAMS_ISOS\DREAMS_WIP_PCJ\DREAMS`) was used as the reference for
which data a demo holds, not run. Its bank (`DREAMS.DAT`, 100,179 bytes) has the retail record
layout (150 records of 0x2200) but its last offset is 0: read to end of file, `project.records`
rejects it. 134 projects are populated; 59 are reachable from Project0, but only 12 have every
asset they name in the demo's `DATA` (0, 30, 31, 39, 46, 48, 62, 76, 95, 99, 108, 122): the
Angkor island chain. The demo ships 11 scenes (`H18ANGKR`, `E10_PIEC`, `E11_ANGK`, `E12_ANGK`,
`E13_ANGK`, `E20_RIDE`, `F07_GROT`, `F34COUL1`, `F36ANGK`, `L07_TUN2`, `END`), 43 MB of `DATA`
(plus a 24 MB AVI, 5 MB of images, DirectX and installer files).

| WIP | Retail equivalent | Match |
|---|---|---|
| Projects 0, 30, 31, 39, 46, 48, 62, 76, 99, 108 | same index, same scene | same names; scenes byte-identical except `E11_ANGK.DSN` (2,277,536 vs 2,270,157 bytes) |
| Project122 `END.DSN` | none (retail Project95 `END.DSN` is missing on disc 1) | WIP `END.DSN` is a byte copy of `E20_RIDE.DSN`; the pack sends Project76 back to Project0 instead |
| Project0 links `[62]` | retail has `[134, 62]` | Project134 (`F08_GPIC`) is not in the demo: its link is cleared |
| Projects 70, 123 (`E11_ANGK`, `E12_ANGK` variants) | same index | not in the pack: WIP references `F11BLEU1.DAN` but ships it as `F11.DAN` (same 98,806 bytes), and their movies are `CINE_ED3/ED1.UBB`, which retail does not have; entering them without the movie stalls. Project62's only link to Project70 needs the item `FRESQUE0.DAN` from another level |
| the demo's 117 `DATA` files | same name | 56 byte-identical (among them `H18ANGKR.DSN`, `F84.DAN`, `F89.DAN`, `F23.DAN`, `OMBRE2.*`, `GR0x.3DC`); 41 only in the demo; 20 differ. Models renamed/changed: `F11.DAN` = retail `F11BLEU1.DAN`, `XH_.DAN` (433,711 vs 544,216), `ARC/EPEE/GUN.3DC`, `F07*.DAN`, `PARTICL2.3DC`, `MINE/LIFE.DAN` differ. Retail uses its own versions |
| WIP movies `CINE_ED1/2/3.UBB`, `ED_07.UBB`, `HNMGROTT.UBB`, `TETE_A1.UBB`, `GENERIC.UBB` | retail `ED0`, `ED1`/`ED2`, `ED_07`, `AR_G_COL`, none, `GENERIC` `.HNM` | by each project's `anim_video`: P30 `ED0`, P62 `ED1`, P39 `ED2`, P31 `AR_G_COL`, P46 `TUNNEL`, P76 `ED_07` (WIP's `TUNNEL.UBB` is not shipped either); WIP ships `.UBB`, retail `.HNM` |
| fonts `COURE.016`, `SSERIFF.0xx`, `FNT_16.SPR` | retail `HI640/480/320.SPR` | the fonts are a different format: retail files used |
| `DIALOG.DRD` 2.1 MB, `FSB.DAT` 1.97 MB, `ICONES.BF` 431 KB | retail 24.6 MB, 741 KB, 372 KB | different contents: retail files used, DRD cut to the entries the levels name |
| `REPLAY.BIN` 192,196 bytes | see below | |
| `AVI/`, `IMAGES/`, `DEMO0/1/2/`, `DIRECTX/`, `SETUP.*`, `EDITOR.DAT`, DOS exes, `CRYO.DLL`, `MSS32.DLL` | not needed | cut (the recomp does not read them) |

## `DEMO0`, `DEMOS1`, `DEMO_` replay

* Retail disc 1 `DEMO0\` is installer UI art (TGA button images, WAV button sounds,
  `SETUP.HLP`); `DEMOS1\3MILL\` is a promotion for another Cryo game (Third Millennium: TGA
  screenshots and a text). Neither is read by `GDIDREAM.EXE`. Not useful, not in the pack.
* `DEMO_RecordFrame`, `DEMO_PlayFrame`, `DEMO_StopPlayback`, `DEMO_SaveReplay`: demo mode
  `0x49d34a` (0 record, 1 play, 2 normal). The start-record and start-playback functions
  (`0x40db80`, `0x40dac2`) have no reachable caller in retail (spec 005); `data\replay.bin` holds
  records (frame count, then 0x70-byte records of input words, player state and position) that
  playback would feed in. Retail's `REPLAY.BIN` is 316 bytes: 3 frames in an older 104-byte layout.
* The July demo's `DATA\REPLAY.BIN` is 192,196 bytes = 4 + 1,848 x 104: a real recording
  (about a minute) in that 104-byte layout, probably the attract mode of that build. It cannot
  drive retail playback as is (112-byte records, an unreachable start, level state not recorded
  in the file). An attract-mode is therefore not a free win; it would need a converter for the
  record layout and a poke of `0x49d34a=1` plus a call of `0x40dac2`. Not attempted.

## What was verified

* `tests/recomp/test_web_demo.py`: DRD trim and chunking on synthetic data; manifest (sizes,
  sha256 of chunks and loose files, forward-slash paths, chunk limit, size budget, every asset of
  every project present, no link to a project outside the pack); native boot of the loose root
  (Project0 reached, dialogue plays, no crash, no unexpected missing file, every host path under
  the cut tree, nothing read from the discs); the Project0 to Project62 exit.
* `verify_cut.py --transitions` (profile `wip`, 2026-10-02): all 10 projects start and run with no
  crash and no unexpected failed open; exits followed to the next level: 0 to 62, 62 to 39,
  39 to 30, 39 to 62, 30 to 39, 108 to 46, 108 to 62, 46 to 108, 46 to 31, 31 to 46, 48 to 31.
  Not exercised: exits that need an item the level hands out (62 to 108, 31 to 76, 31 to 48,
  99 to 31), 31 to 99 (needs the level's enemies dead), 76 to 0 (a copy of the start project is
  slot 0 of the test variants, so the loop back cannot be seen by name), and the walk in the
  levels (these runs stand still). With walking keys the recording of Project31 (full retail data,
  not the cut, new game with that project in slot 0) crashed in `SCENE_CheckExits` (null read at
  guest VA 0x3c) and Project99's connection dropped the same way; standing still both run. Not
  explained, and not caused by the cut; the exits reached by arrival (46 to 31) run.
* Not done: a browser run of the pack (this stream has no browser build to run).
