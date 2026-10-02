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

- The recompiled game builds and plays on Windows; the original software
  rendering is `run.py --renderer software`.
- The direct GPU renderer is the default on Windows (software elsewhere) and
  stops on a case it does not support; full Windows acceptance is open
  ([spec 006](docs/specs/006-recomp-glide-renderer/spec.md)).
- The game runs straight from the two original disc images (`.cue` + `.bin`):
  no installer, no extraction, no disc-swap prompt, CD music included. A
  launcher asks for the images once
  ([spec 007](docs/specs/007-port-launcher/spec.md)).
- The Win32 layers the game uses (KERNEL32, USER32, GDI32, WinMM, DirectSound)
  run on SDL3. Windows is the tested platform; the Linux build boots to the
  first level; macOS has not been built.

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

## Playing from your disc images

Start the built program with no arguments and the launcher opens:

```powershell
out\recomp\windream\build\windream_recomp.exe
```

1. **Discs.** Browse to, or drop, the `.cue` of each disc, in either order.
   The launcher checks that each is a Dreams to Reality disc, that you have
   one of each, and that disc 1 is the European English release the port is
   built from. A bare `.iso` works without music.
2. **Port settings.** Things the 1997 game does not have: the original
   software renderer or the new GPU one, window size and scaling filter, a
   frame cap, keyboard remapping (for example W, A, S, D for the arrows), and
   gamepad mode, direction source, buttons and deadzones. The game's own
   options stay in its in-game menu.
3. **Play.** The choices are saved in `dreams.ini` beside the program, and
   saves go to `userdata\` there (or to your per-user folder when the
   program's folder is not writable).

Nothing is copied from the discs: the game reads its files, its executable and
its music from the images, and switches between the two discs by itself where
the original asked you to swap them.

```powershell
windream_recomp.exe --play                               # skip the window, use dreams.ini
windream_recomp.exe --play --disc1 D1.cue --disc2 D2.cue --data saves
uv run python recomp/windream/run.py --discs             # development: no launcher
uv run --with pefile python recomp/windream/release.py   # DreamsToReality.exe and a zip
```

How the original installed itself, what it read from where and what made it
ask for the other disc is in
[docs/research/install-and-discs.md](docs/research/install-and-discs.md).

## Driving the game from a script or an AI assistant

Development builds contain a control channel: a local socket through which a
script can press keys, wait for something to happen (a file opened, a value in
the game's memory, a disc change), take a screenshot, pause and step frames,
and record the sound output. It is how the tests play through the menus, the
disc change and save loading without a person. It is off unless asked for, and
release builds do not contain it.

```powershell
uv run python recomp/windream/run.py --discs --headless --ctl --tag play
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play status
uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-play key ESC
```

The same channel is offered as an MCP server, `dreams-game`, registered in
[`.mcp.json`](.mcp.json). An assistant that speaks MCP (Claude Code asks once
to approve it when started in this folder) then has tools to start a game,
press keys, wait on conditions, read memory and look at a frame:

```powershell
uv run --with mcp --with pillow python recomp/windream/debug/wd_mcp.py   # what .mcp.json runs
```

Commands, the tool list and the helpers built on them
(`recomp/windream/debug/game_nav.py` for the menus, `bank_patch.py` to start
in any level) are in [recomp/README.md](recomp/README.md); `AGENTS.md` maps
each testing need to its tool.

## Layout

| Path | What |
|---|---|
| `recomp/` | **The project.** `windream/` lifts, builds and runs the game (`lift/`, `host/`, `verify/`, `debug/`, and `devtools/`, the development control channel); `render/` is the GPU renderer; `disc/` reads disc images; `launcher/` is the launcher window; `difftest/` tests the lifter on small Watcom programs |
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
uv run --with mcp --with pillow pytest   # also the MCP server's tests
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
