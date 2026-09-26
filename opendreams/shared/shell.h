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
};

class Shell {
public:
    bool init(const ShellOptions& options);
    SDL_AppResult event(const SDL_Event& event);
    SDL_AppResult iterate();
    void shutdown();
    const std::string& error() const { return error_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string error_;
};

} // namespace od
