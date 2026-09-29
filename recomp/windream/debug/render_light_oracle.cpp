#include "render/direct.h"
#include "render_scene_draw.h"
#include <algorithm>
extern "C" int wd_flat_light_shade(const int32_t *vertices, const int32_t *normal, int32_t plane,
                                   const od_radial_light *lights, size_t count, uint8_t *shade) {
    return od_radial_flat_shade(vertices, normal, plane, lights, count, shade);
}
extern "C" int wd_light_local(const float *world, const int32_t *position, int32_t *local) {
    return od_radial_light_local(world, position, local);
}
extern "C" int wd_lit_pair(const int32_t *points, const od_radial_light *light, uint8_t *shades,
                           int32_t *normal_dots, int32_t *local) {
    wd::SceneSnapshot scene;
    scene.light_transform_count = 1;
    scene.poses.push_back({-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    scene.nodes.resize(1);
    scene.nodes[0].submitted = true;
    scene.nodes[0].light_count = 1;
    auto &source = scene.lights[0];
    source.type = 1;
    std::copy_n(light->position, 3, source.position.begin());
    source.inner_radius = light->inner_radius;
    source.outer_radius = light->outer_radius;
    source.intensity = light->intensity;
    for (unsigned i = 0; i < 6; ++i) {
        scene.vertices.push_back(
            {{float(points[i * 3]), float(points[i * 3 + 1]), float(points[i * 3 + 2])}});
        scene.source_vertices.push_back({points[i * 3], points[i * 3 + 1], points[i * 3 + 2]});
    }
    for (unsigned i = 0; i < 2; ++i) {
        wd::SceneFace face;
        face.type = 3;
        face.owner = 0;
        face.address = 0x18002000 + i * 0x80;
        face.normal_address = 0x18003000 + i * 16;
        face.normal = {0, 0, -32768};
        face.plane_distance = -10;
        face.shade = shades[i];
        for (unsigned c = 0; c < 3; ++c)
            face.corners[c] = {0, i * 3 + c, {0, 0}};
        scene.faces.push_back(face);
    }
    float vp[16];
    od_projection_hor_plus(1, 1, 1, 100, vp);
    vp[5] = -1;
    wd::SceneLighting result;
    std::string error;
    if (!wd::prepare_radial_lighting(scene, vp, result, error, true))
        return 0;
    std::copy_n(result.shades.begin(), 2, shades);
    for (const auto &w : result.writes) {
        if (w.address >= 0x672764 && w.address < 0x672770)
            local[(w.address - 0x672764) / 4] = int32_t(w.value);
        if (w.address == 0x1800300c)
            normal_dots[0] = int32_t(w.value);
        if (w.address == 0x1800301c)
            normal_dots[1] = int32_t(w.value);
    }
    return 1;
}
