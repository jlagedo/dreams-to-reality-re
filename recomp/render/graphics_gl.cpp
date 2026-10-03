#include "render/graphics_backend.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <emscripten/html5_webgl.h>
#endif

namespace od {
namespace {

// The few GL entry points the readbacks (capture, read_image) call. Everything
// else goes through sokol_gfx, which owns the context's state. WebGL2 (GLES3)
// links them directly; desktop GL gets them from the context, which is how a
// loader would, because only GL 1.1 symbols are exported by the system library.
// The names and values are the OpenGL 3.0 / GLES 3.0 ones.
struct GLApi {
    using Enum = unsigned int;
    using Uint = unsigned int;
    using Int = int;
    using Sizei = int;
    void (*GenFramebuffers)(Sizei, Uint*) = nullptr;
    void (*DeleteFramebuffers)(Sizei, const Uint*) = nullptr;
    void (*BindFramebuffer)(Enum, Uint) = nullptr;
    void (*FramebufferTexture2D)(Enum, Enum, Enum, Uint, Int) = nullptr;
    Enum (*CheckFramebufferStatus)(Enum) = nullptr;
    void (*ReadBuffer)(Enum) = nullptr;
    void (*ReadPixels)(Int, Int, Sizei, Sizei, Enum, Enum, void*) = nullptr;
    void (*GetIntegerv)(Enum, Int*) = nullptr;
    void (*PixelStorei)(Enum, Int) = nullptr;
    Enum (*GetError)() = nullptr;
    bool loaded = false;

    static constexpr Enum FRAMEBUFFER = 0x8D40, READ_FRAMEBUFFER = 0x8CA8,
                          COLOR_ATTACHMENT0 = 0x8CE0, FRAMEBUFFER_COMPLETE = 0x8CD5,
                          FRAMEBUFFER_BINDING = 0x8CA6, READ_FRAMEBUFFER_BINDING = 0x8CAA,
                          READ_BUFFER = 0x0C02, BACK = 0x0405, TEXTURE_2D = 0x0DE1,
                          RGBA = 0x1908, UNSIGNED_BYTE = 0x1401, PACK_ALIGNMENT = 0x0D05,
                          NO_ERROR_ = 0;

    bool load() {
        if (loaded) return true;
#ifdef __EMSCRIPTEN__
        GenFramebuffers = glGenFramebuffers;
        DeleteFramebuffers = glDeleteFramebuffers;
        BindFramebuffer = glBindFramebuffer;
        FramebufferTexture2D = glFramebufferTexture2D;
        CheckFramebufferStatus = glCheckFramebufferStatus;
        ReadBuffer = glReadBuffer;
        ReadPixels = glReadPixels;
        GetIntegerv = glGetIntegerv;
        PixelStorei = glPixelStorei;
        GetError = glGetError;
#else
#define OD_GL(name)                                                                  \
    name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name));      \
    if (!name) return false;
        OD_GL(GenFramebuffers) OD_GL(DeleteFramebuffers) OD_GL(BindFramebuffer)
        OD_GL(FramebufferTexture2D) OD_GL(CheckFramebufferStatus) OD_GL(ReadBuffer)
        OD_GL(ReadPixels) OD_GL(GetIntegerv) OD_GL(PixelStorei) OD_GL(GetError)
#undef OD_GL
#endif
        loaded = true;
        return true;
    }
};

struct GLState {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GLApi gl;
};

// GL stores a framebuffer bottom row first; the D3D11 backend hands out rows
// top first, so a framebuffer readback is reversed into that order.
void flip_rows(std::vector<uint8_t>& rgba, int width, int height) {
    const size_t pitch = size_t(width) * 4;
    std::vector<uint8_t> row(pitch);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* a = rgba.data() + size_t(y) * pitch;
        uint8_t* b = rgba.data() + size_t(height - 1 - y) * pitch;
        std::copy(a, a + pitch, row.begin());
        std::copy(b, b + pitch, a);
        std::copy(row.begin(), row.end(), b);
    }
}

} // namespace

SDL_WindowFlags GraphicsBackend::window_flags() { return SDL_WINDOW_OPENGL; }

