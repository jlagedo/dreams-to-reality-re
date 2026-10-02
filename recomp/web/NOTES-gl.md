# GL stream notes: ODRender / ODGraphics under Emscripten (WebGL2)

Owner: GL stream (`recomp/render/**`). Everything here was run, not just read;
"Not verified" at the end lists what was not.

## Result in one paragraph

The renderer (sokol_gfx `SOKOL_GLES3`, the `glsl300es` shader variants SDL3 +
`graphics_gl.cpp`) runs unchanged in the browser on WebGL2. The library's own
GPU test suite (`direct_gpu_tests.cpp`: scene, depth, materials, shadow, fog,
131,072-sample fog sweep, 131,072 packed round trips, Bresenham lines) passes in
headless Chrome 154 on SwiftShader and on the real GPU (ANGLE/D3D11, RTX 5070
Ti). The same demo frame rendered natively (D3D11) and in WebGL2 on the GPU
differs in 1 of 307,200 pixels, by 1 in one channel. Windows native tests do not
regress. What the game build needs from this stream is listed below; the one
real decision (how a frame reaches the page from a pthread) is already made by
the WASM stream's ImageBitmap relay, which does not conflict with anything here.

## What the WASM stream links

No change needed to `recomp/windream/CMakeLists.txt`: it includes
`render/cmake/Dependencies.cmake`, `Shaders.cmake`, `RenderCore.cmake`, and the
last has the `if(EMSCRIPTEN)` branch.

| Target | What | Defines / options |
|---|---|---|
| `ODRender` (static) | sokol_gfx core, `direct.cpp`, `direct_shadow.cpp`, `direct_math.cpp`, `fog.cpp`, generated `direct.glsl.h` | `SOKOL_GLES3` (PRIVATE); `-pthread` (PUBLIC) |
| `ODGraphics` (static) | `graphics_gl.cpp`: window flags, ES 3.0 context attributes, swapchain, present, `read_image` | links `ODRender` and `SDL3::SDL3-static`; no `OpenGL::GL` under Emscripten; `-pthread` PUBLIC compile option; INTERFACE link options `-pthread -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2` |

- New in `RenderCore.cmake` (EMSCRIPTEN only): the options above, and the
  variable `OD_WEB_PTHREAD_CANVAS_LINK_OPTIONS` (`-sOFFSCREENCANVAS_SUPPORT
  -sJSPI`) for an executable that renders on a pthread without relying on a
  frame relay. The windream build does not need it (it has its own relay).
- Shaders: `od_generate_shader` already asks sokol-shdc for
  `glsl410:glsl300es:hlsl5:metal_macos`, so the `glsl300es` code is in every
  build. Cross-compile with `-DOD_SHDC_EXECUTABLE="$env:DREAMS_SHDC"` (the pinned
  Windows host tool; the SHA-256 check in `Shaders.cmake` still runs) and
  `-DSDL3_DIR="$env:SDL3_DIR"`. sokol is fetched by `Dependencies.cmake`
  (FetchContent, network on first configure).
- Not needed: `-sFULL_ES3`, `-sUSE_WEBGL2` (`MIN/MAX_WEBGL_VERSION` select
  WebGL2), `-sGL_ENABLE_GET_PROC_ADDRESS`. The renderer uses no
  client-side arrays, `glMapBuffer` or extension functions; `read_image` links
  `glReadPixels` and friends directly under Emscripten.

## Init call sequence (identical to native GL; this is what `render_live.cpp` already does)

```cpp
SDL_Init(SDL_INIT_VIDEO);
od::GraphicsBackend::configure_window(err);  // ES profile 3.0, double buffer, depth 0 (alpha 0, stencil 0: default)
SDL_Window *w = SDL_CreateWindow(title, W, H, od::GraphicsBackend::window_flags());   // SDL_WINDOW_OPENGL
od::GraphicsBackend gfx;  gfx.init(w, err);  // SDL_GL_CreateContext + MakeCurrent + SetSwapInterval(1)
sg_desc d{}; d.environment = gfx.environment(); sg_setup(&d);   // RGBA8 swapchain, no depth, 1 sample
od_renderer *r = od_renderer_create();
// per frame
sg_swapchain sc; gfx.acquire(w, sc, err);   // FrameState::{ready,skipped,failed}
sg_begin_pass({.swapchain = sc}); od_renderer_output(r, target, gamma); sg_end_pass();
sg_commit(); od_renderer_frame_complete(r); gfx.present(err);   // SDL_GL_SwapWindow
```

- SDL3's Emscripten driver binds the window to `#canvas` (the
  `SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR` default). **Do not pass
  `SDL_WINDOW_HIDDEN`** in the browser: SDL then hides the canvas (headless
  options of the host must not apply).
