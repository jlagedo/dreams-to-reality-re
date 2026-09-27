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

The source layout distinguishes new platform/render/UI code from ported game
code. `shared/port/` receives reconstructed functions only after their
program address, checked name, source-block evidence, callers, globals and
adaptations are recorded. Do not infer Cryo's original filenames from these
directories. The detailed mapping policy is in [PORT_MAP.md](PORT_MAP.md).
No game functions were ported in spec 001.

The spec 002 file-root getters in `shared/port/file_roots.cpp` use caller-owned
state in place of retail globals. `shared/port/vfs.cpp` now ports the VFS
open/read/seek/close path and BF archive registration and member access. Each
`VfsContext` owns one selected disc source and its own handle and archive table.
Loose ISO files take precedence over registered BF members, as in retail;
later BF mounts replace same-name members in the retail table while `BF_Mount`
also returns every physical row for the viewer catalog. The disc-image boundary
is read-only, validates archive extents and reports recoverable errors.
`port-map.tsv` records both Windows binaries' source addresses and C++ symbols;
`ghidra_scripts/ApplyPortMap.java` generates Function Tags and implementation
location plate comments in Ghidra.

`shared/port/ddat.cpp` now ports `DDAT_Load`, `DDAT_LoadRecord`,
`DDAT_InitEmptyRecords` and `RLE_UnpackZeros`. The bank reads `DREAMS.DAT`
through the selected VFS source. `DDAT_LoadRecord` returns one reusable
`0x2200` working buffer and retains the game's previous-name fallback;
catalog callers can check `has_record` before requesting a name. The corpus
test checks all 150 records from each original image against aggregate CRCs
from the independent Python decoder.

`shared/port/stream.cpp` ports the retail ring-buffer stream's create,
open/fill/peek/commit/close/free calls. `shared/port/dsn.cpp` ports
`DSN_InitState`, `DSN_ResetState` and `DSN_LoadHeader`. A successful DSN load
leaves the stream at the first packed-body tag and exposes the copied 11-byte
name and 20-byte object records. The corpus test checks all 98 physical `.DSN`
headers across both discs against the Python header parser.

`shared/port/dan.cpp` ports the DAN archive opener, type-3 animation-chunk
reader, count/name getters and close path. The copied 11-byte name slots and
13-byte clip slots are source-scoped logical children of each physical `.DAN`.
The shared reader keeps the retail `0x96000` compressed-work buffer and maps
each declared clip to its type-3 payload; it skips earlier type-1/2 ranges by
their recorded lengths during metadata indexing. The corpus test validates
all 191 physical DAN archives against the Python directory and payload oracle.

`shared/port/drd.cpp` ports the dialogue-bank open, entry read, caption-count,
portrait and close functions. It reads the retail offset table at `0x15` as
absolute `u32` file positions, retains one reusable entry buffer, and exposes
the current entry's WAVE, timed-line and portrait extents. The corpus test
checks all 178 Disc 1 entries against the corrected Python decoder; Disc 2
reports the bank absent.

`shared/port/fsb.cpp` ports the sound-bank load, indexed sample getter and
free path. It preserves the retail contiguous sample allocation and clip
order for one selected disc, with checked table/EOF ranges. The two original
images contain the same 24-clip bank; the corpus test compares every sample
byte and table extent with the Python decoder.

`shared/port/sprite.cpp` ports the general sprite-set and font loaders, the
five fixed BF icon-bank loaders and cleanup, multiply tables and the
executable's 72-name icon lookup. It keeps raw palette/descriptor records and
the pixels read by the retail calls. One malformed `HI320` glyph remains
visible as an invalid slot while the other 255 load. Corpus checks cover the
five icon banks, `SOUR.ALP`, all three fonts and Disc 2's supplemental
`TITRES.SPR` member.

## Shared disc access (spec 002 foundation)

`shared/disc/image.h` is the portable source boundary for both applications and
the retail VFS port. `od::disc::Image::open(cue, error)` owns one CUE/BIN
mount. It reports every track's mode, file-relative indexes and backing-file
status; lists ISO entries with their original identifiers and source-scoped
`FileId`; and identifies Disc 1/2 from `DATA/1CD.ID` and `DATA/2CD.ID`. `find`
normalizes game-style case and separators, `children` lists a directory, and
`read_at` performs checked reads by file ID and byte offset. A file ID from an
old or different mount is rejected. Callers can build two mounts independently
and replace either only after a new `open` succeeds.

The initial CUE reader supports the original split-track layout: one
`MODE1/2352` data BIN followed by separate audio BINs, each with its own
file-relative `INDEX 00/01`. Missing audio remains visible in track status and
does not hide a readable data track. CUE/sector access is new portability code;
the [vendored lib9660](third_party/lib9660/README.md) reads ISO directory
records. Neither code path uses the installed `CRYO` cache, an extracted tree,
or Python at runtime. The adapted retail `VFS_*` functions use `find` and
`read_at` without duplicating ISO parsing.

Native data-free tests run with
`ctest --test-dir build/<preset> -R ODDiscFixtures --output-on-failure`.
These include loose-file and BF member reads, member replacement, seek/close
behavior, root aliases and malformed BF bounds.
For an optional original-disc comparison, set `DREAMS_CUE1` and `DREAMS_CUE2`
to the original `.cue` paths in the process environment, then run
`ctest --test-dir build/<preset> -R ODDiscCorpus --output-on-failure`.
These settings are distinct from the Python toolkit's `DREAMS_DISC1/2`
extracted-directory paths. When those extracted paths are also exported to the
test process, the corpus check compares full `DREAMS.DAT` and `ICONES.BF` bytes
with each mounted image. The CTest case reports a skip when CUE paths are
absent. The corpus check also mounts both shipping `ICONES.BF` files through
the VFS path and verifies every member's name and payload against its physical
archive extent. Disc-image opening in the Emscripten apps remains deferred by spec 002;
the shared source still compiles there.

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
