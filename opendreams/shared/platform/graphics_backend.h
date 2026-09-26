#pragma once

#include <SDL3/SDL.h>
#include <sokol_gfx.h>

#include <string>

namespace od {

enum class FrameState { ready, skipped, failed };

class GraphicsBackend {
public:
    static SDL_WindowFlags window_flags();
    bool init(SDL_Window* window, std::string& error);
    sg_environment environment() const;
    FrameState acquire(SDL_Window* window, sg_swapchain& swapchain, std::string& error);
    bool present(std::string& error);
    void shutdown();

private:
    void* state_ = nullptr;
};

} // namespace od
