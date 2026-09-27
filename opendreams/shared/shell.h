#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace od {

using DrawUi = void (*)(void* user);

struct ShellOptions {
    const char* title;
    DrawUi draw_ui;
    void* user;
    int max_frames;
    float ui_font_pixels = 0.0f; // 0 keeps the Dear ImGui default.
    float ui_size_scale = 1.0f;
    const char* icon_rgba = nullptr;
    int window_width = 1024;
    int window_height = 640;
};

class Shell {
public:
    bool init(const ShellOptions& options);
    SDL_AppResult event(const SDL_Event& event);
    SDL_AppResult iterate();
    void shutdown();
    const std::string& error() const { return error_; }
    SDL_Window* window() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string error_;
};

} // namespace od
