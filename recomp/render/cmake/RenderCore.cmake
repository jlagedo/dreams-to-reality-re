# The recomp's GPU renderer: ODRender (sokol_gfx core) and ODGraphics (SDL3 backend).
get_filename_component(OD_RENDER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(EMSCRIPTEN)
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/graphics_gl.cpp")
    set(OD_SOKOL_BACKEND SOKOL_GLES3)
elseif(WIN32)
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/graphics_d3d11.cpp")
    set(OD_SOKOL_BACKEND SOKOL_D3D11)
elseif(APPLE)
    enable_language(OBJCXX)
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/graphics_metal.mm")
    set(OD_SOKOL_BACKEND SOKOL_METAL)
    set_source_files_properties("${OD_RENDER_ROOT}/sokol_impl.cpp"
        PROPERTIES LANGUAGE OBJCXX)
else()
    set(OD_BACKEND_SOURCE "${OD_RENDER_ROOT}/graphics_gl.cpp")
    set(OD_SOKOL_BACKEND SOKOL_GLCORE)
endif()

od_generate_shader(OD_DIRECT_SHADER "${OD_RENDER_ROOT}/direct.glsl")
add_library(ODRender STATIC
    "${OD_RENDER_ROOT}/fog.cpp"
    "${OD_RENDER_ROOT}/sokol_impl.cpp"
    "${OD_RENDER_ROOT}/direct.cpp"
    "${OD_RENDER_ROOT}/direct_math.cpp"
    "${OD_RENDER_ROOT}/direct_shadow.cpp"
    ${OD_DIRECT_SHADER}
)
target_include_directories(ODRender PUBLIC "${OD_RENDER_ROOT}/.." "${sokol_SOURCE_DIR}")
target_include_directories(ODRender PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(ODRender PUBLIC cxx_std_17)
target_compile_definitions(ODRender PRIVATE ${OD_SOKOL_BACKEND})

add_library(ODGraphics STATIC ${OD_BACKEND_SOURCE})
target_link_libraries(ODGraphics PUBLIC ODRender SDL3::SDL3-static)
if(EMSCRIPTEN)
    # Browser: sokol_gfx runs on WebGL2 (GLES3), SDL3 is the pthreads build.
    # Whatever links ODGraphics gets these; see recomp/web/NOTES-gl.md.
    target_compile_options(ODRender PUBLIC -pthread)
    target_compile_options(ODGraphics PUBLIC -pthread)
    target_link_options(ODGraphics INTERFACE -pthread -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2)
    # Add these to an executable whose guest/main loop runs on a pthread
    # (-sPROXY_TO_PTHREAD): the canvas goes to that thread as an OffscreenCanvas
    # and JSPI lets SDL_GL_SwapWindow yield to the browser so the frame shows.
    set(OD_WEB_PTHREAD_CANVAS_LINK_OPTIONS -sOFFSCREENCANVAS_SUPPORT -sJSPI)
endif()
if(WIN32)
    target_link_libraries(ODGraphics PRIVATE d3d11 dxgi dwmapi)
elseif(APPLE)
    target_link_libraries(ODGraphics PRIVATE "-framework Metal" "-framework QuartzCore" "-framework Foundation")
elseif(NOT EMSCRIPTEN)
    find_package(OpenGL REQUIRED)
    target_link_libraries(ODGraphics PRIVATE OpenGL::GL)
endif()
