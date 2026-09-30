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
    scene.faces[0].flags = 8;
    wd::SceneCollector collector;
    const auto collector_before = od_renderer_stats(renderer);
    check(wd::prepare_scene_collector(scene, 256, 128, false, collector, error), error.c_str());
    check(collector.total_triangles == 2 && collector.triangles.size() == 2 &&
              collector.triangles[0].face_flags == 1,
          "collector visibility or source face flag");
    const auto collector_after = od_renderer_stats(renderer);
    check(collector_before.scene_triangles == collector_after.scene_triangles &&
              collector_before.draws_2d == collector_after.draws_2d,
          "collector issued GPU work");
    wd::SceneCollector bounded;
    check(wd::prepare_scene_collector(scene, 256, 128, false, bounded, error, 1), error.c_str());
    check(bounded.triangles.size() == 1 && bounded.total_triangles == 2,
          "collector capacity lost full diagnostic count");
    auto cross_scene = scene;
    cross_scene.nodes.push_back(scene.nodes[0]);
    cross_scene.poses.push_back(scene.poses[0]);
    cross_scene.poses[1].local[3] = 1;
    cross_scene.faces[0].corners[0].node = 1;
    check(wd::prepare_scene_collector(cross_scene, 256, 128, false, bounded, error), error.c_str());
    check(bounded.triangles[0].points[0][0] == collector.triangles[0].points[0][0] + 32,
          "collector ignored per-corner transform owner");
    auto near_scene = scene;
    near_scene.faces = {scene.faces.front()};
    near_scene.vertices[near_scene.faces[0].corners[0].vertex].xyz[2] = .5f;
    check(wd::prepare_scene_collector(near_scene, 256, 128, false, bounded, error), error.c_str());
    check(!bounded.triangles.empty() && std::any_of(bounded.triangles.begin(), bounded.triangles.end(),
              [](const auto &t) { return t.clipped[0] || t.clipped[1] || t.clipped[2]; }),
          "collector near-plane clipping lost geometry/flags");
    for (const auto &triangle : bounded.triangles)
        for (const auto &point : triangle.points)
            check(point[0] >= 0 && point[0] <= 256 && point[1] >= 0 && point[1] <= 128,
                  "collector projected outside clip canvas");
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
    check(!wd::prepare_scene_lighting(lit, lit_vp, rejected, error, true),
          "unrefreshed light prefix accepted");
    lit.light_transform_count = 1;
    lit.lights[0].type = 2;
    lit.lights[0].orientation = {32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
    lit.lights[0].intensity = 15;
    check(wd::prepare_scene_lighting(lit, lit_vp, rejected, error, true), error.c_str());
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
    check(!wd::prepare_scene_lighting(lit, lit_vp, rejected, error, true),
          "unsupported light kind accepted");
    // Demo/retail Glide uses the three face bytes at +0x41..+0x43 for
    // type 0x16..0x18, shifting them by three before iterated RGB sampling.
    lit.nodes[0].light_count = 0;
    lit.faces.resize(1);
    lit.faces[0].type = 0x16;
    lit.faces[0].corner_shades = {0, 31, 31};
    lit.nodes[0].first_vertex = 0;
    lit.nodes[0].vertex_count = uint32_t(lit.vertices.size());
    lit.vertex_addresses.resize(lit.vertices.size());
    lit.materials[0].page += 0x10000;
    lit.materials[0].palette.fill(0xffff);
    const int32_t gouraud_points[9] = {-2, -2, 4, 0, 2, 4, 2, -2, 4};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned axis = 0; axis < 3; ++axis) {
            lit.source_vertices[i][axis] = gouraud_points[i * 3 + axis];
            lit.vertices[i].xyz[axis] = float(gouraud_points[i * 3 + axis]);
        }
    check(wd::write_scene(lit, argv[1], error), error.c_str());
    wd::SceneSnapshot gouraud_copy;
    check(wd::read_scene(argv[1], gouraud_copy, error), error.c_str());
    check(gouraud_copy.faces[0].corner_shades == lit.faces[0].corner_shades,
          "WDS7 corner shades round trip");
    check(draw.submit(renderer, gouraud_copy, target, 256, 128, false, error), error.c_str());
    sg_commit();
    std::vector<uint32_t> gouraud_pixels;
    check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128,
                             gouraud_pixels, error), error.c_str());
    const auto red = [](uint32_t pixel) { return pixel & 255u; };
    check(red(gouraud_pixels[50 * 256 + 96]) < red(gouraud_pixels[50 * 256 + 160]),
          "Gouraud corner brightness did not interpolate");
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    gouraud_copy.nodes[0].light_count = 1;
    gouraud_copy.lights[0].type = 2;
    check(!draw.submit(renderer, gouraud_copy, target, 256, 128, false, error),
          "lit Gouraud accepted missing corner normals");
    for (unsigned c = 0; c < 3; ++c) {
        wd::SceneNormal normal;
        normal.address = 0x18004000 + c * 16;
        normal.xyz = {0, 0, -32768 + int32_t(c) * 16384};
        normal.dot = 91 + c;
        gouraud_copy.nodes[0].vertex_normals.push_back(normal);
        gouraud_copy.faces[0].corner_normals[c] = normal;
    }
    check(wd::write_scene(gouraud_copy, argv[1], error), error.c_str());
    wd::SceneSnapshot lit_copy;
    check(wd::read_scene(argv[1], lit_copy, error), error.c_str());
    check(lit_copy.nodes[0].vertex_normals.size() == 3 &&
              lit_copy.faces[0].corner_normals[1].address == 0x18004010 &&
              lit_copy.faces[0].corner_normals[1].dot == 92,
          "WDS8 normal pool and corner scratch round trip");
    for (int type : {0x16, 0x17, 0x18}) {
        draw.reset(renderer);
        lit_copy.faces[0].type = type;
        std::vector<wd::SceneLightingWrite> writes;
        check(draw.submit(renderer, lit_copy, target, 256, 128, false, error, &writes), error.c_str());
        sg_commit();
        std::vector<uint32_t> lit_pixels;
        check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128,
                                 lit_pixels, error), error.c_str());
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
        if (type != 0x18)
            check(red(lit_pixels[50 * 256 + 96]) > red(lit_pixels[50 * 256 + 160]),
                  "lit Gouraud ignored computed corner shades");
        auto baked = lit_copy;
        baked.nodes[0].light_count = 0;
        for (const auto &write : writes)
            if (write.address >= baked.faces[0].address + 0x41 &&
                write.address <= baked.faces[0].address + 0x43)
                baked.faces[0].corner_shades[write.address - baked.faces[0].address - 0x41] =
                    uint8_t(write.value);
        if (type == 0x18)
            check(baked.faces[0].corner_shades == lit_copy.faces[0].corner_shades,
                  "type 0x18 flat lighting modified corner bytes");
        else
            check(baked.faces[0].corner_shades == std::array<uint8_t, 3>{15, 7, 0},
                  "oriented Gouraud corner contributions");
        draw.reset(renderer);
        check(draw.submit(renderer, baked, target, 256, 128, false, error), error.c_str());
        sg_commit();
        std::vector<uint32_t> baked_pixels;
        check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128,
                                 baked_pixels, error), error.c_str());
        check(baked_pixels == lit_pixels, "lit Gouraud GPU output differs from baked retail shades");
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
    }
    draw.reset(renderer);
    wd::SceneSnapshot env_scene;
    env_scene.camera = lit.camera;
    env_scene.camera.address = 0x1000;
    env_scene.camera.local_rotation = {32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
    env_scene.nodes.resize(3);
    env_scene.poses.resize(3);
    for (unsigned i = 0; i < 3; ++i) {
        env_scene.nodes[i].address = 0x1000 + i * 0x100;
        env_scene.poses[i] = {-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
        if (i) {
            env_scene.nodes[i].submitted = env_scene.nodes[i].visual_active = true;
            env_scene.poses[i].parent = 0;
            env_scene.poses[i].local[3] = i == 1 ? -1 : 1;
        }
    }
    env_scene.nodes[1].flags = 0x800;
    env_scene.materials.resize(1);
    auto &env_material = env_scene.materials[0];
    env_material.page = 0x300000;
    env_material.indices.resize(65536);
    for (unsigned i = 0; i < 65536; ++i)
        env_material.indices[i] = (i % 256) < 128 ? 1 : 2;
    for (unsigned row = 0; row < 32; ++row) {
        env_material.palette[row * 256 + 1] = 0xf800;
        env_material.palette[row * 256 + 2] = 0x07e0;
    }
    for (unsigned owner = 1; owner < 3; ++owner) {
        wd::SceneFace f;
        f.address = 0x2000 + owner * 0x100;
        f.owner = owner;
        f.block = owner;
        f.type = 3;
        f.material = 0;
        auto &node = env_scene.nodes[owner];
        node.first_vertex = uint32_t(env_scene.vertices.size());
        node.vertex_count = 3;
        for (unsigned c = 0; c < 3; ++c) {
            const auto vertex = uint32_t(env_scene.vertices.size());
            std::array<int32_t, 3> p;
            std::copy_n(gouraud_points + c * 3, 3, p.begin());
            env_scene.source_vertices.push_back(p);
            env_scene.vertices.push_back({{float(p[0]), float(p[1]), float(p[2])}});
            env_scene.vertex_addresses.push_back(0x5000 + vertex * 40);
            f.corners[c] = {owner, vertex, {0, 0}};
            f.uv_addresses[c] = 0x4000; // all corners and both objects share one UV pair
            f.corner_normals[c].address = 0x6000 + c * 16;
            f.corner_normals[c].xyz = {int32_t(c) * 32768 - 32768, 0, 0};
        }
        env_scene.faces.push_back(f);
    }
    float env_vp[16];
    check(env_scene.view_projection(256, 128, false, env_vp), "environment projection");
    wd::SceneLighting prepared;
    check(wd::prepare_scene_lighting(env_scene, env_vp, prepared, error), error.c_str());
    check(prepared.corners[0][0].uv[0] == 0 && prepared.corners[1][0].uv[0] == 1,
          "post-draw UV update did not preserve current/next object versions");
    check(prepared.writes.size() == 6 && prepared.writes.back().value == 0x800000,
          "shared environment UV write order");
    check(wd::write_scene(env_scene, argv[1], error), error.c_str());
    wd::SceneSnapshot env_copy;
    check(wd::read_scene(argv[1], env_copy, error), error.c_str());
    check(env_copy.faces[0].uv_addresses == env_scene.faces[0].uv_addresses &&
              env_copy.nodes[1].source_rotation == env_scene.nodes[1].source_rotation &&
              env_copy.nodes[1].visual_active,
          "WDS9 environment source/alias round trip");
    check(draw.submit(renderer, env_copy, target, 256, 128, false, error), error.c_str());
    sg_commit();
    std::vector<uint32_t> env_pixels;
    check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128,
                             env_pixels, error), error.c_str());
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    auto baked_env = env_copy;
    // Preparation can precede a UI-writing callback, while final scene flush
    // retains retail overwrite order and the exact prepared UV versions.
    draw.reset(renderer);
    od_draw_2d callback_fill{OD_DRAW_FILL, target, 0, {0, 0, 256, 128}, OD_RGB565, 0xffff};
    check(od_renderer_draw_2d(renderer, &callback_fill), "callback diagnostic fill");
    std::vector<wd::SceneLightingWrite> delayed_writes;
    check(draw.submit(renderer, env_copy, target, 256, 128, false, error,
                       &delayed_writes, &prepared), error.c_str());
    check(delayed_writes.size() == prepared.writes.size() && prepared.writes.size() == 6,
          "prepared metadata lost or consumed during delayed submission");
    sg_commit();
    std::vector<uint32_t> delayed_pixels;
    check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128,
                             delayed_pixels, error), error.c_str());
    check(delayed_pixels == env_pixels, "delayed prepared scene changed UV or callback order");
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    auto malformed = prepared;
    malformed.corners.pop_back();
    const auto submissions = od_renderer_stats(renderer).scene_triangles;
    check(!draw.submit(renderer, env_copy, target, 256, 128, false, error, nullptr, &malformed),
          "mismatched prepared metadata accepted");
    check(od_renderer_stats(renderer).scene_triangles == submissions,
          "mismatched metadata partially submitted");
    for (unsigned i = 0; i < 2; ++i) {
        baked_env.nodes[i + 1].flags &= ~0x800u;
        baked_env.faces[i].corners = prepared.corners[i];
        baked_env.faces[i].uv_addresses.fill(0);
    }
    draw.reset(renderer);
    check(draw.submit(renderer, baked_env, target, 256, 128, false, error), error.c_str());
    sg_commit();
    std::vector<uint32_t> baked_env_pixels;
    check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128,
                             baked_env_pixels, error), error.c_str());
    check(baked_env_pixels == env_pixels, "GPU environment draw used the wrong UV version");
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    auto repeat_env = env_copy;
    for (auto &f : repeat_env.faces)
        for (unsigned c = 0; c < 3; ++c) {
            f.source_uvs[c] = {0x1000000, 0x800000}; // first pass's last shared write
            f.corners[c].uv[0] = 1;
            f.corners[c].uv[1] = .5f;
        }
    repeat_env.faces[0].corner_normals[2].xyz[0] = 0;
    check(wd::prepare_scene_lighting(repeat_env, env_vp, prepared, error), error.c_str());
    check(prepared.corners[0][0].uv[0] == 1 && prepared.corners[1][0].uv[0] == .5f,
          "repeated environment pass lost the previous source version");
    const auto offscreen = od_renderer_target(renderer, 64, 64, 64, 64);
    draw.reset(renderer);
    std::vector<wd::SceneLightingWrite> offscreen_writes;
    check(draw.submit(renderer, repeat_env, offscreen, 64, 64, false, error, &offscreen_writes),
          error.c_str());
    check(offscreen_writes.size() == 6 && offscreen_writes[4].value == 0x800000,
          "offscreen environment pass changed the source update contract");
    sg_commit();
    draw.finish_frame(renderer);
    od_renderer_frame_complete(renderer);
    od_renderer_release(renderer, offscreen);
    env_copy.nodes[1].submitted = false;
    check(wd::prepare_scene_lighting(env_copy, env_vp, prepared, error), error.c_str());
    check(prepared.corners[1][0].uv[0] == 1 && prepared.writes.size() == 6,
          "hook-disabled object lost post-draw environment update");
    env_copy.nodes[1].submitted = true;
    for (unsigned i = 0; i < 3; ++i)
        env_copy.vertices[i].xyz[0] += 100;
    check(wd::prepare_scene_lighting(env_copy, env_vp, prepared, error), error.c_str());
    check(prepared.corners[1][0].uv[0] == 0 && prepared.writes.empty(),
          "culled face changed environment UVs");
    for (unsigned i = 0; i < 3; ++i)
        env_copy.vertices[i].xyz[0] -= 100;
    env_copy.faces[0].type = -3;
    env_copy.nodes[2].flags = 0x800;
    for (auto &normal : env_copy.faces[1].corner_normals)
        normal.xyz = {-32768, 0, 0};
    check(wd::prepare_scene_lighting(env_copy, env_vp, prepared, error), error.c_str());
    check(prepared.corners[0][0].uv[0] == 0 && prepared.corners[1][0].uv[0] == 1,
          "deferred block did not consume the final post-update UV version");
    od_renderer_destroy(renderer);
    sg_shutdown();
    backend.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("scene modes: diagnostic colour sequence, winding/frustum rejection, block reset and "
              "Hor+ visibility passed");
    std::puts("scene lighting: lit/culled/lit palette rows, metadata feedback and reused-source "
              "shade passed");
    std::puts("scene Gouraud: WDS8 normal identities, lit 0x16/0x17 interpolation and flat 0x18 passed");
    std::puts("scene environment: WDS9 UV aliases, opaque/deferred versions, hook-disabled and culled updates passed");
}
