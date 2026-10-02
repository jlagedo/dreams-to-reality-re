# WASM stream notes: the browser build of the host (`recomp/windream`)

Owner: WASM stream. Build, test and design of `out/recomp/windream/build-web/dreams.{js,wasm}`.

## Build

```sh
uv run python recomp/windream/build.py --web                 # development: -O1, ~30 s
uv run python recomp/windream/build.py --web --web-opt Release   # -O3 (slow compile, faster game)
uv run python recomp/windream/build.py --web --web-gen DIR --web-exe NAME.EXE --web-name build-web-x
```

`build.py --web` reads `out/recomp/web-tools/tools.json` (the same file `recomp/web-env.ps1`
uses), builds an Emscripten environment from it itself (works from Git Bash, PowerShell or
CI; no need to dot-source `web-env.ps1`), configures `recomp/windream/CMakeLists.txt` with the
Emscripten toolchain, and links `dreams.js`/`dreams.wasm` (no separate worker file: workers
re-run `dreams.js`). Code: `recomp/windream/web_build.py`, the `EMSCRIPTEN` branches of
`CMakeLists.txt`, `host/web/` (`web_glue.c`, `pre.js`), `host/vm/vm_os_wasm.c`.
`-O0` does not load (wasm local count limit on the biggest lifted functions), hence `-O1`.
The lifted sources, and the guest EXE name, are parameters (`--web-gen`, `--web-exe`).

## What the page must know

- The guest EXE is read **at runtime from the FS** (`/dreams/GDIDREAM.EXE`, `WD_WEB_EXE_NAME`):
  only the PE headers and the initialised sections are copied into the guest address space
  (`load_image` in `host/core/runtime.c`); the code is the lifted C in the wasm. It could be
  embedded into the wasm (`--embed-file`), which would remove it from the pack: say so if wanted.
- Layout: `/dreams` is both the CD root and the install root (`CRYO\DREAMS\x` and `x` are the
  same file, `host/sdl/files.c` `guest_rel`). Everything is read from `/dreams` and written to
  `/dreams` (write root = install root, so `DATA/GAME` is the saves directory). Paths are
  matched without regard to case (`DATA\FONT\HI640.SPR` finds `DATA/FONT/hi640.spr`), so the pack
  may use any case. A path can be a symlink-free plain tree.
- Needed for the first screens (file log of a boot): `GDIDREAM.EXE`, `DREAMS.DAT`,
  `DATA/FONT/{HI640,HI480,HI320}.SPR`, `DATA/OBJET/*`, `DATA/ICONE/ICONES.BF`, `DATA/SOUND/FSB.DAT`,
  `DATA/3DC/DIALOG.DRD`; optional: `DATA/GAME/*` (saves), `DATA/HNM/INTRO.HNM`, `GENERIC.HNM`
  (see "Unattended boot"). The demo pack (68 files) boots with exactly what it has.
  `DATA/1CD.ID`, `DATA/2CD.ID` and `DATA/HD.ID` (markers the game only opens) are **created at
  startup when missing**; `DATA/FULL.ID` is **never seen** even if present (it would put the
  game in its copy-to-hard-disk mode, which purges and refills `DATA/3DC`).
- Missing optional files (CD audio tracks, movies, other disc) fail as in Windows: the open
  returns an error and the game goes on (CD audio: `[cd] 0 tracks`, silent). A missing
  required file is the game's own error box, reported as `onDreamsStatus("fatal", text)`.
- Status: `Module.onDreamsStatus(kind, text)` is called from the guest thread through the main
  thread: `boot` (start), `running` (entering the game), `fatal` (an error box, a direct-renderer
  fatal, no program), `exit` (the program returned). `-lidbfs.js` is linked; `FS`, `ENV`,
  `GL`, `addRunDependency`, `removeRunDependency` are exported.
- Environment: the host reads `WD_INSTALL_ROOT` (default `/dreams`), `WD_RENDERER` etc. Do not
  rely on `Module.ENV` reaching the guest thread; instead pass options as **arguments**:
  `createDreams({arguments: ["WD_RENDERER=software", "WD_FPS=25"]})` (any `WD_*=value`
  argument becomes an environment variable before anything else).
- Threads: the guest runs on a pthread (`-sPROXY_TO_PTHREAD`), so the page's main thread is
  free; SharedArrayBuffer needs COOP/COEP (see CONTRACT.md). 8 workers are pre-spawned.
- Memory: one malloc'd 400 MB guest arena (wasm memory grows to it, ~2 GB maximum); only
  touched pages are real memory. The game uses about 32 MB at the menu.

## How frames reach the page (for GL and PAGE)

A WebGL context on an OffscreenCanvas in a worker presents only when the worker's task ends,
and the guest never returns to the event loop, so a canvas transferred to the guest thread
stays black. Therefore:

