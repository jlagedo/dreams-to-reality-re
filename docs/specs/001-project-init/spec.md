# 001 — OpenDreams project initialization

Status: **Draft — planning only**  
Date: 2026-09-26

## Purpose

**Build the C/C++ foundation, establish the build, and wire the frameworks.**
This is a planning document. The implementation deliverable is two runnable
applications, **ODRuntime and ODViewer**, each displaying **Hello World** through
the actual SDL3, sokol_gfx and Dear ImGui stack shared in ODShared.

The greeting must exercise the real window, input, UI, GPU and presentation
path. A console print or an executable that merely links the frameworks does
not satisfy this spec. Dear ImGui is a selected dependency of 001.

| Scope | Deliverable |
|---|---|
| **001 — foundation** | Reproducible builds, dependencies and framework integration; both applications run and display an interactive Hello World UI. No game data is needed. |
| **002 — retail asset access and game asset navigation** | Port the retail lookup/open paths into ODShared, mount both original images, and browse searchable asset-kind groups and complete per-disc source trees. No media previews or playback. |
| **Subsequent specs** | Extend the shared retail-function port to full asset presentation, add model/map/animation previews and media playback, then rebuild gameplay. |

001 includes no disc-image parser, asset enumeration, game-format decoding,
asset rendering/playback or gameplay. The long-term application responsibilities
and recovered-code constraints below guide the foundation; they are not extra
001 deliverables. The ImGui default font and procedural test graphics are
framework test resources, not original game content.

The [north star](../../north-star.md) defines the game's goals, fidelity rules,
technology choices and overall milestones. This draft elaborates those choices
and records the agreed application split: **ODShared, ODRuntime and ODViewer**.
Research evidence and unresolved format coverage remain in
[RE status](../../re-status.md) and the linked subsystem documents.

The implementation is a **faithful decompilation rebuild**, with adaptations
where modern platforms require them. Its source organization follows the
recovered game structure. It must remain practical to compare a ported function
side by side with its decompilation; a new layered engine architecture is not
a prerequisite to that work.

## Reconstruction priorities

The priorities, in order, are:

1. Preserve recognizable functions, recovered source groupings, state and
   execution order.
2. Replace what needs replacing for portability, GPU rendering and the agreed
   timing model.
3. Document those changes so they can be traced back to the original.

Globals, mixed responsibilities, awkward call chains and strange function
boundaries can stay when they reflect the original. They help us compare the
port beside the decompilation and catch mistakes. Their presence alone is not
a reason to refactor a function or move its state.

**ODShared can start as one tangled library shared by two executables.** Cleaner
boundaries can emerge where the evidence supports them. They are not
prerequisites for getting the game rebuilt. Proposed abstractions and file
splits must serve these priorities, not become requirements of their own.

## Decisions and proposals

### Agreed direction

- Keep OpenDreams in this repository under a new top-level `opendreams/` folder.
- Use the C++17, SDL3, sokol_gfx and **Dear ImGui** stack defined in the north star.
- Complete 001 with both applications displaying Hello World through that stack.
  Disc loading and navigation belong to 002; asset previews come afterward.
- Share the engine implementation between two applications: **ODRuntime**, the
  game, and **ODViewer**, a separately distributable asset browser.
- Preserve recovered function groupings, call relationships, tables and shared
  state. Adapt platform and rendering operations where needed, recording the
  changes rather than reorganizing functions to fit predetermined libraries.
- Keep ODViewer focused on reading, displaying and playing source assets.
  It does not host a game session or provide gameplay inspection.
- Put gameplay debugging in an optional overlay inside ODRuntime.
- Keep the Python decoders as reference implementations and test oracles.
- Make ODViewer call the same ported retail lookup and loader functions in
  ODShared that ODRuntime uses. Viewer-specific read-only decoding is allowed
  after those functions load an asset when no retail function supplies the
  additional information the viewer needs. New disc-image access and browser UI
  are supporting code.
- Read original disc images directly on desktop. Do not introduce an extraction
  or bake requirement for either native application.
- Work desktop first while keeping both applications compiling for Emscripten.
  Browser delivery and packing remain deferred.

### Proposed implementation defaults

The enclosing directory layout, static linkage, dependency policy
and release arrangement below are the initial proposal, not implemented facts.
They can be refined before implementation without changing the product split.

## First compiler and runnable target

Proposed initial baseline: **Windows x64, MSVC, CMake with Ninja, D3D11**.
The first application target to build and launch is **ODViewer**, linked with
ODShared. Its first screen is an interactive Dear ImGui Hello World panel.
ODRuntime must also run and show its own Hello World panel in this same spec,
using the same shared implementation. Building ODViewer first is only work order;
it does not make the runtime optional.

