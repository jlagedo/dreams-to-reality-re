#pragma once

#include <SDL3/SDL.h>
#include <sokol_gfx.h>

#include <string>
#include <vector>
#include <cstdint>

namespace od {

enum class FrameState { ready, skipped, failed };

class GraphicsBackend {
public:
    static SDL_WindowFlags window_flags();
    static bool configure_window(std::string& error);
    bool init(SDL_Window* window, std::string& error);
    sg_environment environment() const;
    FrameState acquire(SDL_Window* window, sg_swapchain& swapchain, std::string& error);
    // Capture the completed swapchain frame without reading the desktop.
    bool capture(const std::string& png_path, std::string& error);
    // Explicit CPU export only. Caller submits prior GPU work before calling.
    bool read_image(sg_image image, int x, int y, int width, int height,
                    std::vector<uint32_t>& rgba, std::string& error);
    bool present(std::string& error);
    void shutdown();
    // Every completed GPU-to-CPU download (capture and read_image), whatever the
    // caller's reason. The host subtracts the ones it logged as explicit.
    uint64_t downloads() const { return downloads_; }

private:
    void* state_ = nullptr;
    uint64_t downloads_ = 0;
};

} // namespace od
