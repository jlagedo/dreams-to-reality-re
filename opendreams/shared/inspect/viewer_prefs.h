#pragma once

#include <array>
#include <string>

namespace od::inspect {

struct ViewerPrefs {
    std::array<std::string, 2> cue_paths{};
};

// SDL chooses a writable, per-user location on each desktop platform.
bool viewer_prefs_file(std::string& file, std::string& error);
bool load_viewer_prefs(const std::string& file, ViewerPrefs& prefs,
                       std::string& error);
bool save_viewer_prefs(const std::string& file, const ViewerPrefs& prefs,
                       std::string& error);

} // namespace od::inspect
