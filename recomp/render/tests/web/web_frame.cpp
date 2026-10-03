// One frame drawn with the library's own API, the same program natively and in
// the browser: textured and flat scene triangles with fog and translucency,
// packed 2D blits, a line, a dim and a band, then the final gamma pass into the
// window. The target is exported with GraphicsBackend::read_image and written
// to "frame.rgba" (width, height as two 32-bit words, then RGBA8 rows, top
// first) so the browser result can be compared with the native one.
//
//   web_frame [--frames N] [--out PATH]
//
// Native (D3D11, Metal or GL) renders one frame and exits. In the browser it
// keeps presenting frames until the page closes.
#include "render/direct_sokol.h"
#include "render/fog.h"
#include "render/graphics_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
constexpr int kWidth = 640, kHeight = 480;

struct App {
    SDL_Window *window = nullptr;
    od::GraphicsBackend backend;
    od_renderer *renderer = nullptr;
    od_render_id target = 0, checker = 0, packed = 0, output_target = 0;
    unsigned frame = 0;
    unsigned max_frames = 0;
    std::string out_path = "frame.rgba";
    bool exported = false;
    uint64_t hash = 0;
};
App app;

void fail(const char *what, const char *detail = "") {
    std::fprintf(stderr, "FAIL: %s %s\n", what, detail);
    std::fflush(stderr);
#ifdef __EMSCRIPTEN__
    const std::string text = std::string(what) + ": " + detail;
    MAIN_THREAD_EM_ASM({ if (Module.onFrameDone) Module.onFrameDone(1, UTF8ToString($0)); },
                       text.c_str());
    emscripten_force_exit(1);
#endif
    std::exit(1);
}
void sokol_log(const char *, uint32_t level, uint32_t item, const char *msg, uint32_t, const char *,
               void *) {
    // The message text exists only in a SOKOL_DEBUG build; the item always.
    std::fprintf(stderr, "sokol[level %u item %u]: %s\n", level, item, msg ? msg : "");
    if (level <= 1) fail("sokol error item", std::to_string(item).c_str());
}

