# Render parity: one program that draws captured scene inputs with the
# production scene adapter and renderer, built natively (D3D11 on Windows) and
# for the browser (WebGL2). Driven by recomp/windream/verify/render_parity.py.
cmake_minimum_required(VERSION 3.28)
project(wd_render_parity C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(DREAMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(WD_HOST "${DREAMS_ROOT}/recomp/windream/host")
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
include("${DREAMS_ROOT}/recomp/render/cmake/Dependencies.cmake")
include("${DREAMS_ROOT}/recomp/render/cmake/Shaders.cmake")
include("${DREAMS_ROOT}/recomp/render/cmake/RenderCore.cmake")

add_executable(wd_render_parity
    "${CMAKE_CURRENT_SOURCE_DIR}/../render_parity.cpp"
    "${WD_HOST}/render/render_scene_draw.cpp"
    "${WD_HOST}/render/render_scene.cpp"
    "${WD_HOST}/render/render_scene_io.cpp")
target_include_directories(wd_render_parity PRIVATE "${WD_HOST}/core" "${WD_HOST}/sdl"
    "${WD_HOST}/render" "${WD_HOST}/hooks")
target_link_libraries(wd_render_parity PRIVATE ODGraphics)

if(EMSCRIPTEN)
    # The memory images are a few hundred megabytes in the file system at most
    # one at a time, hence the growth limit. -pthread and the WebGL2 flags come
    # from ODGraphics (RenderCore.cmake).
    target_link_options(wd_render_parity PRIVATE
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=4GB
        -sPTHREAD_POOL_SIZE=1 -sEXIT_RUNTIME=0 -sFORCE_FILESYSTEM=1
        -sSTACK_SIZE=1MB   # as the game build (recomp/windream/CMakeLists.txt)
        -sEXPORTED_RUNTIME_METHODS=FS,ccall,UTF8ToString
        -sEXPORTED_FUNCTIONS=_main,_wd_parity_case,_wd_parity_error
        --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
    set_target_properties(wd_render_parity PROPERTIES SUFFIX .html)
    set_property(TARGET wd_render_parity APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
endif()
