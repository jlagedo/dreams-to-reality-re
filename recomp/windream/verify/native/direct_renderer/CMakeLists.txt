cmake_minimum_required(VERSION 3.28)
project(DirectRendererValidation LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(DREAMS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(WD_HOST "${DREAMS_ROOT}/recomp/windream/host")
set(WD_NATIVE "${DREAMS_ROOT}/recomp/windream/verify/native")
set(WD_HOST_INCLUDES "${WD_HOST}/core" "${WD_HOST}/sdl" "${WD_HOST}/win32"
    "${WD_HOST}/render" "${WD_HOST}/hooks")
find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3-static)
include("${DREAMS_ROOT}/recomp/render/cmake/Dependencies.cmake")
include("${DREAMS_ROOT}/recomp/render/cmake/Shaders.cmake")
include("${DREAMS_ROOT}/recomp/render/cmake/RenderCore.cmake")
add_library(WDSceneAdapter STATIC
    "${WD_HOST}/render/render_scene_draw.cpp"
    "${WD_HOST}/render/render_scene.cpp"
    "${WD_HOST}/render/render_scene_io.cpp")
target_link_libraries(WDSceneAdapter PUBLIC ODRender)
target_include_directories(WDSceneAdapter PUBLIC ${WD_HOST_INCLUDES})
add_library(WDSceneAdapterOracle SHARED
    "${WD_HOST}/render/render_scene_draw.cpp"
    "${WD_NATIVE}/render_light_oracle.cpp"
    "${WD_NATIVE}/render_fog_oracle.cpp"
    "${WD_HOST}/render/render_scene.cpp"
    "${WD_HOST}/render/render_scene_io.cpp")
target_link_libraries(WDSceneAdapterOracle PRIVATE ODRender)
target_include_directories(WDSceneAdapterOracle PRIVATE ${WD_HOST_INCLUDES})
set_target_properties(WDSceneAdapterOracle PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_library(WDUiAdapterOracle SHARED
    "${WD_HOST}/render/render_ui.cpp"
    "${WD_NATIVE}/render_ui_oracle.cpp")
target_link_libraries(WDUiAdapterOracle PRIVATE ODRender)
target_include_directories(WDUiAdapterOracle PRIVATE ${WD_HOST_INCLUDES})
set_target_properties(WDUiAdapterOracle PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
add_executable(ODDirectMathTests "${DREAMS_ROOT}/recomp/render/tests/direct_math_tests.cpp")
target_link_libraries(ODDirectMathTests PRIVATE ODRender)
add_executable(WDRenderBoundaryTests
    "${WD_NATIVE}/render_boundary_tests.cpp"
    "${WD_HOST}/render/render_boundary.cpp"
    "${WD_HOST}/render/render_fatal.c")
target_include_directories(WDRenderBoundaryTests PRIVATE ${WD_HOST_INCLUDES})
if(WIN32)
    add_executable(WDRenderMetricsTests "${WD_NATIVE}/render_metrics_tests.cpp")
    target_link_libraries(WDRenderMetricsTests PRIVATE d3d11)
    add_executable(WDSceneModeTests "${WD_NATIVE}/render_scene_mode_tests.cpp")
    target_link_libraries(WDSceneModeTests PRIVATE WDSceneAdapter ODGraphics)
    add_executable(ODDirectExportTests "${DREAMS_ROOT}/recomp/render/tests/direct_export_tests.cpp")
    target_link_libraries(ODDirectExportTests PRIVATE ODGraphics)
    add_executable(WDSceneGpuTests "${WD_NATIVE}/render_scene_gpu.cpp")
    target_link_libraries(WDSceneGpuTests PRIVATE WDSceneAdapter SDL3::SDL3-static d3d11)
    add_executable(ODDirectGpuTests "${DREAMS_ROOT}/recomp/render/tests/direct_gpu_tests.cpp")
    target_link_libraries(ODDirectGpuTests PRIVATE ODRender d3d11)
endif()
enable_testing()
add_test(NAME ODDirectMath COMMAND ODDirectMathTests)
add_test(NAME WDRenderBoundary COMMAND WDRenderBoundaryTests)
