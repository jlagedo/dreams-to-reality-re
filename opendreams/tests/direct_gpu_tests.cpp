// Native readback here is an oracle operation, never a compositor dependency.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include "render/direct_sokol.h"
#include "port/fog.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
static void check(bool b, const char *text) {
    if (!b) {
        std::fprintf(stderr, "%s\n", text);
        std::exit(1);
    }
}
static uint32_t word(FILE *f) {
    uint32_t v;
    check(std::fread(&v, 4, 1, f) == 1, "short fixture");
    return v;
}
static void logger(const char *, uint32_t level, uint32_t, const char *msg, uint32_t, const char *,
                   void *) {
    if (level <= 1) {
        std::fprintf(stderr, "sokol: %s\n", msg ? msg : "error");
        std::exit(2);
    }
}
static ID3D11Device *device;
static ID3D11DeviceContext *context;
static std::vector<uint32_t> capture(od_renderer *r, od_render_id id) {
    auto *image = (ID3D11Texture2D *)sg_d3d11_query_image_info(od_renderer_image(r, id)).tex2d;
    D3D11_TEXTURE2D_DESC d{};
    image->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D *staging = nullptr;
    check(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "staging allocation");
    context->CopyResource(staging, image);
    D3D11_MAPPED_SUBRESOURCE m{};
    check(SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &m)), "readback map");
    std::vector<uint32_t> pixels(size_t(d.Width) * d.Height);
    for (unsigned y = 0; y < d.Height; ++y)
        std::memcpy(pixels.data() + size_t(y) * d.Width, (char *)m.pData + size_t(y) * m.RowPitch,
                    size_t(d.Width) * 4);
    context->Unmap(staging, 0);
    staging->Release();
    sg_reset_state_cache();
    return pixels;
}
static void submit(od_renderer *r, od_draw_2d c) {
    check(od_renderer_draw_2d(r, &c) != 0, od_renderer_error(r));
}
static void finish(od_renderer *r) {
    sg_commit();
    od_renderer_frame_complete(r);
}
static void packed_round_trips(od_renderer *r) {
    std::vector<uint32_t> packed(65536);
    for (uint32_t i = 0; i < 65536; ++i)
        packed[i] = i | (63u << 16);
    auto source = od_renderer_upload_packed(r, 256, 256, packed.data(), 256 * 4);
    // A mutation after upload must not change the immutable source version.
    std::fill(packed.begin(), packed.end(), 0);
    for (int f = 0; f < 2; ++f) {
        auto target = od_renderer_target(r, 256, 256, 256, 256);
        submit(r, {OD_DRAW_RAW, target, source, {0, 0, 256, 256}, od_pixel_format(f), 0});
        finish(r);
        auto pixels = capture(r, target);
        for (uint32_t i = 0; i < 65536; ++i)
            check(od_pack_colour(pixels[i], od_pixel_format(f)) == i, "GPU packed round trip");
        check(od_renderer_release(r, target) != 0, "packed target release");
    }
    check(od_renderer_release(r, source) != 0, "packed source release");
    finish(r);
    std::puts("direct GPU: 131,072 packed round trips and immutable source mutation passed");
}
static void fixture(od_renderer *r, const char *path) {
    FILE *f = nullptr;
    fopen_s(&f, path, "rb");
    check(f && word(f) == 0x31443244, "invalid fixture");
    const uint32_t w = word(f), h = word(f), fmt = word(f), nt = word(f), nc = word(f);
    check(w && h && w <= 1920 && h <= 1080 && fmt < 2 && nt <= 8 && nc <= 256, "fixture limits");
    std::vector<od_render_id> targets(nt);
    for (auto &id : targets) {
        std::vector<uint32_t> data(size_t(w) * h);
        check(std::fread(data.data(), 4, data.size(), f) == data.size(), "initial data");
        for (auto &v : data)
            v |= 63u << 16;
        id = od_renderer_target(r, int(w), int(h), int(w), int(h));
        check(id != 0, od_renderer_error(r));
        auto src = od_renderer_upload_packed(r, int(w), int(h), data.data(), w * 4);
        auto src_pixels = capture(r, src);
        check(src_pixels[0] == data[0], "source upload bytes mismatch");
        submit(r, {OD_DRAW_RAW, id, src, {0, 0, int(w), int(h)}, od_pixel_format(fmt), 0});
        check(od_renderer_release(r, src) != 0, "release source");
        finish(r);
        auto init_pixels = capture(r, id);
        if (od_pack_colour(init_pixels[0], od_pixel_format(fmt)) != uint16_t(data[0])) {
            std::fprintf(stderr, "initial source %08x output %08x\n", data[0], init_pixels[0]);
            check(false, "initial packed upload mismatch");
        }
    }
    finish(r);
    uint64_t pixels = 0;
    for (uint32_t k = 0; k < nc; ++k) {
        const auto kind = od_draw_kind(word(f));
        const unsigned dest = word(f), source = word(f);
        od_rect rect{int32_t(word(f)), int32_t(word(f)), int(word(f)), int(word(f))};
        const unsigned param = word(f), n = word(f);
        check(dest < nt && source < nt && n <= w * h * 2 + 7168, "command layout");
        std::vector<uint32_t> data(n);
        check(std::fread(data.data(), 4, n, f) == n, "source data");
        std::vector<uint16_t> expected(size_t(w) * h);
        check(std::fread(expected.data(), 2, expected.size(), f) == expected.size(),
              "expected pixels");
        od_render_id src = targets[source];
        od_render_id lookup = 0;
        if (n) {
            const uint32_t count = uint32_t(rect.width * rect.height);
            check(n == count + (kind == OD_DRAW_LOOKUP ? 7168 : 0), "source dimensions");
            src = od_renderer_upload_packed(r, rect.width, rect.height, data.data(),
                                            size_t(rect.width) * 4);
            if (kind == OD_DRAW_LOOKUP)
                lookup = od_renderer_upload_packed(r, 256, 28, data.data() + count, 256 * 4);
        }
        submit(r, {kind, targets[dest], src, rect, od_pixel_format(fmt), param, 0, lookup});
        if (lookup)
            check(od_renderer_release(r, lookup) != 0, "release queued lookup snapshot");
        if (n)
            check(od_renderer_release(r, src) != 0, "release queued source");
        finish(r);
        auto actual = capture(r, targets[dest]);
        for (size_t i = 0; i < actual.size(); ++i)
            if (od_pack_colour(actual[i], od_pixel_format(fmt)) != expected[i]) {
                std::fprintf(
                    stderr, "%s command %u pixel %zu: %04x != %04x (rgba %08x, wanted %08x)\n",
                    path, k, i, od_pack_colour(actual[i], od_pixel_format(fmt)), expected[i],
                    actual[i], od_expand_colour(expected[i], od_pixel_format(fmt)));
                std::exit(1);
            }
        pixels += actual.size();
    }
    std::fclose(f);
    for (auto id : targets)
        check(od_renderer_release(r, id) != 0, "release target");
    finish(r);
    std::printf("%s: %u checkpoints, %llu pixels, zero mismatches\n", path, nc,
                (unsigned long long)pixels);
}
static void scene_checks(od_renderer *r) {
    auto target = od_renderer_target(r, 64, 48, 64, 48);
    od_pose_node nodes[2] = {{-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}},
                             {-1, {1, 0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0}}};
    // Corner 1 belongs to another translated node. Ignoring its owner makes
    // these triangles degenerate and the centre-pixel check fails.
    od_scene_vertex v[6] = {{{-1, -1, 1}}, {{-1, -1, 1}}, {{0, 1, 1}},
                            {{-1, -1, 2}}, {{-1, -1, 2}}, {{0, 1, 2}}};
    od_scene_triangle tri[2]{};
    for (unsigned t = 0; t < 2; ++t)
        for (unsigned c = 0; c < 3; ++c)
            tri[t].corners[c] = {c % 2, t * 3 + c, {0, 0}};
    tri[0].colour = 0xff2143a5;
    tri[1].colour = 0xff00ff00;
    od_scene_packet p{};
    p.target = target;
    p.nodes = nodes;
    p.node_count = 2;
    p.vertices = v;
    p.vertex_count = 6;
    p.triangles = tri;
    p.triangle_count = 2;
    check(od_projection_hor_plus(1, 4.0f / 3, 0.1f, 100, p.view_projection) != 0, "projection");
    p.clear = 1;
    p.clear_colour[3] = 1;
    check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
    finish(r);
    auto before = capture(r, target);
    check(before[24 * 64 + 32] == 0xff2143a5, "GREATER depth or modern colour");
    submit(r, {OD_DRAW_BAND, target, 0, {0, 0, 64, 4}, OD_RGB565, 0});
    finish(r);
    auto after = capture(r, target);
    check(after[24 * 64 + 32] == before[24 * 64 + 32], "UI changed untouched modern colour");
    submit(r, {OD_DRAW_BAND, target, 0, {32, 24, 1, 1}, OD_RGB565, 0});
    finish(r);
    const uint16_t dimmed =
        uint16_t((od_pack_colour(before[24 * 64 + 32], OD_RGB565) & 0xf7deu) >> 1);
    after = capture(r, target);
    check(after[24 * 64 + 32] == od_expand_colour(dimmed, OD_RGB565),
          "integer effect over modern destination");
    auto output = od_renderer_target(r, 64, 48, 64, 48);
    sg_view_desc vd{};
    vd.color_attachment.image = od_renderer_image(r, output);
    auto attachment = sg_make_view(&vd);
    sg_pass pass{};
    pass.attachments.colors[0] = attachment;
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    sg_begin_pass(&pass);
    check(od_renderer_output(r, target, 0.8f) != 0, "output correction");
    sg_end_pass();
    finish(r);
    auto corrected = capture(r, output);
    for (unsigned shift : {0u, 8u, 16u}) {
        const float linear = float((after[24 * 64 + 32] >> shift) & 255) / 255;
        const int expected = int(std::round(std::pow(linear, 1.25f) * 255));
        check(std::abs(int((corrected[24 * 64 + 32] >> shift) & 255) - expected) <= 1,
              "gamma after composition");
    }
    sg_destroy_view(attachment);
    check(od_renderer_release(r, output) != 0, "output release");
    check(od_renderer_release(r, target) != 0, "target release");
    auto next = od_renderer_target(r, 64, 48, 64, 48);
    check(next != target, "generation did not advance");
    od_draw_2d stale{OD_DRAW_FILL, target, 0, {0, 0, 64, 48}, OD_RGB565, 0};
    check(!od_renderer_draw_2d(r, &stale), "stale target accepted");
    check(od_renderer_release(r, next) != 0, "new target release");
    auto wide = od_renderer_target(r, 1920, 1080, 640, 480);
    submit(r, {OD_DRAW_FILL, wide, 0, {0, 0, 640, 480}, OD_RGB565, 0xffff});
    finish(r);
    auto canvas = capture(r, wide);
    check(canvas[540 * 1920 + 239] == 0xff000000 && canvas[540 * 1920 + 240] == 0xffffffff &&
              canvas[540 * 1920 + 1679] == 0xffffffff && canvas[540 * 1920 + 1680] == 0xff000000,
          "1920x1080 centered canvas");
    check(od_renderer_release(r, wide) != 0, "wide target release");
    finish(r);
    check(od_renderer_stats(r).live_resources == 0, "resource leak");
    std::puts(
        "direct scene: cross-node corners, GREATER depth, modern colour, integer UI, final gamma, "
        "1920x1080 canvas and target generations passed");
}
static void material_checks(od_renderer *r) {
    const uint32_t texels[2] = {0xff0000ffu, 0xff00ff00u};
    auto texture = od_renderer_upload_rgba(r, 2, 1, texels, 8);
    auto target = od_renderer_target(r, 16, 16, 16, 16);
    od_pose_node node{-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    od_scene_vertex vertices[6] = {{{-1, -1, 0.8f}}, {{3, -1, 0.8f}}, {{-1, 3, 0.8f}},
                                   {{-1, -1, 0.5f}}, {{3, -1, 0.5f}}, {{-1, 3, 0.5f}}};
    od_scene_triangle triangles[2]{};
    for (unsigned i = 0; i < 3; ++i) {
        triangles[0].corners[i] = {0, i, {1.25f, 0.5f}};
        triangles[1].corners[i] = {0, i + 3, {0, 0}};
    }
    triangles[0].texture = texture;
    triangles[1].colour = 0xff00ff00u;
    od_scene_packet p{};
    p.target = target;
    p.nodes = &node;
    p.node_count = 1;
    p.vertices = vertices;
    p.vertex_count = 6;
    p.triangles = triangles;
    p.triangle_count = 1;
    p.clear = 1;
    p.clear_colour[2] = p.clear_colour[3] = 1;
    p.view_projection[0] = p.view_projection[5] = p.view_projection[10] = p.view_projection[15] = 1;
    check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
    finish(r);
    check(capture(r, target)[8 * 16 + 8] == 0xff00ff00u, "material clamp");
    triangles[0].wrap_texture = 1;
    check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
    finish(r);
    check(capture(r, target)[8 * 16 + 8] == 0xff0000ffu, "material repeat");
    triangles[0].texture = 0;
    triangles[0].colour = 0xff0000ffu;
    triangles[0].mode = OD_FACE_TRANSLUCENT;
    p.triangle_count = 2;
    check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
    finish(r);
    check(capture(r, target)[8 * 16 + 8] == 0x807f0080u,
          "alpha 128/255 or translucent depth write");
    for (unsigned i = 3; i < 6; ++i)
        vertices[i].xyz[2] = 0.8f;
    check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
    finish(r);
    check(capture(r, target)[8 * 16 + 8] == 0x807f0080u, "equal depth must fail GREATER");
    od_renderer_release(r, target);
    od_renderer_release(r, texture);
    finish(r);
    std::puts("direct material: clamp/repeat, alpha 128/255, ordered depth writes and equal-depth "
              "rejection passed");
}
static void shadow_checks(od_renderer *r) {
    auto mask = od_renderer_target(r, 128, 256, 128, 256);
    od_pose_node node{-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    std::vector<od_scene_vertex> vertices;
    std::vector<od_scene_triangle> triangles;
    for (int y = 0; y < 256; ++y)
        if ((y / 2) % 2 == 0) {
            const float top = 1.0f - 2.0f * y / 256, bottom = 1.0f - 2.0f * (y + 1) / 256;
            const unsigned base = unsigned(vertices.size());
            vertices.insert(
                vertices.end(),
                {{{-1, top, 0.5f}}, {{0, top, 0.5f}}, {{0, bottom, 0.5f}}, {{-1, bottom, 0.5f}}});
            for (auto corners : {std::array<unsigned, 3>{0, 1, 2}, {0, 2, 3}}) {
                od_scene_triangle t{};
                t.colour = 0xff010101;
                for (int c = 0; c < 3; ++c)
                    t.corners[c] = {0, base + corners[c], {0, 0}};
                triangles.push_back(t);
            }
        }
    od_scene_packet p{};
    p.target = mask;
    p.nodes = &node;
    p.node_count = 1;
    p.vertices = vertices.data();
    p.vertex_count = vertices.size();
    p.triangles = triangles.data();
    p.triangle_count = triangles.size();
    p.clear = 1;
    p.clear_colour[3] = 1;
    p.view_projection[0] = p.view_projection[5] = p.view_projection[10] = p.view_projection[15] = 1;
    check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
    uint16_t palette[256]{};
    palette[0] = 0x001f;
    palette[1] = 0xf800;
    auto resolved = od_renderer_resolve_shadow(r, mask, palette);
    check(resolved != 0, od_renderer_error(r));
    finish(r);
    auto pixels = capture(r, resolved);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            check(pixels[y * 128 + x] == (x < 64 && y % 2 == 0 ? 0xff0000f8u : 0x00f80000u),
                  "shadow P8 pair/LOD/palette resolve");
    palette[1] = palette[0];
    auto keyed = od_renderer_resolve_shadow(r, mask, palette);
    check(keyed != 0, od_renderer_error(r));
    finish(r);
    for (auto value : capture(r, keyed))
        check(value == 0x00f80000u, "shadow colour-key equality");
    check(capture(r, resolved) == pixels, "shadow resolved version mutated");
    od_renderer_release(r, resolved);
    od_renderer_release(r, keyed);
    od_renderer_release(r, mask);
    finish(r);
    std::puts(
        "direct shadow: GPU mask, paired-P8 LOD mapping, palette key and immutable resolve passed");
}
static void fog_checks(od_renderer *r) {
    auto target = od_renderer_target(r, 16, 16, 16, 16);
    od_pose_node node{-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    od_scene_vertex vertices[3]{};
    od_scene_triangle triangle{};
    triangle.colour = 0xffff0000u;
    for (unsigned i = 0; i < 3; ++i)
        triangle.corners[i] = {0, i, {0, 0}};
    od_scene_packet p{};
    p.target = target;
    p.nodes = &node;
    p.node_count = 1;
    p.vertices = vertices;
    p.vertex_count = 3;
    p.triangles = &triangle;
    p.triangle_count = 1;
    p.clear = 1;
    p.clear_colour[3] = 1;
    p.fog.enabled = 1;
    p.fog.colour = 0xff0000ff;
    for (int i = 0; i < 64; ++i)
        p.fog.table[i] = uint8_t(i * 4);
    check(od_projection_hor_plus(1, 1, .5f, 100000, p.view_projection) != 0, "fog camera");
    auto at = [&](float z) {
        vertices[0] = {{-z, -z, z}};
        vertices[1] = {{3 * z, -z, z}};
        vertices[2] = {{-z, 3 * z, z}};
        check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
        finish(r);
        return capture(r, target)[8 * 16 + 8];
    };
    for (int i = 0; i < 64; ++i) {
        const uint32_t pixel = at(od::port::guFogTableIndexToW(i));
        check(std::abs(int(pixel & 255) - i * 4) <= 1 &&
                  std::abs(int((pixel >> 16) & 255) - (255 - i * 4)) <= 1,
              "fog table breakpoint/depth response");
        if (i < 63) {
            const auto midpoint =
                at((od::port::guFogTableIndexToW(i) + od::port::guFogTableIndexToW(i + 1)) * .5f);
            check(std::abs(int(midpoint & 255) - (i * 4 + 2)) <= 1, "fog table interpolation");
        }
    }
    p.fog.enabled = 0;
    check(at(100) == 0xffff0000u, "disabled fog changed colour");
    p.fog.enabled = 1;
    std::fill(std::begin(p.fog.table),std::end(p.fog.table),0);
    check(at(100)==0xfffe0001u,"enabled zero fog table must retain SST1 blend bias");
    std::fill(std::begin(p.fog.table), std::end(p.fog.table), 255);
    triangle.mode = OD_FACE_TRANSLUCENT;
    p.clear_colour[1] = 1;
    const auto alpha = at(100);
    check((alpha & 255) == 128 && ((alpha >> 8) & 255) == 127 && ((alpha >> 16) & 255) == 0,
          "fog must precede translucent blending and preserve alpha");
    submit(r, {OD_DRAW_FILL, target, 0, {8, 8, 1, 1}, OD_RGB565, 0xffff});
    finish(r);
    check(capture(r, target)[8 * 16 + 8] == 0xffffffff, "fog affected later UI");
    od_renderer_release(r, target);
    finish(r);
    std::puts(
        "direct fog: 64 depth knots, 63 interpolants, disable, alpha order and unfogged UI passed");
}
// Independent fixed-point reference: MAME compute_wfloat/apply_fogging,
// pinned d0c76bd663cbf52adcce086cb3eacff612a2f77f. The shader instead derives
// the selector from IEEE float exponent/mantissa. No original RGB565 packing.
static unsigned fog_reference(float w, const std::array<uint8_t, 64> &table) {
    const uint64_t iterw = uint64_t(281474976710656.0 / double(w));
    uint64_t scan = iterw;
    int highest = -1;
    while (scan) {
        ++highest;
        scan >>= 1;
    }
    const int exponent = 47 - highest;
    uint32_t depth;
    if (exponent < 0)
        depth = 0;
    else if (exponent >= 16)
        depth = 65535;
    else
        depth = std::min(
            65535u,
            uint32_t(((uint64_t(exponent) << 12) | ((iterw >> (35 - exponent)) ^ 0x1fff)) + 1));
    const auto pairs = od::port::grFogTable_pairs(table);
    const unsigned index = depth >> 10, shift = (index & 1) * 16;
    const unsigned pair = pairs[index / 2] >> shift;
    return ((pair >> 8) & 255) + (((pair & 255) * ((depth >> 2) & 255)) >> 10);
}
static void fog_selector_checks(od_renderer *r) {
    constexpr unsigned count = 65536, width = 256;
    auto target = od_renderer_target(r, width, width, width, width);
    od_pose_node node{-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}};
    std::vector<od_scene_vertex> vertices;
    std::vector<od_scene_triangle> triangles;
    std::vector<float> depths;
    vertices.reserve(count * 6);
    triangles.reserve(count * 2);
    depths.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const double code = i ? double(i) - .5 : 0;
        const int exponent = int(code) / 4096;
        const float z = float(std::ldexp(8192.0, exponent) / (8192.0 - (code - exponent * 4096)));
        depths.push_back(z);
        const float x0 = 2.0f * (i % width) / width - 1, x1 = x0 + 2.0f / width;
        const float y0 = 1 - 2.0f * (i / width) / width, y1 = y0 - 2.0f / width;
        const uint32_t base = uint32_t(vertices.size());
        vertices.insert(vertices.end(), {{{x0 * z, y0 * z, z}},
                                         {{x1 * z, y0 * z, z}},
                                         {{x1 * z, y1 * z, z}},
                                         {{x0 * z, y0 * z, z}},
                                         {{x1 * z, y1 * z, z}},
                                         {{x0 * z, y1 * z, z}}});
        for (unsigned t = 0; t < 2; ++t) {
            od_scene_triangle tri{};
            tri.colour = 0xffff0000;
            for (unsigned c = 0; c < 3; ++c)
                tri.corners[c] = {0, base + t * 3 + c, {0, 0}};
            triangles.push_back(tri);
        }
    }
    od_scene_packet p{};
    p.target = target;
    p.nodes = &node;
    p.node_count = 1;
    p.vertices = vertices.data();
    p.vertex_count = vertices.size();
    p.triangles = triangles.data();
    p.triangle_count = triangles.size();
    p.clear = 1;
    p.clear_colour[3] = 1;
    p.fog.enabled = 1;
    p.fog.colour = 0xff0000ff;
    check(od_projection_hor_plus(1, 1, .5f, 100000, p.view_projection) != 0, "fog sweep camera");
    unsigned maximum_error = 0;
    for (int pattern = 0; pattern < 2; ++pattern) {
        std::array<uint8_t, 64> table{};
        if (pattern == 0)
            od::port::guFogGenerateExp(table, .00018f);
        else
            for (unsigned i = 0; i < table.size(); ++i)
                table[i] = uint8_t((i * 73) ^ 0xb5);
        std::copy(table.begin(), table.end(), p.fog.table);
        check(od_renderer_scene(r, &p) != 0, od_renderer_error(r));
        finish(r);
        auto pixels = capture(r, target);
        for (unsigned i = 0; i < count; ++i) {
            const float factor = float(fog_reference(depths[i], table) + 1) / 256;
            const int expected_r = int(std::round(std::clamp(factor, 0.0f, 1.0f) * 255));
            const int expected_b = int(std::round(std::clamp(1 - factor, 0.0f, 1.0f) * 255));
            const unsigned error =
                unsigned(std::max(std::abs(int(pixels[i] & 255) - expected_r),
                                  std::abs(int((pixels[i] >> 16) & 255) - expected_b)));
            maximum_error = std::max(maximum_error, error);
            if (error > 1) {
                std::fprintf(stderr, "fog selector %u pattern %d z=%f error=%u\n", i, pattern,
                             depths[i], error);
                check(false, "fog reciprocal-W/delta mismatch");
            }
        }
    }
    od_renderer_release(r, target);
    finish(r);
    std::printf("direct fog: 131072 reciprocal-W samples versus fixed-point reference; max RGBA8 "
                "channel error %u\n",
                maximum_error);
}
int main(int argc, char **argv) {
    const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL got;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &requested, 1,
                                      D3D11_SDK_VERSION, &device, &got, &context)),
          "hardware D3D11 unavailable");
    sg_desc d{};
    d.environment.d3d11.device = device;
    d.environment.d3d11.device_context = context;
    d.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    d.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    d.environment.defaults.sample_count = 1;
    d.logger.func = logger;
    sg_setup(&d);
    auto *r = od_renderer_create();
    check(r != nullptr, "renderer creation");
    scene_checks(r);
    material_checks(r);
    shadow_checks(r);
    fog_checks(r);
    fog_selector_checks(r);
    packed_round_trips(r);
    for (int i = 1; i < argc; ++i)
        fixture(r, argv[i]);
    od_renderer_destroy(r);
    sg_shutdown();
    context->Release();
    device->Release();
}
