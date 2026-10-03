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
    // Before init: a swapchain that tells when the display can take a frame
    // without blocking (D3D11: the frame-latency waitable object, one frame
    // queued). Display interpolation draws its frames on that signal.
    void request_frame_waits() { want_frame_waits_ = true; }
    bool has_frame_waits() const;
    // Waits up to timeout_ns for the display to take a new frame. True: draw
    // and present one now. Without the object: true at once.
    bool wait_frame(uint64_t timeout_ns);
    // The display's own refresh timing: when the last refresh happened (on
    // SDL_GetTicksNS's clock) and the exact period. False where the platform
    // gives none, and the caller estimates them. D3D11: DWM's composition
    // timing (the display it composes for).
    bool refresh_timing(uint64_t& last_refresh_ns, uint64_t& period_ns);
    void shutdown();
    // Every completed GPU-to-CPU download (capture and read_image), whatever the
    // caller's reason. The host subtracts the ones it logged as explicit.
    uint64_t downloads() const { return downloads_; }

private:
    void* state_ = nullptr;
    uint64_t downloads_ = 0;
    bool want_frame_waits_ = false;
};

} // namespace od
