#include "render/direct.h"
#include "render_scene_draw.h"
#include <algorithm>
extern "C" int wd_shadow_projection_file(const char *path, int32_t *poses, float *view,
                                          int32_t *screen) {
    wd::SceneSnapshot scene;
    wd::ShadowInput input;
    std::string error;
    if(!wd::read_scene(path,scene,error)||!wd::prepare_shadow_input(scene,0,input,error))return 0;
    return od_shadow_project(&input.packet,poses,view,screen);
}
extern "C" int wd_environment_uv(const int32_t *rotation, const int32_t *normal,
                                  unsigned corner, int32_t *uv) {
    return od_environment_uv(rotation, normal, corner, uv);
}
extern "C" int wd_feedback_rotation_chain(const int32_t *camera, const int32_t *local,
                                           const int32_t *parents, size_t count, int32_t *output) {
    if (!count || count > 10000)
        return 0;
    wd::SceneSnapshot scene;
    scene.camera.address = 0x1000;
    std::copy_n(camera, 9, scene.camera.local_rotation.begin());
    scene.nodes.resize(count);
    scene.poses.resize(count);
    for (size_t i = 0; i < count; ++i) {
        scene.nodes[i].address = 0x1000 + uint32_t(i) * 0x100;
        scene.poses[i].parent = parents[i];
        std::copy_n(local + i * 9, 9, scene.nodes[i].source_rotation.begin());
    }
    std::vector<int32_t> rotations;
    if (!wd::compose_feedback_rotations(scene, rotations))
        return 0;
    std::copy(rotations.begin(), rotations.end(), output);
    return 1;
}
extern "C" int wd_feedback_rotations_file(const char *path, int32_t *output, size_t count) {
    wd::SceneSnapshot scene;
    std::string error;
    std::vector<int32_t> rotations;
    if (!wd::read_scene(path, scene, error) || count != scene.nodes.size() ||
        !wd::compose_feedback_rotations(scene, rotations))
        return 0;
    std::copy(rotations.begin(), rotations.end(), output);
    return 1;
}
extern "C" int wd_gouraud_lights(const int32_t *vertices, const int32_t *normals,
                                 const od_local_light *lights, size_t count, uint8_t *shades,
                                 int32_t *dots) {
    if (count > 8)
        return 0;
    std::fill_n(shades, 3, uint8_t(0));
    for (size_t light = 0; light < count; ++light) {
        for (unsigned corner = 0; corner < 3; ++corner) {
            dots[corner] = od_light_normal_dot(normals + corner * 3, &lights[light]);
            shades[corner] = uint8_t(uint32_t(shades[corner]) + uint32_t(
                od_gouraud_light_contribution(vertices + corner * 3, normals + corner * 3,
                                              dots[corner], &lights[light])));
        }
    }
    return 1;
}
extern "C" int wd_gouraud_packet(uint32_t kind, uint32_t culled, uint32_t flat_first,
                                 const od_local_light *lights, size_t count, uint8_t *shades,
                                 int32_t *dots, uint32_t *writes, size_t capacity) {
    if (count > 8)
        return -1;
    wd::SceneSnapshot scene;
    scene.light_transform_count = uint32_t(count);
    scene.poses.push_back({-1, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0}});
    scene.nodes.resize(1);
    auto &node = scene.nodes[0];
    node.submitted = true;
    node.light_count = uint32_t(count);
    for (uint32_t i = 0; i < count; ++i) {
        node.light_indices[i] = uint8_t(i);
        auto &source = scene.lights[i];
        source.type = lights[i].type;
        std::copy_n(lights[i].radial.position, 3, source.position.begin());
        source.inner_radius = lights[i].radial.inner_radius;
        source.outer_radius = lights[i].radial.outer_radius;
        source.intensity = lights[i].radial.intensity;
        for (unsigned axis = 0; axis < 3; ++axis)
            source.orientation[axis * 3 + 2] = lights[i].axis[axis];
    }
    std::array<wd::SceneNormal, 5> normals;
    for (unsigned i = 0; i < normals.size(); ++i) {
        normals[i].address = 0x18003000 + i * 16;
        normals[i].xyz = {0, 0, -32768 + int32_t(i) * 1000};
        normals[i].dot = dots[i];
        if (i < 4)
            node.vertex_normals.push_back(normals[i]);
    }
    for (unsigned i = 0; i < 3; ++i) {
        wd::SceneFace face;
        face.address = 0x18002000 + i * 0x80;
        face.owner = 0;
        face.type = i == 2 ? 0x18 : int32_t(kind);
        face.block = i == 2 ? 2 : 1;
        face.shade = shades[i * 4];
        std::copy_n(shades + i * 4 + 1, 3, face.corner_shades.begin());
        face.normal_address = normals[0].address;
        face.normal = normals[0].xyz;
        face.plane_distance = -10;
        const int32_t points[9] = {-1, -1, 10, 0, 1, 10, 1, -1, 10};
        for (unsigned c = 0; c < 3; ++c) {
            std::array<int32_t, 3> point;
            std::copy_n(points + c * 3, 3, point.begin());
            if (culled & (1u << i))
                point[0] += 100;
            const auto vertex = uint32_t(scene.vertices.size());
            scene.source_vertices.push_back(point);
            scene.vertices.push_back({{float(point[0]), float(point[1]), float(point[2])}});
            face.corners[c] = {0, vertex, {0, 0}};
            // Two faces share a normal; corner 2 lies outside the refreshed pool.
            face.corner_normals[c] = normals[c == 2 ? 4 : c];
        }
        scene.faces.push_back(face);
    }
    if (flat_first)
        std::rotate(scene.faces.begin(), scene.faces.begin() + 2, scene.faces.end());
    float vp[16];
    od_projection_hor_plus(1, 1, 1, 100, vp);
    vp[5] = -1;
    wd::SceneLighting result;
    std::string error;
    if (!wd::prepare_scene_lighting(scene, vp, result, error, true))
        return -1;
    size_t used = 0;
    for (const auto &write : result.writes) {
        if (write.address < 0x18002000 || write.address >= 0x18004000)
            continue;
        if (used >= capacity)
            return -1;
        writes[used * 3] = write.address;
        writes[used * 3 + 1] = write.value;
        writes[used * 3 + 2] = write.bytes;
        ++used;
        for (unsigned i = 0; i < 3; ++i)
            if (write.address >= 0x18002040 + i * 0x80 &&
                write.address <= 0x18002043 + i * 0x80)
                shades[i * 4 + write.address - (0x18002040 + i * 0x80)] = uint8_t(write.value);
        for (unsigned i = 0; i < normals.size(); ++i)
            if (write.address == normals[i].address + 12)
                dots[i] = int32_t(write.value);
    }
    return int(used);
}
extern "C" int wd_flat_lights(const int32_t *vertices, const int32_t *normal, int32_t plane,
                              const od_local_light *lights, size_t count, uint8_t *shade,
                              int32_t *dot) {
    const int result = od_flat_light_shade(vertices, normal, plane, lights, count, nullptr, shade);
    if (result && count && dot)
        *dot = od_light_normal_dot(normal, &lights[count - 1]);
    return result;
}
extern "C" int wd_light_axis(const float *world, const int32_t *orientation, int32_t *axis) {
    return od_oriented_light_axis(world, orientation, axis);
}
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
    if (!wd::prepare_scene_lighting(scene, vp, result, error, true))
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
extern "C" uint32_t wd_dos_palette_rows(const od_dos_palette_update *update, const uint8_t *source,
                                         uint16_t *bank) {
    return od_dos_palette_rows(update, source, bank);
}
