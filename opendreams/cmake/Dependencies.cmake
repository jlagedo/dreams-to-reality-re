include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(imgui
    URL https://codeload.github.com/ocornut/imgui/tar.gz/f1cc2ae15e53a861a874c3034aae6798fde194ab
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _opendreams_no_cmake
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(sokol
    URL https://codeload.github.com/floooh/sokol/tar.gz/2e75443dbd4940b5aa8d76a8e479f8e4b270b9a3
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    SOURCE_SUBDIR _opendreams_no_cmake
)
FetchContent_MakeAvailable(sokol)
