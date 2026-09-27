#include "shell.h"

#include "platform/graphics_backend.h"
#include "render/background.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <sokol_log.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <cstdint>
#include <new>

namespace od {

struct Shell::Impl {
    ShellOptions options{};
    SDL_Window* window = nullptr;
    GraphicsBackend graphics;
    Background background;
    uint64_t last_tick_ns = 0;
    int frames = 0;
    bool sokol_ready = false;
    bool imgui_ready = false;
    bool sdl_backend_ready = false;
};

SDL_Window* Shell::window() const { return impl_ ? impl_->window : nullptr; }

bool Shell::init(const ShellOptions& options) {
    error_.clear();
    if (!options.title || !options.draw_ui || options.max_frames < 0 ||
        options.ui_font_pixels < 0.0f || options.ui_size_scale <= 0.0f ||
        options.window_width <= 0 || options.window_height <= 0) {
        error_ = "invalid OpenDreams shell options";
        return false;
    }
    impl_ = new (std::nothrow) Impl();
    if (!impl_) {
        error_ = "out of memory while creating OpenDreams shell";
        return false;
    }
    impl_->options = options;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        error_ = std::string("SDL initialization failed: ") + SDL_GetError();
        shutdown();
        return false;
    }
    if (!GraphicsBackend::configure_window(error_)) {
        shutdown();
        return false;
    }
    const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | GraphicsBackend::window_flags());
    int window_width = options.window_width;
    int window_height = options.window_height;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable)) {
        window_width = std::min(window_width, usable.w);
        window_height = std::min(window_height, usable.h);
    }
    impl_->window = SDL_CreateWindow(options.title, window_width, window_height, flags);
    if (!impl_->window) {
        error_ = std::string("SDL window creation failed: ") + SDL_GetError();
        shutdown();
        return false;
    }
#ifndef __EMSCRIPTEN__
    if (options.icon_rgba) {
        const char* base_path = SDL_GetBasePath();
        std::string icon_path = base_path ? base_path : "";
#if defined(__APPLE__)
        icon_path += "../Resources/";
#endif
        icon_path += options.icon_rgba;
        size_t pixel_bytes = 0;
        void* pixels = SDL_LoadFile(icon_path.c_str(), &pixel_bytes);
        if (pixels && pixel_bytes == 64u * 64u * 4u) {
            SDL_Surface* icon = SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_RGBA32,
                                                      pixels, 64 * 4);
            if (icon) {
                if (!SDL_SetWindowIcon(impl_->window, icon))
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "could not set window icon: %s", SDL_GetError());
                SDL_DestroySurface(icon);
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "could not create window icon: %s", SDL_GetError());
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "could not load window icon: %s", icon_path.c_str());
        }
        SDL_free(pixels);
    }
#endif
    if (!impl_->graphics.init(impl_->window, error_)) {
        shutdown();
        return false;
    }

    sg_desc gfx_desc{};
    gfx_desc.environment = impl_->graphics.environment();
    gfx_desc.logger.func = slog_func;
    sg_setup(&gfx_desc);
    if (!sg_isvalid()) {
        error_ = "sokol_gfx initialization failed";
        shutdown();
        return false;
    }
    impl_->sokol_ready = true;
    if (!impl_->background.init(error_)) {
        shutdown();
        return false;
    }

    simgui_desc_t ui_desc{};
    ui_desc.logger.func = slog_func;
    ui_desc.no_default_font = options.ui_font_pixels > 0.0f;
    simgui_setup(&ui_desc); // creates the one ImGui context and its GPU resources
    impl_->imgui_ready = true;
    if (options.ui_font_pixels > 0.0f) {
        ImFontConfig font_config;
        font_config.SizePixels = options.ui_font_pixels;
        ImGui::GetIO().Fonts->AddFontDefaultVector(&font_config);
    }
    if (options.ui_size_scale != 1.0f)
        ImGui::GetStyle().ScaleAllSizes(options.ui_size_scale);
#if defined(SOKOL_D3D11)
    impl_->sdl_backend_ready = ImGui_ImplSDL3_InitForD3D(impl_->window);
#elif defined(SOKOL_METAL)
    impl_->sdl_backend_ready = ImGui_ImplSDL3_InitForMetal(impl_->window);
#else
    impl_->sdl_backend_ready = ImGui_ImplSDL3_InitForOpenGL(impl_->window, SDL_GL_GetCurrentContext());
#endif
    if (!impl_->sdl_backend_ready) {
        error_ = "Dear ImGui SDL3 input backend initialization failed";
        shutdown();
        return false;
    }
    impl_->last_tick_ns = SDL_GetTicksNS();
    return true;
}

SDL_AppResult Shell::event(const SDL_Event& event) {
    if (!impl_) return SDL_APP_FAILURE;
    if (impl_->sdl_backend_ready) ImGui_ImplSDL3_ProcessEvent(&event);
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult Shell::iterate() {
    if (!impl_) return SDL_APP_FAILURE;
    sg_swapchain swapchain{};
    const FrameState frame = impl_->graphics.acquire(impl_->window, swapchain, error_);
    if (frame == FrameState::failed) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error_.c_str());
        return SDL_APP_FAILURE;
    }
    if (frame == FrameState::skipped) {
#ifndef __EMSCRIPTEN__
        SDL_Delay(16);
#endif
        return SDL_APP_CONTINUE;
    }

    const uint64_t now = SDL_GetTicksNS();
    double dt = static_cast<double>(now - impl_->last_tick_ns) / 1000000000.0;
    if (dt <= 0.0 || dt > 0.25) dt = 1.0 / 60.0;
    impl_->last_tick_ns = now;

    ImGui_ImplSDL3_NewFrame();
    int logical_width = 0;
    int logical_height = 0;
    SDL_GetWindowSize(impl_->window, &logical_width, &logical_height);
    const float dpi = logical_width > 0
        ? static_cast<float>(swapchain.width) / static_cast<float>(logical_width)
        : 1.0f;
    simgui_frame_desc_t frame_desc{};
    frame_desc.width = swapchain.width;
    frame_desc.height = swapchain.height;
    frame_desc.delta_time = dt;
    frame_desc.dpi_scale = dpi;
    simgui_new_frame(&frame_desc); // calls ImGui::NewFrame exactly once
    impl_->options.draw_ui(impl_->options.user);

    sg_pass pass{};
    pass.swapchain = swapchain;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 1.0f};
    sg_begin_pass(&pass);
    impl_->background.draw();
    simgui_render(); // calls ImGui::Render exactly once
    sg_end_pass();
    sg_commit();
    if (!impl_->graphics.present(error_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error_.c_str());
        return SDL_APP_FAILURE;
    }

    ++impl_->frames;
    if (impl_->options.max_frames > 0 && impl_->frames >= impl_->options.max_frames) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void Shell::shutdown() {
    if (!impl_) return;
    if (impl_->sdl_backend_ready) ImGui_ImplSDL3_Shutdown();
    if (impl_->imgui_ready) simgui_shutdown();
    if (impl_->sokol_ready) {
        impl_->background.shutdown();
        sg_shutdown();
    }
    impl_->graphics.shutdown();
    if (impl_->window) SDL_DestroyWindow(impl_->window);
    delete impl_;
    impl_ = nullptr;
}

} // namespace od