- `host/web/pre.js` creates an **OffscreenCanvas of its own** and registers it under the page
  canvas's id (`GL.offscreenCanvases`), so the runtime gives *that* one to the guest thread; SDL's
  `#canvas` (SDL_GL_CreateContext, SDL_Renderer, sokol) finds it there. The page's own canvas
  is **never transferred**: the page must not call `getContext` on it.
- Every `SDL_GL_SwapWindow` is wrapped at link time (`-Wl,--wrap=SDL_GL_SwapWindow`,
  `web_glue.c`): after the real swap the frame is taken with `transferToImageBitmap()` and posted
  to the main thread with the pthread `callHandler` message (`cmd: 9`, Emscripten 6.0.10
  `libpthread.js`), where `Module.dreamsFrame` (pre.js) draws it with a `bitmaprenderer`
  context and sets `canvas.width/height` to the frame size. Works for the software path
  (SDL_Renderer) and the direct renderer alike; **GL stream: no change needed in
  `graphics_gl.cpp`, do not rely on a swap for presenting, do not use explicit swap control.**
- The canvas pixel size is whatever the game's window is (640x480); the page scales it with
  CSS only.

## Unattended boot (pack without the intro movie)

`host/web/web_glue.c` (`wd_web_boot_note`, `wd_web_boot_key`) answers the demo pack's missing
`INTRO.HNM`/`GENERIC.HNM` without the page: when the guest's open of `data\hnm\intro.hnm` fails, the host
reports ESC (150 ms pulses through `GetAsyncKeyState`) until `generic.hnm` is opened, then RETURN
until `dreams.dat` is opened again (New game). Driven by file opens, not time. If the intro opens,
nothing is pressed (the player skips it). `WD_WEB_AUTOSKIP=0` turns it off. The page needs to
send no key.

## Things found on the way (for whoever touches the host)

- Page events (keys, mouse, focus) reach the guest thread through its proxy queue, which only runs
  when the thread sleeps or yields. A guest loop that polls keys without sleeping (the black wait for
  ESC) never saw them; `host_pump` now calls `emscripten_current_thread_process_queued_calls()`.
- `-O0` does not load ("local count too large" on the biggest lifted functions): `-O1` is the
  development setting, `--web-opt Release` (-O3, ~2 min compile, 7.5 MB wasm) the smaller one. Speed is
  the same: the game paces itself (about 21 fps in the level, as natively).
- `MessageBoxA` shows no dialog in the browser; an error box goes to `onDreamsStatus("fatal", text)`.
- The unset `WD_RENDERER` means `direct` (WebGL2) in the browser as on Windows.
- The Emscripten `CMD_CALL_HANDLER` constant (9) is internal to `libpthread.js`: re-check it when the
  SDK is updated (`tests/recomp/test_web_host.py` checks the two halves of the relay agree, not the number).

## Verified (headless Chrome 154, Windows; Playwright)

- `recomp/web/browser_check.py --real --run-seconds 40`: 21/21 checks, including the game starting
  after the click, pack integrity, IDBFS saves, reload from cache, damaged chunk, missing COOP/COEP,
  pack on another origin.
- Own harness (`out/recomp/windream/web-test/`, not part of the product: `serve.py`, `index.html`,
  `drive.py`; serves the install root + disc 1 files, or the unpacked demo): with the retail data the
  intro movie, the "Please LOGIN" screen and the menus run with `WD_RENDERER=software` and with
  `direct`; with the demo pack the host's boot assist reaches Project0 with no key sent, the 3D
  scene renders through the direct renderer (WebGL2), the level autosave writes `game0.dat`,
  `game.dat` and `game0.ico` (the 64x64 thumbnail readback works), and the SDL audio callback
  receives mixed non-zero samples (about 640 of 1000 callbacks in the first 40 s).

## Gaps / open

- Chrome only. Firefox and Safari not run (OffscreenCanvas WebGL2 in workers, `transferToImageBitmap`
  and SharedArrayBuffer are required; JSPI is not used).
- Level exits, the movie of a level entered by an exit, the in-game menu and long play were not
  driven in the browser (the intro HNM movie path and the level run were).
- The page's audio start is the page's job (AudioContext resume on the click); the mixer runs
  regardless. Audio output was measured at the SDL callback, not heard.
- `-pthread` + memory growth: `em++` suggests `-sGROWABLE_ARRAYBUFFERS=2`; not tried.
- Memory: 400 MB arena (virtual until touched) + 256 MB initial wasm memory; mobile browsers may refuse.
  `WD_HEAP_SIZE` (imports.h) and `WD_WEB_INITIAL_MEMORY`/`WD_WEB_MAX_MEMORY` (CMake cache) are the knobs.
- `build.py --web` needs the tools of `recomp/web-env.ps1`; no Linux/macOS path for the web build was run
  (the code for it is in `web_build.py`).
