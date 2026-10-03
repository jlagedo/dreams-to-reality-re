# Standalone checks of the GPU renderer outside the game: the library's own
# test scenes in a window (native) or in a page (Emscripten, WebGL2).
#
#   Browser (see recomp/web/NOTES-gl.md):
#     . ./recomp/web-env.ps1
#     emcmake cmake -S recomp/render/tests/web -B out/recomp/render-web/build -G Ninja
#         -DSDL3_DIR="$env:SDL3_DIR" -DOD_SHDC_EXECUTABLE="$env:DREAMS_SHDC"
#     cmake --build out/recomp/render-web/build
#   Run them with recomp/render/tests/web/run_browser.py.
cmake_minimum_required(VERSION 3.28)
project(od_render_web_checks C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(DREAMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
include("${DREAMS_ROOT}/recomp/render/cmake/Dependencies.cmake")
include("${DREAMS_ROOT}/recomp/render/cmake/Shaders.cmake")
include("${DREAMS_ROOT}/recomp/render/cmake/RenderCore.cmake")

add_executable(od_web_frame web_frame.cpp)
target_link_libraries(od_web_frame PRIVATE ODGraphics)
add_executable(od_gpu_tests "${DREAMS_ROOT}/recomp/render/tests/direct_gpu_tests.cpp")
target_link_libraries(od_gpu_tests PRIVATE ODGraphics)
target_compile_definitions(od_gpu_tests PRIVATE OD_TEST_SDL)

if(EMSCRIPTEN)
    # -pthread and the WebGL2 version flags come from ODGraphics itself
    # (RenderCore.cmake); these are the program's own.
    set(OD_WEB_LINK_FLAGS -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=2GB
        -sPTHREAD_POOL_SIZE=1 -sEXIT_RUNTIME=0
        -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS
        --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    # od_web_frame_mt: the same program with main running on a pthread and the
    # canvas transferred to it (what the game build will do).
    foreach(target od_web_frame od_gpu_tests)
        target_link_options(${target} PRIVATE ${OD_WEB_LINK_FLAGS})
        set_target_properties(${target} PROPERTIES SUFFIX .html)
    endforeach()
    add_executable(od_web_frame_mt web_frame.cpp)
    target_link_libraries(od_web_frame_mt PRIVATE ODGraphics)
    target_link_options(od_web_frame_mt PRIVATE ${OD_WEB_LINK_FLAGS}
        -sPROXY_TO_PTHREAD -sOFFSCREENCANVAS_SUPPORT)
    set_target_properties(od_web_frame_mt PROPERTIES SUFFIX .html)
    # The same program with a blocking present-and-sleep loop, the shape of the
    # game's own, in the ways a pthread can reach the canvas (see
    # recomp/web/NOTES-gl.md "Threading"):
    #   blocking         OffscreenCanvas transferred to the thread, no yielding
    #   blocking_proxied no transfer: the context lives on the main thread and
    #                    every GL call is proxied to it
    #   blocking_jspi    OffscreenCanvas plus JSPI, so SDL_GL_SwapWindow yields
    function(od_blocking_variant name)
        add_executable(${name} web_frame.cpp)
        target_link_libraries(${name} PRIVATE ODGraphics)
        target_compile_definitions(${name} PRIVATE OD_WEB_BLOCKING_LOOP)
        target_link_options(${name} PRIVATE ${OD_WEB_LINK_FLAGS} -sPROXY_TO_PTHREAD ${ARGN})
        set_target_properties(${name} PROPERTIES SUFFIX .html)
    endfunction()
    od_blocking_variant(od_web_frame_blocking -sOFFSCREENCANVAS_SUPPORT)
    od_blocking_variant(od_web_frame_blocking_proxied)
    od_blocking_variant(od_web_frame_blocking_jspi ${OD_WEB_PTHREAD_CANVAS_LINK_OPTIONS})
endif()
