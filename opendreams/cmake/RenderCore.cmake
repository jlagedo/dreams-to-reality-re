# Included by OpenDreams and the recomp. No loaders, shell or ImGui here.
get_filename_component(OD_RENDER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(EMSCRIPTEN)
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/shared/platform/graphics_gl.cpp")
    set(OD_SOKOL_BACKEND SOKOL_GLES3)
elseif(WIN32)
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/shared/platform/graphics_d3d11.cpp")
    set(OD_SOKOL_BACKEND SOKOL_D3D11)
elseif(APPLE)
    enable_language(OBJCXX)
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/shared/platform/graphics_metal.mm")
    set(OD_SOKOL_BACKEND SOKOL_METAL)
    set_source_files_properties("${OD_RENDER_ROOT}/shared/render/sokol_impl.cpp"
        PROPERTIES LANGUAGE OBJCXX)
else()
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/shared/platform/graphics_gl.cpp")
    set(OD_SOKOL_BACKEND SOKOL_GLCORE)
endif()

od_generate_shader(OD_DIRECT_SHADER "${OD_RENDER_ROOT}/shared/render/direct.glsl")
add_library(ODRender STATIC
    "${OD_RENDER_ROOT}/shared/port/fog.cpp"
    "${OD_RENDER_ROOT}/shared/render/sokol_impl.cpp"
    "${OD_RENDER_ROOT}/shared/render/direct.cpp"
    "${OD_RENDER_ROOT}/shared/render/direct_math.cpp"
    ${OD_DIRECT_SHADER}
)
target_include_directories(ODRender PUBLIC "${OD_RENDER_ROOT}/shared" "${sokol_SOURCE_DIR}")
target_include_directories(ODRender PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(ODRender PUBLIC cxx_std_17)
target_compile_definitions(ODRender PRIVATE ${OD_SOKOL_BACKEND})

add_library(ODGraphics STATIC ${OD_BACKEND_SOURCE})
target_link_libraries(ODGraphics PUBLIC ODRender SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(ODGraphics PRIVATE d3d11 dxgi)
elseif(APPLE)
    target_link_libraries(ODGraphics PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
elseif(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    target_link_libraries(ODGraphics PRIVATE OpenGL::GL)
endif()
