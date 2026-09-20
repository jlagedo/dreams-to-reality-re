# dreams

Reverse-engineering toolkit and research notes for **Dreams to Reality**
(Cryo Interactive Entertainment, 1997 — DOS / Windows).

The goal is to understand and decode the game's proprietary formats. The engine
target is undecided; right now this is exploration and decoding.

**No game data lives in this repo.** The discs are copyrighted; `.gitignore`
blocks every asset extension, extraction directory and derived media type. Keep
the images on local disk and point the toolkit at them.

## Setup

```bash
uv sync
uv run dreams --help
```

Tell it where your extracted discs are — environment variables:

```bash
export DREAMS_DISC1=/path/to/Disc-1/extracted
export DREAMS_DISC2=/path/to/Disc-2/extracted
```

…or a gitignored `dreams.local.toml` in the repo root:

```toml
[paths]
disc1 = "E:/dev_game/Dreams-to-Reality_Win_EN_Disc-Image-Disk-1/extracted"
disc2 = "E:/dev_game/Dreams-to-Reality_Win_EN_Disc-Image-Disk-2/extracted"
out   = "./out"
```

Check it resolved: `uv run dreams config`

## Getting the files off a disc image

The dumps are MODE1/2352 — 16-byte sector header, 2048 bytes of payload,
288 bytes of ECC. Strip it, then unpack the ISO9660 filesystem:

```bash
uv run dreams disc cue "Dreams to Reality (Europe) (Disc 1).cue"
uv run dreams disc iso "...(Track 01).bin" disc1.iso
7z x disc1.iso -oextracted
```

Tracks 2+ are redbook audio and hold the game's music. They never appear as
files — **mount the `.cue`, never the `.iso`**, or the game runs silent and
throws MCI errors.

## Extracting everything

One command decodes every asset whose format is solved, losslessly:

```bash
uv run dreams extract              # -> E:\dreams-work\extract
uv run dreams extract --list       # show the groups
uv run dreams extract --only music,sprites --force
```

| Output | Format | Contents |
|---|---|---|
| `audio/music/` | FLAC | 21 redbook CD tracks, de-duplicated across discs |
| `audio/sfx/` | FLAC | 24 effects from `FSB.DAT` |
| `audio/voice/` | FLAC | 178 clips from `DIALOG.DRD` |
| `video/` | FFV1 in MKV | 113 HNM4/5/6 files, mathematically lossless |
| `images/level-textures/` | PNG | **level textures** — 64 tiles of 32x32 per object, 95 scenes |
| `images/` | PNG | sprites, icon-bundle members, TGA gallery |
| `metadata/` | JSON | decoded headers for the formats whose bodies stay packed |
| `text/` | UTF-8 | `DREAMS.INI`, manifests, transcoded from CP1252 |

It writes a `manifest.json` recording every output with its provenance, and a
`README.md` listing the caveats. Needs `ffmpeg` on PATH, plus `na_game_tool` for
video (see [docs/hnm-video.md](docs/hnm-video.md)) — groups whose tool is missing
are skipped, not failed.

Output goes **outside the repo** by design: it is derived game content.

## What works

| Command | Status |
|---|---|
| `dreams extract` | **solved** — every decodable asset, lossless |
| `dreams audio info/unpack` | **solved** — extracts all 202 clips as WAV |
| `dreams video` | **solved** — HNM4/5/6 headers |
| `dreams res` | **solved** — 30 items, 150 levels from `DREAMS.INI` |
| `dreams disc iso/cue` | **solved** |
| `dreams pe` | **solved** — sections, imports, exports, toolchain |
| `dreams bundle` | **solved** — `UBIK` table and members |
| `dreams scene` / `anim` | partial — headers exact; bodies are packed |
| `dreams model` | partial — `F3DC` header, materials, `PAK0` chunk bounds |

Exploration helpers: `census`, `identify`, `stats`, `regions`, `tags`,
`strings`, `dump`, `render`, `stride`.

