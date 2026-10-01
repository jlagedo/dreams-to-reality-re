# Launcher library: disc setup and port settings in a Dear ImGui window, with one
# C entry point (launcher.h). It depends on SDL3, Dear ImGui and the disc library
# only, and includes nothing from recomp/windream or recomp/render.
#
# Standalone:  uv run python recomp/launcher/build.py   (builds launcher_demo too)
# From another project, two lines:
#     add_subdirectory(<path>/launcher launcher)      # also adds ../disc unless dreams_disc exists
#     target_link_libraries(<exe> PRIVATE dreams_launcher)
# SDL3 comes from the parent (a target SDL3::SDL3-static, or SDL3::SDL3) or, failing that,
# from find_package(SDL3 CONFIG) with CMAKE_PREFIX_PATH pointing at the shared static build
# (recomp_env.ensure_sdl3()); this project never builds SDL itself.
cmake_minimum_required(VERSION 3.24)
project(dreams_launcher LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_launcher_top ON)
else()
    set(_launcher_top OFF)
endif()
option(DREAMS_LAUNCHER_DEMO "Build the launcher_demo executable" ${_launcher_top})
# Test-only machinery (testing.h): the DREAMS_LAUNCHER_SCRIPT driver (testscript.cpp) and the
# test-only environment variables HOME, SHOT, SHOT_TAB, VERBOSE named in launcher.h. ON for
# development and for tests/recomp/test_launcher*.py; the game's release build forces it OFF
# (recomp/windream/CMakeLists.txt, WD_RELEASE) and release.py checks the executable for the text
# "launcher-testing" that the compiled-in code carries.
option(DREAMS_LAUNCHER_TESTING "compile the launcher's test script and test-only environment variables" ON)

if(NOT TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3)
    find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
endif()
if(TARGET SDL3::SDL3-static)
    set(_launcher_sdl SDL3::SDL3-static)
else()
    set(_launcher_sdl SDL3::SDL3)
endif()

if(NOT TARGET dreams_disc)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../disc ${CMAKE_CURRENT_BINARY_DIR}/disc)
endif()

# Dear ImGui v1.92.9b, pinned by commit like SDL3 and sokol in
# recomp/render/cmake/Dependencies.cmake. It has no CMakeLists of its own.
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/f1cc2ae15e53a861a874c3034aae6798fde194ab
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _launcher_no_cmake
)
FetchContent_MakeAvailable(imgui)

set(_imgui_sources
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
# Third-party code: no warnings of ours.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set_source_files_properties(${_imgui_sources} PROPERTIES COMPILE_OPTIONS /w)
else()
    set_source_files_properties(${_imgui_sources} PROPERTIES COMPILE_OPTIONS -w)
endif()

add_library(dreams_launcher STATIC
    launcher.cpp ini.cpp settings.cpp validate.cpp ui.cpp
    ${_imgui_sources}
)
if(DREAMS_LAUNCHER_TESTING)
    target_sources(dreams_launcher PRIVATE testscript.cpp)
    target_compile_definitions(dreams_launcher PRIVATE DREAMS_LAUNCHER_TESTING)
endif()
target_include_directories(dreams_launcher PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(dreams_launcher PRIVATE ${imgui_SOURCE_DIR})
target_compile_features(dreams_launcher PRIVATE cxx_std_17)
target_link_libraries(dreams_launcher PRIVATE dreams_disc ${_launcher_sdl})
if(MSVC)
    target_compile_definitions(dreams_launcher PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(DREAMS_LAUNCHER_DEMO)
    add_executable(launcher_demo demo/launcher_demo.c)
    target_link_libraries(launcher_demo PRIVATE dreams_launcher ${_launcher_sdl})
    set_target_properties(launcher_demo PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
    if(MSVC)
        target_compile_definitions(launcher_demo PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()