- The swapchain size is `SDL_GetWindowSizeInPixels` = the canvas drawing-buffer
  size. The renderer letterboxes the 4:3 logical canvas into it (`od_centered_canvas`),
  so any canvas size works.
- No explicit swap control and no `emscripten_webgl_commit_frame` is used.
- `GraphicsBackend::acquire` returns `FrameState::failed` ("the WebGL context was
  lost") when the context is lost; sokol cannot rebuild resources in place, so
  the host's existing fatal path ends the run and the page should offer a reload.

## Threading (measured; all four with the same `web_frame.cpp`)

Test programs, built from `recomp/render/tests/web/CMakeLists.txt`:

| Program | Shape | Result in headless Chrome |
|---|---|---|
| `od_web_frame` | main thread, `emscripten_set_main_loop` | frames reach the canvas |
| `od_web_frame_mt` | `-sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT`, loop with `emscripten_set_main_loop` on the pthread | frames reach the canvas |
| `od_web_frame_blocking` | same link, but a blocking `for(;;){frame; SDL_Delay}` | **renders correctly but the canvas never shows a frame** (a worker's OffscreenCanvas presents only when its task ends) |
| `od_web_frame_blocking_jspi` | blocking loop + `-sOFFSCREENCANVAS_SUPPORT -sJSPI` | frames reach the canvas: SDL's `SDL_GL_SwapWindow` calls `emscripten_sleep(0)`, which under JSPI yields |
| `od_web_frame_blocking_proxied` | `-sPROXY_TO_PTHREAD` without OFFSCREENCANVAS_SUPPORT | **fails**: `SDL_GL_CreateContext` returns an error in the worker |

The WASM stream's choice (a private OffscreenCanvas, `transferToImageBitmap`
after each `SDL_GL_SwapWindow`, posted to the page, `bitmaprenderer`) is
equivalent to the JSPI result and works in browsers without JSPI. Nothing in
`graphics_gl.cpp` depends on the choice. Two points about it:

- The relay needs the frame complete at swap time, which holds: the output pass
  writes every pixel of the swapchain (`SG_LOADACTION_CLEAR` or the full-screen
  draw), and `transferToImageBitmap` leaves the drawing buffer empty after.
- The canvas has no alpha (`SDL_GL_ALPHA_SIZE` default 0), so no
  premultiplied-alpha compositing artifacts.

## Explicit export (`read_image`) now works on the GL backends

`GraphicsBackend::read_image` was "not implemented" in `graphics_gl.cpp`. It now
does what the D3D11 one does (RGBA8 region, `rgba[row*w+col] = R | G<<8 | B<<16 | A<<24`,
row 0 = logical top, same validation and errors), through a temporary FBO and
`glReadPixels`. The renderer's own targets are stored bottom-up on GL (the
shaders flip when sampling, `flip` in `direct.cpp`), so their rows are turned
over so callers see the D3D11 orientation. It is what `render_live.cpp` uses
for the 64x64 save thumbnail (one synchronous `readPixels`, a GPU stall once per
thumbnail; Chrome logs "GPU stall due to ReadPixels"). Only RGBA8 images are
exported (packed R32UI sources are not).

## WebGL2 / GLES3 limits and what degrades

| Item | Behaviour in WebGL2 |
|---|---|
| Geometry/compute/storage | The renderer uses none (vertex + fragment only). |
| Texture formats | RGBA8 for uploads and render targets, R32UI (colour 16 bits + coverage 16 bits) for packed 2D sources and the blend lookup table, sampled with `texelFetch` and nonfiltering samplers; `DEPTH` (32-bit float) for scene depth. No 8-bit palette textures exist: the P8 to RGBA8 expansion is on the CPU already (`od_expand_material_page`). All created and sampled on WebGL2. |
| Shader precision | The generated `glsl300es` fragment shaders default to `mediump float` but every variable, uniform and sampler is emitted `highp`; the `usampler2D` packed sources are `highp`. Checked in the generated header; matters on mobile GPUs, which were not tested. |
| Depth range | WebGL2 has no `glClipControl`, so the scene vertex shader remaps depth `[0,w]` to `[-w,w]` (`depth_mode.x` set when `origin_top_left` is false). Tests (GREATER, equal-depth rejection, 64 fog knots) pass; behaviour at the far plane with real game scenes was not compared with D3D11. |
| Integer vertex attributes, `flat` varyings, `texelFetch`, loops | ES 3.0 core; used by the shadow mask pass; pass. |
| Hidden/presented frame capture (`capture()`) | Not available on any GL backend (D3D11 only): returns the error "hidden frame capture currently requires the D3D11 backend". Use a target + `read_image`, or the page reading the canvas. The development screenshot hook therefore does not work in the browser. |
| Readbacks | WebGL2 has no synchronous-free download. `readPixels` is only used by `read_image` (thumbnails, tests). Nothing reads back in a normal frame (the host prints `routine_readbacks`). |
| Swap interval | `SDL_GL_SetSwapInterval(1)` is harmless: SDL only retimes an Emscripten main loop, and there is none in the game. Frame pacing is the host's limiter or the page. |
| Context loss | Reported as a fatal `FrameState::failed` (see above); no restore. |
| Max texture size | Reported by `sg_query_limits().max_image_size_2d` (8192 on SwiftShader, 16384 on the RTX 5070 Ti through ANGLE); WebGL2 guarantees at least 2048. Targets are as large as the canvas drawing buffer. |
| MSAA, multiple render targets, float targets | Not used. |

