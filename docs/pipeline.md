# Pipeline — extract, bake, pack

How game data goes from the discs to the web app and to a hosted release.
Three stages, each with one rule. The web app reads only the output of the
second stage, the **data root**, and a release is a copy of part of it.

```
discs ──extract──> extract/ ──bake──> baked/ ──pack──> releases/<name>/site/
         lossless,             what the app       a subset plus the
         faithful to disc      reads, as served   app build, deployable
```

| Stage | Reads → writes | Rule | Does | Never does |
|---|---|---|---|---|
| `dreams extract` | discs → `extract/` | faithful to the disc | decodes formats to open, lossless ones: FLAC, FFV1, PNG, glTF, JSON, raw records | make a choice for the app: web codecs, unit conversion, folder layout |
| `dreams bake` | `extract/` → `baked/` | exactly what the app reads, laid out as it is served | web codecs, the JSON the app reads, folder layout, `index.json` | read the discs, or depend on files placed by hand |
| `dreams pack` | `baked/` + app build → `releases/<name>/` | a deployable site | select files, rebuild the index for the subset, write `_headers`, check host limits | change a content file |

Two tests for where a piece of work belongs:

- If a change to the web app would force a change to it, it is **bake**, not extract.
- If it could make dev and a release behave differently, it is **not pack**.

## Commands

```powershell
uv run dreams extract                          # discs -> extract/   (slow; rarely)
uv run dreams bake                             # extract/ -> baked/  (the dev data root)
npm --prefix web run dev                       # the app, with baked/ served at /data
uv run dreams pack demo --projects 0,62,134    # -> releases/demo/site/
npx wrangler pages deploy releases/demo/site --project-name <name>
```

`extract` and `bake` take `--only`, `--skip` and `--force`, and `--list` to
show their groups. `pack` without `--projects` ships the whole data root.

## Where things live

All three outputs are outside the repository, under `DREAMS_WORK_ROOT`:

| Setting | Default | Contents |
|---|---|---|
| `DREAMS_EXTRACT` | `$DREAMS_WORK_ROOT/extract` | the lossless archive, ~4.6 GB; a cache, kept between runs |
| `DREAMS_BAKED` | `$DREAMS_WORK_ROOT/baked` | the data root the app reads |
| `DREAMS_RELEASES` | `$DREAMS_WORK_ROOT/releases` | one folder per packed release |

Game data never enters the app build: `vite.config.ts` sets `publicDir: false`,
and `web/dist` holds only `index.html` and `assets/`. The repository's `out/`
stays research scratch; no pipeline stage writes there.

## The data root

```
baked/
  index.json                 the only listing: static hosts cannot list folders
  manifest.json              what bake did, per item (not shipped)
  projects/<n>.json  <n>.bin one per DREAMS.DAT project: decoded fields + the raw record
  scenes/<stem>/             scene.gltf  scene.bin  tex/<object>.png
  models/<id>/               model.gltf  model.bin  tex*.png
                             skin.json  clips.json  clips/<clip>.json   (animated models)
  audio/music/ sfx/ voice/   Opus
  video/cutscenes/ movies/ textures/   H.264 MP4
  ui/menu/                   boot-menu corner markers and titles, by name
  ui/icons/<bank>/           every ICONES.BF bank, plus the SOUR cursor
  ui/fonts/<hi320..hi640>/   glyph PNGs (no metrics yet)
  text/                      dreams-ini.json  dialogue.json  manifests.json
```

### Rules

1. **No versioning, no compatibility code.** Bake and the app change in the
   same commit, and old baked data is re-baked, never migrated. A missing
   file fails loudly with its path. The animation JSON keeps the
   `schemaVersion` and `rigId` it already had; `rigId` catches a real decode
   mismatch between a clip and its model, not format drift.
2. **Names come from the game.** Folders use the disc file stem in lowercase;
   projects use their `DREAMS.DAT` index. The discs never change, so these
   identifiers never need a version. A model's id is its stem without trailing
   underscores, as `extract` already names it: `XH_.DAN` is `models/xh/`. No
   two model files collide under that rule.
