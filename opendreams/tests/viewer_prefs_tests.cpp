#include "inspect/viewer_prefs.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <iostream>
#include <string>

int main() {
    const std::string file =
        (std::filesystem::temp_directory_path() /
         ("odviewer-prefs-" + std::to_string(SDL_GetTicksNS()) + ".ini")).u8string();
    od::inspect::ViewerPrefs saved;
    saved.cue_paths[0] = "C:\\Dreams\\disc=1.cue";
    saved.cue_paths[1] = u8"/home/João/Dreams/Disc 2.cue";
    std::string error;
    if (!od::inspect::save_viewer_prefs(file, saved, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    od::inspect::ViewerPrefs loaded;
    if (!od::inspect::load_viewer_prefs(file, loaded, error) ||
        loaded.cue_paths != saved.cue_paths) {
        std::cerr << "viewer settings round trip failed: " << error << '\n';
        SDL_RemovePath(file.c_str());
        return 2;
    }
    saved.cue_paths[0].clear();
    saved.cue_paths[1] = "/media/disc=2\\with\\slashes.cue";
    if (!od::inspect::save_viewer_prefs(file, saved, error) ||
        !od::inspect::load_viewer_prefs(file, loaded, error) ||
        loaded.cue_paths != saved.cue_paths) {
        std::cerr << "viewer settings replacement failed: " << error << '\n';
        SDL_RemovePath(file.c_str());
        return 3;
    }
    constexpr char invalid[] = "version=2\nsource_a=x\n";
    if (!SDL_SaveFile(file.c_str(), invalid, sizeof(invalid) - 1) ||
        od::inspect::load_viewer_prefs(file, loaded, error) || error.empty()) {
        std::cerr << "unsupported settings version was accepted\n";
        SDL_RemovePath(file.c_str());
        return 4;
    }
    SDL_RemovePath(file.c_str());
    return 0;
}
