#include "render/direct.h"
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
int main() {
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
