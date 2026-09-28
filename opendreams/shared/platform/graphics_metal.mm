#include "platform/graphics_backend.h"

#include <SDL3/SDL_metal.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <new>

namespace od {
namespace {

struct MetalState {
    SDL_MetalView view = nullptr;
    CAMetalLayer* layer = nil; // owned by SDL_MetalView
    id<MTLDevice> device = nil;
    id<CAMetalDrawable> drawable = nil;
};

} // namespace

SDL_WindowFlags GraphicsBackend::window_flags() { return SDL_WINDOW_METAL; }

bool GraphicsBackend::configure_window(std::string&) { return true; }

bool GraphicsBackend::init(SDL_Window* window, std::string& error) {
    auto* state = new (std::nothrow) MetalState();
    if (!state) {
        error = "out of memory while creating Metal state";
        return false;
    }
    state_ = state;
    state->view = SDL_Metal_CreateView(window);
    if (!state->view) {
        error = std::string("SDL Metal view creation failed: ") + SDL_GetError();
        return false;
    }
    state->layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(state->view);
    state->device = MTLCreateSystemDefaultDevice();
    if (!state->layer || !state->device) {
        error = "Metal layer or GPU device is unavailable";
        return false;
    }
    state->layer.device = state->device;
    state->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    state->layer.framebufferOnly = YES;
    return true;
}

sg_environment GraphicsBackend::environment() const {
    const auto* state = static_cast<const MetalState*>(state_);
    sg_environment env{};
    env.defaults.color_format = SG_PIXELFORMAT_BGRA8;
    env.defaults.depth_format = SG_PIXELFORMAT_NONE;
    env.defaults.sample_count = 1;
    env.metal.device = (__bridge const void*)state->device;
    return env;
}

FrameState GraphicsBackend::acquire(SDL_Window* window, sg_swapchain& out, std::string& error) {
    auto* state = static_cast<MetalState*>(state_);
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) {
        error = std::string("SDL drawable-size query failed: ") + SDL_GetError();
        return FrameState::failed;
    }
    if (width == 0 || height == 0) return FrameState::skipped;
    state->layer.drawableSize = CGSizeMake(width, height);
    state->drawable = [[state->layer nextDrawable] retain];
    if (!state->drawable) return FrameState::skipped;
    out.width = width;
    out.height = height;
    out.color_format = SG_PIXELFORMAT_BGRA8;
    out.depth_format = SG_PIXELFORMAT_NONE;
    out.sample_count = 1;
    out.metal.current_drawable = (__bridge const void*)state->drawable;
    return FrameState::ready;
}

bool GraphicsBackend::capture(const std::string&, std::string& error) {
    error = "hidden frame capture currently requires the D3D11 backend";
    return false;
}

bool GraphicsBackend::present(std::string&) {
    // sokol_gfx schedules presentation in sg_commit().
    auto* state = static_cast<MetalState*>(state_);
    [state->drawable release];
    state->drawable = nil;
    return true;
}

void GraphicsBackend::shutdown() {
    auto* state = static_cast<MetalState*>(state_);
    if (!state) return;
    [state->drawable release];
    if (state->view) SDL_Metal_DestroyView(state->view);
    [state->device release];
    delete state;
    state_ = nullptr;
}

} // namespace od
