# WINDREAM static recompilation (feasibility build; build.py configures it).
# Generated sources come from lift.py and gen_imports.py and stay under out/
# (game-derived, never committed).
cmake_minimum_required(VERSION 3.20)
project(windream_recomp C)

# SDL3 (window, input, audio): a static build shared by every recomp build,
# made by recomp_env.py from the pin in opendreams/cmake/Dependencies.cmake
# and found through CMAKE_PREFIX_PATH.
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)

# WD_GEN_DIR: lift.py + gen_imports.py output (the game's by default; a
# difftest program's own directory otherwise).
set(WD_GEN_DIR ${CMAKE_SOURCE_DIR}/../../out/recomp/windream/gen CACHE PATH "generated sources")
file(GLOB GEN_SRC CONFIGURE_DEPENDS ${WD_GEN_DIR}/recomp_*.c)
set(RUNTIME_SRC
    runtime/runtime.c runtime/vm.c runtime/threads.c runtime/kernel.c
    runtime/files.c ${WD_GEN_DIR}/imports_gen.c
    runtime/user.c runtime/gdi.c runtime/winmm.c runtime/dsound.c
    runtime/crash_report.c runtime/recomp_trace.c runtime/phys_hook.c)
if(WIN32)
    list(APPEND RUNTIME_SRC runtime/win32_abi_check.c)   # guest_win32.h against the SDK
endif()

add_executable(windream_recomp ${RUNTIME_SRC} ${GEN_SRC} ${WD_GEN_DIR}/recomp_dispatch.c)
target_include_directories(windream_recomp PRIVATE runtime ${WD_GEN_DIR})
target_compile_definitions(windream_recomp PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN)
option(WD_TRACE "function-entry trace ring (RECOMP_TRACE)" OFF)
if(WD_TRACE)
    target_compile_definitions(windream_recomp PRIVATE RECOMP_TRACE)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    # Lifted code is one C statement per x86 instruction: silence the noise
    # (unused labels, parentheses, volatile casts) and keep real errors.
    set_source_files_properties(${GEN_SRC} PROPERTIES COMPILE_OPTIONS "-w")
endif()
target_link_libraries(windream_recomp PRIVATE SDL3::SDL3-static dbghelp)
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
