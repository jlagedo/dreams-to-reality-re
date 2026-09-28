#include "platform/graphics_backend.h"

#include <new>

namespace od {
namespace {

struct GLState {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
};

} // namespace

SDL_WindowFlags GraphicsBackend::window_flags() { return SDL_WINDOW_OPENGL; }

bool GraphicsBackend::configure_window(std::string& error) {
#ifdef __EMSCRIPTEN__
    const int profile = SDL_GL_CONTEXT_PROFILE_ES;
    const int major = 3;
    const int minor = 0;
#else
    const int profile = SDL_GL_CONTEXT_PROFILE_CORE;
    const int major = 4;
    const int minor = 1;
#endif
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, profile) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor) ||
        !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) ||
        !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0)) {
        error = std::string("SDL OpenGL attribute setup failed: ") + SDL_GetError();
        return false;
    }
    return true;
}

bool GraphicsBackend::init(SDL_Window* window, std::string& error) {
    auto* state = new (std::nothrow) GLState();
    if (!state) {
        error = "out of memory while creating OpenGL state";
        return false;
    }
    state_ = state;
    state->window = window;
    state->context = SDL_GL_CreateContext(window);
    if (!state->context) {
        error = std::string("SDL OpenGL context creation failed: ") + SDL_GetError();
        return false;
    }
    if (!SDL_GL_MakeCurrent(window, state->context)) {
        error = std::string("SDL OpenGL context activation failed: ") + SDL_GetError();
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    return true;
}

sg_environment GraphicsBackend::environment() const {
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    return env;
}

FrameState GraphicsBackend::acquire(SDL_Window* window, sg_swapchain& out, std::string& error) {
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        error = std::string("SDL drawable-size query failed: ") + SDL_GetError();
        return FrameState::failed;
    }
    if (width == 0 || height == 0) return FrameState::skipped;
    out.width = width;
    out.height = height;
    out.color_format = SG_PIXELFORMAT_RGBA8;
    out.depth_format = SG_PIXELFORMAT_NONE;
    out.sample_count = 1;
    out.gl.framebuffer = 0;
    return FrameState::ready;
}

bool GraphicsBackend::capture(const std::string&, std::string& error) {
    error = "hidden frame capture currently requires the D3D11 backend";
    return false;
}

bool GraphicsBackend::present(std::string& error) {
    auto* state = static_cast<GLState*>(state_);
    if (!SDL_GL_SwapWindow(state->window)) {
        error = std::string("SDL OpenGL presentation failed: ") + SDL_GetError();
        return false;
    }
    return true;
}

void GraphicsBackend::shutdown() {
    auto* state = static_cast<GLState*>(state_);
    if (!state) return;
    if (state->context) SDL_GL_DestroyContext(state->context);
    delete state;
    state_ = nullptr;
}

} // namespace od
