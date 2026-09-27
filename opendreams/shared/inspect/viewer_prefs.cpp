#include "inspect/viewer_prefs.h"

#include <SDL3/SDL.h>

#include <string_view>
#include <utility>

namespace od::inspect {
namespace {

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

std::string escape(std::string_view value) {
    std::string result;
    for (char ch : value) {
        switch (ch) {
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += ch; break;
        }
    }
    return result;
}

bool unescape(std::string_view encoded, std::string& value,
              std::string& error) {
    value.clear();
    for (size_t i = 0; i < encoded.size(); ++i) {
        char ch = encoded[i];
        if (ch == '\\') {
            if (++i == encoded.size())
                return fail(error, "viewer settings end inside an escape");
            switch (encoded[i]) {
            case '\\': ch = '\\'; break;
            case 'n': ch = '\n'; break;
            case 'r': ch = '\r'; break;
            case 't': ch = '\t'; break;
            default: return fail(error, "viewer settings contain an unknown escape");
            }
        } else if (static_cast<unsigned char>(ch) < 0x20) {
            return fail(error, "viewer settings contain an unescaped control byte");
        }
        value += ch;
    }
    if (value.size() >= 1024)
        return fail(error, "remembered CUE path exceeds the viewer path field");
    return true;
}

bool parse(std::string_view text, ViewerPrefs& prefs, std::string& error) {
    ViewerPrefs candidate;
    bool version = false;
    std::array<bool, 2> seen{};
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        std::string_view line = text.substr(at, end == std::string_view::npos
            ? text.size() - at : end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        at = end == std::string_view::npos ? text.size() : end + 1;
        if (line.empty() || line.front() == '#') continue;
        const size_t equal = line.find('=');
        if (equal == std::string_view::npos)
            return fail(error, "viewer settings contain a line without '='");
        const std::string_view key = line.substr(0, equal);
        const std::string_view value = line.substr(equal + 1);
        if (key == "version") {
            if (version || value != "1")
                return fail(error, "viewer settings have an unsupported version");
            version = true;
            continue;
        }
        size_t slot = 2;
        if (key == "source_a") slot = 0;
        if (key == "source_b") slot = 1;
        if (slot == 2) continue; // Future version-one keys do not lose paths.
        if (seen[slot])
            return fail(error, "viewer settings repeat a source key");
        seen[slot] = true;
        if (!unescape(value, candidate.cue_paths[slot], error)) return false;
    }
    if (!version)
        return fail(error, "viewer settings have no version");
    prefs = std::move(candidate);
    return true;
}

} // namespace

bool viewer_prefs_file(std::string& file, std::string& error) {
    file.clear();
    error.clear();
    char* directory = SDL_GetPrefPath("OpenDreams", "ODViewer");
    if (!directory)
        return fail(error, std::string("cannot open viewer preference directory: ") +
                           SDL_GetError());
    file = directory;
    SDL_free(directory);
    file += "sources.ini";
    return true;
}

bool load_viewer_prefs(const std::string& file, ViewerPrefs& prefs,
                       std::string& error) {
    error.clear();
    prefs = {};
    SDL_PathInfo info{};
    if (!SDL_GetPathInfo(file.c_str(), &info)) return true; // First launch.
    if (info.type != SDL_PATHTYPE_FILE)
        return fail(error, "viewer settings path is not a file");
    size_t size = 0;
    void* bytes = SDL_LoadFile(file.c_str(), &size);
    if (!bytes)
        return fail(error, std::string("cannot read viewer settings: ") +
                           SDL_GetError());
    bool success = false;
    if (size > 65536)
        fail(error, "viewer settings exceed the size limit");
    else
        success = parse(std::string_view(static_cast<const char*>(bytes), size),
                        prefs, error);
    SDL_free(bytes);
    return success;
}

bool save_viewer_prefs(const std::string& file, const ViewerPrefs& prefs,
                       std::string& error) {
    error.clear();
    std::string bytes = "# ODViewer source images\nversion=1\n";
    bytes += "source_a=" + escape(prefs.cue_paths[0]) + "\n";
    bytes += "source_b=" + escape(prefs.cue_paths[1]) + "\n";
    const std::string temporary = file + ".tmp";
    if (!SDL_SaveFile(temporary.c_str(), bytes.data(), bytes.size()))
        return fail(error, std::string("cannot write viewer settings: ") +
                           SDL_GetError());
    if (!SDL_RenamePath(temporary.c_str(), file.c_str())) {
        const std::string message = std::string("cannot replace viewer settings: ") +
                                    SDL_GetError();
        SDL_RemovePath(temporary.c_str());
        return fail(error, message);
    }
    return true;
}

} // namespace od::inspect
