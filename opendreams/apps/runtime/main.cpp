#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "shell.h"

#include <imgui.h>

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

namespace {

od::Shell shell;

struct RuntimeUi {
    int clicks = 0;
    char text[128]{};
};
RuntimeUi ui;

void draw_ui(void* user) {
    auto& state = *static_cast<RuntimeUi*>(user);
    ImGui::SetNextWindowPos(ImVec2(24.0f, 24.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 170.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("ODRuntime - Hello World");
    ImGui::TextUnformatted("Hello World from ODRuntime");
    ImGui::TextUnformatted("SDL3 + sokol_gfx + Dear ImGui");
    if (ImGui::Button("Click me")) ++state.clicks;
    ImGui::SameLine();
    ImGui::Text("Count: %d", state.clicks);
    ImGui::InputText("Text input", state.text, sizeof(state.text));
    ImGui::End();
}

bool parse_frames(int argc, char** argv, int& frames) {
    if (argc == 1) return true;
    if (argc != 3 || std::strcmp(argv[1], "--frames") != 0) return false;
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(argv[2], &end, 10);
    if (errno || !end || *end || parsed < 1 || parsed > INT_MAX) return false;
    frames = static_cast<int>(parsed);
    return true;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    *appstate = &shell;
    int frames = 0;
    if (!parse_frames(argc, argv, frames)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "usage: ODRuntime [--frames positive-integer]");
        return SDL_APP_FAILURE;
    }
    if (!shell.init({"ODRuntime", draw_ui, &ui, frames})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", shell.error().c_str());
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return static_cast<od::Shell*>(appstate)->event(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    return static_cast<od::Shell*>(appstate)->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    if (appstate) static_cast<od::Shell*>(appstate)->shutdown();
}
