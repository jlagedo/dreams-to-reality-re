#include "source_config.h"

#include <SDL3/SDL.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>

namespace od::runtime {
namespace {

constexpr size_t max_config_bytes = 64 * 1024;

std::string_view trim(std::string_view text) {
    const auto space = [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\r';
    };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    return text;
}

bool load_config(const std::filesystem::path& file, SourceOptions& options,
                 std::string& error) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "cannot open runtime config: " + file.u8string();
        return false;
    }
    const auto end = input.tellg();
    if (end < 0 || end > static_cast<std::streamoff>(max_config_bytes)) {
        error = "runtime config exceeds the 64 KiB limit: " + file.u8string();
        return false;
    }
    std::string bytes(static_cast<size_t>(end), '\0');
    input.seekg(0);
    if (!bytes.empty() && !input.read(bytes.data(), end)) {
        error = "cannot read runtime config: " + file.u8string();
        return false;
    }
    std::string_view text(bytes);
    if (text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
    bool version_seen = false;
    std::array<bool, 2> cue_seen{};
    size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const size_t newline = text.find('\n');
        const std::string_view line = trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} :
            text.substr(newline + 1);
        if (line.empty() || line.front() == '#') continue;
        const size_t equal = line.find('=');
        if (equal == std::string_view::npos) {
            error = "runtime config line " + std::to_string(line_number) +
                " has no '='";
            return false;
        }
        const std::string_view key = trim(line.substr(0, equal));
        const std::string_view value = trim(line.substr(equal + 1));
        if (key == "version") {
            if (version_seen || value != "1") {
                error = "runtime config has a duplicate or unsupported version";
                return false;
            }
            version_seen = true;
            continue;
        }
        const size_t slot = key == "cue1" ? 0 : key == "cue2" ? 1 : 2;
        if (slot == 2 || cue_seen[slot]) {
            error = "runtime config line " + std::to_string(line_number) +
                " has an unknown or duplicate key";
            return false;
        }
        cue_seen[slot] = true;
        if (!value.empty()) {
            auto path = std::filesystem::u8path(value.begin(), value.end());
            options.cues[slot] = path.is_absolute() ? path : file.parent_path() / path;
        }
    }
    if (!version_seen) {
        error = "runtime config has no version=1 line";
        return false;
    }
    options.config_file = file;
    return true;
}

bool default_config(std::filesystem::path& file, std::string& error) {
    char* preference_dir = SDL_GetPrefPath("OpenDreams", "ODRuntime");
    if (!preference_dir) {
        error = std::string("cannot locate the runtime preference directory: ") +
                SDL_GetError();
        return false;
    }
    const auto user_config = std::filesystem::u8path(preference_dir) / "sources.ini";
    SDL_free(preference_dir);
    std::error_code ec;
    if (std::filesystem::exists(user_config, ec)) {
        file = user_config;
        return true;
    }
    if (ec) {
        error = "cannot inspect runtime user config: " + ec.message();
        return false;
    }
    // Explorer starts the executable with its build directory as CWD. Walk
    // only the executable's own directory and two parents so a development
    // checkout still finds opendreams/runtime.local.ini.
    if (const char* base = SDL_GetBasePath()) {
        auto directory = std::filesystem::u8path(base).parent_path();
        for (int level = 0; level < 3; ++level) {
            const auto candidate = directory / "runtime.local.ini";
            if (std::filesystem::exists(candidate, ec)) {
                file = candidate;
                return true;
            }
            if (ec) {
                error = "cannot inspect executable-side runtime config: " +
                        ec.message();
                return false;
            }
            directory = directory.parent_path();
        }
    }
    const auto cwd = std::filesystem::current_path(ec);
    if (ec) {
        error = "cannot determine the current directory: " + ec.message();
        return false;
    }
    const std::array<std::filesystem::path, 2> candidates = {
        cwd / "runtime.local.ini", cwd / "opendreams" / "runtime.local.ini"
    };
    for (const auto& candidate : candidates) {
        const bool exists = std::filesystem::exists(candidate, ec);
        if (ec) {
            error = "cannot inspect runtime config: " + ec.message();
            return false;
        }
        if (exists) {
            file = candidate;
            return true;
        }
    }
    return true;
}

} // namespace

bool parse_source_options(int argc, char** argv, SourceOptions& options,
                          std::string& error) {
    options = {};
    error.clear();
    std::array<std::filesystem::path, 2> overrides{};
    std::filesystem::path explicit_config;
    for (int i = 1; i < argc;) {
        if (std::strcmp(argv[i], "--skip-intro") == 0) {
            options.skip_intro = true;
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--start-new-game") == 0) {
            options.start_new_game = true;
            ++i;
            continue;
        }
        if (i + 1 >= argc) {
            error = "every ODRuntime option needs a value";
            return false;
        }
        if (std::strcmp(argv[i], "--cue1") == 0 ||
            std::strcmp(argv[i], "--cue2") == 0) {
            const size_t slot = std::strcmp(argv[i], "--cue1") == 0 ? 0 : 1;
            if (!*argv[i + 1]) {
                error = "a --cue path cannot be empty";
                return false;
            }
            overrides[slot] = std::filesystem::u8path(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--config") == 0) {
            if (!*argv[i + 1]) {
                error = "--config needs a file path";
                return false;
            }
            explicit_config = std::filesystem::u8path(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--menu-page") == 0) {
            options.menu_page = argv[i + 1];
            if (options.menu_page != "load" && options.menu_page != "options") {
                error = "--menu-page must be load or options";
                return false;
            }
            options.skip_intro = true;
        } else if (std::strcmp(argv[i], "--save-root") == 0) {
            options.save_root = std::filesystem::u8path(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--capture") == 0) {
            if (!*argv[i + 1]) {
                error = "--capture needs a PNG file path";
                return false;
            }
            options.capture_file = std::filesystem::u8path(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--frames") == 0) {
            errno = 0;
            char* end = nullptr;
            const long parsed = std::strtol(argv[i + 1], &end, 10);
            if (errno || !end || *end || parsed < 1 || parsed > INT_MAX) {
                error = "--frames needs a positive integer";
                return false;
            }
            options.max_frames = static_cast<int>(parsed);
        } else {
            error = std::string("unknown ODRuntime option: ") + argv[i];
            return false;
        }
        i += 2;
    }
    std::filesystem::path config = explicit_config;
    if (config.empty() && (overrides[0].empty() || overrides[1].empty()) &&
        !default_config(config, error)) return false;
    if (!config.empty() && !load_config(config, options, error)) return false;
    for (size_t slot = 0; slot < overrides.size(); ++slot)
        if (!overrides[slot].empty()) options.cues[slot] = overrides[slot];
    if (!options.capture_file.empty() && options.max_frames == 0)
        options.max_frames = 10;
    return true;
}

} // namespace od::runtime