3. **Raw engine units.** Project JSON holds the record's own integers: scene
   units with negative Y up, and 12-bit headings where 4096 is a full turn.
   Only the renderer converts, in `web/src/units.ts`, by the rule
   `gltf.write` uses: `(x, -y, z) × 0.01`. glTF files stay in render space.
4. **A field name shows how sure we are.** A field gets a name only where the
   docs mark its meaning **[verified]**; otherwise it is named by its offset
   (`x6c`). When a finding settles one, rename it in bake and the app.
5. **Raw bytes travel with partial decodes.** `projects/<n>.bin` is the exact
   0x2200-byte record, so a new reading of an offset can be tried in the app
   without re-extracting.

### `index.json`

Rebuilt from disk at the end of every bake. `pack` rebuilds it for the subset
with the same function, so it always lists exactly what the folder holds.

| Key | Contents |
|---|---|
| `projects` | `[{index, id, name, scene}]`; `name` is `DREAMS.INI`'s label |
| `scenes` | `{stem: {textured}}` — `textured` is false for the 37 scenes still decoded from tag 2 |
| `models` | `{id: {textured, animation?}}`; `animation` is the rig's catalog entry |
| `audio`, `video` | file lists per group |
| `boot` | `intro`, `warp`, `elder` videos, `menuMusic` (track 13), `startProject` (0) — named by `WINDREAM.EXE`, [boot-sequence.md](boot-sequence.md) |
| `resident` | the always-loaded set from `LISTL0.TXT`: `h03paque` and `ch0`, `holo`, `xh`, `mhe` |

### `projects/<n>.json`

```json
{
  "index": 0, "id": "Project0", "name": "Ile d'Angkor", "scene": "h18angkr", "disc": 1,
  "spawn": [-319, -625, -3187], "heading": 3046,
  "ambient": [152, 168, 126], "light1": [0, 0, 0], "light2": [0, 0, 0],
  "fog": [0, 0, 0, 0], "sky": [0, 0, 0, 0], "fov": 64,
  "cdTrack": 2, "music": "audio/music/d1_track02.opus", "aiSchedule": 0, "x138": 16,
  "videos": [], "headerStrings": {"x3d": "ETE_E~1.HNM"},
  "links":   [{"slot": 0, "name": "LINK0", "destination": "Project134", "to": 134,
               "min": [-1227, -1796, 237], "max": [-1152, -1640, 339]}],
  "objects": [{"slot": 2, "name": "OBJET2", "asset": "F07BLEU.DAN", "model": "f07bleu",
               "pos": [-695, -875, -210], "heading": 752, "flags": 19,
               "x3c": 0, "x64": 3, "x68": 113, "x6c": 113, "x70": 70, "x78": 0, "x88": 0}],
  "boxes":   [{"slot": 0, "name": "BOX0", "xf0": 1, "points": [[-913, -5919, 1054]]}],
  "advents": [{"slot": 0, "name": "LINKADVENT0", "x14": 2, "x1c": 1, "x20": 64, "x24": 32,
               "video": "", "videoPath": null}],
  "needs":   ["projects/0.json", "projects/0.bin", "scenes/h18angkr/", "models/f84/", "…"],
  "missing": []
}
```

- **Named** (verified): header spawn `+0xB4`, heading `+0x10C`, ambient
  `+0x30`, lights `+0x18`/`+0x24`, fog `+0xE0`, sky `+0xF0`, FOV `+0xA4`, CD
  track `+0x1F8`, AI schedule `+0xD0`; object asset, position `+0x40`,
  heading `+0x5C`, flags `+0x34`; link destination and volume.
- **By offset** (open): header `+0x138` (documented as day/night 0/1; P0
  holds 16); object `+0x3C`, `+0x64`, `+0x68`, `+0x6C`, `+0x70`, `+0x78`,
  `+0x88`; box `+0xF0`; advent `+0x14`, `+0x1C`, `+0x20`, `+0x24`.
