cmake_minimum_required(VERSION 3.28)
project(OpenDreams VERSION 0.1.0 LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
endif()

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
        target_compile_options(${target} PRIVATE /W4 /fp:precise
            $<$<COMPILE_LANGUAGE:CXX>:/GR->
            $<$<COMPILE_LANGUAGE:CXX>:/EHs->
            $<$<COMPILE_LANGUAGE:CXX>:/EHc->)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
            -fno-exceptions -fno-fast-math -ffp-contract=off
            $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
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

include(cmake/RenderCore.cmake)
od_project_options(ODRender)
od_project_options(ODGraphics)

od_generate_shader(OD_BACKGROUND_SHADER shared/render/background.glsl)

add_library(ODShared STATIC
    shared/disc/image.cpp
    shared/inspect/catalog.cpp
    shared/inspect/still_preview.cpp
    shared/inspect/viewer_prefs.cpp
    shared/port/file_roots.cpp
    shared/port/vfs.cpp
    shared/port/lz.cpp
    shared/port/model.cpp
    shared/port/math.cpp
    shared/port/glide_model.cpp
    shared/port/random.cpp
    shared/port/palette_lighting.cpp
    shared/port/level_materials.cpp
    shared/port/animation.cpp
    shared/port/boot_menu.cpp
    shared/port/save_game.cpp
    shared/port/menj.cpp
    shared/port/game_state.cpp
    shared/port/camera.cpp
    shared/port/player.cpp
    shared/port/game_entry.cpp
    shared/port/resource.cpp
    shared/port/scene.cpp
    shared/port/ddat.cpp
    shared/port/stream.cpp
    shared/port/dsn.cpp
    shared/port/dan.cpp
    shared/port/drd.cpp
    shared/port/fsb.cpp
    shared/port/sprite.cpp
    shared/port/video.cpp
    shared/port/hnm6.cpp
    shared/port/hnm4.cpp
    shared/port/hnm5.cpp
    shared/port/video_audio.cpp
    shared/port/wave.cpp
    shared/port/sound.cpp
    shared/audio/audio_output.cpp
    shared/audio/audio_preview.cpp
    shared/animation/animation_preview.cpp
    shared/shell.cpp
    shared/render/background.cpp
    shared/render/boot_menu.cpp
    shared/render/glide_compat.cpp
    shared/render/video_preview.cpp
    shared/render/model_preview.cpp
    shared/render/level_materials.cpp
    shared/render/still_preview.cpp
    shared/render/sokol_imgui_impl.cpp
    ${OD_BACKGROUND_SHADER}
    third_party/lib9660/lib9660.c
)
target_include_directories(ODShared
    PUBLIC shared
    PRIVATE ${sokol_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/generated
            third_party/lib9660
)
target_compile_definitions(ODShared PRIVATE ${OD_SOKOL_BACKEND} SOKOL_IMGUI_NO_SOKOL_APP)
target_link_libraries(ODShared PUBLIC ODGraphics ODImGui)
od_project_options(ODShared)

foreach(app IN ITEMS ODViewer ODRuntime)
    if(app STREQUAL "ODViewer")
        set(app_source apps/viewer/main.cpp)
    else()
        set(app_source apps/runtime/main.cpp)
    endif()
    add_executable(${app} ${app_source})
    if(app STREQUAL "ODRuntime")
        target_sources(${app} PRIVATE apps/runtime/source_config.cpp
            apps/runtime/front_end.cpp)
    endif()
    if(WIN32)
        set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${app}.ico")
        configure_file(cmake/AppIcon.rc.in
            "${CMAKE_CURRENT_BINARY_DIR}/generated/${app}.rc" @ONLY)
        target_sources(${app} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/${app}.rc")
    elseif(APPLE)
        set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${app}.icns")
        set_source_files_properties(${app_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(${app} PRIVATE ${app_icon})
        set_target_properties(${app} PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_ICON_FILE "${app}.icns"
            MACOSX_BUNDLE_GUI_IDENTIFIER "org.opendreams.${app}"
            MACOSX_BUNDLE_BUNDLE_NAME "${app}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    endif()
    if(NOT EMSCRIPTEN)
        set(window_icon "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${app}-window.rgba")
        if(APPLE)
            set_source_files_properties(${window_icon} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
            target_sources(${app} PRIVATE ${window_icon})
        else()
            add_custom_command(TARGET ${app} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${window_icon}
                    "$<TARGET_FILE_DIR:${app}>/${app}-window.rgba")
        endif()
    endif()
    target_link_libraries(${app} PRIVATE ODShared)
    target_include_directories(${app} PRIVATE ${imgui_SOURCE_DIR} ${sokol_SOURCE_DIR})
    target_compile_definitions(${app} PRIVATE SOKOL_IMGUI_NO_SOKOL_APP)
    od_project_options(${app})
    if(EMSCRIPTEN)
        set_target_properties(${app} PROPERTIES SUFFIX ".html")
        target_link_options(${app} PRIVATE "-sUSE_WEBGL2=1" "-sMIN_WEBGL_VERSION=2" "-sMAX_WEBGL_VERSION=2" "-sALLOW_MEMORY_GROWTH=1")
    endif()
endforeach()

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
    install(TARGETS ODViewer ODRuntime RUNTIME DESTINATION bin)
    foreach(app IN ITEMS ODViewer ODRuntime)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${app}-512.png"
            DESTINATION share/icons/hicolor/512x512/apps
            RENAME "${app}.png")
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${app}-window.rgba"
            DESTINATION bin)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/${app}.desktop"
            DESTINATION share/applications)
    endforeach()
endif()

if(BUILD_TESTING AND NOT EMSCRIPTEN)
    add_executable(ODDirectMathTests tests/direct_math_tests.cpp)
    target_link_libraries(ODDirectMathTests PRIVATE ODRender)
    od_project_options(ODDirectMathTests)
    add_test(NAME ODDirectMath COMMAND ODDirectMathTests)
    add_executable(WDRenderBoundaryTests
        ../recomp/windream/debug/render_boundary_tests.cpp
        ../recomp/windream/runtime/render_boundary.cpp)
    target_include_directories(WDRenderBoundaryTests PRIVATE ../recomp/windream/runtime)
    od_project_options(WDRenderBoundaryTests)
    add_test(NAME WDRenderBoundary COMMAND WDRenderBoundaryTests)
    if(WIN32)
        add_executable(ODDirectGpuTests tests/direct_gpu_tests.cpp)
        add_executable(ODModelAdapterTests tests/model_adapter_tests.cpp)
        target_link_libraries(ODModelAdapterTests PRIVATE ODShared)
        od_project_options(ODModelAdapterTests)
        target_link_libraries(ODDirectGpuTests PRIVATE ODRender d3d11)
        od_project_options(ODDirectGpuTests)
        # Explicit invocation: requires a hardware graphics device.
    endif()

    add_executable(ODPlayerConeTests tests/player_cone_tests.cpp)
    target_link_libraries(ODPlayerConeTests PRIVATE ODShared)
    od_project_options(ODPlayerConeTests)
    add_test(NAME ODPlayerConeCorpus COMMAND ODPlayerConeTests)

    add_executable(ODGameEntryTests tests/game_entry_tests.cpp)
    target_link_libraries(ODGameEntryTests PRIVATE ODShared)
    od_project_options(ODGameEntryTests)
    add_test(NAME ODGameEntry COMMAND ODGameEntryTests)

    add_executable(ODBootMenuTests tests/boot_menu_tests.cpp)
    target_link_libraries(ODBootMenuTests PRIVATE ODShared)
    od_project_options(ODBootMenuTests)
    add_test(NAME ODBootMenu COMMAND ODBootMenuTests)

    add_executable(ODAnimationTests tests/animation_tests.cpp)
    target_link_libraries(ODAnimationTests PRIVATE ODShared)
    od_project_options(ODAnimationTests)
    add_test(NAME ODAnimation COMMAND ODAnimationTests)
    set_tests_properties(ODAnimation PROPERTIES LABELS "animation;corpus" TIMEOUT 180)

    add_executable(ODMathTests tests/math_tests.cpp)
    target_link_libraries(ODMathTests PRIVATE ODShared)
    od_project_options(ODMathTests)
    add_test(NAME ODMath COMMAND ODMathTests)
    add_test(NAME ODMathRetailExe COMMAND ODMathTests --retail-exe)
    set_tests_properties(ODMathRetailExe PROPERTIES SKIP_RETURN_CODE 77)

    add_executable(ODScenePreviewTests tests/scene_preview_tests.cpp)
    target_link_libraries(ODScenePreviewTests PRIVATE ODShared)
    od_project_options(ODScenePreviewTests)
    add_test(NAME ODScenePreviewCorpus COMMAND ODScenePreviewTests)
    set_tests_properties(ODScenePreviewCorpus PROPERTIES LABELS "scene;corpus" TIMEOUT 300 SKIP_RETURN_CODE 77)

    add_executable(ODMaterialBindingTests tests/material_binding_tests.cpp)
    target_link_libraries(ODMaterialBindingTests PRIVATE ODShared)
    od_project_options(ODMaterialBindingTests)
    add_test(NAME ODMaterialBindingCorpus COMMAND ODMaterialBindingTests)
    set_tests_properties(ODMaterialBindingCorpus PROPERTIES LABELS "model;corpus" TIMEOUT 600 SKIP_RETURN_CODE 77)

    add_executable(ODPaletteLightingTests tests/palette_lighting_tests.cpp)
    target_link_libraries(ODPaletteLightingTests PRIVATE ODShared)
    od_project_options(ODPaletteLightingTests)
    add_test(NAME ODPaletteLighting COMMAND ODPaletteLightingTests)
    set_tests_properties(ODPaletteLighting PROPERTIES LABELS render TIMEOUT 30)
    add_test(NAME ODPaletteLightingDisc COMMAND ODPaletteLightingTests --disc)
    set_tests_properties(ODPaletteLightingDisc PROPERTIES LABELS "render;corpus" TIMEOUT 180
        SKIP_RETURN_CODE 77)

    add_executable(ODModelDrawTests tests/model_draw_tests.cpp)
    target_link_libraries(ODModelDrawTests PRIVATE ODShared)
    od_project_options(ODModelDrawTests)
    add_test(NAME ODModelDraw COMMAND ODModelDrawTests)
    set_tests_properties(ODModelDraw PROPERTIES LABELS "model;corpus" TIMEOUT 240)

    add_executable(ODAudioPreviewTests tests/audio_preview_tests.cpp)
    target_link_libraries(ODAudioPreviewTests PRIVATE ODShared)
    od_project_options(ODAudioPreviewTests)
    add_test(NAME ODAudioPreview COMMAND ODAudioPreviewTests)
    set_tests_properties(ODAudioPreview PROPERTIES LABELS "audio;corpus" TIMEOUT 60
        SKIP_RETURN_CODE 77 ENVIRONMENT "SDL_AUDIODRIVER=dummy")

    add_executable(ODStillPreviewTests tests/still_preview_tests.cpp)
    target_link_libraries(ODStillPreviewTests PRIVATE ODShared)
    od_project_options(ODStillPreviewTests)
    add_test(NAME ODStillPreview COMMAND ODStillPreviewTests)
    set_tests_properties(ODStillPreview PROPERTIES LABELS "preview;corpus" TIMEOUT 60 SKIP_RETURN_CODE 77)

    add_executable(ODGlideCompatTests tests/glide_compat_tests.cpp)
    target_link_libraries(ODGlideCompatTests PRIVATE ODShared)
    od_project_options(ODGlideCompatTests)
    add_test(NAME ODGlideCompat COMMAND ODGlideCompatTests)
    set_tests_properties(ODGlideCompat PROPERTIES LABELS video TIMEOUT 30)

    add_executable(ODHnm6Tests tests/hnm6_tests.cpp)
    target_link_libraries(ODHnm6Tests PRIVATE ODShared)
    od_project_options(ODHnm6Tests)
    add_test(NAME ODHnm6 COMMAND ODHnm6Tests)
    set_tests_properties(ODHnm6 PROPERTIES LABELS video TIMEOUT 30 SKIP_RETURN_CODE 77)

    add_executable(ODHnm4Tests tests/hnm4_tests.cpp)
    target_link_libraries(ODHnm4Tests PRIVATE ODShared)
    od_project_options(ODHnm4Tests)
    add_test(NAME ODHnm4 COMMAND ODHnm4Tests)
    set_tests_properties(ODHnm4 PROPERTIES LABELS video TIMEOUT 120 SKIP_RETURN_CODE 77)

    add_executable(ODMenjTests tests/menj_tests.cpp)
    target_link_libraries(ODMenjTests PRIVATE ODShared)
    od_project_options(ODMenjTests)
    add_test(NAME ODMenj COMMAND ODMenjTests)
    set_tests_properties(ODMenj PROPERTIES LABELS audio TIMEOUT 10)

    add_executable(ODMovieClockTests tests/movie_clock_tests.cpp)
    target_link_libraries(ODMovieClockTests PRIVATE ODShared)
    od_project_options(ODMovieClockTests)
    add_test(NAME ODMovieClock COMMAND ODMovieClockTests)
    set_tests_properties(ODMovieClock PROPERTIES LABELS video TIMEOUT 10)

    add_executable(ODVideoPlaybackTests tests/video_playback_tests.cpp)
    target_link_libraries(ODVideoPlaybackTests PRIVATE ODShared)
    od_project_options(ODVideoPlaybackTests)
    add_test(NAME ODVideoPlayback COMMAND ODVideoPlaybackTests)
    set_tests_properties(ODVideoPlayback PROPERTIES LABELS video TIMEOUT 30 SKIP_RETURN_CODE 77)

    add_executable(ODVideoCorpusTests tests/video_corpus_tests.cpp)
    target_link_libraries(ODVideoCorpusTests PRIVATE ODShared)
    od_project_options(ODVideoCorpusTests)
    add_test(NAME ODVideoCorpus COMMAND ODVideoCorpusTests)
    set_tests_properties(ODVideoCorpus PROPERTIES LABELS "video;corpus" TIMEOUT 300 SKIP_RETURN_CODE 77)

    add_executable(ODViewerPrefsTests tests/viewer_prefs_tests.cpp)
    target_link_libraries(ODViewerPrefsTests PRIVATE ODShared)
    od_project_options(ODViewerPrefsTests)
    add_test(NAME ODViewerPrefs COMMAND ODViewerPrefsTests)
    set_tests_properties(ODViewerPrefs PROPERTIES LABELS prefs TIMEOUT 30)

    add_executable(ODLzTests tests/lz_tests.cpp)
    target_link_libraries(ODLzTests PRIVATE ODShared)
    od_project_options(ODLzTests)
    add_test(NAME ODLzFixtures COMMAND ODLzTests)
    set_tests_properties(ODLzFixtures PROPERTIES LABELS lz TIMEOUT 30)

    add_executable(ODDanPayloadTests tests/dan_payload_tests.cpp)
    target_link_libraries(ODDanPayloadTests PRIVATE ODShared)
    od_project_options(ODDanPayloadTests)
    add_test(NAME ODDanPayloadCorpus COMMAND ODDanPayloadTests)
    set_tests_properties(ODDanPayloadCorpus PROPERTIES LABELS "dan;corpus" SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(ODDsnPayloadTests tests/dsn_payload_tests.cpp)
    target_link_libraries(ODDsnPayloadTests PRIVATE ODShared)
    od_project_options(ODDsnPayloadTests)
    add_test(NAME ODDsnPayloadCorpus COMMAND ODDsnPayloadTests)
    set_tests_properties(ODDsnPayloadCorpus PROPERTIES LABELS "dsn;corpus" SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(ODPreviewLevelTests tests/preview_level_tests.cpp)
    target_link_libraries(ODPreviewLevelTests PRIVATE ODShared)
    od_project_options(ODPreviewLevelTests)
    add_test(NAME ODPreviewLevelCorpus COMMAND ODPreviewLevelTests)
    set_tests_properties(ODPreviewLevelCorpus PROPERTIES LABELS "preview;corpus" SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(ODCatalogTests tests/catalog_tests.cpp)
    target_link_libraries(ODCatalogTests PRIVATE ODShared)
    od_project_options(ODCatalogTests)
    add_test(NAME ODCatalogCorpus COMMAND ODCatalogTests)
    set_tests_properties(ODCatalogCorpus PROPERTIES LABELS "catalog;corpus" SKIP_RETURN_CODE 77 TIMEOUT 180)

    add_executable(ODDdatTests tests/ddat_tests.cpp)
    target_link_libraries(ODDdatTests PRIVATE ODShared)
    od_project_options(ODDdatTests)
    add_test(NAME ODDdatFixtures COMMAND ODDdatTests)
    add_test(NAME ODDdatCorpus COMMAND ODDdatTests --corpus)
    set_tests_properties(ODDdatFixtures PROPERTIES LABELS ddat TIMEOUT 30)
    set_tests_properties(ODDdatCorpus PROPERTIES LABELS "ddat;corpus" SKIP_RETURN_CODE 77 TIMEOUT 120)

    add_executable(ODPortFileRootsTests tests/port_file_roots_tests.cpp)
    target_link_libraries(ODPortFileRootsTests PRIVATE ODShared)
    od_project_options(ODPortFileRootsTests)
    add_test(NAME ODPortFileRoots COMMAND ODPortFileRootsTests)

    add_executable(ODDiscTests tests/disc_tests.cpp)
    target_link_libraries(ODDiscTests PRIVATE ODShared)
    target_include_directories(ODDiscTests PRIVATE third_party/lib9660)
    od_project_options(ODDiscTests)
    add_test(NAME ODDiscFixtures COMMAND ODDiscTests)
    add_test(NAME ODDiscCorpus COMMAND ODDiscTests --corpus)
    set_tests_properties(ODDiscFixtures PROPERTIES LABELS disc TIMEOUT 30)
    set_tests_properties(ODDiscCorpus PROPERTIES LABELS "disc;corpus" SKIP_RETURN_CODE 77 TIMEOUT 120)

    option(OD_ENABLE_GRAPHICS_TESTS "Register display-dependent finite-frame smoke tests" OFF)
    if(OD_ENABLE_GRAPHICS_TESTS)
        add_test(NAME ODViewerGraphics COMMAND ODViewer --frames 10)
        add_test(NAME ODRuntimeGraphics COMMAND ODRuntime --frames 10)
        set_tests_properties(ODViewerGraphics ODRuntimeGraphics PROPERTIES LABELS graphics TIMEOUT 30)
    endif()
endif()