## How it was checked

Browser build (PowerShell, repository root; builds under `out/recomp/render-web/`):

```powershell
. ./recomp/web-env.ps1
emcmake cmake -S recomp/render/tests/web -B out/recomp/render-web/build -G Ninja "-DSDL3_DIR=$env:SDL3_DIR" "-DOD_SHDC_EXECUTABLE=$env:DREAMS_SHDC" -DCMAKE_BUILD_TYPE=Release
cmake --build out/recomp/render-web/build
uv run --with playwright python recomp/render/tests/web/run_browser.py od_gpu_tests          # SwiftShader
uv run --with playwright python recomp/render/tests/web/run_browser.py od_gpu_tests --gpu    # real GPU through ANGLE
uv run --with playwright python recomp/render/tests/web/run_browser.py od_web_frame          # also _mt, _blocking, _blocking_jspi, _blocking_proxied
uv run --with playwright python recomp/render/tests/web/run_browser.py od_web_frame --lose-context
```

`run_browser.py` serves `build/` with COOP/COEP, starts Chrome (installed one,
`channel="chrome"`, flags `--use-gl=angle --use-angle=swiftshader`), waits for
the page's `window.odResult`, saves the canvas screenshot and the exported frame
to `out/recomp/render-web/check/`.

Native comparison (Windows D3D11) and regression:

```powershell
uv run python recomp/render/tests/web/native_build.py             # builds recomp/render/tests/web natively into out/recomp/render-web/build-native
out/recomp/render-web/build-native/od_web_frame.exe --out out/recomp/render-web/check/native_frame.rgba
uv run --with pillow python recomp/render/tests/web/compare_frames.py out/recomp/render-web/check/native_frame.rgba out/recomp/render-web/check/od_web_frame.rgba
uv run python recomp/windream/verify/direct_render_validate.py --gpu        # existing Windows suite, passes
```

Results:

- `od_gpu_tests` in the browser: all seven checks pass, SwiftShader and RTX 5070 Ti; the
  fog sweep's maximum channel error is 1, the same as native D3D11.
- The same test source built with `OD_TEST_SDL` natively (D3D11 through `read_image`)
  and on Linux desktop GL (WSL, Mesa llvmpipe, GLCORE 4.1) passes too, so the new
  GL `read_image` and the portable test path are checked on a third backend.
- Frame comparison: native D3D11 vs WebGL2 on the real GPU: 1 of 307,200 pixels
  differ, by 1. Vs SwiftShader: mostly 1-LSB differences (software rasterizer
  rounding), 288 pixels beyond 2 on texture and quad edges, 6 pixels at one
  quad edge off by up to 80 (a different edge pixel). The 2D integer paths
  (packed blits, line, band, half blend) are identical on every backend.
- Context loss (`WEBGL_lose_context`) makes the program stop with a reported
  failure rather than draw nothing.
- The Windows renderer suite (`direct_render_validate.py --gpu`) passes after every change.

Files added or changed (all under `recomp/render/` plus this note):
`graphics_gl.cpp` (export, context-loss check, comments), `cmake/RenderCore.cmake`
(EMSCRIPTEN options), `tests/direct_gpu_tests.cpp` (portable path via
`GraphicsBackend`; `OD_TEST_D3D11` default on Windows is unchanged),
`tests/web/{CMakeLists.txt,web_frame.cpp,shell.html,run_browser.py,compare_frames.py,native_build.py}`.

## Not verified

- Firefox and Safari WebGL2 (only Chrome 154 with ANGLE SwiftShader and ANGLE D3D11).
  JSPI (the `_jspi` variant) is Chromium-only as far as tested; the host's relay does not use it.
- Mobile GPUs, Linux/macOS browsers, HiDPI canvases (`SDL_WINDOW_HIGH_PIXEL_DENSITY`).
- Real game scenes through the browser renderer (the WASM stream's runs), the far-plane
  depth behaviour compared with D3D11, long-run memory growth of sokol resources.
- A browser run of the Windows-only retail fixtures (`fixture` / `line_fixture` in
  `direct_gpu_tests.cpp`): they read files from the command line and
  upload/read R32UI images, which `read_image` does not export.
