cmake_minimum_required(VERSION 3.28)
project(DirectRendererValidation LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(DREAMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
set(OD_RENDER_ONLY ON)
include("${DREAMS_ROOT}/opendreams/cmake/Dependencies.cmake")
include("${DREAMS_ROOT}/opendreams/cmake/Shaders.cmake")
include("${DREAMS_ROOT}/opendreams/cmake/RenderCore.cmake")
add_library(WDSceneAdapter STATIC
    "${DREAMS_ROOT}/recomp/windream/runtime/render_scene_draw.cpp"
    "${DREAMS_ROOT}/recomp/windream/runtime/render_scene.cpp"
    "${DREAMS_ROOT}/recomp/windream/runtime/render_scene_io.cpp")
target_link_libraries(WDSceneAdapter PUBLIC ODRender)
target_include_directories(WDSceneAdapter PUBLIC "${DREAMS_ROOT}/recomp/windream/runtime")
add_library(WDSceneAdapterOracle SHARED
    "${DREAMS_ROOT}/recomp/windream/runtime/render_scene_draw.cpp"
    "${DREAMS_ROOT}/recomp/windream/debug/render_light_oracle.cpp"
    "${DREAMS_ROOT}/recomp/windream/debug/render_fog_oracle.cpp"
    "${DREAMS_ROOT}/recomp/windream/runtime/render_scene.cpp"
    "${DREAMS_ROOT}/recomp/windream/runtime/render_scene_io.cpp")
target_link_libraries(WDSceneAdapterOracle PRIVATE ODRender)
target_include_directories(WDSceneAdapterOracle PRIVATE "${DREAMS_ROOT}/recomp/windream/runtime")
set_target_properties(WDSceneAdapterOracle PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(WDUiAdapterOracle SHARED
    "${DREAMS_ROOT}/recomp/windream/runtime/render_ui.cpp"
    "${DREAMS_ROOT}/recomp/windream/debug/render_ui_oracle.cpp")
target_link_libraries(WDUiAdapterOracle PRIVATE ODRender)
target_include_directories(WDUiAdapterOracle PRIVATE "${DREAMS_ROOT}/recomp/windream/runtime")
set_target_properties(WDUiAdapterOracle PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_executable(ODDirectMathTests "${DREAMS_ROOT}/opendreams/tests/direct_math_tests.cpp")
target_link_libraries(ODDirectMathTests PRIVATE ODRender)
add_executable(WDRenderBoundaryTests
    "${DREAMS_ROOT}/recomp/windream/debug/render_boundary_tests.cpp"
    "${DREAMS_ROOT}/recomp/windream/runtime/render_boundary.cpp")
target_include_directories(WDRenderBoundaryTests PRIVATE "${DREAMS_ROOT}/recomp/windream/runtime")
if(WIN32)
    add_executable(WDSceneModeTests "${DREAMS_ROOT}/recomp/windream/debug/render_scene_mode_tests.cpp")
    target_link_libraries(WDSceneModeTests PRIVATE WDSceneAdapter ODGraphics)
    add_executable(ODDirectExportTests "${DREAMS_ROOT}/opendreams/tests/direct_export_tests.cpp")
    target_link_libraries(ODDirectExportTests PRIVATE ODGraphics)
    add_executable(WDSceneGpuTests "${DREAMS_ROOT}/recomp/windream/debug/render_scene_gpu.cpp")
    target_link_libraries(WDSceneGpuTests PRIVATE WDSceneAdapter SDL3::SDL3-static d3d11)
    add_executable(ODDirectGpuTests "${DREAMS_ROOT}/opendreams/tests/direct_gpu_tests.cpp")
    target_link_libraries(ODDirectGpuTests PRIVATE ODRender d3d11)
endif()
enable_testing()
add_test(NAME ODDirectMath COMMAND ODDirectMathTests)
add_test(NAME WDRenderBoundary COMMAND WDRenderBoundaryTests)
