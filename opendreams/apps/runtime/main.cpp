#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "source_config.h"
#include "front_end.h"
#include "disc/image.h"
#include "shell.h"

#include <imgui.h>

#include <array>
#include <memory>
#include <string>

namespace {

od::Shell shell;

struct RuntimeUi {
    int clicks = 0;
    char text[128]{};
    std::array<std::shared_ptr<const od::disc::Image>, 2> images{};
    std::string config_file;
    std::string capture_path;
    od::runtime::FrontEnd front_end;
    bool front_end_ready = false;
};
RuntimeUi ui;

const char* identity_name(od::disc::Identity identity) {
    switch (identity) {
    case od::disc::Identity::disc1: return "Disc 1";
    case od::disc::Identity::disc2: return "Disc 2";
    case od::disc::Identity::ambiguous: return "ambiguous";
    default: return "unknown";
    }
}

bool mount_sources(const od::runtime::SourceOptions& options, std::string& error) {
    ui.images = {};
    ui.config_file = options.config_file.u8string();
    for (size_t slot = 0; slot < options.cues.size(); ++slot) {
        if (options.cues[slot].empty()) continue;
        od::disc::Error disc_error;
        auto image = od::disc::Image::open(options.cues[slot], disc_error);
        if (!image) {
            error = "Disc " + std::to_string(slot + 1) + ": " + disc_error.message;
            ui.images = {};
            return false;
        }
        const auto expected = slot == 0 ? od::disc::Identity::disc1
                                        : od::disc::Identity::disc2;
        if (image->identity() != expected) {
            error = "Disc " + std::to_string(slot + 1) + " path identifies as " +
                    identity_name(image->identity()) + ": " +
                    image->cue_path().u8string();
            ui.images = {};
            return false;
        }
        SDL_Log("ODRuntime mounted %s: %zu files, %zu tracks", identity_name(expected),
                image->file_count(), image->tracks().size());
        ui.images[slot] = std::shared_ptr<const od::disc::Image>(std::move(image));
    }
    return true;
}

void draw_ui(void* user) {
    auto& state = *static_cast<RuntimeUi*>(user);
    if (state.front_end_ready) {
        state.front_end.draw();
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(620.0f, 290.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("ODRuntime - Hello World");
    ImGui::TextUnformatted("Hello World from ODRuntime");
    ImGui::TextUnformatted("SDL3 + sokol_gfx + Dear ImGui");
    if (ImGui::Button("Click me")) ++state.clicks;
    ImGui::SameLine();
    ImGui::Text("Count: %d", state.clicks);
    ImGui::InputText("Text input", state.text, sizeof(state.text));
    ImGui::Separator();
    if (!state.config_file.empty())
        ImGui::TextWrapped("Source config: %s", state.config_file.c_str());
    for (size_t slot = 0; slot < state.images.size(); ++slot) {
        const auto& image = state.images[slot];
        if (!image) {
            ImGui::Text("Disc %zu: not configured", slot + 1);
            continue;
        }
        ImGui::Text("Disc %zu: %zu files, %zu tracks", slot + 1,
                    image->file_count(), image->tracks().size());
        ImGui::TextWrapped("%s", image->cue_path().u8string().c_str());
    }
    ImGui::End();
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    *appstate = &shell;
    od::runtime::SourceOptions options;
    std::string error;
    if (!od::runtime::parse_source_options(argc, argv, options, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s\nusage: ODRuntime [--config path] [--cue1 path] [--cue2 path] [--skip-intro] [--start-new-game] [--menu-page load|options] [--save-root dir] [--capture image.png] [--frames positive-integer]",
                     error.c_str());
        return SDL_APP_FAILURE;
    }
    if (!options.config_file.empty())
        SDL_Log("ODRuntime source config: %s", options.config_file.u8string().c_str());
    else if (options.cues[0].empty() && options.cues[1].empty())
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ODRuntime found no source config or CUE arguments");
    if (!mount_sources(options, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
        return SDL_APP_FAILURE;
    }
    if (options.start_new_game && !ui.images[0]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "--start-new-game requires a configured Disc 1 image");
        return SDL_APP_FAILURE;
    }
    ui.capture_path = options.capture_file.u8string();
    if (!shell.init({"ODRuntime", draw_ui, &ui, options.max_frames, 0.0f, 1.0f,
                     "ODRuntime-window.rgba", 640, 480, true,
                     ui.capture_path.empty() ? nullptr : ui.capture_path.c_str(),
                     !ui.capture_path.empty()})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", shell.error().c_str());
        ui.images = {};
        return SDL_APP_FAILURE;
    }
    if (ui.images[0]) {
        std::filesystem::path save_root = options.save_root;
        if (save_root.empty()) {
            // Per-user replacement for the retail install root.
            if (char* pref = SDL_GetPrefPath("OpenDreams", "ODRuntime")) {
                save_root = std::filesystem::u8path(pref);
                SDL_free(pref);
            }
        }
        if (!ui.front_end.init(ui.images[0], ui.images[1], shell.window(),
                               options.skip_intro, options.start_new_game,
                               save_root, error)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "runtime front end: %s", error.c_str());
            ui.front_end.shutdown();
            shell.shutdown();
            ui.images = {};
            return SDL_APP_FAILURE;
        }
        ui.front_end.open_menu_page(options.menu_page);
        ui.front_end_ready = true;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return static_cast<od::Shell*>(appstate)->event(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    const SDL_AppResult result = static_cast<od::Shell*>(appstate)->iterate();
    if (result != SDL_APP_CONTINUE || !ui.front_end_ready ||
        !ui.front_end.wants_quit()) return result;
    if (!ui.front_end.error().empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", ui.front_end.error().c_str());
        return SDL_APP_FAILURE;
    }
    return SDL_APP_SUCCESS;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    ui.front_end.shutdown();
    ui.front_end_ready = false;
    if (appstate) static_cast<od::Shell*>(appstate)->shutdown();
    ui.images = {};
}