| Choice | Initial plan |
|---|---|
| Host and target | Windows host, x86-64 native executable |
| Compiler | MSVC `cl.exe`, using the installed Visual Studio C++ toolchain |
| Languages | C++17 for the reconstructed code; enable C as well for C sources/dependencies, using C11 where required by our own C files |
| Build driver | CMake 3.28 or newer, Ninja 1.11 or newer, checked-in configure/build presets |
| First configuration | Debug, with symbols and optimization disabled for our code |
| First CMake application target | `ODViewer`, which builds/links `ODShared` and the required dependencies |
| First graphics target | D3D11 through sokol_gfx in an SDL3 window |
| First visible result | A resizable SDL3 window with Dear ImGui Hello World rendered through sokol/D3D11, working UI input, close events and useful initialization errors |
| End of spec 001 | Both applications run the Hello World stack; the platform matrix below builds both |

Local inventory checked on 2026-09-26: Visual Studio Community 2026 18.9;
MSVC toolset directory 14.51.36231, compiler file version 19.51.36256.0;
CMake 4.3.1-msvc1; Ninja 1.13.2; Windows SDKs 10.0.22621.0 and 10.0.26100.0.
Clang-cl 22.1.3 is also installed, but is not the first compiler target.
These are detected tools, not a successful OpenDreams build. CMake/Ninja/MSVC
need the Visual Studio developer environment; they are not all on the ordinary
shell's PATH. Do not hard-code this machine's installation paths in CMake.

The first desktop build uses native 64-bit pointers. Original 32-bit fields,
offsets and handles remain explicitly sized where their semantics require it.
The 1997 executable's in-memory structures cannot simply be cast onto modern
host structures. Browser compilation uses wasm32 and provides an early check
against accidental dependence on the desktop pointer width.

## Products and responsibilities (long-term context)

The following describes the eventual applications. In 001 both are Hello World
shells; in 002 ODViewer gains a searchable game asset catalog and source-disc
navigation.

| Component | Responsibility |
|---|---|
| **ODShared** | The reconstructed game/engine implementation and required modern replacements. Includes original gameplay, loaders, animation, presentation and platform services, organized by recovered source evidence. |
| **ODRuntime** | The player-facing application. Starts the original boot/game flow, runs the simulation, and optionally exposes debugging controls and overlays. |
| **ODViewer** | The inspection and validation application for the ported game engine. Its browser and later previews consume the same recovered ODShared loaders and presentation functions that ODRuntime uses. |

ODShared contains the ported game logic as well as the infrastructure. It is
specific to Dreams to Reality; making it a general-purpose engine is out of scope.
The applications own their entry points and application-specific interfaces.
Neither application depends on the other.

**There is one implementation of each retail game function.** When retail code
opens, indexes, loads or decodes an asset, port that function and its needed call
path into ODShared from Ghidra evidence. ODViewer calls that shared port;
ODRuntime later calls the same implementation. Once the asset has been obtained
through that path, ODViewer may add read-only interpretation or display data for
which no retail function exists. Such viewer extensions are identified as new
tooling and must not replace or reimplement a retail loader. File dialogs,
CUE/BIN and ISO access, catalog grouping/search and ImGui controls are also
support code because the retail executable has no corresponding feature.

### ODViewer: source asset browser

The viewer exists to answer concrete questions:

| Question | Shared functionality exercised |
|---|---|
| Can we read the source data? | Disc mounting, virtual filesystem, archive access and native format decoding. |
| Can we render this model or prop? | Geometry, skeletons, materials, textures and lighting. |
| Can we render this map? | Scene decoding, geometry and object placement, materials and lighting. |
| Can we render this animation? | Animation decoding, pose evaluation, blending where applicable and skinning. |
| Can we play this movie? | HNM decoding, audio synchronization and video presentation. |
| Can we play this sound or music? | Sound and voice decoding, captions where available, CD audio access and mixing. |
| Can we display this sprite or font? | Image, palette, transparency and font decoding and rendering. |

Viewer-owned controls include asset selection, file metadata, an orbit/fly
camera, playback and scrubbing, wireframe, skeleton display, and material and
decoder diagnostics. They operate on a preview session, not a running game.

The viewer does not run AI, combat, triggers, progression, inventory, saves or
the original boot flow. It does not embed ODRuntime, implement a play mode,
author levels or write modified game assets. A preview does not need to become
an editor in order to inspect the decoded content.

Map previews show decoded scene geometry and placements without running a
level. Content whose state depends on gameplay is investigated in ODRuntime.
Unsupported or unverified placement cases must be identified rather than
silently filled in with substitute behaviour.

### ODRuntime: game and gameplay debugging

ODRuntime runs the original game through the shared simulation and presentation
systems. Original menus, dialogue, saves and gameplay rules are shared engine
code; they are not reimplemented in the application entry point.

**Restore the original development tools as the starting point for runtime
debugging.** Their own labels, field selections and call sites help validate the
reconstruction. Add new diagnostics where the surviving tools do not cover a
need; do not require a replacement inspector framework first.

The following is the proposed restoration order, as the corresponding runtime
systems become available:

