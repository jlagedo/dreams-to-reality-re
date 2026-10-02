#include "render/graphics_backend.h"

#include <cstdint>
#include <new>
#include <vector>

namespace od {
namespace {

struct GLState {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
};

// The few GL calls the readbacks need, resolved through SDL at first use so no
// GL header or loader is involved (sokol loads its own). The names and values
// are the OpenGL 3.0 core ones; the context is 4.1 core (configure_window).
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kReadFramebufferBinding = 0x8CAA;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kTexture2D = 0x0DE1;
constexpr GLenum kRGBA = 0x1908;
constexpr GLenum kUnsignedByte = 0x1401;
constexpr GLenum kPackAlignment = 0x0D05;
constexpr GLenum kBack = 0x0405;
constexpr GLenum kReadBuffer = 0x0C02;

struct GLReadback {
    void (*genFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*deleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*bindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*framebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*checkFramebufferStatus)(GLenum) = nullptr;
    void (*readBuffer)(GLenum) = nullptr;
    void (*readPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*pixelStorei)(GLenum, GLint) = nullptr;
    void (*getIntegerv)(GLenum, GLint*) = nullptr;
    GLenum (*getError)() = nullptr;
    bool loaded = false;

    template <typename F>
    static bool load(F& slot, const char* name) {
        slot = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
        return slot != nullptr;
    }

    bool ensure(std::string& error) {
        if (loaded) return true;
        loaded = load(genFramebuffers, "glGenFramebuffers") &&
                 load(deleteFramebuffers, "glDeleteFramebuffers") &&
                 load(bindFramebuffer, "glBindFramebuffer") &&
                 load(framebufferTexture2D, "glFramebufferTexture2D") &&
                 load(checkFramebufferStatus, "glCheckFramebufferStatus") &&
                 load(readBuffer, "glReadBuffer") && load(readPixels, "glReadPixels") &&
                 load(pixelStorei, "glPixelStorei") && load(getIntegerv, "glGetIntegerv") &&
                 load(getError, "glGetError");
        if (!loaded) error = "OpenGL readback entry points are missing";
        return loaded;
    }
};

GLReadback g_gl;

// GL stores a framebuffer bottom row first; the D3D11 backend hands out rows
// top first, so every readback is reversed into that order.
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

// The frame is complete and not yet presented (wd_render_capture runs while a
// present is pending), so the back buffer holds it.
bool GraphicsBackend::capture(const std::string& png_path, std::string& error) {
    auto* state = static_cast<GLState*>(state_);
    if (!state || !g_gl.ensure(error)) return false;
    int width = 0;
    int height = 0;
    if (!SDL_GetWindowSizeInPixels(state->window, &width, &height) || width < 1 || height < 1) {
        error = std::string("OpenGL frame capture has no drawable: ") + SDL_GetError();
        return false;
    }
    GLint read_fbo = 0;
    GLint read_buffer = 0;
    g_gl.getIntegerv(kReadFramebufferBinding, &read_fbo);
    g_gl.getIntegerv(kReadBuffer, &read_buffer);
    g_gl.bindFramebuffer(kReadFramebuffer, 0);
    g_gl.readBuffer(kBack);
    g_gl.pixelStorei(kPackAlignment, 1);
    while (g_gl.getError() != 0) {
    }
    std::vector<uint8_t> rgba(size_t(width) * height * 4u);
    g_gl.readPixels(0, 0, width, height, kRGBA, kUnsignedByte, rgba.data());
    const GLenum status = g_gl.getError();
    g_gl.bindFramebuffer(kReadFramebuffer, GLuint(read_fbo));
    if (read_fbo != 0) g_gl.readBuffer(GLenum(read_buffer));
    sg_reset_state_cache();
    if (status != 0) {
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

// A sokol render-target texture, read through a framebuffer of its own. The
// renderer draws into GL textures bottom row first and flips when it samples
// them (direct.glsl operation.w), so the rows are reversed here as in capture.
bool GraphicsBackend::read_image(sg_image image, int x, int y, int width, int height,
                                 std::vector<uint32_t>& rgba, std::string& error) {
    auto* state = static_cast<GLState*>(state_);
    if (!state || !sg_isvalid() || sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
        x < 0 || y < 0 || width <= 0 || height <= 0) {
        error = "invalid GPU export request";
        return false;
    }
    const sg_image_desc desc = sg_query_image_desc(image);
    if (desc.pixel_format != SG_PIXELFORMAT_RGBA8 || desc.sample_count != 1 ||
        x + width > desc.width || y + height > desc.height) {
        error = "GPU export requires an in-bounds single-sample RGBA8 region";
        return false;
    }
    const sg_gl_image_info info = sg_gl_query_image_info(image);
    const GLuint texture = info.tex[info.active_slot];
    if (!texture || info.tex_target != kTexture2D) {
        error = "GPU export needs a 2D OpenGL texture";
        return false;
    }
    if (!g_gl.ensure(error)) return false;
    GLint read_fbo = 0;
    g_gl.getIntegerv(kReadFramebufferBinding, &read_fbo);
    GLuint fbo = 0;
    g_gl.genFramebuffers(1, &fbo);
    g_gl.bindFramebuffer(kReadFramebuffer, fbo);
    g_gl.framebufferTexture2D(kReadFramebuffer, kColorAttachment0, kTexture2D, texture, 0);
    bool ok = g_gl.checkFramebufferStatus(kReadFramebuffer) == kFramebufferComplete;
    std::vector<uint8_t> bytes;
    if (ok) {
        g_gl.readBuffer(kColorAttachment0);
        g_gl.pixelStorei(kPackAlignment, 1);
        while (g_gl.getError() != 0) {
        }
        bytes.resize(size_t(width) * height * 4u);
        // Rows are reversed below, so the region is read from the matching
        // bottom-up position.
        const int gl_y = desc.height - (y + height);
        g_gl.readPixels(x, gl_y, width, height, kRGBA, kUnsignedByte, bytes.data());
        ok = g_gl.getError() == 0;
        if (!ok) error = "OpenGL image export failed: glReadPixels";
    } else {
        error = "OpenGL image export: the texture does not attach to a framebuffer";
    }
    g_gl.bindFramebuffer(kReadFramebuffer, GLuint(read_fbo));
    g_gl.deleteFramebuffers(1, &fbo);
    sg_reset_state_cache();
    if (!ok) return false;
    ++downloads_;
    flip_rows(bytes, width, height);
    rgba.resize(size_t(width) * height);
    for (size_t i = 0; i < rgba.size(); ++i) {
        const uint8_t* p = bytes.data() + i * 4;
        rgba[i] = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
                  uint32_t(p[3]) << 24;
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
