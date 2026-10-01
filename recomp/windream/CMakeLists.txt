# WINDREAM static recompilation (feasibility build; build.py configures it).
# Generated sources come from lift.py and gen_imports.py and stay under out/
# (game-derived, never committed).
cmake_minimum_required(VERSION 3.28)
project(windream_recomp C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3 (window, input, audio): a static build shared by every recomp build,
# made by recomp_env.py from the pin in recomp/render/cmake/Dependencies.cmake
# and found through CMAKE_PREFIX_PATH.
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
include(../render/cmake/Dependencies.cmake)
include(../render/cmake/Shaders.cmake)
include(../render/cmake/RenderCore.cmake)

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
if(WIN32)
    set(VM_OS_SRC host/vm/vm_os_win32.c)
elseif(EMSCRIPTEN)
    set(VM_OS_SRC host/vm/vm_os_wasm.c)
else()
    set(VM_OS_SRC host/vm/vm_os_posix.c)
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
    host/core/runtime.c host/core/crash_report.c host/core/recomp_trace.c
    ${WD_GEN_DIR}/imports_gen.c
    ${VM_SRC}
    host/sdl/threads.c host/sdl/kernel.c host/sdl/files.c
    host/sdl/user.c host/sdl/gdi.c host/sdl/winmm.c host/sdl/dsound.c
    host/hooks/phys_hook.c
    host/render/render_boundary.cpp host/render/render_scene.cpp host/render/render_scene_io.cpp
    host/render/render_scene_draw.cpp host/render/render_scene_probe.c host/render/render_ui.cpp
    host/render/render_live.cpp host/render/render_hooks.c host/render/render_metrics.cpp
    host/render/render_movie.c)
if(WIN32)
    list(APPEND RUNTIME_SRC host/sdl/win32_abi_check.c)   # guest_win32.h against the SDK
endif()

add_executable(windream_recomp ${RUNTIME_SRC} ${GEN_SRC} ${WD_GEN_DIR}/recomp_dispatch.c)
target_include_directories(windream_recomp PRIVATE
    host/core host/sdl host/vm host/render host/hooks ${WD_GEN_DIR})
target_compile_definitions(windream_recomp PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
option(WD_TRACE "function-entry trace ring (RECOMP_TRACE)" OFF)
option(WD_RENDER_AUDIT "guest memory checks against registered GPU-owned surfaces" OFF)
if(WD_RENDER_AUDIT)
    target_compile_definitions(windream_recomp PRIVATE WD_RENDER_AUDIT)
endif()
if(WD_TRACE)
    target_compile_definitions(windream_recomp PRIVATE RECOMP_TRACE)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Lifted code is one C statement per x86 instruction: silence the noise
    # (unused labels, parentheses, volatile casts) and keep real errors.
    set_source_files_properties(${GEN_SRC} PROPERTIES COMPILE_OPTIONS "-w")
endif()
target_link_libraries(windream_recomp PRIVATE ODGraphics dbghelp)
# Line tables + PDB in every config: the crash report walks the host stack and
# maps each lifted frame back to its guest instruction (the generated line's
# comment). Line tables only: full debug info of 700k generated lines is slow.
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(windream_recomp PRIVATE /Z7 -gline-tables-only)
    target_link_options(windream_recomp PRIVATE /DEBUG)
endif()
# One unoptimized development build (build.py leaves CMAKE_BUILD_TYPE empty).
# Optimizing buys nothing while we debug and dominates the build: sub_0044E9B0
# alone is 27k generated lines, 90 s to compile at /O2, 68 s at /O1, 4 s at /Od.
if(NOT CMAKE_BUILD_TYPE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(windream_recomp PRIVATE /Od)
endif()