- **`videos`** are the names the engine reads at `+0x3C` and `+0x5C` as C
  strings, with the baked file. **`headerStrings`** is every printable run in
  `+0x3C..+0x9C`, keyed by its offset — see the findings below.
- **`slot`** is the record's physical slot; unused slots are skipped.
- **`needs`** lists data-root paths (folders end in `/`) the project uses.
  It is the only thing `pack` reads to choose files.
- **`missing`** lists names the record references that were not extracted
  or are on neither disc.
- **`disc`** is the disc holding the scene file; `music` takes the CD track
  from that disc, since MCI plays whichever disc is in the drive.
  **[unverified]** — the game's own disc bookkeeping is not traced.

### Scenes and models

`extract` writes flat names (`h18angkr.gltf`, `h18angkr_h18_sol.png`). Bake
gives each its own folder and rewrites the glTF URIs to match: `scene.bin`,
`tex/h18_sol.png`; `model.bin`, `tex0.png`. Nothing else in the glTF changes.

Animated models also get `skin.json` (the rig and vertex bindings),
`clips/<clip>.json` (one per `.3DA`, e.g. `xh_an000`), and `clips.json`: the
catalog entry plus the clip list. Bindings index the glTF's corners, so the
`models` and `animations` extract groups must come from the same decoder run
— re-extract them together.

### UI and text

- `ui/menu/` names the boot menu's sprites the way the app asks for them:
  `bracket_uplf.png` … from `INTERF` slots 0–7, and
  `title_<new_game|load_game|options|quit>_<normal|active|pressed>.png` from
  `TITRES.SPR`'s twelve images (four titles × three states, read off the
  contact sheet). Disc 2's `ICONES.BF` is used, as only it has `TITRES`.
- `text/dialogue.json` carries each entry's raw line timings (the game
  scales by 15/100 when it shows them) and its voice file. Entries and voice
  clips pair 1:1 in file order when their counts agree, which they do: 178.

## Bake groups

| Group | From `extract/` | To `baked/` | Re-run cost |
|---|---|---|---|
| `projects` | `projects/*.bin`, `text/dreams.ini`, `metadata/scenes.json` | `projects/` | always rewritten, < 1 s |
| `scenes` | `scenes/` | `scenes/<stem>/` | copies only what changed |
| `models` | `models/` | `models/<id>/` | copies only what changed |
| `clips` | `animations/` | `models/<id>/clips*`, `skin.json` | copies only what changed |
| `ui` | `images/icons/`, `images/sprites/hi*` | `ui/` | copies only what changed |
| `text` | `text/` | `text/*.json` | always rewritten |
| `music`, `sfx`, `voice` | `audio/*/*.flac` | `audio/*/*.opus` | ffmpeg; skips existing files |
| `cutscenes`, `movies`, `textures` | `video/*/*.mkv` | `video/*/*.mp4` | ffmpeg; skips existing files |

A file is rewritten when it is missing or older than its source; `--force`
rewrites everything. The data groups together take about 10 seconds.
`manifest.json` merges across runs, so `--only projects` keeps the record of
the last video bake. `index.json` is rebuilt every time.

`extract` gained two groups for this: `projects` (the decompressed records)
and `animations` (clips, rigs, skins — the old `export-animations` output,
which remains as a standalone debugging command). `dialogue` also writes
`text/dialogue.json`.

## The app side

- **`web/src/content.ts`** builds every data URL from one relative root,
  `./data/`, and holds the TypeScript shapes of `index.json` and project
  JSON. No other module writes a path.
- **`web/src/units.ts`** is the one engine-to-render conversion.
- **The dev server** mounts `baked/` at `/data` as plain files, with Range
  support for video and a 404 that names the missing path. There are no
  routes or queries: what works in dev works from any static host.
- **Projects, not scenes.** The viewer loads a project by index: its scene,
  then the objects, links and boxes its record places. The 150 projects use
  95 scenes, so the old lookup by scene name returned another project for 55
  of them — P108 *Ile d'Angkor 2* got P20 *Cauchemard Angkor*'s objects and
  links. The Scenes browser still shows geometry alone.