void draw_scene() {
    od_renderer *r = app.renderer;
    // Camera space, looking down +z. Node 0 identity, node 1 shifted.
    od_pose_node nodes[2] = {{-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
                             {-1, {1, 0, 0, 0.6f, 0, 1, 0, 0.1f, 0, 0, 1, 0.5f}}};
    std::vector<od_scene_vertex> v;
    std::vector<od_scene_triangle> t;
    auto quad = [&](unsigned node, float x0, float y0, float x1, float y1, float z0, float z1,
                    float u, float vv, od_render_id texture, uint32_t colour, od_face_mode mode,
                    bool wrap) {
        const unsigned base = unsigned(v.size());
        v.push_back({{x0, y0, z0}});
        v.push_back({{x1, y0, z0}});
        v.push_back({{x1, y1, z1}});
        v.push_back({{x0, y1, z1}});
        const float uv[4][2] = {{0, 0}, {u, 0}, {u, vv}, {0, vv}};
        const unsigned idx[2][3] = {{0, 1, 2}, {0, 2, 3}};
        for (auto &tri : idx) {
            od_scene_triangle f{};
            for (int c = 0; c < 3; ++c)
                f.corners[c] = {node, base + tri[c], {uv[tri[c]][0], uv[tri[c]][1]}};
            f.texture = texture;
            f.colour = colour;
            f.mode = mode;
            f.wrap_texture = wrap;
            t.push_back(f);
        }
    };
    // Floor: receding textured plane (y down is +y in this projection), repeated.
    quad(0, -6, 1.2f, 6, -1, 20, 3, 6, 6, app.checker, 0xffffffff, OD_FACE_OPAQUE, true);
    // Coloured quads at different depths and one translucent in front.
    quad(0, -3.2f, -0.9f, -1.2f, 0.5f, 7, 7, 1, 1, 0, 0xff2040e0, OD_FACE_OPAQUE, false);
    quad(1, -1.5f, -0.6f, 0.8f, 0.8f, 6, 6, 1, 1, 0, 0xff30c050, OD_FACE_OPAQUE, false);
    quad(0, 1.8f, -0.7f, 3.5f, 0.6f, 9, 9, 1, 1, 0, 0xffe0a020, OD_FACE_OPAQUE, false);
    quad(0, -1.0f, -0.5f, 1.6f, 0.4f, 4.5f, 4.5f, 1, 1, 0, 0xc0ffffff, OD_FACE_TRANSLUCENT, false);
    // Iterated brightness on one more.
    {
        const size_t first = t.size();
        quad(0, 3.5f, -1.0f, 5.5f, 0.2f, 8, 8, 1, 1, 0, 0xffffffff, OD_FACE_OPAQUE, false);
        for (size_t i = first; i < t.size(); ++i) {
            t[i].use_corner_brightness = 1;
            for (int c = 0; c < 3; ++c)
                t[i].corner_brightness[c] = uint8_t(60 + 70 * ((i - first) + c));
        }
    }
    od_scene_packet p{};
    p.target = app.target;
    p.nodes = nodes;
    p.node_count = 2;
    p.vertices = v.data();
    p.vertex_count = v.size();
    p.triangles = t.data();
    p.triangle_count = t.size();
    p.clear = 1;
    // After 120 frames the sky turns warm, so a screenshot taken later shows
    // whether frames keep reaching the page.
    p.clear_colour[0] = app.frame >= 120 ? 0.85f : 0.25f;
    p.clear_colour[1] = 0.45f;
    p.clear_colour[2] = 0.7f;
    p.clear_colour[3] = 1;
    p.fog.enabled = 1;
    p.fog.colour = 0xffb0a090;
    std::array<uint8_t, 64> table{};
    od::port::guFogGenerateExp(table, .06f);
    std::copy(table.begin(), table.end(), p.fog.table);
    if (!od_projection_hor_plus(1.0f, float(kWidth) / kHeight, .5f, 1000, p.view_projection))
        fail("projection");
    if (!od_renderer_scene(r, &p)) fail("scene", od_renderer_error(r));
}

void draw_2d() {
    od_renderer *r = app.renderer;
    auto submit = [&](od_draw_2d d) {
        if (!od_renderer_draw_2d(r, &d)) fail("draw_2d", od_renderer_error(r));
    };
    // The packed gradient (RGB565, full coverage) twice: plain and blended.
    submit({OD_DRAW_RAW, app.target, app.packed, {16, 16, 64, 64}, OD_RGB565, 0});
    submit({OD_DRAW_FILL, app.target, 0, {100, 16, 40, 40}, OD_RGB565, 0xf800});
    submit({OD_DRAW_HALF, app.target, app.packed, {60, 40, 64, 64}, OD_RGB565, 0});
    submit({OD_DRAW_BAND, app.target, 0, {16, 90, 200, 12}, OD_RGB565, 0});
    if (!od_renderer_line_2d(r, app.target, 150, 20, 300, 80, 0xffe0, OD_RGB565))
        fail("line", od_renderer_error(r));
    if (!od_renderer_line_2d(r, app.target, 300, 20, 150, 80, 0x07ff, OD_RGB565))
        fail("line", od_renderer_error(r));
}

uint64_t fnv(const std::vector<uint32_t> &pixels) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t p : pixels)
        for (int i = 0; i < 4; ++i) {
            h ^= (p >> (8 * i)) & 255;
            h *= 1099511628211ull;
        }
    return h;
}

void export_frame() {
    std::vector<uint32_t> pixels;
    std::string error;
    if (!app.backend.read_image(od_renderer_image(app.renderer, app.target), 0, 0, kWidth, kHeight,
                                pixels, error))
        fail("read_image", error.c_str());
    app.hash = fnv(pixels);
    if (FILE *f = std::fopen(app.out_path.c_str(), "wb")) {
        const uint32_t header[2] = {kWidth, kHeight};
        std::fwrite(header, 4, 2, f);
        std::fwrite(pixels.data(), 4, pixels.size(), f);
        std::fclose(f);
    }
    std::printf("frame exported: %dx%d fnv1a=%016llx corner=%08x centre=%08x\n", kWidth, kHeight,
                (unsigned long long)app.hash, pixels[0], pixels[(kHeight / 2) * kWidth + kWidth / 2]);
    std::fflush(stdout);
}

