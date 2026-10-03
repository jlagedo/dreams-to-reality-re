# PAGE stream notes: what the web shell expects from the engine build

Owner: PAGE stream (`recomp/web/index.html`, `loader.js`, `package.py`, `serve.py`,
`browser_check.py`). The shell was tested against a tiny real Emscripten module
(`recomp/web/mock/mock_dreams.c`) linked with the flags below, so these are
known to work together.

## Link flags the page relies on

```
-sMODULARIZE -sEXPORT_NAME=createDreams -sENVIRONMENT=web,worker -pthread
-sEXPORTED_RUNTIME_METHODS=FS,ENV,addRunDependency,removeRunDependency
-lidbfs.js -sFORCE_FILESYSTEM
```

| Needed | Why | Without it |
|---|---|---|
| `FS` exported | the page writes the pack into `/dreams` in `preRun` | the page reports "does not export the Emscripten FS" and does not start |
| `ENV` exported | `Module.ENV.WD_INSTALL_ROOT = '/dreams'` is set in `preRun` | warning in the log; the host must then default to `/dreams` itself |
| `-lidbfs.js` + `addRunDependency`/`removeRunDependency` | saves: IDBFS mounted at `/dreams/DATA/GAME`, loaded before `main` | warning; saves are not persisted, everything else works |
| `Module.onDreamsStatus(kind, text)` called with `boot`, `running`, `fatal` | hides the overlay on `running`, shows `fatal` text in the error panel | the overlay hides 4 s after instantiation if no status was ever received |

## Other facts the host side should know

- The page calls `createDreams({canvas: #canvas, arguments: [], locateFile, print, printErr, preRun, onAbort, onDreamsStatus})`
  after the user's click. `locateFile` maps every name to the folder `dreams.js` was loaded from
  (`dreams.wasm`, a worker file if the build has one). Workers load same origin, so engine files
  must not be moved to another origin.
- The pack is written into MEMFS (`FS.writeFile(..., {canOwn: true})`), so the whole pack lives in
  the JS heap: a minimal demo pack, not the full `DREAMS.DAT`.
- `#canvas` fills the 4:3 stage. The page asks for its pixel size through
  `Module._wd_web_view_size(w, h)` (the canvas in device pixels, or 640x480 for pixel-perfect);
  the engine applies it with `SDL_SetWindowSize` at its next message pump
  (`recomp/windream/host/web/web_glue.c`) and `pre.js` sizes `canvas.width/height` from each frame.
  An engine without the export keeps 640x480 and the browser stretches it.
- Audio: the page wraps `window.AudioContext` before the engine loads and routes everything the
  engine connects to the destination through one gain node (mute), and resumes contexts on the
  user's click. The engine's `AudioContext` must be created on the main thread (SDL3 does).
  An `AudioWorklet` that connects to the destination itself, from inside the worklet scope, is not
  covered by the mute button.
- Keys: the page calls `preventDefault` (capture phase on `window`, no `stopPropagation`) on every key
  while running except F5, F11, F12 and Ctrl/Meta+letter. Esc is the game's menu key: in fullscreen
  the page asks for Keyboard Lock on Escape (Chromium only).
- `pagehide`, `visibilitychange` (hidden) and a 5 s timer call `FS.syncfs(false)` for the save
  directory. The page assumes saves only change on the main thread's FS; with `PROXY_TO_PTHREAD` and
  a proxied FS that stays true, but a worker-side MEMFS would not be seen.

## Status against the real engine (build-web as of 00:10, fake pack)

Handshake works: `FS`, `ENV`, `addRunDependency`, `removeRunDependency` are exported, the pack is
written, `onDreamsStatus` `boot` then `fatal: cannot load the game program (is /dreams filled?)`
reach the page (the pack was fake, so no `GDIDREAM.EXE`). Request to the WASM stream: link
`-lidbfs.js`; the build has only `MEMFS` in `FS.filesystems`, so the page logs "saves are not
persisted" until it does.