- **`?project=62`** skips the boot flow and starts in that project.

## Pack and deploy

`dreams pack <name> --projects 0,62,134` ships:

- each listed project's `needs`;
- the boot videos and menu music from `index.json`, and the resident scene
  and models;
- `ui/`, `audio/sfx/` and `text/`, which every screen uses.

Voice clips, other levels' media and `manifest.json` are left out. The copy
is byte-for-byte; then `index.json` is rebuilt for the subset. New Game starts
at P0, or at the first packed project if P0 is not in the subset. A `LINK` to
a project outside the subset shows *not in this build*.

```
releases/<name>/
  manifest.json      git commit and dirty flag, projects, files, bytes, largest file
  site/              upload as-is
    index.html  assets/  data/  _headers
```

`_headers` caches `assets/*` for a year (Vite hashes their names) and
`data/*` for ten minutes, so a redeploy shows within that window. Pack fails
if a file exceeds 25 MiB or the site exceeds 20,000 files, the free-plan
limits of Cloudflare Pages and Workers static assets **[sourced]**,
[Pages limits](https://developers.cloudflare.com/pages/platform/limits/).
Preview a release with any static server:

```powershell
python -m http.server 8765 -d <DREAMS_RELEASES>\demo\site   # then http://127.0.0.1:8765/
```

**Measured** — `pack demo --projects 0,62,134`: 1,280 files, 52 MB. App 12
MB (Babylon 5.8 MB; the Inspector's 6 MB loads on demand), video 21 MB (the
intro is 15.1 MB, the largest file), models 12 MB (animation JSON is most of
it), scenes 6 MB, audio 3.9 MB, UI 1.5 MB (767 font glyphs).

## After an RE finding

Say a finding settles `OBJET +0x6C`:

1. Name it in [`project.py`](../src/dreams/formats/project.py) and in bake's
   project JSON.
2. `uv run dreams bake --only projects` — about a second.
3. Rename it in `content.ts` and wherever the app reads it; reload.

No migration and nothing else to regenerate. A decoder change goes one step
further back: `dreams extract --only <group> --force`, then bake that group.

## What bake surfaced

Writing every reference out as a path and checking it found these.
**[verified]** as observations; interpretations are marked.

- **Header names sit in 16-byte slots.** Printable runs in `+0x3C..+0x9C`
  start at `+0x3C`, `+0x4C`, `+0x5C`, `+0x6C`, `+0x7C` and `+0x8C` across
  the 150 records — not at the `char[32]` boundaries in
  [file-formats.md](file-formats.md) (video `+0x3C`, material `+0x6C`, …).
  Which material slot goes with which video is therefore open; the parser's
  `anim_material` / `anim_material2` (`+0x6C` / `+0x8C`) assume the
  `char[32]` layout, so bake does not use them.
- **36 names begin one byte late, behind a zero** — in every slot, e.g.
  P0's `+0x3C` holds `\0ETE_E~1.HNM`. The compressed stream has no dropped
  byte (`7e 00 04 45 54 45 …`: a literal, four zeros, `ETE…`), so this is
  the data. The engine reads the slot as an empty string. **[unverified]**
  reading: an editor disabled the entry by zeroing its first byte —
  `TETE_E~1.HNM` minus its `T`. That would explain the "ETE mystery" in
  [boot-sequence.md](boot-sequence.md) without a failed file open.
- **Nine projects set CD track 1**, the data track; there is nothing to play.
- **Seven `.UBB` cutscenes named by records are on neither disc** (searched
  by filename across both): `F06FEU.UBB` (3 projects), `F07EAU.UBB` (3),
  `OEIL_HI.UBB`, `CINE_ED1.UBB`, `CINE_ED2.UBB`, `CINE_ED3.UBB`, `ARAI_06.UBB`.

## Not done yet

- Font metrics: `ui/fonts/` holds glyph images only; the advance table
  (`width - descriptor[+0x0c]`) is not exported.
- Dialogue and voice are baked but not packed or played.
- Collision data (tag 2) is not in the data root yet; the viewer still
  raycasts against render meshes.