bool one_frame() {
    od_renderer *r = app.renderer;
    draw_scene();
    draw_2d();
    sg_swapchain swapchain{};
    std::string error;
    const od::FrameState state = app.backend.acquire(app.window, swapchain, error);
    if (state == od::FrameState::failed) fail("acquire", error.c_str());
    if (state == od::FrameState::ready) {
        sg_pass pass{};
        pass.swapchain = swapchain;
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = {0, 0, 0, 1};
        sg_begin_pass(&pass);
        if (!od_renderer_output(r, app.target, 1.0f)) fail("output", od_renderer_error(r));
        sg_end_pass();
    }
    sg_commit();
    od_renderer_frame_complete(r);
    if (!app.exported) {
        export_frame();
        app.exported = true;
#ifdef __EMSCRIPTEN__
        MAIN_THREAD_EM_ASM({ if (Module.onFrameDone) Module.onFrameDone(0, UTF8ToString($0)); },
               std::to_string(app.hash).c_str());
#endif
    }
    if (state == od::FrameState::ready && !app.backend.present(error)) fail("present", error.c_str());
    ++app.frame;
    if (app.frame % 60 == 0) {
        std::printf("frame %u at %.2f s\n", app.frame, SDL_GetTicks() / 1000.0);
        std::fflush(stdout);
    }
    return app.max_frames == 0 || app.frame < app.max_frames;
}

#ifdef __EMSCRIPTEN__
void loop() {
    one_frame();
}
#endif
} // namespace

int main(int argc, char **argv) {
#ifndef __EMSCRIPTEN__
    app.max_frames = 1;
#endif
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) app.max_frames = unsigned(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) app.out_path = argv[++i];
    }
    std::string error;
    if (!SDL_Init(SDL_INIT_VIDEO)) fail("SDL_Init", SDL_GetError());
    if (!od::GraphicsBackend::configure_window(error)) fail("configure_window", error.c_str());
    Uint32 flags = od::GraphicsBackend::window_flags();
#ifndef __EMSCRIPTEN__
    flags |= SDL_WINDOW_HIDDEN;
#endif
    app.window = SDL_CreateWindow("ODRender web frame", kWidth, kHeight, flags);
    if (!app.window) fail("SDL_CreateWindow", SDL_GetError());
    if (!app.backend.init(app.window, error)) fail("backend.init", error.c_str());
    sg_desc desc{};
    desc.environment = app.backend.environment();
    desc.logger.func = sokol_log;
    sg_setup(&desc);
    if (!sg_isvalid()) fail("sg_setup");
    std::printf("sokol backend %d, origin_top_left %d, max_image_size %d\n", int(sg_query_backend()),
                int(sg_query_features().origin_top_left), sg_query_limits().max_image_size_2d);
    app.renderer = od_renderer_create();
    if (!app.renderer) fail("od_renderer_create");
    app.target = od_renderer_target(app.renderer, kWidth, kHeight, kWidth, kHeight);
    if (!app.target) fail("target", od_renderer_error(app.renderer));
    std::vector<uint32_t> texels(16 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            texels[y * 16 + x] = ((x / 4 + y / 4) & 1) ? 0xff303030u : 0xffe8e8e8u;
    app.checker = od_renderer_upload_rgba(app.renderer, 16, 16, texels.data(), 16 * 4);
    if (!app.checker) fail("checker", od_renderer_error(app.renderer));
    std::vector<uint32_t> packed(64 * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            packed[y * 64 + x] =
                uint32_t(((x >> 1) << 11) | ((y) << 5) | (31 - (x >> 1))) | (63u << 16);
    app.packed = od_renderer_upload_packed(app.renderer, 64, 64, packed.data(), 64 * 4);
    if (!app.packed) fail("packed", od_renderer_error(app.renderer));
#if defined(__EMSCRIPTEN__) && defined(OD_WEB_BLOCKING_LOOP)
    // How the game's own loop runs on a pthread: it never returns to the
    // browser's event loop, it presents and sleeps.
    for (;;) {
        one_frame();
        SDL_Delay(16);
    }
#elif defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(loop, 0, true);
#else
    while (one_frame()) {
    }
    od_renderer_destroy(app.renderer);
    sg_shutdown();
    app.backend.shutdown();
    SDL_DestroyWindow(app.window);
    SDL_Quit();
#endif
    return 0;
}
