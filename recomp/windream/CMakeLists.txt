# WINDREAM static recompilation (feasibility build; build.py configures it).
# Generated sources come from lift.py and gen_imports.py and stay under out/
# (game-derived, never committed).
cmake_minimum_required(VERSION 3.28)
project(windream_recomp C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(EMSCRIPTEN)
    # The browser build (build.py --web): every object is compiled for shared
    # memory, as the pthreads SDL3 build it links is.
    add_compile_options(-pthread)
endif()

# SDL3 (window, input, audio): a static build shared by every recomp build,
# made by recomp_env.py from the pin in recomp/render/cmake/Dependencies.cmake
# and found through CMAKE_PREFIX_PATH.
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
include(../render/cmake/Dependencies.cmake)
include(../render/cmake/Shaders.cmake)
include(../render/cmake/RenderCore.cmake)
# The disc library (recomp/disc): the host reads the game from the user's disc
# images (host/sdl/files.c, winmm.c). Its disc_list tool is not built here.
add_subdirectory(../disc ${CMAKE_BINARY_DIR}/disc EXCLUDE_FROM_ALL)
# The launcher (recomp/launcher): disc setup and port settings in a Dear ImGui
# window. main (host/core/runtime.c, under WD_WITH_LAUNCHER) runs it when the
# program is started with neither an EXE path nor WD_DISC1, and puts the WD_*
# pairs it returns into the environment; nothing else in the host knows it.
if(EMSCRIPTEN)
    set(_wd_launcher_default OFF)   # the page is the launcher; no disc mode in the browser
else()
    set(_wd_launcher_default ON)
endif()
option(WD_LAUNCHER "link the launcher and run it from main" ${_wd_launcher_default})
# The launcher's test-only script and environment variables (launcher/testing.h): on for
# development builds, never in a release, whatever the cache says (release.py checks).
if(WD_RELEASE OR EMSCRIPTEN)
    set(DREAMS_LAUNCHER_TESTING OFF CACHE BOOL "compile the launcher's test script and test-only environment variables" FORCE)
endif()
if(WD_LAUNCHER)
    add_subdirectory(../launcher ${CMAKE_BINARY_DIR}/launcher EXCLUDE_FROM_ALL)
endif()

# WD_GEN_DIR: lift.py + gen_imports.py output (the game's by default; a
# difftest program's own directory otherwise).
set(WD_GEN_DIR ${CMAKE_SOURCE_DIR}/../../out/recomp/windream/gen CACHE PATH "generated sources")
file(GLOB GEN_SRC CONFIGURE_DEPENDS ${WD_GEN_DIR}/recomp_*.c)
# host/: core (guest runtime), sdl (KERNEL32 files, process and threads,
# USER32, GDI32, WinMM and DirectSound on SDL3), vm (the guest's virtual
# memory), render (GPU renderer adapters), hooks (diagnostics).
#
# WD_VM picks the virtual-memory implementation (host/vm/vm_impl.h):
#   ledger  the default, portable: its own page bitmap over vm_os_*.c
#   win32   the original: Windows' page state is the truth (Windows only);
#           kept as the reference the ledger is compared with
#   shadow  both; win32 owns the memory, ledger answers the same questions and
#           every disagreement is reported (WD_VM_SHADOW=abort stops at the first)
set(WD_VM "ledger" CACHE STRING "virtual memory implementation: win32, ledger or shadow")
set_property(CACHE WD_VM PROPERTY STRINGS win32 ledger shadow)
# The same split picks the crash handler (host/core/crash_*.c) under the
# portable report in crash_report.c: minidump and dbghelp stack on Windows,
# signals and backtrace on Linux and macOS, nothing in the browser.
if(WIN32)
    set(VM_OS_SRC host/vm/vm_os_win32.c)
    set(CRASH_OS_SRC host/core/crash_win32.c)
elseif(EMSCRIPTEN)
    set(VM_OS_SRC host/vm/vm_os_wasm.c)
    set(CRASH_OS_SRC host/core/crash_none.c)
else()
    set(VM_OS_SRC host/vm/vm_os_posix.c)
    set(CRASH_OS_SRC host/core/crash_posix.c)
endif()
if(NOT WIN32 AND WD_VM MATCHES "^(win32|shadow)$")
    message(FATAL_ERROR "WD_VM=${WD_VM} needs host/vm/vm_win32.c, which is Windows only; use ledger")
endif()
set(VM_SRC host/vm/vm_front.c)
if(WD_VM STREQUAL "win32")
    list(APPEND VM_SRC host/vm/vm_win32.c)
elseif(WD_VM STREQUAL "ledger")
    list(APPEND VM_SRC host/vm/vm_ledger.c ${VM_OS_SRC})
elseif(WD_VM STREQUAL "shadow")
    list(APPEND VM_SRC host/vm/vm_shadow.c host/vm/vm_win32.c host/vm/vm_ledger.c host/vm/vm_os_null.c)
    set_source_files_properties(host/vm/vm_win32.c PROPERTIES COMPILE_DEFINITIONS "VM_PREFIX=vm_win32_")
    set_source_files_properties(host/vm/vm_ledger.c PROPERTIES COMPILE_DEFINITIONS "VM_PREFIX=vm_ledger_;VM_SECONDARY")
else()
    message(FATAL_ERROR "WD_VM must be win32, ledger or shadow (got '${WD_VM}')")
endif()
set(RUNTIME_SRC
    host/core/runtime.c host/core/crash_report.c ${CRASH_OS_SRC} host/core/recomp_trace.c
    ${WD_GEN_DIR}/imports_gen.c
    ${VM_SRC}
    host/sdl/threads.c host/sdl/kernel.c host/sdl/files.c
    host/sdl/user.c host/sdl/gdi.c host/sdl/pacing.c host/sdl/winmm.c host/sdl/dsound.c host/sdl/launch_mode.c
    host/sdl/editor_menu.c host/sdl/dev_keys.c host/sdl/dev_tools.c host/sdl/dev_console.c host/sdl/dev_save_page.c
    host/sdl/dev_overlay.c
    host/sdl/save_guard.c
    host/sdl/editor_bank.c host/sdl/editor_bank_rle.c host/sdl/editor_pickers.c
    host/hooks/phys_hook.c
    host/render/render_boundary.cpp host/render/render_scene.cpp host/render/render_scene_io.cpp
    host/render/render_scene_draw.cpp host/render/render_scene_probe.c host/render/render_ui.cpp
    host/render/render_live.cpp host/render/render_interp.cpp host/render/render_hooks.c host/render/render_metrics.cpp
    host/render/render_movie.c host/render/render_fatal.c)
if(WIN32)
    list(APPEND RUNTIME_SRC host/sdl/win32_abi_check.c)   # guest_win32.h against the SDK
elseif(EMSCRIPTEN)
    list(APPEND RUNTIME_SRC host/web/web_glue.c)          # the page's status callback and arguments
endif()

add_executable(windream_recomp ${RUNTIME_SRC} ${GEN_SRC} ${WD_GEN_DIR}/recomp_dispatch.c)
target_include_directories(windream_recomp PRIVATE
    host/core host/sdl host/vm host/render host/hooks ${WD_GEN_DIR})
if(WIN32)
    target_compile_definitions(windream_recomp PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
else()
    # clock_gettime (recomp_types.h) and the signal context's register names
    # (crash_posix.c), whatever C standard and include order a file has.
    target_compile_definitions(windream_recomp PRIVATE _GNU_SOURCE)
endif()
option(WD_TRACE "function-entry trace ring (RECOMP_TRACE)" OFF)
option(WD_RENDER_AUDIT "guest memory checks against registered GPU-owned surfaces" OFF)
if(WD_RENDER_AUDIT)
    target_compile_definitions(windream_recomp PRIVATE WD_RENDER_AUDIT)
endif()
if(WD_TRACE)
    target_compile_definitions(windream_recomp PRIVATE RECOMP_TRACE)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    # Lifted code is one C statement per x86 instruction: silence the noise
    # (unused labels, parentheses, volatile casts) and keep real errors.
    set_source_files_properties(${GEN_SRC} PROPERTIES COMPILE_OPTIONS "-w")
endif()
# The lifted x87 code is modelled with doubles and has to give the same results
# on every host: no fused multiply-add (arm64 compilers contract a*b+c by
# default). clang-cl ignores the plain spelling with a warning and needs it
# passed through /clang:; cl has no such option.
include(CheckCCompilerFlag)
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(WD_FP_CONTRACT_OFF /clang:-ffp-contract=off)
else()
    set(WD_FP_CONTRACT_OFF -ffp-contract=off)
endif()
check_c_compiler_flag(${WD_FP_CONTRACT_OFF} WD_HAS_FP_CONTRACT_OFF)
if(WD_HAS_FP_CONTRACT_OFF)
    target_compile_options(windream_recomp PRIVATE ${WD_FP_CONTRACT_OFF})
endif()
target_link_libraries(windream_recomp PRIVATE ODGraphics dreams_disc)
if(WD_LAUNCHER)
    target_link_libraries(windream_recomp PRIVATE dreams_launcher)
    target_compile_definitions(windream_recomp PRIVATE WD_WITH_LAUNCHER)
endif()
# WD_RELEASE (release.py): the executable a user gets. Named DreamsToReality,
# and on Windows a GUI-subsystem program (no console window; SDL_main.h in
# runtime.c supplies WinMain) whose stderr and stdout go to log.txt in the user
# data directory (WD_GUI_LOG). release.py also links the C runtime statically
# (CMAKE_MSVC_RUNTIME_LIBRARY), so the executable needs no redistributable.
option(WD_RELEASE "the release executable: DreamsToReality, no console on Windows" OFF)
if(WD_RELEASE)
    set_target_properties(windream_recomp PROPERTIES OUTPUT_NAME DreamsToReality)
    if(WIN32)
        set_target_properties(windream_recomp PROPERTIES WIN32_EXECUTABLE ON)
        target_compile_definitions(windream_recomp PRIVATE WD_GUI_LOG)
    endif()
endif()
# WD_VERSION (release.py: git describe): the build's name, the log's first
# line (runtime.c). Empty in a development build.
set(WD_VERSION "" CACHE STRING "build name for the log, e.g. v0.1.0; empty in development builds")
if(WD_VERSION)
    target_compile_definitions(windream_recomp PRIVATE "WD_VERSION=\"${WD_VERSION}\"")
endif()
# WD_DEVTOOLS: the development control channel (devtools/, enabled at run time
# with WD_CTL; recomp/README.md). The host calls it through devtools/devtools.h
# alone, whose functions are empty without the WD_DEVTOOLS definition, so
# leaving it out needs nothing else. A release never has it: WD_RELEASE forces
# it off whatever the cache says, and release.py checks the executable.
option(WD_DEVTOOLS "development control channel (WD_CTL); never in a release" ON)
if(WD_RELEASE OR EMSCRIPTEN)
    set(WD_DEVTOOLS OFF CACHE BOOL "development control channel (WD_CTL); never in a release" FORCE)
endif()
if(WD_DEVTOOLS)
    target_sources(windream_recomp PRIVATE devtools/devtools.c devtools/ctl_json.c devtools/ctl_net.c)
    target_compile_definitions(windream_recomp PRIVATE WD_DEVTOOLS)
    if(WIN32)
        target_link_libraries(windream_recomp PRIVATE ws2_32)
    endif()
endif()
if(EMSCRIPTEN)
    # Browser build. The guest runs on a pthread (PROXY_TO_PTHREAD: main is
    # started on a worker), so its blocking message loop, Sleep and waits never
    # block the page; SDL's canvas moves to that thread as an OffscreenCanvas.
    # The 400 MB guest arena is one malloc'd block (vm_os_wasm.c), hence memory
    # growth up to 2 GB; INITIAL_MEMORY covers the image, stacks and the usual heap.
    # No ASYNCIFY, no launcher, no disc mode: the page puts the game's files in
    # the Emscripten FS under /dreams (WD_INSTALL_ROOT).
    set(WD_WEB_INITIAL_MEMORY 256MB CACHE STRING "initial wasm memory of the browser build")
    set(WD_WEB_MAX_MEMORY 2GB CACHE STRING "maximum wasm memory of the browser build")
    set(WD_WEB_OPT -O1 CACHE STRING "optimization of the browser build's own and lifted code (-O0 does not load)")
    set(WD_WEB_POOL 8 CACHE STRING "pre-spawned pthread workers (guest threads + SDL audio and timers)")
    # The guest program in the install root: GDIDREAM.EXE for the retail game;
    # build.py --web-exe changes it with --web-gen (another lifted program).
    set(WD_WEB_EXE GDIDREAM.EXE CACHE STRING "name of the guest EXE in the install root of the browser build")
    # The frame relay (host/web/web_glue.c) posts the pthread runtime's own
    # "call a Module handler" message; its number is internal to the SDK, so it
    # is read from the SDK being used instead of being written down.
    set(_wd_libpthread ${EMSCRIPTEN_ROOT_PATH}/src/lib/libpthread.js)
    if(NOT EXISTS ${_wd_libpthread})
        message(FATAL_ERROR "cannot find ${_wd_libpthread}: the frame relay needs its CMD_CALL_HANDLER")
    endif()
    file(STRINGS ${_wd_libpthread} _wd_call_handler REGEX "^const CMD_CALL_HANDLER = [0-9]+")
    if(NOT _wd_call_handler MATCHES "CMD_CALL_HANDLER = ([0-9]+)")
        message(FATAL_ERROR "no CMD_CALL_HANDLER in ${_wd_libpthread}: the SDK changed, see host/web/web_glue.c")
    endif()
    set(WD_WEB_CALL_HANDLER ${CMAKE_MATCH_1})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_wd_libpthread})
    target_compile_definitions(windream_recomp PRIVATE WD_WEB WD_WEB_EXE_NAME="${WD_WEB_EXE}"
        WD_WEB_CALL_HANDLER=${WD_WEB_CALL_HANDLER})
    # Frame capture for verify/render_web_capture.py (host/web/web_glue.c): the
    # page can ask for a frame's scene, pixels and guest memory. A test aid;
    # turn it off for a public build.
    option(WD_WEB_CAPTURE "browser build: frame capture for the verification tools" ON)
    if(WD_WEB_CAPTURE)
        target_compile_definitions(windream_recomp PRIVATE WD_WEB_CAPTURE)
    endif()
    set_target_properties(windream_recomp PROPERTIES OUTPUT_NAME dreams SUFFIX .js LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/host/web/pre.js)
    target_link_options(windream_recomp PRIVATE
        -pthread -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT
        --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/host/web/pre.js -Wl,--wrap=SDL_GL_SwapWindow -lidbfs.js
        -sMODULARIZE=1 -sEXPORT_NAME=createDreams -sENVIRONMENT=web,worker,node
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=${WD_WEB_INITIAL_MEMORY} -sMAXIMUM_MEMORY=${WD_WEB_MAX_MEMORY}
        -sSTACK_SIZE=1MB -sDEFAULT_PTHREAD_STACK_SIZE=1MB -sPTHREAD_POOL_SIZE=${WD_WEB_POOL}
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
        -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -sINVOKE_RUN=1
        -sEXPORTED_RUNTIME_METHODS=FS,ENV,GL,addRunDependency,removeRunDependency,callMain,ccall,cwrap
        -sEXPORTED_FUNCTIONS=_main,_malloc,_free)