bool GraphicsBackend::configure_window(std::string& error) {
#ifdef __EMSCRIPTEN__
    // WebGL2 is GLES 3.0. The page's canvas has no depth or stencil: the
    // renderer draws into its own targets and only blits colour to the canvas.
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
#ifdef __EMSCRIPTEN__
    // A WebGL context can be lost at any time (GPU reset, the browser
    // reclaiming it). sokol_gfx cannot rebuild its resources in place, so this
    // is a fatal, reported failure instead of a silent black canvas; the page
    // decides whether to reload.
    if (emscripten_is_webgl_context_lost(emscripten_webgl_get_current_context())) {
        error = "the WebGL context was lost";
        return FrameState::failed;
    }
#endif
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

// The frame is complete and not yet presented (wd_render_capture runs while a
// present is pending), so the back buffer holds it. The browser build takes
// its frames in host/web/web_glue.c instead, before the canvas is handed over.
bool GraphicsBackend::capture(const std::string& png_path, std::string& error) {
    auto* state = static_cast<GLState*>(state_);
    if (!state) return false;
    if (!state->gl.load()) {
        error = "OpenGL readback entry points are missing";
        return false;
    }
    GLApi& gl = state->gl;
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(state->window, &width, &height) || width < 1 || height < 1) {
        error = std::string("OpenGL frame capture has no drawable: ") + SDL_GetError();
        return false;
    }
    GLApi::Int read_fbo = 0;
    GLApi::Int read_buffer = 0;
    gl.GetIntegerv(GLApi::READ_FRAMEBUFFER_BINDING, &read_fbo);
    gl.GetIntegerv(GLApi::READ_BUFFER, &read_buffer);
    gl.BindFramebuffer(GLApi::READ_FRAMEBUFFER, 0);
    gl.ReadBuffer(GLApi::BACK);
    gl.PixelStorei(GLApi::PACK_ALIGNMENT, 1);
    while (gl.GetError() != GLApi::NO_ERROR_) {
    }
    std::vector<uint8_t> rgba(size_t(width) * height * 4u);
    gl.ReadPixels(0, 0, width, height, GLApi::RGBA, GLApi::UNSIGNED_BYTE, rgba.data());
    const GLApi::Enum status = gl.GetError();
    gl.BindFramebuffer(GLApi::READ_FRAMEBUFFER, GLApi::Uint(read_fbo));
    if (read_fbo != 0) gl.ReadBuffer(GLApi::Enum(read_buffer));
    sg_reset_state_cache();
    if (status != GLApi::NO_ERROR_) {
        error = "OpenGL frame capture failed: glReadPixels error " + std::to_string(status);
        return false;
    }
    ++downloads_;
    flip_rows(rgba, width, height);
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                                 rgba.data(), width * 4);
    if (!surface) {
        error = std::string("cannot create capture surface: ") + SDL_GetError();
        return false;
    }
    const bool saved = SDL_SavePNG(surface, png_path.c_str());
    if (!saved) error = std::string("cannot save frame capture: ") + SDL_GetError();
    SDL_DestroySurface(surface);
    return saved;
}

bool GraphicsBackend::present(std::string& error) {
    auto* state = static_cast<GLState*>(state_);
    if (!SDL_GL_SwapWindow(state->window)) {
        error = std::string("SDL OpenGL presentation failed: ") + SDL_GetError();
        return false;
    }
    return true;
}

// Explicit CPU export of an RGBA8 image region, as the D3D11 backend does it:
// rgba[row * width + col] is R | G << 8 | B << 16 | A << 24 and row 0 is the
// logical top. Textures made from uploads are stored top row first. The
// renderer's own targets are drawn with GL's origin at the bottom (the shaders
// flip the sampling to compensate, `flip` in direct.cpp), so their rows are
// turned over here and callers see the same orientation as on D3D11.
// ReadPixels waits for the GPU; WebGL2 has no other synchronous download, so
// this is for tests, thumbnails and captures, never for a frame.
bool GraphicsBackend::read_image(sg_image image, int x, int y, int width, int height,
                                 std::vector<uint32_t>& rgba, std::string& error) {
    auto* state = static_cast<GLState*>(state_);
    if (!state || !sg_isvalid() || sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
        x < 0 || y < 0 || width <= 0 || height <= 0) {
        error = "invalid GPU export request";
        return false;
    }
    const int image_width = sg_query_image_width(image);
    const int image_height = sg_query_image_height(image);
    if (sg_query_image_pixelformat(image) != SG_PIXELFORMAT_RGBA8 ||
        sg_query_image_sample_count(image) != 1 || uint64_t(x) + width > uint64_t(image_width) ||
        uint64_t(y) + height > uint64_t(image_height)) {
        error = "GPU export requires an in-bounds single-sample RGBA8 region";
        return false;
    }
    if (!state->gl.load()) {
        error = "OpenGL framebuffer functions are unavailable for GPU export";
        return false;
    }
    GLApi& gl = state->gl;
    const sg_gl_image_info info = sg_gl_query_image_info(image);
    const GLApi::Uint texture = info.tex[info.active_slot];
    if (!texture || info.tex_target != GLApi::TEXTURE_2D) {
        error = "GPU export requires a 2D texture";
        return false;
    }
    const bool target_rows_flipped = sg_query_image_usage(image).color_attachment;
    const int first_row = target_rows_flipped ? image_height - y - height : y;
    GLApi::Int previous = 0;
    gl.GetIntegerv(GLApi::FRAMEBUFFER_BINDING, &previous);
    GLApi::Uint framebuffer = 0;
    gl.GenFramebuffers(1, &framebuffer);
    gl.BindFramebuffer(GLApi::FRAMEBUFFER, framebuffer);
    gl.FramebufferTexture2D(GLApi::FRAMEBUFFER, GLApi::COLOR_ATTACHMENT0, GLApi::TEXTURE_2D,
                            texture, 0);
    bool ok = gl.CheckFramebufferStatus(GLApi::FRAMEBUFFER) == GLApi::FRAMEBUFFER_COMPLETE;
    std::vector<uint32_t> rows;
    if (ok) {
        rows.resize(size_t(width) * height);
        gl.PixelStorei(GLApi::PACK_ALIGNMENT, 4);
        gl.ReadPixels(x, first_row, width, height, GLApi::RGBA, GLApi::UNSIGNED_BYTE, rows.data());
        ok = gl.GetError() == GLApi::NO_ERROR_;
    }
    gl.BindFramebuffer(GLApi::FRAMEBUFFER, GLApi::Uint(previous));
    gl.DeleteFramebuffers(1, &framebuffer);
    sg_reset_state_cache();
    if (!ok) {
        error = "GPU export readback failed";
        return false;
    }
    ++downloads_;
    rgba.resize(size_t(width) * height);
    for (int row = 0; row < height; ++row) {
        const int source_row = target_rows_flipped ? height - 1 - row : row;
        std::memcpy(rgba.data() + size_t(row) * width, rows.data() + size_t(source_row) * width,
                    size_t(width) * sizeof(uint32_t));
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
