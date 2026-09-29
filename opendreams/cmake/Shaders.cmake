set(OD_SHDC_EXECUTABLE "" CACHE FILEPATH "Path to the pinned host sokol-shdc executable")

if(CMAKE_HOST_WIN32)
    set(_od_shdc_path bin/win32/sokol-shdc.exe)
    set(_od_shdc_sha BD616287F9EA689D53C6D260E443EE733E61AE1B73A9B37ADC482EAD0364D561)
elseif(CMAKE_HOST_APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_od_shdc_path bin/osx_arm64/sokol-shdc)
        set(_od_shdc_sha 92DB37975AD7FF3C3C9BC27CBA1503287377CB287EBABF60D1C6B597ABFA3244)
    else()
        message(FATAL_ERROR "The pinned shader tool supports only macOS arm64 in this project")
    endif()
elseif(CMAKE_HOST_UNIX)
    set(_od_shdc_path bin/linux/sokol-shdc)
    set(_od_shdc_sha ED35E89EF381D521A499096ED4ADA85E4D135D8011E151CCA6B7D893C43B21DF)
else()
    message(FATAL_ERROR "No pinned sokol-shdc binary for this build host")
endif()

if(NOT OD_SHDC_EXECUTABLE)
    get_filename_component(_od_shdc_name "${_od_shdc_path}" NAME)
    set(OD_SHDC_EXECUTABLE "${CMAKE_BINARY_DIR}/host-tools/${_od_shdc_name}")
    if(NOT EXISTS "${OD_SHDC_EXECUTABLE}")
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/host-tools")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/floooh/sokol-tools-bin/11d0cf678105d614d675e6d9bd2aaf3eeff12f8c/${_od_shdc_path}"
            "${OD_SHDC_EXECUTABLE}"
            EXPECTED_HASH "SHA256=${_od_shdc_sha}"
            STATUS _od_download_status
        )
        list(GET _od_download_status 0 _od_download_code)
        if(NOT _od_download_code EQUAL 0)
            message(FATAL_ERROR "Could not download pinned sokol-shdc: ${_od_download_status}")
        endif()
    endif()
    if(NOT CMAKE_HOST_WIN32)
        file(CHMOD "${OD_SHDC_EXECUTABLE}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    endif()
endif()

if(NOT EXISTS "${OD_SHDC_EXECUTABLE}")
    message(FATAL_ERROR "sokol-shdc does not exist: ${OD_SHDC_EXECUTABLE}")
endif()
file(SHA256 "${OD_SHDC_EXECUTABLE}" _od_actual_sha)
string(TOLOWER "${_od_shdc_sha}" _od_expected_sha)
if(NOT _od_actual_sha STREQUAL _od_expected_sha)
    message(FATAL_ERROR "sokol-shdc checksum mismatch: expected ${_od_shdc_sha}, got ${_od_actual_sha}")
endif()

function(od_generate_shader output_var source_path)
    get_filename_component(source_path "${source_path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    get_filename_component(_od_name "${source_path}" NAME_WE)
    set(_od_output "${CMAKE_CURRENT_BINARY_DIR}/generated/${_od_name}.glsl.h")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    add_custom_command(
        OUTPUT "${_od_output}"
        COMMAND "${OD_SHDC_EXECUTABLE}" --input "${source_path}" --output "${_od_output}" --slang glsl410:glsl300es:hlsl5:metal_macos
        DEPENDS "${source_path}" "${OD_SHDC_EXECUTABLE}"
        VERBATIM
    )
    set(${output_var} "${_od_output}" PARENT_SCOPE)
endfunction()