elseif(WIN32)
    target_link_libraries(windream_recomp PRIVATE dbghelp)
else()
    # SDL3's static target brings its own m, dl and pthread; the host uses
    # them directly too (libm in recomp_types.h, dladdr in crash_posix.c).
    find_package(Threads REQUIRED)
    target_link_libraries(windream_recomp PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(NOT APPLE)
        target_link_libraries(windream_recomp PRIVATE m)
    endif()
    # Export the executable's symbols (-rdynamic) so backtrace_symbols_fd in
    # crash_posix.c can name the lifted functions.
    set_target_properties(windream_recomp PROPERTIES ENABLE_EXPORTS ON)
endif()
# Line tables + PDB in every config: the crash report walks the host stack and
# maps each lifted frame back to its guest instruction (the generated line's
# comment). Line tables only: full debug info of 700k generated lines is slow.
# By front end, not compiler: clang-cl is Clang with the MSVC command line.
if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(windream_recomp PRIVATE /Z7 -gline-tables-only)
        target_link_options(windream_recomp PRIVATE /DEBUG)
    endif()
elseif(NOT EMSCRIPTEN)
    target_compile_options(windream_recomp PRIVATE -g1)
endif()
# One unoptimized development build (build.py leaves CMAKE_BUILD_TYPE empty).
# Optimizing buys nothing while we debug and dominates the build: sub_0044E9B0
# alone is 27k generated lines, 90 s to compile at /O2, 68 s at /O1, 4 s at /Od.
if(NOT CMAKE_BUILD_TYPE)
    if(EMSCRIPTEN)
        # -O0 leaves one wasm local per temporary: the biggest lifted functions
        # exceed the engines' limit of 50000 locals ("local count too large").
        target_compile_options(windream_recomp PRIVATE ${WD_WEB_OPT})
        # Function names in the wasm, so a browser stack trace names sub_XXXXXXXX
        # and host functions instead of wasm-function[1177].
        target_link_options(windream_recomp PRIVATE --profiling-funcs)
    elseif(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_compile_options(windream_recomp PRIVATE /Od)
        endif()
    else()
        target_compile_options(windream_recomp PRIVATE -O0)
    endif()
    # The direct renderer's hand-written host code and its library are
    # optimized all the same: they read each frame's scene from guest memory
    # and build its GPU work, about 16 ms of a 33 ms frame unoptimized, the
    # time display interpolation (WD_INTERPOLATE) draws its extra frames in.
    # Source options follow the target's on the command line, so /O2 wins.
    if(NOT EMSCRIPTEN)
        set(WD_RENDER_HOST_SRC host/render/render_scene.cpp host/render/render_scene_draw.cpp
            host/render/render_interp.cpp host/render/render_live.cpp host/render/render_ui.cpp
            host/render/render_boundary.cpp host/render/render_scene_probe.c)
        if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            set_source_files_properties(${WD_RENDER_HOST_SRC} PROPERTIES COMPILE_OPTIONS /O2)
            target_compile_options(ODRender PRIVATE /O2)
        else()
            set_source_files_properties(${WD_RENDER_HOST_SRC} PROPERTIES COMPILE_OPTIONS -O2)
            target_compile_options(ODRender PRIVATE -O2)
        endif()
    endif()
endif()
