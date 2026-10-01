# dreams

A static recompilation of **Dreams to Reality** (Cryo Interactive
Entertainment, 1997), built with
[pcrecomp](https://github.com/sp00nznet/pcrecomp).

The retail Windows executable (`GDIDREAM.EXE`) is lifted instruction by
instruction to C, compiled with clang and linked against a hand-written host
runtime: SDL3 for the window, input and sound, and a GPU renderer on sokol_gfx
in place of the original software rasterizer. The game's own code runs
unchanged; only the plumbing under it is new.

**No game data or game code lives in this repository.** The discs are
copyrighted. The lifted C, the builds and everything else derived from the
game are generated locally under `out/` and never committed. Supply your own
copy of the game.

## Status

- The recompiled game builds and plays on Windows with the original software
  rendering.
- The direct GPU renderer (`run.py --renderer direct`) runs the tested first
  scene, HUD and dialogue; full Windows acceptance is open
  ([spec 006](docs/specs/006-recomp-glide-renderer/spec.md)).
- The USER32, GDI32, WinMM and DirectSound layers run on SDL3. The KERNEL32
  layer still calls Win32, so macOS and Linux are later work.

## Quick start

```powershell
Copy-Item .dreams.example.env .dreams.local.env   # then set this machine's paths
uv sync
git clone https://github.com/sp00nznet/pcrecomp out/recomp/pcrecomp
uv run --with capstone --with pefile python recomp/windream/lift/lift.py
uv run --with capstone --with pefile python recomp/windream/lift/gen_imports.py
uv run python recomp/windream/build.py
uv run python recomp/windream/run.py
```

Requirements, options, controls and the verification scripts are in
[recomp/README.md](recomp/README.md).

## Layout

| Path | What |
|---|---|
| `recomp/` | **The project.** `windream/` lifts, builds and runs the game (`lift/`, `host/`, `verify/`, `debug/`); `render/` is the GPU renderer; `difftest/` tests the lifter on small Watcom programs |
| `re/` | Reverse engineering that feeds the recomp: Ghidra scripts, tools, the function-name registry, symbols, boundaries, prototypes, struct layouts, and the matching-decompilation scripts |
| `src/dreams/` | Small Python support package: local path settings and the format readers the verification scripts use |
| `tests/` | Python tests, by area: `recomp/`, `re/`, `toolkit/` |
| `docs/` | `specs/` for the recomp's plans and status; `research/` for what is known about the game and its binaries |
| `out/` | Everything generated (gitignored) |

[`AGENTS.md`](AGENTS.md) is the path and command reference for coding agents.

## History

Before the recomp became the project, this repository held a hand-written C++
port (OpenDreams, with ODRuntime and ODViewer) and a larger Python asset
toolkit. Both were removed; the last commit that has them is tagged
`opendreams-final`. Some research documents still mention them.

## Development

```bash
uv run pytest
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

- **Does**: document the game's formats and behaviour, and provide tools that
  operate on a copy you already own. In the EU, the Software Directive
  (2009/24/EC) Art. 5(3) permits studying a program you are licensed to use
  and Art. 6 permits decompilation for interoperability. France implements
  both.
- **Does not**: include, redistribute or reproduce any Cryo code, asset,
  binary or disc image. `.gitignore` enforces this.

Do not commit lifted or matched C, extracted audio, textures, models,
executables or disc images, and do not publish a playable build.

Not legal advice.

## License

[MIT](LICENSE) for everything in this repository. pcrecomp is MIT-licensed
(`recomp/LICENSE-pcrecomp`); third-party material (the mirrored MultimediaWiki
HNM6 description) is acknowledged in `LICENSE`.
