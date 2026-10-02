# Browser demo build: contract between the four work streams

Target: opening the page loads a minimal demo of the game and runs it. The demo
content is deployed by the owner on Cloudflare. No disc is needed.

Streams (each owns its files; do not edit another stream's files, write a
request in `recomp/web/NOTES-<stream>.md` instead):

| Stream | Owns | Delivers |
|---|---|---|
| WASM | `recomp/windream/**` (host, CMake, `build.py --web`) | `out/recomp/windream/build-web/dreams.{js,wasm}` (+ worker/data if any) |
| GL | `recomp/render/**` | WebGL2 (GLES3) path of ODRender/ODGraphics under Emscripten |
| DEMO | `recomp/web/demo/**`, `src/dreams/**` additions | `out/recomp/web/demo/` pack: files + `manifest.json` |
| PAGE | `recomp/web/*` (not `demo/`), `recomp/README.md` | `index.html`, loader JS, `serve.py`, `package.py`, `_headers`, `out/recomp/web/dist/` |

## Module interface (WASM <-> PAGE)

- Built with `-sMODULARIZE -sEXPORT_NAME=createDreams`, output name `dreams`.
  `createDreams({canvas, arguments, preRun, print, printErr, locateFile})`.
- Canvas is `#canvas`. The page owns the DOM; the host owns the GL context.
- Game data lives in the Emscripten FS under `/dreams` (the equivalent of the
  retail install root `CRYO\DREAMS`). The host is started with
  `WD_INSTALL_ROOT=/dreams` in `Module.ENV` (set in `preRun`), no launcher, no
  disc mode. Saves go to `/dreams/DATA/GAME` (the page may mount IDBFS there).
- The host reports readiness and fatal errors to the page through
  `Module.onDreamsStatus(kind, text)` if defined (`boot`, `running`, `fatal`);
  the page must work without it defined.
- Needs cross-origin isolation (pthreads): COOP `same-origin`, COEP `require-corp`.

## Demo pack (DEMO <-> PAGE)

`manifest.json`:

```json
{"version": 1, "name": "demo", "total": 123, "files": [
  {"path": "DREAMS.DAT", "size": 123, "sha256": "...",
   "chunks": [{"url": "DREAMS.DAT.000", "size": 100}, {"url": "DREAMS.DAT.001", "size": 23}]}
]}
```

- `path` is relative to `/dreams`, forward slashes, exact case the game opens.
- Every file has `chunks` (one chunk for small files); chunk size at most 20 MiB
  (Cloudflare Pages limit is 25 MiB per asset). Chunk `url` is relative to the
  manifest URL. Pack is immutable per version; names may carry a content hash.
- Pack base URL: `?demo=<url>` or `window.DREAMS_DEMO_BASE`, default `demo/`.

## Rules for everyone

- Game-derived data and generated code only under `out/`; never commit them.
- Windows/Linux builds and `uv run pytest` must not regress; guard web code
  with `EMSCRIPTEN` / `__EMSCRIPTEN__`.
- Shared working tree: no `git commit`, `stash`, `checkout`, `reset`.
  Do not rebuild a directory another stream is using (use your own build dir).
- Final report: what works, exact commands, what was verified, gaps.