| Tool | Recovered implementation | Restoration work |
|---|---|---|
| Object debug HUD | `DBG_DrawObjectInfo` (`0x416606`), called by `GAME_TickFrame`, gated by `0x49d5d0`. Displays project/object names, position, speed, physics speed, flags and animation fields. | Preserve field selection, labels and formatting; send the text drawing through the replacement renderer. Trace the activation mechanism and provide an explicit runtime toggle where needed. |
| Collision wireframe | `DBG_DrawCollisionMesh` (`0x45f2a0`), called conditionally from `PHYS_ResolveCollisions` (`0x40bff8`). The original call is wrapped in framebuffer lock/unlock operations. | Preserve the geometry and selection logic; replace framebuffer line drawing with GPU debug primitives. Validate coordinate transforms and visibility against the original path. |
| Original free camera | `CAM_ToggleFree` (`0x40b386`) and `CAM_TickFree` (`0x40b429`), selected by camera message `0x31`. | Restore the recovered controls and camera behaviour, and expose activation in ODRuntime. Treat it as a state-changing developer mode. |

The HUD and wireframe connections were rechecked by read-only Ghidra
decompilation on 2026-09-26. Camera behaviour is documented in
[engine.md](../../engine.md#the-camera-is-node-0-driven-by-messages).
These are confirmed code paths, not completed ports or proof that every retail
activation sequence is known. Initialization prerequisites, activation gates
and presentation still need checking as each tool is restored.

Recovered debug functions remain in ODShared with their original source
groupings. ODRuntime owns any new activation UI and application-specific debug
controls; shared drawing replacements stay with the renderer. There is no
requirement to move original `DBG_*` code into `apps/runtime/debug/`.

Pause, single-step, later replay/rewind, and any new AI/trigger inspection extend
this foundation from the playable milestone. They are new additions unless an
original counterpart is established. Tools arrive with the systems they inspect,
not as a prerequisite to project initialization. They remain outside ODViewer.

Ordinary observation does not intentionally modify gameplay state. State-changing
tools, including the original free camera, are explicitly activated. That camera
updates the actual game camera node; camera-dependent sound and line-of-sight
calculations can therefore change. Preserve and document this behaviour rather
than silently converting the original tool into a detached view. If a separate
inspection-only camera is later added, it changes only the displayed view and
must not write the game camera or its camera-space position feedback.

## What the recovered code supports

Reviewed on 2026-09-26 against the checked name registry, existing source-block
analysis and fresh read-only Ghidra decompilation of `GAME_TickFrame`,
`SCENE_InitLevel` and `MGM_DispatchMessages`. This is a focused architecture
review, not a complete dependency audit of every function.

| Evidence | Implication for the port |
|---|---|
| `GAME_TickFrame` (`0x416d45`) calls `GAME_Tick`, input/demo functions, `UI_DrawHud`, `DBG_DrawObjectInfo`, `VID_DecodeFrame`, `VID_Swap` and `MGM_SendMessage`. | The original frame orchestration crosses gameplay, UI, video, debugging and presentation. Splitting it into isolated libraries would require deliberate source changes. |
| `SCENE_InitLevel` (`0x41f42e`) calls entity/animation/physics setup, camera code, viewport setters, `SW_SelectFlush` and `MGM_SendMessage`. | Level initialization is not a pure scene-data operation. Adapt the obsolete rendering/platform calls while retaining the sequence and its side effects. |
| `MGM_DispatchMessages` (`0x43a64c`) calls `PeekMessageA`, `TranslateMessage`, `DispatchMessageA`, input posting, sound/CD updates and video stream work. | Platform replacement belongs at concrete call sites inside a recovered subsystem. Its original control flow is evidence to preserve, with necessary scheduling changes documented. |
| The source-block report groups `ANIM_Tick*` with `ENT_Tick*` in `0x40484d–0x407b51`, and `INPUT_Init` with `MGM_*` in `0x43a13a–0x43aa97`. These groups use `calls+shared` evidence and have candidate end boundaries. | Functional prefixes are not sufficient to assign one source file or library per subsystem. These groupings are evidence, not a complete recovered source tree. |
| `REND_DrawObject` writes node `+0x4c`, read later by positional sound and AI/attack line-of-sight code. | Rendering cannot simply be declared independent of gameplay. Preserve this known feedback and its timing while replacing GPU-facing work, as specified in the north star. |

Sources: [frame/boot flow](../../boot-sequence.md),
[renderer feedback](../../engine.md#what-the-game-reads-from-the-renderer-verified),
[source-block method](../../re-setup.md#source-file-blocks-find_modulespy),
[name registry](../../../re/names/WINDREAM.EXE.tsv) and
[the analysis tool](../../../tools/find_modules.py). The generated source-block
report is local at `out/ghidra/modules/WINDREAM.EXE.tsv` and can be regenerated
with the documented tool; it is not a required checked-in input to the build.

**The original filenames and directory tree are not recovered.** The local
source-block report inspected for this review contains 168 blocks, including
99 single-function blocks, and 18 proven boundaries. These are report entries,
not a recovered file count. A candidate boundary is not proof of a separate file;
adjacent blocks may belong together. Preserve established grouping and function
order, record uncertain assignments, and refine them when evidence improves.
Do not present new filenames or directories as Cryo's original names.

## Source preservation and necessary adaptations

Apply the reconstruction priorities to the actual recovered code. There is no
required dependency-free `core`, isolated `sim` library or separate
`scene`/`animation` library.

For every group of functions ported:

1. Identify the original program, addresses, checked names, likely source block,
   callers, callees, globals and related structures before assigning files.
   Record unknowns explicitly; incomplete source-boundary recovery does not
   block porting a known function with a provisional, documented file assignment.
2. Keep the recovered functions, tables and module state together where the
   evidence supports it. Keep original names and recognizable call order.
   Shared globals and handle tables are allowed; converting them to injected
   services, classes or independently owned sessions is not an initial goal.
3. Replace unavailable OS, device, runtime and rasterizer operations where
   needed. Prefer narrow replacements at the existing calls; a function that
   mixes platform and game work need not be dismantled to satisfy a layer rule.
4. Record adaptations such as fixed-step scheduling, GPU rendering, safe
   parsing, pointer-width changes and browser-compatible control flow beside
   their original counterparts. Changes must preserve relevant arithmetic,
   state effects and ordering, with comparisons appropriate to each change.
5. Track the mapping from original functions/source blocks to C++ files and
   distinguish ported, adapted, replaced and intentionally omitted functions.
   The existing name registry remains the authority for names; a port map adds
   implementation locations and evidence, not a competing naming system.

Preserve known binary layouts at decoding boundaries. Do not copy decompiler
pointer casts or assume a modern pointer has the original width. Necessary
representation changes must remain traceable to the original fields and uses.

SDL/sokol use in ODShared is expected. Prefer localized backend implementations
for portability and review, but there is no blanket ban on those dependencies
in the reconstructed source units. Avoid introducing API-specific behaviour
into arithmetic that does not need it; a naturally pure math function can stay
pure without moving it into a new `core` library.

The fixed-step and fidelity rules still apply: physical display timing, debug
controls and viewer preview state must not change the game's decisions. The
GPU replacement must preserve the known renderer feedback on the CPU at the
original point in the tick. These are behavioural requirements, not proof of
an existing clean library boundary.

## Proposed source layout

The C++ project has its own CMake entry point. Existing Python, web and research
directories retain their current roles. Only the enclosing structure is proposed
here; the files inside the reconstructed code follow source evidence.

```text
opendreams/
  CMakeLists.txt
  CMakePresets.json
  README.md
  cmake/                 Build options, dependency pins, toolchain support
  shared/                ODShared library
    port/                Reconstructed source units, tables and globals
    platform/            New SDL3/OS replacement implementations as needed
    render/              New sokol GPU implementation and shaders
    ui/                  Shared Dear ImGui setup and SDL/sokol integration
    support/             New support such as cue/bin mounting and ISO access
  apps/
    runtime/             ODRuntime entry point and player-facing application
      debug/             Optional runtime debugging UI and overlays
    viewer/              ODViewer entry point, asset browser and preview UI
  tests/
    unit/                Focused tests, using test services where needed
    reference/           Python comparisons and optional local corpus tests
  tools/                 Later trace/compare tools and the deferred pack writer
```

`port/`, `platform/`, `render/`, `ui/` and `support/` are new organizational labels,
not claims about the original tree. Do not split a recovered source unit merely
to fill them. An adapted function may stay beside its original neighbours and
call replacement code. Add folders and interfaces only when needed.

## Build targets and application boundaries

Start with one static **ODShared** library and two executable targets,
**ODRuntime** and **ODViewer**. ODShared can link SDL3, sokol and Dear ImGui. There is no
requirement for a standalone DLL, an installed SDK or an independently versioned
engine API. Further internal libraries need a demonstrated benefit and evidence
that the split does not obstruct the faithful port.

Both applications call the same reconstructed code and replacements. ODShared
does not depend on either application's UI. Platform initialization and resource
setup may be shared, with application-specific startup kept in the frontends.

**The viewer's scope is an execution boundary, not a link dependency ban.** It
may link the full engine and initialize shared tables, resource arenas, model
handles and presentation services. It does not start the gameplay loop, AI,
combat or progression. A shared library containing those functions does not
turn the viewer into a game host.

For asset previews, inspect the actual loader and playback prerequisites. Call
the recovered functions at an appropriate entry point with the minimal required
setup. Add small preview adapters if necessary, documenting what they initialize
or bypass. Do not duplicate animation/rendering algorithms or split original
source files simply to make a preview routine appear dependency-free.

Tests may link ODShared and use test implementations of device services where
necessary. Headless execution is useful; it does not require the entire port
to have no SDL/sokol link dependency. Select isolated functions or execution
paths based on their actual needs and avoid a speculative replacement-service
framework during initialization.

## Technical constraints inherited from the north star

- C++17, no exceptions, no RTTI; the standard library is allowed. Decode and
  initialization failures need explicit error results useful to both UIs and
  command-line tests, including the source file/offset where relevant.
- SDL3 provides the platform layer; sokol_gfx provides the GPU renderer with
  the selected native backends and WebGL2. Use sokol-shdc for shader variants.
- Dear ImGui provides the 001 Hello World UI and the later viewer interface.
  Sharing its integration does not replace the original game's menus or HUD.
- The game is ported from the original code, retaining its recovered names,
  arithmetic and behaviour. Follow the repository's naming/provenance rules.
- Simulation runs at 30 Hz with delta 1.0, independently of display refresh.
  Logical input, gameplay randomness and simulation time remain isolated from
  rendering, preview controls and debugging UI.
- Preserve the north star's floating-point build restrictions and shared
  trigonometric implementation requirements. Verify replay portability with
  tests as simulation arrives; project structure alone does not establish it.
- Rendering follows the documented original appearance contract and GPU
  boundary. The original projection path remains a validation reference.
- Native HNM playback uses the shared decoder and follows audio sample time.
- No original disc data or derived game media is committed or shipped with
  either application. The project remains MIT under the repository licence.

This spec does not reopen the renderer, simulation rate, game fidelity or
desktop-first decisions. Unknown game behaviour is resolved through research,
not invented to make a preview or playable scene work.

## Foundation dependency map

| Dependency or tool | Role | Needed in 001? | Acquisition/build plan |
|---|---|---|---|
| CMake + Ninja | Configure, build, presets and test registration | Yes | Host tools; minimum versions above, exact CI versions recorded |
| Native compiler + platform SDK | Compile C/C++, link native libraries and graphics APIs | Yes | MSVC/Windows SDK first; platform matrix below |
| SDL3 | Window, events and eventual input/audio/platform services | Yes | Pin upstream source; build a static library with the target toolchain through CMake |
| sokol_gfx | GPU rendering API | Yes | Pin upstream headers; compile the implementation once in ODShared per build, selecting one backend |
| Dear ImGui | Hello World panels in both applications, later viewer UI | **Yes — selected** | Pin source; compile core and the SDL3 platform backend with the project toolchain |
| sokol_imgui | Render Dear ImGui through sokol_gfx | Yes | Use the helper from the pinned sokol revision, in C++ mode with its sokol_app dependency disabled; verify compatibility with the ImGui pin |
| sokol-shdc | Translate our shader source into backend-specific generated headers | Yes | Pin a host executable from upstream `sokol-tools-bin`, verify its checksum; keep compatible with the selected sokol revision |
| Emscripten SDK | Browser C/C++ compiler, linker and WebAssembly support | Yes, for the compile check | Pin an SDK release in CI; build SDL3 and ODShared for wasm32 |
| CTest | Run registered checks and report failures | Yes | Included with CMake; no separate C++ test framework needed just to launch the skeleton |
| Python toolkit | Reference decoder comparisons | Later, optional tests | Existing repository environment; not linked or required to build the native applications |
| Vorbis decoder / pack encoder tooling | Deferred browser content delivery | No | Address with the web pack; not a dependency of the native skeleton |

Game formats, recovered maths, physics, animation and HNM decoding remain
project code. This plan does not add a separate general-purpose engine,
physics framework or asset importer. The restored runtime HUD uses the game's
text path; Dear ImGui does not replace the recovered debug routines.

### Dependency acquisition and pins

Use **CMake FetchContent** for pinned source dependencies, with declarations in
`opendreams/cmake/Dependencies.cmake`. Prefer immutable source archives with
SHA-256 checksums or full commit IDs; normal builds must not follow `master`,
`main` or `latest`. Build SDL with `SDL_STATIC=ON`, `SDL_SHARED=OFF`, and its
examples/tests disabled for the application build. Keep third-party notices.
SDL documents both this CMake integration and the static target options in its
[CMake guide](https://wiki.libsdl.org/SDL3/README-cmake).

Fetch the shader compiler separately as a **host tool**, not a target library.
For example, a Windows-hosted WebAssembly build still runs the Windows shader
compiler. Provide an explicit local-path override for an already provisioned
host tool. Generated shader headers belong in the build tree, with dependency
tracking so editing a shader or changing the pinned tool rebuilds them.
See the [upstream tool distribution](https://github.com/floooh/sokol-tools) and
[shader compiler documentation](https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md).

**Exact SDL, sokol, Dear ImGui, shader-tool and Emscripten revisions are not yet selected or
compatibility-tested.** Selecting and recording that set is the first task of
001 implementation, before the first integration build. Pinning policy and a
dependency inventory do not by themselves prove a working combination. Record
the successful versions in the dependency declarations and CI configuration;
do not leave floating versions in the completed foundation.

Downloaded sources and tools stay in ignored build/cache directories. Once
provisioned, incremental builds should reuse them without routine network
updates. No vcpkg/Conan requirement or Git submodule workflow is introduced in
this initial proposal. FetchContent's supported options are documented
[by CMake](https://cmake.org/cmake/help/latest/module/FetchContent.html).

## Compilation and graphics integration

Configure the C++ project directly from `opendreams/`; the Python package and
Babylon viewer keep independent build commands. Building either native
application must not require the Python reference toolkit, Node/Babylon or
Ghidra. Compiler, CMake and shader build tools are normal build dependencies.
Emscripten's own SDK tools are a separate requirement of the browser build.

### Compiler and target policy

- Enable C and C++ in CMake. Compile C++ sources as C++17 without compiler
  language extensions; retain C-like source structure where it matches the
  recovered code. Compile dependency C sources as C, not forcibly as C++.
- Build one static ODShared library for each target/configuration, then link it
  into both applications. List translation units explicitly; no unity build.
- Provide Debug and RelWithDebInfo configurations initially. Keep link-time
  optimization off during the foundation work.
- Disable RTTI and C++ exceptions in our C++ code. For MSVC, remove inherited
  exception-enabling options, apply the no-exception configuration consistently
  with its standard-library headers, and use `/GR-`. For GCC/Clang use
  `-fno-exceptions -fno-rtti`. Inspect the generated commands during validation;
  a preset name is not proof that the options took effect.
- Use `/fp:precise` on the selected modern MSVC baseline, without `/fp:fast` or
  `/fp:contract`. On GCC/Clang use `-fno-fast-math -ffp-contract=off`. The modern
  MSVC behaviour is documented in its
  [floating-point options](https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior).
  This does not emulate x87 or establish cross-platform replay parity by itself.
- Apply our warning and numerical policies to our targets, not indiscriminately
  to downloaded dependencies. Enable useful warnings without making cleanup of
  all inherited code a prerequisite to the port.
- On MSVC, use the matching debug/release DLL CRT configuration consistently
  across ODShared, SDL and the applications. Static SDL linkage does not imply
  a completely static executable or remove the Windows runtime dependencies.

### SDL3 and sokol need explicit integration

SDL owns the application window and platform events. Use sokol_gfx for graphics;
do not add a second window/event owner through sokol_app. The integration code
must create the graphics device/context, manage drawable size and framebuffer
resources, provide sokol's environment/swapchain information, and present frames.
Sokol explicitly leaves these operations to the application; this is real
foundation work, not something linking the two libraries does automatically.
See [sokol_gfx's integration contract](https://github.com/floooh/sokol/blob/master/sokol_gfx.h).

### Dear ImGui and the Hello World path

Use Dear ImGui's SDL3 platform backend for events/input and the upstream
`sokol_imgui.h` helper for rendering, with `SOKOL_IMGUI_NO_SOKOL_APP` enabled.
The helper can render through sokol without owning platform events; SDL remains
the window/event owner. Use the C++ API directly; no C binding is required.
See [the SDL3 backend](https://github.com/ocornut/imgui/blob/master/backends/imgui_impl_sdl3.h)
and [sokol_imgui](https://github.com/floooh/sokol/blob/master/util/sokol_imgui.h).

The integration must coordinate one ImGui context and one frame lifecycle per
application, including input, delta time, drawable size/DPI, default-font
resources, rendering and shutdown. Avoid duplicate `NewFrame`/`Render` calls
when combining the helpers. Pin and validate these libraries together.

Both applications must display `Hello World` and their own name in an ImGui
panel. Add a tiny button/counter and a text-input field to verify mouse and
keyboard/text input. These are integration checks, not a viewer feature set.
Keep the initial UI in one native window; docking and multiple platform windows
are not requirements of 001.

The complete path is:

```text
SDL3 window and events -> Dear ImGui UI -> sokol_imgui -> sokol_gfx
                      -> native graphics backend -> window presentation
```

Run sokol-shdc on a small project-owned shader and use it for a procedural
background behind the greeting. This proves the shader build step and generated
output are actually used; the helper's embedded UI shaders alone would not
exercise our shader-generation pipeline. No external image or game font is
needed. Share this setup/rendering code in ODShared; frontends supply their
application identity and greeting UI.

### Platform matrix

| Target | Compiler / SDK | Graphics integration | Shader output |
|---|---|---|---|
| **Windows x64 — first** | MSVC + Windows SDK | SDL window/native handle; D3D11 device, DXGI swapchain, resize and presentation owned by our integration | HLSL5 |
| macOS arm64 | Apple Clang + macOS SDK | SDL Metal view and native Metal setup; compile the small Metal integration/implementation as Objective-C++ where needed | Metal for macOS |
| Linux x64 | GCC + distribution development packages | SDL OpenGL context, initially targeting the GL 4.1 feature level; GL presentation and required window-system libraries | GLSL 410 |
| Browser wasm32 | Emscripten Clang | SDL/Emscripten canvas and WebGL2 context; return control to the browser between frames | GLSL 300 ES |

The SDL APIs for
[native window properties](https://wiki.libsdl.org/SDL3/SDL_GetWindowProperties)
and [Metal views](https://wiki.libsdl.org/SDL3/SDL_Metal_CreateView) provide the
platform entry points. Linux CI must explicitly install the window-system,
OpenGL and other SDL development packages it enables; document the selected
distribution/package list using the
[SDL Linux guide](https://wiki.libsdl.org/SDL3/README-linux).
Do not claim all four backends are validated just because the Windows one works.

For the skeleton, use SDL's main callbacks as the proposed application entry
mechanism, keeping the frontend callbacks thin. This accommodates a browser
frame loop without a blocking desktop loop. It does not require rewriting the
ported functions into callbacks; adapting original nested loops remains separate
work when those functions arrive. See
[SDL main callbacks](https://wiki.libsdl.org/SDL3/README-main-functions).

The Emscripten preset uses its CMake toolchain through `emcmake`, builds the same
pinned SDL sources for the target, and enables WebGL2. Compile/link both frontends
to their own HTML/JS/Wasm outputs; do not reuse native libraries in that build.
Keep the first browser skeleton single-threaded, with no game data, pack, cache
or delivery service. Emscripten's SDK supplies its build-time Python/Node tools;
this is unrelated to the existing Python toolkit or Babylon application.
See [Emscripten CMake integration](https://emscripten.org/docs/compiling/Building-Projects.html)
and [SDL's Emscripten guide](https://wiki.libsdl.org/SDL3/README-emscripten).

### Presets and expected commands

Proposed preset names: `win-msvc-x64-debug`, `win-msvc-x64-relwithdebinfo`,
`macos-clang-arm64-debug`, `linux-gcc-x64-debug` and `web-debug`. Each gets its
own build directory. These commands describe the planned interface; the CMake
files and presets do not exist yet.

From `opendreams/` in the Visual Studio x64 developer environment:

```powershell
cmake --preset win-msvc-x64-debug
cmake --build --preset win-msvc-x64-debug --target ODViewer
cmake --build --preset win-msvc-x64-debug --target ODRuntime
```

After activating the pinned Emscripten SDK:

```text
emcmake cmake --preset web-debug
cmake --build --preset web-debug --target ODViewer ODRuntime
```

CTest registers real checks as they become available. Foundation smoke checks
cover startup, the rendered Hello World panels, button/text input, DPI and
resize/minimize/restore, close events and clear initialization failures. An
optional finite-frame run can help CI where a graphics environment exists.
Keep compile/link results separate from visual execution results; the greeting
validates framework integration, not game logic.

Use separate output directories for each platform/configuration. Build output,
fetched dependencies, generated shader headers and local settings must not enter
source control. Review the existing ignore rules during initialization rather
than relaxing the game-data exclusions.

## Local game data (later milestones)

Desktop data access mounts the two cue/bin images using shared ISO 9660 and CD
audio support, following the north star's merge, case and language rules. A
directory mount is a development convenience only.

Existing `DREAMS_DISC1` and `DREAMS_DISC2` settings describe **extracted
directories**. Do not silently reinterpret them as image paths. Define explicit
image-path configuration when disc mounting is implemented, with both native
applications using the same meaning. Machine-specific paths stay untracked.

The Python [extract/bake/pack pipeline](../../pipeline.md) continues to serve
the Babylon viewer. Its baked data root is not the native engine's production
input. Python output may be used in comparison tests without becoming a
runtime dependency.

## Initialization work plan

This spec ends with two runnable Hello World applications exercising the full
foundation stack. The next spec introduces disc access and a searchable asset
browser; this spec does not load or preview game assets.

| Step | Work | Completion evidence |
|---|---|---|
| 1. Establish the project | Select immutable dependency/tool pins; add the C/C++ CMake project, ODShared, two application targets, presets and output exclusions. Start with Windows x64/MSVC/Ninja. | A clean checkout configures the first preset without Python-toolkit or web-app setup; compiler/options and pins are recorded. |
| 2. Wire the frameworks | Build SDL3, sokol and Dear ImGui; connect input, GPU setup, swapchain/presentation and host shader generation. | ODViewer shows its Hello World panel over the procedural background, with working button/text input and resize/close handling. |
| 3. Run both applications | Bring up ODRuntime through the same shared setup/drawing/UI integration. Keep entry points thin and give the windows distinct identities. | Both applications run the real framework stack, display Hello World and report initialization failures clearly without disc data. |
| 4. Establish portable builds | Build both applications on Windows, macOS and Linux; add an Emscripten compile/link check for both. | CI builds the selected backends and shaders. Desktop launch checks are recorded separately from compile checks. |
| 5. Establish validation entry points | Register C++ tests and optional Python/corpus comparison hooks as their first meaningful cases become available. Document local data configuration and test separation. | Ordinary builds and data-free tests work without discs. Requested corpus checks clearly report missing data. |

Do not implement disc loaders, asset browsing, previews, gameplay, the original
debugging tools or the pack format in 001. The Hello World UI is the complete
application feature scope for this foundation.

### Acceptance criteria for project initialization

- `opendreams/` is an independently configurable CMake project with documented
  build and launch steps.
- ODRuntime and ODViewer are separate executables built from one engine source
  tree; window, GPU and ImGui integration code are shared.
- **Both display Hello World through Dear ImGui and sokol**, with their own
  application identity. Button and keyboard/text input work through SDL3.
- Both native applications launch and shut down without game data.
- Windows, macOS and Linux builds succeed; both Emscripten targets compile
  and link, including their shader variants. Browser data delivery is not
  required for this milestone.
- Desktop window/render smoke checks are performed and recorded; a successful
  compile is not reported as successful GPU execution.
- Third-party revisions and tool requirements are recorded and reproducible.
- The first successful target is recorded as Windows x64/MSVC/D3D11 ODViewer;
  ODRuntime then exercises the same shared foundation.
- Shader generation runs as a host build step and responds to source/tool
  changes; the generated shader draws the procedural background. Backend
  initialization, resize and presentation are explicit code.
- The dependency map distinguishes runtime libraries, host tools and deferred
  media dependencies; Dear ImGui and its SDL/sokol integration are included,
  with no floating upstream revisions remaining.
- The test setup permits focused reference tests and later headless execution
  where practical, without mandating pure subsystem libraries or removing
  SDL/sokol link dependencies from ODShared.
- The source-placement policy records known and uncertain original groupings;
  the skeleton does not impose a new architecture on the ported functions.
- No game data, build products or machine paths are added to version control.
- No disc mounting, file navigation, asset decoding, previews or gameplay are
  required or implemented to satisfy 001.

## Work after initialization

1. **[Spec 002 — retail asset access and game asset navigation](../002-disc-navigation/spec.md):**
   mount both original images using shared cue/bin and ISO 9660 access. ODViewer
   uses ImGui for a searchable, filterable browser grouped by game asset kind,
   alongside complete per-disc source trees. It consumes selected retail
   lookup/loader functions ported into ODShared, which ODRuntime will use too;
   viewer-only inspection may enrich their loaded results where retail has no
   matching function.
   Model/map/image display, animation and audio/video playback are deferred.
   ODRuntime need not start game loading merely because these shared functions
   are available.
2. **Later asset preview specs:** extend the same ported loaders through full
   asset decoding and presentation, comparing them with Python. Models, maps,
   props, animations, sprites/fonts, sounds, voice/captions, music and movies
   continue to use the shared code.
3. **ODViewer scene validation:** verify placement, materials, lighting and
   projection through map previews and reference tests. A fly camera is a
   preview control; exercising gameplay-dependent camera behaviour belongs to
   ODRuntime or a focused headless test.
4. **ODRuntime first playable slice:** connect the recovered game camera,
   movement, collision, physics and HUD to the fixed-step simulation. Add the
   runtime overlay and time controls alongside that work.
5. **Full gameplay and progression:** follow the later north-star milestones.
   ODViewer remains an asset browser throughout this work.
6. **Packaging and browser delivery:** preserve the two-application split when
   addressing desktop distribution and the deferred browser pack/cache work.

## Validation and release model

Use three distinct kinds of validation:

- **Data-free tests:** synthetic byte buffers, recovered math and animation
  cases, error handling, and later headless execution using the actual shared
  state and service setup required by the functions under test.
- **Reference/corpus tests:** locally supplied data compared with Python
  decoders, then original traces as gameplay arrives. These tests are optional
  when data is unavailable and report skips explicitly. A designated corpus
  validation run must not treat absent data as a successful comparison.
- **Application checks:** ODViewer verifies asset presentation; ODRuntime
  verifies gameplay and its debugging overlay. A successful preview is not
  evidence that gameplay using that asset is faithful.

Use build/test scopes so unrelated web work does not automatically require a
full native matrix, while changes to shared build configuration, dependencies
or reference decoders run the checks they affect.

Proposed release model: one OpenDreams version from one source revision, with
separate ODRuntime and ODViewer downloads. The viewer can be distributed before
the game is playable. ODShared is linked into those applications and has no
independent SDK, DLL compatibility promise or release schedule initially.

The Babylon viewer stays available during the transition. It is retired when
ODViewer covers the required inspection workflows and its browser delivery is
available where the existing web viewer is still needed. No immediate removal
or rewrite of `web/` is part of this spec.

## Open implementation decisions

- Exact SDL3/sokol/Dear ImGui/shader-tool/Emscripten pins and verified compatibility. The
  acquisition mechanism, minimum CMake/Ninja versions and first native compiler
  are proposed above; the pin set must be closed during 001 implementation.
- Exact macOS/Linux CI toolchain versions, deployment baselines and Linux system
  package list; close these before claiming the corresponding matrix rows pass.
- The first reconstructed source units, uncertain boundaries, shared headers,
  port-map representation and minimum required platform replacement interfaces.
- A richer C++ test framework, if needed, and the representation used for Python
  comparison results. CTest is sufficient to establish the 001 test entry point.
- Image-path setting names, executable/package filenames and packaging details.

The browser pack format, streaming/cache policy and web video representation
remain parked under the north star. None is a blocker to native initialization.

## Documentation alignment before implementation

The north star is aligned with this correction: preserve recovered source
structure, use one shared implementation and two applications, and replace the
mandatory pure `sim` target with documented platform adaptations and explicit
behavioural constraints. ODViewer remains a maintained asset browser; gameplay
debugging belongs to ODRuntime.

This draft and its north-star alignment change planning documentation only.
They do not implement C++, change the Python pipeline or alter RE findings.
