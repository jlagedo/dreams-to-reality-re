#pragma once

#include <array>
#include <filesystem>
#include <string>

namespace od::runtime {

struct SourceOptions {
    int max_frames = 0;
    bool skip_intro = false;
    bool start_new_game = false;
    std::string menu_page; // Development: "load" or "options" (implies skip intro).
    std::filesystem::path capture_file;
    std::array<std::filesystem::path, 2> cues{};
    std::filesystem::path config_file;
    // Replaces the retail X:\CRYO\DREAMS\ install root for data\game\game.dat.
    std::filesystem::path save_root;
};

// --cue1/--cue2 override the matching entries in --config or the default
// runtime.local.ini. A missing default config leaves both sources unconfigured.
bool parse_source_options(int argc, char** argv, SourceOptions& options,
                          std::string& error);

} // namespace od::runtime
