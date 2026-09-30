#include "render/direct.h"
#include "render/direct_edges.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #e);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static int source_edge_tests() {
    od_pose_node poses[4]{};
    poses[0].parent = -1;
    poses[1].parent = 0;
    poses[2].parent = poses[3].parent = 1;
    od_scene_triangle triangles[2]{};
    for (unsigned i = 0; i < 6; ++i)
        triangles[i / 3].corners[i % 3] = {i < 3 ? 2u : 3u, i, {float(i), float(i + 1)}};
    od_scene_packet packet{};
    packet.nodes = poses;
    packet.node_count = 4;
    packet.triangles = triangles;
    packet.triangle_count = 2;
    packet.source_edge_quantum = 1;
    const std::array<float, 18> source{0, 0, 0, 100, 0, 0, 0, 0, 100,
                                      100, 1, 0, 0, 1, 0, 0, 101, 0};
    auto points = source;
    size_t joins = 0;
    std::string error;
    CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error));
    CHECK(joins == 1 && points[10] == 0 && points[13] == 0);
    CHECK(points[0] == 0 && points[7] == 0 && points[16] == 101);
    CHECK(triangles[1].corners[0].uv[0] == 3); // Identity/material metadata unchanged.
    for (unsigned i = 0; i < 6; ++i) {
        double distance = 0;
        for (unsigned axis = 0; axis < 3; ++axis)
            distance += std::pow(points[i * 3 + axis] - source[i * 3 + axis], 2);
        CHECK(distance <= 1.000001);
    }
    packet.source_edge_quantum = 0;
    points = source;
    CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error) && !joins && points == source);
    packet.source_edge_quantum = 1;
    poses[3].parent = 0; // A separate actor/model root must never attach.
    CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error) && !joins && points == source);
    poses[3].parent = 1;
    for (auto mode : {OD_FACE_CHROMA, OD_FACE_TRANSLUCENT}) {
        triangles[1].mode = mode;
        points = source;
        CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error) && !joins && points == source);
    }
    triangles[1].mode = OD_FACE_OPAQUE;
    points = source;
    points[10] = points[13] = 2; // A larger, intentional separation is retained.
    const auto separated = points;
    CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error) && !joins && points == separated);
    points = source;
    points[16] = 1;
    points[17] = 100; // Parallel layered planes, even only one unit apart.
    const auto parallel = points;
    CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error) && !joins && points == parallel);
    for (float shift : {-10000.f, 0.f, 12345.f}) {
        points = source;
        for (unsigned i = 0; i < 6; ++i)
            points[i * 3] += shift;
        CHECK(od::stitch_source_edges(packet, points.data(), 3, joins, error) && joins == 1);
        CHECK(points[10] == 0 && points[13] == 0 && points[9] == 100 + shift);
    }
    // A second nearby layer cannot pull an established join along a chain.
    od_scene_triangle chain[3] = {triangles[0], triangles[1], triangles[0]};
    for (unsigned c = 0; c < 3; ++c)
        chain[2].corners[c].vertex = 6 + c;
    std::vector<float> chain_points(source.begin(), source.end());
    chain_points.insert(chain_points.end(), {0, 2, 0, 100, 2, 0, 0, 2, 100});
    packet.triangles = chain;
    packet.triangle_count = 3;
    CHECK(od::stitch_source_edges(packet, chain_points.data(), 3, joins, error) && joins == 1);
    CHECK(chain_points[10] == 0 && chain_points[13] == 0);
    CHECK(chain_points[19] == 2 && chain_points[22] == 2 && chain_points[25] == 2);
    packet.triangles = triangles;
    packet.triangle_count = 2;
    packet.source_edge_quantum = -1;
    CHECK(!od::stitch_source_edges(packet, points.data(), 3, joins, error));
    std::puts("source edges: bounded plane joins, root separation, alpha/layer protection and stable motion passed");
    return 0;
}
int main() {
    CHECK(source_edge_tests() == 0);
    float clip[12] = {-.5f, -.5f, .5f, 1, .5f, -.5f, .5f, 1, 0, .5f, .5f, 1};
    CHECK(od_triangle_visible(clip, 1) == 1);
    std::swap(clip[0], clip[4]);
    CHECK(od_triangle_visible(clip, 1) == 0);
    CHECK(od_triangle_visible(clip, 0) == 1);
    std::swap(clip[0], clip[4]);
    clip[10] = 2;
    CHECK(od_triangle_visible(clip, 1) == 1); // near-plane crossing
    clip[2] = clip[6] = 2;
    CHECK(od_triangle_visible(clip, 1) == 0);
    clip[2] = clip[6] = clip[10] = -.5f;
    CHECK(od_triangle_visible(clip, 1) == 0);
    clip[2] = clip[6] = clip[10] = .5f;
    clip[8] = 3;
    CHECK(od_triangle_visible(clip, 1) == 1); // side-plane crossing
    clip[0] = clip[4] = 2;
    CHECK(od_triangle_visible(clip, 1) == 0);
    clip[0] = NAN;
    CHECK(od_triangle_visible(clip, 1) == -1);
    uint8_t indices[256 * 256]{};
    uint16_t palette[256]{};
    uint32_t rgba[128 * 128]{};
    palette[0] = palette[1] = 0x1234;
    palette[2] = 0xffff;
    palette[3] = 0xf800;
    indices[0] = 1;
    indices[1] = 3;
    indices[2] = 2;
    indices[256 * 2] = 3;
    CHECK(od_expand_material_page(indices, 256, palette, rgba, 128));
    CHECK((rgba[0] >> 24) == 0);    // key COLOUR, even with nonzero index
    CHECK(rgba[1] == 0xfff8fcf8);   // no bit replication in Glide palette expansion
    CHECK(rgba[128] == 0xff0000f8); // even source rows/columns form the 128 LOD
    CHECK(!od_expand_material_page(indices, 256, palette, rgba, 64));
    for (int f = 0; f < 2; ++f)
        for (unsigned c = 0; c < 65536u; ++c)
            CHECK(od_pack_colour(od_expand_colour(uint16_t(c), od_pixel_format(f)),
                                 od_pixel_format(f)) == c);
    auto v = od_centered_canvas(1920, 1080, 640, 480);
    CHECK(v.x == 240 && v.y == 0 && v.width == 1440 && v.height == 1080);
    float x = -1, y = -1;
    CHECK(od_canvas_point(v, 640, 480, 960, 540, &x, &y) && x == 320 && y == 240);
    CHECK(!od_canvas_point(v, 640, 480, 0, 540, &x, &y) && x == 0 && y == 240);
    od_pose_node nodes[3] = {{1, {1, 0, 0, 1, 0, 1, 0, 2, 0, 0, 1, 3}},
                             {-1, {0, -1, 0, 10, 1, 0, 0, 20, 0, 0, 1, 30}},
                             {0, {1, 0, 0, 5, 0, 1, 0, 0, 0, 0, 1, 0}}};
    float world[36]{};
    CHECK(od_compose_pose(nodes, 3, world));
    CHECK(world[3] == 8 && world[7] == 21 && world[11] == 33);
    CHECK(world[27] == 8 && world[31] == 26 && world[35] == 33);
    nodes[1].parent = 2;
    CHECK(!od_compose_pose(nodes, 3, world));
    nodes[1].parent = 9;
    CHECK(!od_compose_pose(nodes, 3, world));
    float narrow[16], wide[16];
    CHECK(od_projection_hor_plus(2, 4.0f / 3, 1, 100, narrow));
    CHECK(od_projection_hor_plus(2, 16.0f / 9, 1, 100, wide));
    CHECK(wide[0] < narrow[0] && wide[5] == narrow[5]);
    CHECK(std::abs((wide[10] + wide[14]) - 1) < 1e-6);
    CHECK(std::abs((wide[10] * 100 + wide[14]) / 100) < 1e-6);
    CHECK(!od_projection_hor_plus(2, 1, 10, 1, wide));
    std::puts("direct math: 131,072 packed round trips; pose, cycle, Hor+ and UI mapping passed");
}
