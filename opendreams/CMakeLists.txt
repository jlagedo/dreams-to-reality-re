cmake_minimum_required(VERSION 3.28)
project(OpenDreams VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
if(MSVC)
    # CMake injects /EHsc globally. Our targets disable exceptions; restore the
    # default only for third-party C++ translation units below.
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
    # sokol's Metal implementation contains Objective-C messages.
    set_source_files_properties(shared/render/sokol_impl.cpp PROPERTIES LANGUAGE OBJCXX)
endif()

include(CTest)
include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

function(od_project_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /GR- /EHs- /EHc- /fp:precise)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti -fno-fast-math -ffp-contract=off)
    endif()
endfunction()

add_library(ODImGui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
)
target_include_directories(ODImGui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(ODImGui PUBLIC SDL3::SDL3-static)
if(MSVC)
    target_compile_options(ODImGui PRIVATE /EHsc)
    target_compile_options(SDL3-static PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
endif()

set(OD_BACKEND_SOURCE "")
if(EMSCRIPTEN)
    set(OD_BACKEND_SOURCE shared/platform/graphics_gl.cpp)
    set(OD_SOKOL_BACKEND SOKOL_GLES3)
elseif(WIN32)
    set(OD_BACKEND_SOURCE shared/platform/graphics_d3d11.cpp)
    set(OD_SOKOL_BACKEND SOKOL_D3D11)
elseif(APPLE)
    set(OD_BACKEND_SOURCE shared/platform/graphics_metal.mm)
    set(OD_SOKOL_BACKEND SOKOL_METAL)
elseif(UNIX)
    set(OD_BACKEND_SOURCE shared/platform/graphics_gl.cpp)
    set(OD_SOKOL_BACKEND SOKOL_GLCORE)
else()
    message(FATAL_ERROR "OpenDreams has no graphics backend for this target")
endif()

od_generate_shader(OD_BACKGROUND_SHADER shared/render/background.glsl)

add_library(ODShared STATIC
    shared/shell.cpp
    shared/render/background.cpp
    shared/render/sokol_impl.cpp
    ${OD_BACKEND_SOURCE}
    ${OD_BACKGROUND_SHADER}
)
target_include_directories(ODShared
    PUBLIC shared
    PRIVATE ${sokol_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_definitions(ODShared PRIVATE ${OD_SOKOL_BACKEND} SOKOL_IMGUI_NO_SOKOL_APP)
target_link_libraries(ODShared PUBLIC SDL3::SDL3-static ODImGui)
od_project_options(ODShared)

if(WIN32)
    target_link_libraries(ODShared PRIVATE d3d11 dxgi)
elseif(APPLE)
    target_link_libraries(ODShared PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
elseif(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    target_link_libraries(ODShared PRIVATE OpenGL::GL)
endif()

foreach(app IN ITEMS ODViewer ODRuntime)
    if(app STREQUAL "ODViewer")
        set(app_source apps/viewer/main.cpp)
    else()
        set(app_source apps/runtime/main.cpp)
    endif()
    add_executable(${app} ${app_source})
    target_link_libraries(${app} PRIVATE ODShared)
    target_include_directories(${app} PRIVATE ${imgui_SOURCE_DIR})
    od_project_options(${app})
    if(EMSCRIPTEN)
        set_target_properties(${app} PROPERTIES SUFFIX ".html")
        target_link_options(${app} PRIVATE "-sUSE_WEBGL2=1" "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2" "-sALLOW_MEMORY_GROWTH=1")
    endif()
endforeach()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
    option(OD_ENABLE_GRAPHICS_TESTS "Register display-dependent finite-frame smoke tests" OFF)
    if(OD_ENABLE_GRAPHICS_TESTS)
        add_test(NAME ODViewerGraphics COMMAND ODViewer --frames 10)
        add_test(NAME ODRuntimeGraphics COMMAND ODRuntime --frames 10)
        set_tests_properties(ODViewerGraphics ODRuntimeGraphics PROPERTIES LABELS graphics TIMEOUT 30)
    endif()
endif()