## Examples

```bash
# Every clip in the voice bank, as .wav
uv run dreams audio unpack "$DREAMS_DISC1/DATA/3DC/DIALOG.DRD"

# What is this file?
uv run dreams identify "$DREAMS_DISC1/DATA/3DC" --pattern "*.DSN"

# Raw or packed? (raw controls here sit at 18-31%, compressed video at 69%)
uv run dreams stats "$DREAMS_DISC1/DATA/3DC/E01GROTT.DSN"

# Room construction: walls by compass direction, floor, ceiling
uv run dreams scene "$DREAMS_DISC1/DATA/3DC/E01GROTT.DSN"

# CryoLib's 165 exports, including the HNM6 decoder
uv run dreams pe "$DREAMS_DISC2/DEMOS2/CRYO.DLL" --exports --grep HNM

# Test a pixel-layout hypothesis
uv run dreams stride "$DREAMS_DISC1/DATA/ICONE/ICONES.BF"
uv run dreams render "$DREAMS_DISC1/DATA/ICONE/ICONES.BF" --offset 16 --width 64 --fmt rgb555
```

## What we know

Full write-ups in [`docs/`](docs/README.md); [`AGENTS.md`](AGENTS.md) is the
orientation summary. Headlines:

- The engine is **Cryo's own C/C++ code, built with Watcom** for all three
  targets (DOS, DOS+3dfx, Windows). Software rasterizer, 16-bit RGB555. Glide is
  the only hardware path and exists only in DOS. Windows uses **DirectDraw only**
  — no Direct3D anywhere.
- `CRYO.DLL` on disc 2 is **CryoLib**, a *debug build* with 165 named exports
  including `_GL_HNM6_Decompression_Warp@8`. A working HNM6 decoder shipped on
  the retail disc. It is a separate codebase from the game.
- **All audio is plain PCM WAV** inside two custom banks: `FSB.DAT` (24 effects,
  16-bit) and `DIALOG.DRD` (178 voice clips, 8-bit). Music is CD audio.
- **The level textures are decoded.** They live inside the 98 `.DSN` scene
  files as fixed-size uncompressed records: a 256-entry **RGB565** palette plus
  64 distinct 32x32 8-bit tiles per object. That is ~97% of the 157 MB. Only
  record tags 1 and 2 (~40 KB per scene) are still packed.

Claims in the docs are tagged **[verified]** (measured here), **[sourced]**
(external, linked) or **[unverified]** (inference). Please keep that up.

## Development

```bash
uv run pytest          # disc-dependent tests skip if paths aren't configured
uv run ruff check .
uv run ruff format .
```

## Legal

**This game is not abandonware in any legal sense.** Cryo Interactive went
bankrupt in 2002; DreamCatcher Interactive absorbed most of its assets, and
**Microïds acquired the intellectual property rights to the entire former Cryo
catalogue in October 2008**. Microïds is an active publisher today. Bankruptcy
transfers copyright, it does not extinguish it, and a 1997 French work stays
protected for decades yet.

"Abandonware" describes enforcement behaviour, not ownership, and has no
standing in law.

What this repo therefore does and does not do:

- **Does**: document file formats, and provide tools that operate on a copy you
  already own. In the EU, the Software Directive (2009/24/EC) Art. 5(3) permits
  studying a program you are licensed to use and Art. 6 permits decompilation
  for interoperability. France implements both.
- **Does not**: include, redistribute or reproduce any Cryo code, asset, binary
  or disc image. `.gitignore` enforces this — see the asset block at the top.

Do not commit extracted audio, textures, models, executables or disc images,
and do not publish a playable build. Supply your own copy of the game.

Not legal advice.

## License

[MIT](LICENSE) for everything in this repository — the notes, the Python
toolkit and the Ghidra scripts. Third-party material (the mirrored MultimediaWiki
HNM6 description, the GhidraMCP patch) is acknowledged in `LICENSE`.
