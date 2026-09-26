# OpenDreams foundation

Spec 001 builds two data-free application shells from one static `ODShared`
library. `ODViewer` and `ODRuntime` each show a Hello World panel over a
shader-generated background. The applications do not load discs or game assets.

## Dependencies and build

CMake 3.28+, Ninja 1.11+, a C/C++17 compiler and its platform SDK are required.
The project fetches pinned SDL3, Dear ImGui and sokol source during the first
configure. It downloads a pinned **host** `sokol-shdc` executable into the build
tree and verifies SHA-256 before using it. Subsequent configurations reuse the
downloaded inputs. `OD_SHDC_EXECUTABLE` may point at a pre-provisioned copy of
the *same* verified binary. No Python package, Node/Babylon setup, Ghidra or
game data is needed for native builds.

| Input | Revision |
|---|---|
| SDL3 | 3.4.16, `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` |
| Dear ImGui | 1.92.9b, `f1cc2ae15e53a861a874c3034aae6798fde194ab` |
| sokol | `2e75443dbd4940b5aa8d76a8e479f8e4b270b9a3` |
| sokol-tools-bin | `11d0cf678105d614d675e6d9bd2aaf3eeff12f8c` |
| Emscripten SDK | 6.0.5, emsdk `dfb9d1a46c3bb8f52e1e6324be23123b9d73c190` |

Run the following from `opendreams/`.

### Windows x64

Use the Visual Studio x64 Developer Command Prompt (MSVC and Ninja must be on
`PATH`).

```powershell
cmake --preset win-msvc-x64-debug
cmake --build --preset win-msvc-x64-debug --target ODViewer
cmake --build --preset win-msvc-x64-debug --target ODRuntime
.\build\win-msvc-x64-debug\ODViewer.exe
.\build\win-msvc-x64-debug\ODRuntime.exe
```

The first graphics backend is D3D11. `win-msvc-x64-relwithdebinfo` provides the
second configuration.

### Linux x64

The local compile baseline is Debian 13 x64 with GCC 14.2. Install CMake,
Ninja, a C++ compiler, pkg-config and the SDL window-system/OpenGL development
packages before configuring. The packages used for the initial WSL build were:

```sh
sudo apt-get install cmake ninja-build g++ pkg-config libgl-dev libegl-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libdbus-1-dev libudev-dev \
  libasound2-dev libpulse-dev
cmake --preset linux-gcc-x64-debug
cmake --build --preset linux-gcc-x64-debug --target ODViewer ODRuntime
```

The Linux backend requests an OpenGL 4.1 core context. The corresponding
RelWithDebInfo preset is `linux-gcc-x64-relwithdebinfo`.

### macOS arm64

Use Apple Clang from Xcode with the macOS SDK. The preset sets an arm64 target
and macOS 13.0 deployment target. It uses an SDL Metal view and a native Metal
drawable; `sg_commit` schedules presentation.

```sh
cmake --preset macos-clang-arm64-debug
cmake --build --preset macos-clang-arm64-debug --target ODViewer ODRuntime
```

`macos-clang-arm64-relwithdebinfo` is also available. This path is awaiting a
build on the separate Mac; it is not recorded as validated.

### WebAssembly

Install and activate Emscripten SDK 6.0.5, then run:

```sh
emcmake cmake --preset web-debug
cmake --build --preset web-debug --target ODViewer ODRuntime
```

Both targets produce their own HTML/JS/Wasm output using an SDL WebGL2 canvas.
The build keeps the browser frame callback nonblocking. Data delivery and
hosting belong to a later spec. `web-relwithdebinfo` is the second preset.

For a checkout on a Windows drive mounted in WSL, keep the WebAssembly build
tree on WSL's native filesystem to avoid archive extraction rename failures:

```sh
emcmake cmake --preset web-debug -B "$HOME/.cache/dreams-opendreams/web-debug"
cmake --build "$HOME/.cache/dreams-opendreams/web-debug" --target ODViewer ODRuntime
```

## Runtime and checks

Either application accepts `--frames N` to exit successfully after N presented
frames. This is a display-dependent smoke check, not a substitute for observing
the panel and input. Configure with `-DOD_ENABLE_GRAPHICS_TESTS=ON` to register
the two finite-frame CTest cases, then run
`ctest --test-dir build/<preset> -L graphics --output-on-failure` where a
graphics environment is available.

Windows visual checks: inspect the background and panel, click the button,
type into the input field, resize the window, minimize and restore it, and
close it. Build results and visual checks must be recorded separately. A
successful compile or finite-frame exit does not prove the UI interaction.

The source layout distinguishes new platform/render/UI code from later ported
game code. `shared/port/` will receive reconstructed functions only after their
program address, checked name, source-block evidence, callers, globals and
adaptations are recorded. Do not infer Cryo's original filenames from these
directories. The detailed mapping policy is in [PORT_MAP.md](PORT_MAP.md).
No game functions were ported in spec 001.

Downloaded sources, tools and generated shader headers remain under ignored
`build/` directories. Existing repository ignore rules also exclude original
disc images and derived game media.

## Validation record (2026-09-26)

| Target | Compile/link | Window and input |
|---|---|---|
| Windows x64, MSVC 19.51, D3D11 | ODViewer and ODRuntime passed Debug and RelWithDebInfo builds | Both rendered; viewer button and text input worked; resize, minimize/restore and close worked; both finite-frame CTest cases passed |
| Debian 13 x64 WSL, GCC 14.2, OpenGL | Both passed Debug compile/link | Not required for the Windows-first visual gate; not run |
| Emscripten SDK 6.0.5, WebGL2 | Both passed Debug compile/link to HTML/JS/Wasm | Browser execution is outside 001 |
| macOS arm64, Metal | Pending build on the separate Mac | Pending |

The Windows shader header regenerated when either its source shader or the
pinned host tool's timestamp changed. Forcing an invalid SDL video driver
produced a clear error and exit status 1. No game data was used in these checks.
