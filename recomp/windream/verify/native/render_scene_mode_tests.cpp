#include "render_scene_draw.h"
#include "render/graphics_backend.h"
#include "render/direct_sokol.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <io.h>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// Collects what the adapter logs to stderr; finish() restores it and echoes.
struct LogCapture {
    static inline LogCapture *active = nullptr;
    std::string path;
    int saved;
    explicit LogCapture(std::string file) : path(std::move(file)) {
        std::fflush(stderr);
        saved = _dup(2);
        FILE *out = std::fopen(path.c_str(), "wb");
        if (saved < 0 || !out || _dup2(_fileno(out), 2) != 0)
            std::abort();
        std::fclose(out);
        active = this;
    }
    std::string finish() {
        active = nullptr;
        std::fflush(stderr);
        _dup2(saved, 2);
        _close(saved);
        std::string text;
        if (FILE *in = std::fopen(path.c_str(), "rb")) {
            for (int c; (c = std::fgetc(in)) != EOF;)
                text.push_back(char(c));
            std::fclose(in);
        }
        std::remove(path.c_str());
        std::fputs(text.c_str(), stderr);
        return text;
    }
};
static void check(bool ok, const char *message) {
    if (!ok) {
        if (LogCapture::active)
            LogCapture::active->finish();
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}
static size_t occurrences(const std::string &text, const std::string &needle) {
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
        ++count;
    return count;
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
    // Beyond the projection's depth range. The camera's own far plane (100)
    // rejects whole nodes and whole faces in retail, which capture_scene
    // applies (the far fixture below); submit does not clip at it.
    face(0, 2000000, false);
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
    {
        // The far plane, through capture_scene on a synthetic guest image.
        // REND_CullObjectSphere (0x478980, DOS 0x91740) rejects a node whose
        // sphere lies wholly at or beyond it, REND_TransformClipVertices
        // (0x478c2c, DOS 0x91984) flags each vertex there and REND_CullFaces
        // (0x47b0bc, DOS 0x93774) rejects a face with one flagged corner.
        // Node flag 0x80 switches both tests off; a flag-0x10 node is drawn
        // when its parent is. Nothing is clipped at the far plane.
        struct Image {
            std::vector<std::pair<uint32_t, std::vector<uint8_t>>> ranges;
            uint32_t next = 0x100000;
            uint32_t allocate(size_t bytes) {
                const uint32_t address = next;
                ranges.push_back({address, std::vector<uint8_t>(bytes)});
                next += uint32_t(bytes + 15) & ~15u;
                return address;
            }
            uint8_t *at(uint32_t address, size_t bytes) {
                for (auto &range : ranges)
                    if (address >= range.first &&
                        uint64_t(address) + bytes <= uint64_t(range.first) + range.second.size())
                        return range.second.data() + (address - range.first);
                return nullptr;
            }
            void put(uint32_t address, uint32_t value) {
                auto *p = at(address, 4);
                check(p != nullptr, "far fixture wrote outside its image");
                for (int i = 0; i < 4; ++i)
                    p[i] = uint8_t(value >> (8 * i));
            }
            uint32_t get(uint32_t address) {
                const auto *p = at(address, 4);
                check(p != nullptr, "far fixture read outside its image");
                return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
                       uint32_t(p[3]) << 24;
            }
            static bool read(void *self, uint32_t address, void *destination, size_t bytes) {
                const auto *p = static_cast<Image *>(self)->at(address, bytes);
                if (p)
                    std::copy_n(p, bytes, static_cast<uint8_t *>(destination));
                return p != nullptr;
            }
        } image;
        const int32_t far_plane = 1000;
        image.ranges.push_back({0x4ac758, std::vector<uint8_t>(4)});
        image.ranges.push_back({0x661e90, std::vector<uint8_t>(0x4c)});
        const float focal[2] = {64, 128};
        for (uint32_t i = 0; i < 2; ++i) {
            uint32_t bits;
            std::memcpy(&bits, &focal[i], 4);
            image.put(0x661e90 + i * 4, bits);
        }
        image.put(0x661e90 + 8, 64);
        image.put(0x661e90 + 12, 128);
        image.put(0x661e90 + 0x20, uint32_t(far_plane));
        image.put(0x661e90 + 0x2c, 256);
        image.put(0x661e90 + 0x30, 128);
        image.put(0x661e90 + 0x38, 128);
        image.put(0x661e90 + 0x3c, 1);
        image.put(0x661e90 + 0x48, 256);
        const uint32_t root = image.allocate(0xd4);
        image.put(root + 12, 0x400);
        for (uint32_t i = 0; i < 3; ++i)
            image.put(root + 0x28 + i * 16, 32768);
        uint32_t colour = 0;
        // One node at camera depth z with one face per entry, each entry the
        // three corner depths relative to the node. Returns its address.
        auto node = [&](uint32_t parent, uint32_t flags, int32_t x, int32_t z, int32_t parent_x,
                        int32_t parent_z, int32_t radius,
                        std::vector<std::array<int32_t, 3>> faces) {
            const uint32_t address = image.allocate(0xd4);
            const uint32_t vertices = image.allocate(faces.size() * 3 * 40);
            const uint32_t records = image.allocate(faces.size() * 56);
            const uint32_t block = image.allocate(0x34);
            image.put(address + 12, flags);
            image.put(address + 16, parent);
            image.put(address + 24, image.get(parent + 20));
            image.put(parent + 20, address);
            image.put(address + 0x1c, uint32_t(x - parent_x));
            image.put(address + 0x24, uint32_t(z - parent_z));
            for (uint32_t i = 0; i < 3; ++i) {
                image.put(address + 0x28 + i * 16, 32768); // local rotation
                image.put(address + 0x58 + i * 16, 32768); // composed rotation
            }
            image.put(address + 0x4c, uint32_t(x));
            image.put(address + 0x54, uint32_t(z));
            image.put(address + 0x7c, uint32_t(faces.size() * 3));
            image.put(address + 0x80, vertices);
            image.put(address + 0xa4, block);
            image.put(address + 0xb0, uint32_t(radius));
            image.put(address + 0xc0, 40);
            image.put(address + 0xd0, 15);
            image.put(block + 4, 1);
            image.put(block + 8, colour++);
            image.put(block + 0x1c, uint32_t(faces.size()));
            image.put(block + 0x20, records);
            image.put(block + 0x2c, 56);
            for (uint32_t f = 0; f < faces.size(); ++f)
                for (uint32_t c = 0; c < 3; ++c) {
                    const uint32_t vertex = vertices + (f * 3 + c) * 40;
                    const int32_t xy[3][2] = {{-40, -40}, {0, 40}, {40, -40}};
                    image.put(vertex + 4, uint32_t(xy[c][0]));
                    image.put(vertex + 8, uint32_t(xy[c][1]));
                    image.put(vertex + 12, uint32_t(faces[f][c]));
                    image.put(records + f * 56 + 8 + c * 12, vertex);
                }
            return address;
        };
        const uint32_t inside = node(root, 0, -300, 500, 0, 0, 100, {{0, 0, 0}});
        const uint32_t beyond = node(root, 0, -200, 1200, 0, 0, 100, {{0, 0, 0}});
        const uint32_t exempt = node(root, 0x80, -100, 1200, 0, 0, 100, {{0, 0, 0}});
        // Crossing: the second face has one corner exactly at the far plane,
        // the third stops one unit short of it.
        const uint32_t crossing =
            node(root, 0, 0, 900, 0, 0, 300, {{0, 0, 0}, {0, 100, 0}, {0, 99, 0}});
        const uint32_t tangent = node(root, 0, 100, 1100, 0, 0, 100, {{0, 0, 0}});
        const uint32_t reaching =
            node(root, 0, 200, 1100, 0, 0, 101, {{0, 0, 0}, {-101, -101, -101}});
        const uint32_t bridge = node(inside, 0x10, 300, 1200, -300, 500, 100, {{0, 0, 0}});
        const uint32_t orphan = node(beyond, 0x10, 400, 1300, -200, 1200, 100, {{0, 0, 0}});
        wd::SceneReader reader;
        reader.context = &image;
        reader.read = Image::read;
        wd::SceneSnapshot captured;
        check(wd::capture_scene(reader, root, captured, error), error.c_str());
        auto submitted = [&](uint32_t address) {
            for (const auto &n : captured.nodes)
                if (n.address == address)
                    return n.submitted;
            check(false, "far fixture lost a node");
            return false;
        };
        auto rejected = [&](uint32_t address) {
            std::string bits;
            for (const auto &f : captured.faces)
                if (captured.nodes[f.owner].address == address)
                    bits.push_back(f.flags & 1 ? '1' : '0');
            return bits;
        };
        check(captured.camera.far_plane == far_plane && captured.nodes.size() == 9 &&
                  captured.faces.size() == 11,
              "far fixture capture");
        check(submitted(inside) && rejected(inside) == "0", "node inside the far plane");
        check(!submitted(beyond) && !submitted(tangent),
              "node sphere wholly at or beyond the far plane was kept");
        check(submitted(exempt) && rejected(exempt) == "0",
              "flag 0x80 node or its faces were far-culled");
        check(submitted(crossing) && rejected(crossing) == "010",
              "face with a corner at the far plane, or one a unit short of it");
        check(submitted(reaching) && rejected(reaching) == "10",
              "node reaching across the far plane: faces beyond and before it");
        check(submitted(bridge) && rejected(bridge) == "1",
              "flag 0x10 node beyond the far plane with a drawn parent");
        check(!submitted(orphan), "flag 0x10 node beyond the far plane with a culled parent");
        // Drawn: inside, exempt (beyond the plane, unclipped), two of
        // crossing and one of reaching.
        wd::SceneDraw far_draw;
        const auto far_before = od_renderer_stats(renderer).scene_triangles;
        check(far_draw.submit(renderer, captured, target, 256, 128, false, error), error.c_str());
        check(od_renderer_stats(renderer).scene_triangles == far_before + 5,
              "far-rejected faces were drawn, or kept faces clipped at the far plane");
        wd::SceneCollector far_collector;
        check(wd::prepare_scene_collector(captured, 256, 128, false, far_collector, error),
              error.c_str());
        check(far_collector.total_triangles == 5, "collector far rejection");
        const std::string far_path = std::string(argv[1]) + ".far";
        wd::SceneSnapshot far_copy;
        check(wd::write_scene(captured, far_path.c_str(), error) &&
                  wd::read_scene(far_path.c_str(), far_copy, error),
              error.c_str());
        std::remove(far_path.c_str());
        check(far_draw.submit(renderer, far_copy, target, 256, 128, false, error), error.c_str());
        check(od_renderer_stats(renderer).scene_triangles == far_before + 10,
              "far rejection did not survive the snapshot round trip");
        sg_commit();
        far_draw.reset(renderer);
    }
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
    {
        // A snapshot holding the DOS 3dfx rows binds physical row = shade,
        // as GLIDE_BindTexture does; the Windows bank above binds 31 - shade.
        auto dos = lit;
        dos.dos_palette = true;
        dos.nodes[0].light_count = 0;
        dos.materials[0].page = 0x380000;
        check(draw.submit(renderer, dos, target, 256, 128, false, error), error.c_str());
        sg_commit();
        std::vector<uint32_t> result;
        check(
            backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128, result, error),
            "DOS row capture");
        check((result[64 * 256 + 153] & 255) == 192, "DOS palette snapshot did not bind row = shade");
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
        draw.reset(renderer);
    }
    {
        // Rows of faces lit by an attack light (host rule, owner decision
        // 2026-10-01): the unlit row's offsets plus the Windows step. Row 0
        // must be the row the DOS routine leaves in row 15 with no effect
        // light, whatever the slot and the project's +0xc8.
        uint8_t source[1024];
        for (unsigned i = 0; i < 1024; ++i)
            source[i] = uint8_t(i * 37 + (i >> 3));
        for (unsigned variant = 0; variant < 8; ++variant) {
            od_dos_palette_update update{};
            update.rgb[0] = -32, update.rgb[1] = 17, update.rgb[2] = variant & 4 ? 140 : -5;
            update.actor_rgb[0] = 64, update.actor_rgb[1] = -64, update.actor_rgb[2] = 3;
            update.actor_scale = 3;
            update.scale = 4;
            update.cursor = variant * 3;
            update.actor_bound = variant & 1;
            update.lit_project = (variant & 2) != 0;
            std::vector<uint16_t> bank(32 * 256, 0x5555), rows(32 * 256);
            check(od_dos_palette_rows(&update, source, bank.data()) & (1u << 15),
                  "DOS routine did not refresh the unlit row");
            const int32_t scale = update.actor_bound ? 8 : 2;
            od_lit_palette_rows(&update, source, scale, rows.data());
            check(std::equal(rows.begin(), rows.begin() + 256, bank.begin() + 15 * 256),
                  "lit row 0 is not the unlit row");
            const int32_t *base = update.actor_bound ? update.actor_rgb : update.rgb;
            const bool quarter = !update.actor_bound && update.lit_project;
            std::vector<uint16_t> expected(256);
            for (uint32_t shade = 0; shade < 32; ++shade) {
                int32_t c[3];
                for (unsigned i = 0; i < 3; ++i)
                    c[i] = (quarter ? (base[i] + 12 * update.scale) >> 2 : base[i]) +
                           ((int32_t(shade) * scale) >> 2);
                od_dos_palette_apply(source, c[0], c[1], c[2], expected.data());
                check(std::equal(expected.begin(), expected.end(), rows.begin() + shade * 256),
                      "lit row is not the unlit offsets plus shade * scale >> 2");
                for (unsigned i = 0; shade && i < 256; ++i)
                    check((rows[shade * 256 + i] >> 11) >= (rows[(shade - 1) * 256 + i] >> 11),
                          "a lighter shade is darker");
            }
        }
        // Each flat-lit face binds the row of its own shade; without the
        // host rows the block binds one row, the head face's.
        auto host = lit;
        host.dos_palette = true;
        host.materials[0].page = 0x390000;
        for (unsigned i = 0; i < 3; ++i) { // the loop above left the first face off screen
            host.source_vertices[i][0] = points[i * 3];
            host.vertices[i].xyz[0] = float(points[i * 3]);
        }
        host.materials[0].lit_palette.assign(32 * 256, 0);
        for (unsigned row = 0; row < 32; ++row)
            host.materials[0].lit_palette[row * 256 + 1] = uint16_t((row / 2 + 1) << 11);
        wd::SceneLighting prepared;
        float host_vp[16];
        check(host.view_projection(256, 128, false, host_vp), "lit row camera");
        check(wd::prepare_scene_lighting(host, host_vp, prepared, error), error.c_str());
        prepared.shades[0] = 0;
        prepared.shades[1] = 20;
        const auto shot = [&](const wd::SceneSnapshot &scene) {
            check(draw.submit(renderer, scene, target, 256, 128, false, error, nullptr, &prepared),
                  error.c_str());
            sg_commit();
            std::vector<uint32_t> result;
            check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128, result,
                                     error),
                  "lit row capture");
            draw.finish_frame(renderer);
            od_renderer_frame_complete(renderer);
            return std::array<unsigned, 2>{result[64 * 256 + 128] & 255, result[64 * 256 + 153] & 255};
        };
        const auto per_face = shot(host);
        check(per_face[0] == 8 && per_face[1] == 88, "lit faces did not bind the row of their shade");
        const std::string lit_path = std::string(argv[1]) + ".lit";
        wd::SceneSnapshot host_copy;
        auto stored = host;
        stored.light_views = true;
        stored.nodes[0].vertex_count = uint32_t(stored.vertices.size());
        stored.vertex_addresses.resize(stored.vertices.size());
        check(wd::write_scene(stored, lit_path.c_str(), error) &&
                  wd::read_scene(lit_path.c_str(), host_copy, error),
              error.c_str());
        std::remove(lit_path.c_str());
        check(host_copy.dos_palette && host_copy.materials.size() == 1 &&
                  host_copy.materials[0].lit_palette == host.materials[0].lit_palette,
              "lit rows did not survive the snapshot round trip (WDSD)");
        auto unlit_node = host;
        unlit_node.nodes[0].light_count = 0;
        unlit_node.nodes[0].shade = 15;
        unlit_node.materials[0].page = 0x3a0000;
        const auto unlit = shot(unlit_node);
        check(unlit[0] == unlit[1] && unlit[0] == 128,
              "a node without lights used the lit rows");
        auto block_row = host;
        block_row.materials[0].lit_palette.clear();
        block_row.materials[0].page = 0x3b0000;
        const auto one_row = shot(block_row);
        check(one_row[0] == one_row[1] && one_row[0] == 248,
              "a block without host rows did not bind the head face's row");
        draw.reset(renderer);
    }
    wd::SceneLighting rejected;
    float lit_vp[16];
    check(lit.view_projection(256, 128, false, lit_vp), "lit test camera");
    lit.light_transform_count = 0;
    check(!wd::prepare_scene_lighting(lit, lit_vp, rejected, error, true),
          "stale light accepted without its retained view transform");
    lit.light_transform_count = 1;
    for (uint32_t kind = 1; kind <= 2; ++kind) {
        // A live slot at or above the count is lit from the view-space
        // transform retail last refreshed, through this frame's camera.
        auto fresh_scene = lit;
        fresh_scene.lights[0].type = kind;
        fresh_scene.lights[0].orientation = {32768, 0, 0, 0, 32768, 0, 0, 0, 32768};
        auto stale_scene = fresh_scene;
        stale_scene.light_transform_count = 0;
        stale_scene.light_views = true;
        stale_scene.camera.view = {1, 0, 0, 5, 0, 0, -1, -3, 0, 1, 0, 2};
        const auto p = fresh_scene.lights[0].position;
        stale_scene.lights[0].position = {777, 777, 777}; // this frame's source is not read
        stale_scene.lights[0].orientation = {0, 0, 32768, 0, 32768, 0, 32768, 0, 0};
        stale_scene.lights[0].view_position = {p[0] + 5, -p[2] - 3, p[1] + 2};
        stale_scene.lights[0].view_orientation = {32768, 0, 0, 0, 0, -32768, 0, 32768, 0};
        wd::SceneLighting fresh, stale;
        check(wd::prepare_scene_lighting(fresh_scene, lit_vp, fresh, error, true), error.c_str());
        check(wd::prepare_scene_lighting(stale_scene, lit_vp, stale, error, true), error.c_str());
        check(fresh.shades[1] == (kind == 2 ? 15 : 14) && stale.shades == fresh.shades &&
                  stale.writes.size() == fresh.writes.size() &&
                  std::equal(stale.writes.begin(), stale.writes.end(), fresh.writes.begin(),
                             [](const auto &a, const auto &b) {
                                 return a.address == b.address && a.value == b.value &&
                                        a.bytes == b.bytes && a.entry == b.entry;
                             }),
              "stale view-space light differs from the same light refreshed");
        check(std::count_if(stale.writes.begin(), stale.writes.end(), [](const auto &w) {
                  return w.address >= 0x672764 && w.address < 0x67277c;
              }) == (kind == 2 ? 6 : 3),
              "stale light lost its owner-space feedback");
        stale_scene.nodes[0].vertex_count = uint32_t(stale_scene.vertices.size());
        stale_scene.vertex_addresses.resize(stale_scene.vertices.size());
        check(wd::write_scene(stale_scene, argv[1], error), error.c_str());
        wd::SceneSnapshot stale_copy;
        check(wd::read_scene(argv[1], stale_copy, error), error.c_str());
        check(stale_copy.light_views &&
                  stale_copy.lights[0].view_position == stale_scene.lights[0].view_position &&
                  stale_copy.lights[0].view_orientation == stale_scene.lights[0].view_orientation,
              "WDSB retained light view transform round trip");
        stale_scene.dos_palette = true;
        check(wd::write_scene(stale_scene, argv[1], error), error.c_str());
        check(wd::read_scene(argv[1], stale_copy, error) && stale_copy.dos_palette &&
                  stale_copy.light_views,
              "WDSC DOS palette mark round trip");
    }
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
    {
        // Eight bound slots; 0, 3 and 7 are removed lights (type 0) or an
        // unknown type. Flat: each re-adds the previous contribution, kept
        // across faces; the first one of the node adds 0 (declared deviation).
        auto slots = lit;
        for (unsigned i = 0; i < 3; ++i) {
            slots.source_vertices[i][0] = points[i * 3];
            slots.vertices[i].xyz[0] = float(points[i * 3]);
        }
        slots.light_transform_count = 8;
        slots.nodes[0].light_count = 8;
        for (unsigned i = 0; i < 8; ++i) {
            slots.nodes[0].light_indices[i] = uint8_t(i);
            slots.lights[i] = lit.lights[0];
            slots.lights[i].type = 1;
            slots.lights[i].intensity = 3;
        }
        slots.lights[0].type = slots.lights[7].type = 0;
        slots.lights[3].type = 3;
        wd::SceneLighting inactive;
        check(wd::prepare_scene_lighting(slots, lit_vp, inactive, error, true), error.c_str());
        // Per light -3 on face 0 and -2 on face 1: 0-3-3-3-3-3-3-3 and -3-2-2-2-2-2-2-2.
        check(inactive.shades[0] == 21 && inactive.shades[1] == 17,
              "inactive flat slots did not re-add the carried contribution");
        size_t dots = 0, shades = 0;
        for (const auto &w : inactive.writes) {
            for (unsigned slot : {0u, 3u, 7u})
                check(w.address < 0x672700 + slot * 0x94 || w.address >= 0x672794 + slot * 0x94,
                      "inactive slot received an owner-space transform");
            dots += w.address == 0x1800300c || w.address == 0x1800301c;
            shades += w.address == 0x18002040 || w.address == 0x180020c0;
        }
        check(dots == 10 && shades == 2 && inactive.writes.size() == 5 * 3 + 12,
              "inactive flat slots changed the feedback writes");
        slots.lights[0].type = 1;
        check(wd::prepare_scene_lighting(slots, lit_vp, inactive, error, true), error.c_str());
        check(inactive.shades[0] == 24 && inactive.shades[1] == 16,
              "active first slot did not replace the carry");
    }
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
    {
        // Gouraud: an inactive slot is skipped, with no pool refresh.
        auto slots = lit_copy;
        slots.faces[0].type = 0x16;
        slots.light_transform_count = 8;
        slots.nodes[0].light_count = 8;
        for (unsigned i = 0; i < 8; ++i) {
            slots.nodes[0].light_indices[i] = uint8_t(i);
            slots.lights[i] = lit_copy.lights[0];
            slots.lights[i].intensity = 3;
        }
        slots.lights[0].type = slots.lights[7].type = 0;
        slots.lights[3].type = 3;
        wd::SceneLighting inactive;
        check(wd::prepare_scene_lighting(slots, lit_vp, inactive, error, true), error.c_str());
        check(inactive.corner_shades[0] == std::array<uint8_t, 3>{15, 5, 0},
              "inactive Gouraud slots contributed");
        check(std::count_if(inactive.writes.begin(), inactive.writes.end(), [](const auto &w) {
                  return w.address >= 0x1800400c && w.address <= 0x1800402c && w.bytes == 4;
              }) == 5 * 3,
              "inactive Gouraud slot refreshed the normal pool");
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
    // Types the Windows dispatch names and the Glide hook does not draw.
    draw.reset(renderer);
    wd::SceneSnapshot modes;
    modes.camera = lit.camera;
    modes.poses.push_back({-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    modes.nodes.resize(1);
    modes.nodes[0].submitted = modes.nodes[0].visual_active = true;
    modes.nodes[0].vertex_count = 6;
    modes.materials.resize(1);
    modes.materials[0].page = 0x500000;
    modes.materials[0].indices.assign(65536, 1);
    for (unsigned row = 0; row < 32; ++row)
        modes.materials[0].palette[row * 256 + 1] = uint16_t((31 - row) << 11);
    for (unsigned i = 0; i < 6; ++i) {
        // Vertices 3..5 repeat the triangle outside the frustum.
        std::array<int32_t, 3> p;
        std::copy_n(gouraud_points + (i % 3) * 3, 3, p.begin());
        p[0] += i < 3 ? 0 : 100;
        modes.source_vertices.push_back(p);
        modes.vertices.push_back({{float(p[0]), float(p[1]), float(p[2])}});
        modes.vertex_addresses.push_back(0x18005000 + i * 40);
    }
    const auto mode_face = [](wd::SceneSnapshot &s, int32_t type, uint32_t block,
                              uint32_t first_vertex = 0) -> wd::SceneFace & {
        wd::SceneFace f;
        f.type = type;
        f.block = block;
        f.material = 0;
        f.address = 0x18010000 + uint32_t(s.faces.size()) * 0x80;
        f.normal_address = 0x18030000 + uint32_t(s.faces.size()) * 16;
        f.normal = {0, 0, -32768};
        f.plane_distance = -4;
        for (uint32_t c = 0; c < 3; ++c)
            f.corners[c] = {0, first_vertex + c, {0, 0}};
        s.faces.push_back(f);
        return s.faces.back();
    };
    const int no_draw_types[] = {-15, -14, -13, -12, -11, -10, -9, -8, -2,   4,
                                 5,   6,   7,   8,   10,  0xb, 0xc, 0x11, 0x12, 0x14,
                                 0x15, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
    auto skipped = modes;
    mode_face(skipped, 3, 1);
    for (int type : no_draw_types)
        mode_face(skipped, type, 2 + uint32_t(skipped.faces.size()));
    {
        LogCapture log(std::string(argv[1]) + ".log");
        for (unsigned pass = 0; pass < 2; ++pass) {
            const auto drawn = od_renderer_stats(renderer).scene_triangles;
            check(draw.submit(renderer, skipped, target, 256, 128, false, error), error.c_str());
            check(od_renderer_stats(renderer).scene_triangles == drawn + 1,
                  "a type Glide does not draw was submitted");
            sg_commit();
            draw.finish_frame(renderer);
            od_renderer_frame_complete(renderer);
        }
        const auto text = log.finish();
        check(occurrences(text, "[direct] glide_no_draw type=") == std::size(no_draw_types) &&
                  occurrences(text, "glide_no_draw type=-13 ") == 1 &&
                  occurrences(text, "glide_no_draw type=30 ") == 1,
              "no-draw types were not logged once each");
    }
    for (int type : {0, -1, 0xd, 0xe, 0xf, 0x10, 0x13, 0x20, -16}) {
        auto unknown = modes;
        mode_face(unknown, type, 1);
        check(!draw.submit(renderer, unknown, target, 256, 128, false, error) &&
                  error == "unimplemented scene face type " + std::to_string(type),
              "a face type outside both dispatches was accepted");
    }
    {
        // DOS still lights the node and updates its environment UVs.
        auto lit_modes = modes;
        lit_modes.light_transform_count = 1;
        lit_modes.nodes[0].light_count = 1;
        lit_modes.lights[0].type = 1;
        lit_modes.lights[0].position = {0, 0, -10};
        lit_modes.lights[0].inner_radius = 1000;
        lit_modes.lights[0].outer_radius = 2000;
        lit_modes.lights[0].intensity = 9;
        mode_face(lit_modes, 3, 1);
        mode_face(lit_modes, 6, 2);
        mode_face(lit_modes, 0x1e, 3);
        wd::SceneLighting no_draw_lighting;
        check(wd::prepare_scene_lighting(lit_modes, lit_vp, no_draw_lighting, error, true),
              error.c_str());
        check(no_draw_lighting.shades[0] == 9 &&
                  std::none_of(no_draw_lighting.writes.begin(), no_draw_lighting.writes.end(),
                               [&](const auto &w) {
                                   return w.address >= lit_modes.faces[1].address &&
                                          w.address < lit_modes.faces[2].address + 0x80;
                               }),
              "lighting of a node with no-draw blocks");
        draw.reset(renderer);
        const auto drawn = od_renderer_stats(renderer).scene_triangles;
        check(draw.submit(renderer, lit_modes, target, 256, 128, false, error), error.c_str());
        check(od_renderer_stats(renderer).scene_triangles == drawn + 1, "lit no-draw submission");
        sg_commit();
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
        auto env_modes = env_copy;
        env_modes.nodes[1].flags = 0;
        env_modes.faces[0].type = 3;
        env_modes.faces[1].type = 0x1d;
        check(wd::prepare_scene_lighting(env_modes, env_vp, prepared, error), error.c_str());
        check(prepared.writes.size() == 6 && prepared.writes.front().entry == 0x47e094,
              "no-draw block lost its environment UV update");
        draw.reset(renderer);
        const auto env_drawn = od_renderer_stats(renderer).scene_triangles;
        check(draw.submit(renderer, env_modes, target, 256, 128, false, error), error.c_str());
        check(od_renderer_stats(renderer).scene_triangles == env_drawn + 1,
              "environment no-draw submission");
        sg_commit();
        draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
    }
    {
        // 257 deferred blocks: DOS exits with -11; here all are drawn and one
        // line reports it. No shipped content.
        auto many = modes;
        for (uint32_t i = 0; i < 257; ++i)
            mode_face(many, i % 3 == 0 ? -7 : i % 3 == 1 ? -4 : -3, 1 + i, i == 5 ? 3 : 0);
        many.nodes.push_back(many.nodes[0]);
        many.poses.push_back(many.poses[0]);
        many.nodes[1].submitted = false; // hook not called: its block is not queued
        many.nodes[1].first_vertex = 6;
        many.nodes[1].vertex_count = 0;
        mode_face(many, -3, 1000).owner = 1;
        wd::SceneDraw deferred_draw;
        LogCapture log(std::string(argv[1]) + ".log");
        for (unsigned pass = 0; pass < 2; ++pass) {
            const auto drawn = od_renderer_stats(renderer).scene_triangles;
            check(deferred_draw.submit(renderer, many, target, 256, 128, false, error),
                  error.c_str());
            check(deferred_draw.deferred_blocks() == 257 &&
                      od_renderer_stats(renderer).scene_triangles == drawn + 257,
                  "deferred overflow dropped or miscounted blocks");
            sg_commit();
            deferred_draw.finish_frame(renderer);
            od_renderer_frame_complete(renderer);
        }
        many.faces.erase(many.faces.begin());
        wd::SceneDraw full_draw;
        check(full_draw.submit(renderer, many, target, 256, 128, false, error), error.c_str());
        check(full_draw.deferred_blocks() == 256, "deferred block count at the Glide limit");
        sg_commit();
        full_draw.finish_frame(renderer);
        od_renderer_frame_complete(renderer);
        const auto text = log.finish();
        check(occurrences(text, "[direct] deferred translucent blocks=") == 1 &&
                  occurrences(text, "deferred translucent blocks=257 ") == 1,
              "deferred overflow was not reported exactly once with its count");
        deferred_draw.reset(renderer);
        full_draw.reset(renderer);
    }
    {
        // Lit type 0x18 (DREAMSFX 0x9420c, the flat branch): +0x40 from the
        // flat kernel, +0x41..+0x43 never written, palette row from the head
        // face even when it is culled, corner grey from the stored bytes.
        auto flat18 = modes;
        flat18.light_transform_count = 1;
        flat18.nodes[0].light_count = 1;
        flat18.nodes[0].shade = 3;
        auto &light = flat18.lights[0];
        light.type = 1;
        light.position = {0, 0, -10};
        light.inner_radius = 1000;
        light.outer_radius = 2000;
        light.intensity = 9;
        auto &head = mode_face(flat18, 0x18, 1, 3);
        head.shade = 11;
        head.corner_shades = {1, 2, 3};
        const uint32_t head_address = head.address;
        auto &seen = mode_face(flat18, 0x18, 1);
        seen.shade = 29;
        seen.corner_shades = {31, 10, 31};
        const uint32_t seen_address = seen.address;
        const od_radial_light local{{0, 0, -10}, 1000, 2000, 9};
        uint8_t expected = 0;
        check(od_radial_flat_shade(gouraud_points, flat18.faces[1].normal.data(), -4, &local, 1,
                                   &expected) && expected == 9, "type 0x18 flat kernel reference");
        const auto pixels_of = [&](const wd::SceneSnapshot &s,
                                   std::vector<wd::SceneLightingWrite> *writes = nullptr) {
            draw.reset(renderer);
            check(draw.submit(renderer, s, target, 256, 128, false, error, writes), error.c_str());
            sg_commit();
            std::vector<uint32_t> result;
            check(backend.read_image(od_renderer_image(renderer, target), 0, 0, 256, 128, result,
                                     error), error.c_str());
            draw.finish_frame(renderer);
            od_renderer_frame_complete(renderer);
            return result;
        };
        wd::SceneLighting lighting18;
        check(wd::prepare_scene_lighting(flat18, lit_vp, lighting18, error, true), error.c_str());
        check(lighting18.shades[0] == 11 && lighting18.shades[1] == expected &&
                  lighting18.corner_shades[0] == flat18.faces[0].corner_shades &&
                  lighting18.corner_shades[1] == flat18.faces[1].corner_shades,
              "type 0x18 flat branch result");
        std::vector<wd::SceneLightingWrite> writes18;
        const auto lit18 = pixels_of(flat18, &writes18);
        size_t shade_writes = 0;
        for (const auto &w : writes18) {
            check(w.address != head_address + 0x40, "culled 0x18 head face was relit");
            for (uint32_t face_address : {head_address, seen_address})
                check(w.address < face_address + 0x41 || w.address > face_address + 0x43,
                      "type 0x18 wrote a corner shade byte");
            shade_writes += w.address == seen_address + 0x40 && w.value == expected && w.bytes == 1;
        }
        check(shade_writes == 1, "type 0x18 flat shade feedback");
        const size_t centre = 64 * 256 + 128;
        auto baked18 = flat18;
        baked18.nodes[0].light_count = 0;
        baked18.nodes[0].shade = 11; // the head face's stored byte
        check(pixels_of(baked18) == lit18 && red(lit18[centre]) != 0,
              "type 0x18 palette row does not follow the head face");
        auto other_row = flat18;
        other_row.faces[0].shade = 20;
        check(red(pixels_of(other_row)[centre]) > red(lit18[centre]),
              "type 0x18 ignored the head face's row");
        auto other_grey = flat18;
        other_grey.faces[1].corner_shades = {31, 31, 31};
        const auto bright18 = pixels_of(other_grey);
        check(red(bright18[centre]) > red(lit18[centre]) &&
                  red(bright18[50 * 256 + 128]) > red(lit18[50 * 256 + 128]),
              "type 0x18 did not take corner grey from the stored face bytes");
    }
    draw.reset(renderer);
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
    std::puts("scene lights: stale view-space slot (WDSB), inactive flat carry and Gouraud skip passed");
    std::puts("scene palette: DOS 3dfx snapshot (WDSC) binds row = shade, Windows snapshot 31 - shade passed");
    std::puts("scene attack lights: lit row 0 equals the DOS unlit row, Windows step per shade, "
              "per-face rows, head row without host rows, WDSD round trip passed");
    std::puts("scene far plane: node sphere, flag 0x80, flag 0x10 under a drawn or culled parent, "
              "faces with a corner at the plane, snapshot round trip passed");
    std::puts("scene Glide parity: 28 no-draw types skipped and logged once, 9 unknown types rejected, "
              "257 deferred blocks drawn and reported once, lit 0x18 fixture passed");
}
