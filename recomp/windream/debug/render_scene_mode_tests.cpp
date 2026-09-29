#include "render_scene_draw.h"
#include "platform/graphics_backend.h"
#include "render/direct_sokol.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

static void check(bool ok, const char *message) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
int main(int argc, char **argv) {
    check(argc == 2, "usage: WDSceneModeTests output.wds");
    wd::SceneSnapshot fog_source, fog_copy;
    std::string io_error;
    fog_source.camera.screen_width = 640;
    fog_source.camera.screen_height = 480;
    fog_source.camera.focal_x = 320;
    fog_source.camera.focal_y = 240;
    fog_source.camera.center_x = 320;
    fog_source.camera.center_y = 240;
    fog_source.camera.near_plane = 1;
    fog_source.camera.far_plane = 100;
    fog_source.camera.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    fog_source.fog.enabled = 1;
    fog_source.vertices.push_back({{16777216.0f, 0, 0}});
    fog_source.vertex_addresses.push_back(0x18000000);
    fog_source.source_vertices.push_back({16777217, 0, 0});
    fog_source.lights[7].type = 1;
    fog_source.lights[7].position = {-10, 20, 30};
    fog_source.lights[7].outer_radius = 100;
    fog_source.lights[7].intensity = 31;
    fog_source.fog.colour = 0xff123456;
    for (int i = 0; i < 64; ++i)
        fog_source.fog.table[i] = uint8_t(i * 4);
    bool transferred = wd::write_scene(fog_source, argv[1], io_error);
    check(transferred, io_error.c_str());
    transferred = wd::read_scene(argv[1], fog_copy, io_error);
    check(transferred, io_error.c_str());
    check(fog_copy.fog.enabled == 1 && fog_copy.fog.colour == fog_source.fog.colour &&
              std::equal(std::begin(fog_source.fog.table), std::end(fog_source.fog.table),
                         fog_copy.fog.table),
          "fog scene round trip");
    check(fog_copy.source_vertices == fog_source.source_vertices &&
              fog_copy.lights[7].position == fog_source.lights[7].position &&
              fog_copy.lights[7].outer_radius == 100 && fog_copy.lights[7].intensity == 31,
          "WDS4 exact lighting inputs round trip");
    check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
    od::GraphicsBackend backend;
    std::string error;
    check(backend.configure_window(error), error.c_str());
    auto *window =
        SDL_CreateWindow("Scene mode oracle", 256, 128, SDL_WINDOW_HIDDEN | backend.window_flags());
    check(window != nullptr, SDL_GetError());
    check(backend.init(window, error), error.c_str());
    sg_desc config{};
    config.environment = backend.environment();
    sg_setup(&config);
    auto *renderer = od_renderer_create();
    check(renderer != nullptr, "renderer creation");
    wd::SceneSnapshot scene;
    scene.poses.push_back({-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    scene.nodes.resize(1);
    scene.nodes[0].submitted = true;
    scene.nodes[0].light_count = 1;
    scene.camera.screen_width = 256;
    scene.camera.screen_height = 128;
    scene.camera.focal_x = 128;
    scene.camera.focal_y = 64;
    scene.camera.center_x = 128;
    scene.camera.center_y = 64;
    scene.camera.near_plane = 1;
    scene.camera.far_plane = 100;
    scene.camera.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    auto face = [&](float x, float z, bool reverse, uint32_t block = 1) {
        const uint32_t base = uint32_t(scene.vertices.size());
        scene.vertices.insert(scene.vertices.end(),
                              {{{x - .8f, -.8f, z}}, {{x, .8f, z}}, {{x + .8f, -.8f, z}}});
        wd::SceneFace f;
        f.type = 1;
        f.block = block;
        f.colour = 0xffff; // deliberately ignored by Glide
        for (uint32_t i = 0; i < 3; ++i)
            f.corners[i] = {0, base + (reverse ? 2 - i : i), {0, 0}};
        scene.faces.push_back(f);
    };
    face(-2, 4, false);
    face(0, 4, true);
    face(20, 4, false);
    face(0, 200, false);
    face(2, 4, false);
    auto target = od_renderer_target(renderer, 256, 128, 256, 128);
    wd::SceneDraw draw;
    auto render = [&]() {
        check(draw.submit(renderer, scene, target, 256, 128, false, error), error.c_str());
        sg_commit();
        std::vector<uint32_t> result;
        check(
            backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128, result, error),
            error.c_str());
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
        return result;
    };
    auto pixels = render();
    check(pixels[64 * 256 + 192] == 0xfff94b02,
          "diagnostic sequence counted rejected faces or used block colour");
    check(od_renderer_stats(renderer).scene_triangles == 2, "diagnostic visible face count");
    scene.faces.back().block = 2;
    pixels = render();
    check(pixels[64 * 256 + 192] == 0xff000000, "diagnostic colour did not reset per block");
    // Geometry outside the old 4:3 frustum must survive Hor+ independently of old flags.
    scene.faces.clear();
    scene.vertices.clear();
    face(5, 4, false);
    scene.camera.screen_width = 640;
    scene.camera.screen_height = 480;
    scene.camera.focal_x = 320;
    scene.camera.focal_y = 240;
    scene.camera.center_x = 320;
    scene.camera.center_y = 240;
    const auto before = od_renderer_stats(renderer).scene_triangles;
    check(draw.submit(renderer, scene, target, 640, 480, true, error), error.c_str());
    check(od_renderer_stats(renderer).scene_triangles == before,
          "narrow frustum accepted outside diagnostic face");
    check(draw.submit(renderer, scene, target, 1920, 1080, true, error), error.c_str());
    check(od_renderer_stats(renderer).scene_triangles == before + 1,
          "Hor+ did not expose diagnostic face");
    sg_commit();
    draw.reset(renderer);
    wd::SceneSnapshot lit;
    lit.light_transform_count = 1;
    lit.camera = fog_source.camera;
    lit.poses.push_back({-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    lit.nodes.resize(1);
    lit.nodes[0].submitted = true;
    lit.nodes[0].light_count = 1;
    lit.nodes[0].shade = 7;
    lit.lights[0].type = 1;
    lit.lights[0].inner_radius = 1000;
    lit.lights[0].outer_radius = 2000;
    lit.materials.resize(1);
    lit.materials[0].indices.assign(65536, 1);
    for (unsigned row = 0; row < 32; ++row)
        lit.materials[0].palette[row * 256 + 1] = uint16_t((31 - row) << 11);
    const int32_t points[18] = {-1, -1, 10, 0, 1, 10, 1, -1, 10, 1, -1, 10, 2, 1, 10, 3, -1, 10};
    for (unsigned i = 0; i < 6; ++i) {
        lit.vertices.push_back(
            {{float(points[i * 3]), float(points[i * 3 + 1]), float(points[i * 3 + 2])}});
        lit.source_vertices.push_back({points[i * 3], points[i * 3 + 1], points[i * 3 + 2]});
    }
    for (unsigned i = 0; i < 2; ++i) {
        wd::SceneFace f;
        f.type = 3;
        f.block = 1;
        f.material = 0;
        f.address = 0x18002000 + i * 0x80;
        f.normal_address = 0x18003000 + i * 16;
        f.normal = {0, 0, -32768};
        f.plane_distance = -10;
        f.shade = i ? 23 : 19;
        for (unsigned c = 0; c < 3; ++c)
            f.corners[c] = {0, i * 3 + c, {0, 0}};
        lit.faces.push_back(f);
    }
    for (unsigned frame = 0; frame < 4; ++frame) {
        const bool culled = frame == 1 || frame == 3;
        for (unsigned i = 0; i < 3; ++i) {
            lit.source_vertices[i][0] = points[i * 3] + (culled ? 100 : 0);
            lit.vertices[i].xyz[0] = float(lit.source_vertices[i][0]);
        }
        lit.lights[0].position[2] = frame ? -10 : 0;
        lit.lights[0].intensity = frame ? 15 : 31;
        lit.materials[0].page =
            0x300000 + frame * 0x10000; // force the palette request to be observable
        if (frame == 3) {
            draw.reset(renderer);
            lit.faces[0].shade = 7;
        }
        std::vector<wd::SceneLightingWrite> writes;
        const bool ok = draw.submit(renderer, lit, target, 256, 128, false, error, &writes);
        check(ok, error.c_str());
        sg_commit();
        std::vector<uint32_t> result;
        check(
            backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128, result, error),
            "lit capture");
        const unsigned expected = frame < 2 ? 248 : frame == 2 ? 120 : 56;
        check((result[64 * 256 + 153] & 255) == expected,
              "lit palette request ignored head/history/source reuse");
        for (const auto &write : writes)
            for (auto &f : lit.faces)
                if (write.address == f.address + 0x40 && write.bytes == 1)
                    f.shade = uint8_t(write.value);
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
    }
    draw.reset(renderer);
    wd::SceneLighting rejected;
    float lit_vp[16];
    check(lit.view_projection(256, 128, false, lit_vp), "lit test camera");
    lit.light_transform_count = 0;
    check(!wd::prepare_flat_lighting(lit, lit_vp, rejected, error, true),
          "unrefreshed light prefix accepted");
    lit.light_transform_count = 1;
    lit.lights[0].type = 2;
    lit.lights[0].orientation = {32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
    lit.lights[0].intensity = 15;
    check(wd::prepare_flat_lighting(lit, lit_vp, rejected, error, true), error.c_str());
    check(rejected.shades[1] == 15, "oriented flat shade");
    auto axis_write = std::find_if(rejected.writes.begin(), rejected.writes.end(),
                                   [](const auto &w) { return w.address == 0x672778; });
    check(axis_write != rejected.writes.end() && axis_write->value == 32768,
          "oriented light direction feedback");
    lit.materials[0].page += 0x10000;
    lit.faces[0].shade = 15; // culled head supplies the palette row
    check(draw.submit(renderer, lit, target, 256, 128, false, error), error.c_str());
    sg_commit();
    std::vector<uint32_t> oriented_pixels;
    check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128, oriented_pixels,
                             error),
          "oriented capture");
    check((oriented_pixels[64 * 256 + 153] & 255) == 120, "oriented GPU palette row");
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    draw.reset(renderer);
    lit.lights[0].type = 3;
    check(!wd::prepare_flat_lighting(lit, lit_vp, rejected, error, true),
          "unsupported light kind accepted");
    od_renderer_destroy(renderer);
    sg_shutdown();
    backend.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("scene modes: diagnostic colour sequence, winding/frustum rejection, block reset and "
              "Hor+ visibility passed");
    std::puts("scene lighting: lit/culled/lit palette rows, metadata feedback and reused-source "
              "shade passed");
}
