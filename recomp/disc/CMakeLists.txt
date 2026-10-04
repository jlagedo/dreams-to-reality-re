# Disc library: reads a game disc (.cue, .iso or an extracted directory) without
# a drive. Plain C99 and libc, no other dependencies.
#
# Standalone:  uv run python recomp/disc/build.py
# From another project:  add_subdirectory(<path>/disc)  and link dreams_disc
# (its include directory, which holds disc.h, is public). The disc_list tool is
# built only when this is the top-level project, or with -DDREAMS_DISC_TOOLS=ON.
cmake_minimum_required(VERSION 3.16)
project(dreams_disc LANGUAGES C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_disc_top ON)
else()
    set(_disc_top OFF)
endif()
option(DREAMS_DISC_TOOLS "Build the disc_list tool" ${_disc_top})

add_library(dreams_disc STATIC cue.c image.c merge.c plat.c sha256.c)
target_include_directories(dreams_disc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(dreams_disc PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
if(MSVC)
    target_compile_definitions(dreams_disc PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(dreams_disc PRIVATE /W3)
else()
    target_compile_options(dreams_disc PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(DREAMS_DISC_TOOLS)
    add_executable(disc_list tools/disc_list.c)
    target_link_libraries(disc_list PRIVATE dreams_disc)
    set_target_properties(disc_list PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(WIN32)
        target_link_libraries(disc_list PRIVATE shell32)
        target_compile_definitions(disc_list PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()
