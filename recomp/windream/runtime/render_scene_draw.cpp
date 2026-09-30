#include "render_scene_draw.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace wd {
namespace {
uint64_t hash_pixels(const std::vector<uint32_t> &pixels) {
    uint64_t hash = 14695981039346656037ull;
    for (auto p : pixels) {
        hash ^= p;
        hash *= 1099511628211ull;
    }
    return hash;
}
bool textured_mode(int32_t type, od_face_mode &mode, uint32_t &wrap) {
    switch (type) {
    case 2:
    case 3:
        mode = OD_FACE_OPAQUE;
        wrap = 0;
        return true;
    case 9:
        mode = OD_FACE_OPAQUE;
        wrap = 1;
        return true;
    case 0x16:
    case 0x17:
    case 0x18:
        mode = OD_FACE_OPAQUE;
        wrap = 0;
        return true;
    case -5:
    case -6:
        mode = OD_FACE_CHROMA;
        wrap = 1;
        return true;
    case -7:
    case -4:
    case -3:
        mode = OD_FACE_TRANSLUCENT;
        wrap = 1;
        return true;
    default:
        return false;
    }
}
bool environment_type(int32_t type) {
    return (type >= -15 && type <= -3) || type == 2 || type == 3 || type == 6 || type == 9 ||
           type == 0xb || type == 0xc || type == 0x12 || (type >= 0x14 && type <= 0x1a) ||
           (type >= 0x1c && type <= 0x1e);
}
// Compatibility UV feedback uses retail's integer camera chain. Geometry
// still uses renderer-composed floating poses and the separate visual camera.
} // namespace
bool compose_feedback_rotations(const SceneSnapshot &scene, std::vector<int32_t> &result) {
    if (scene.nodes.size() != scene.poses.size())
        return false;
    std::array<int32_t, 9> camera;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
            camera[r * 3 + c] = scene.camera.local_rotation[c * 3 + r];
    result.resize(scene.nodes.size() * 9);
    std::vector<uint8_t> done(scene.nodes.size());
    std::vector<size_t> path;
    for (size_t start = 0; start < scene.nodes.size(); ++start) {
        size_t node = start;
        while (done[node] != 2) {
            if (done[node] == 1)
                return false;
            done[node] = 1;
            path.push_back(node);
            const int32_t parent = scene.poses[node].parent;
            if (parent == -1)
                break;
            if (parent < 0 || size_t(parent) >= scene.nodes.size())
                return false;
            node = size_t(parent);
        }
        while (!path.empty()) {
            node = path.back();
            path.pop_back();
            int32_t *output = result.data() + node * 9;
            if (scene.poses[node].parent == -1 && scene.nodes[node].address == scene.camera.address)
                std::copy(camera.begin(), camera.end(), output);
            else {
                const int32_t *parent = scene.poses[node].parent == -1
                                            ? camera.data()
                                            : result.data() + size_t(scene.poses[node].parent) * 9;
                const auto &local = scene.nodes[node].source_rotation;
                for (unsigned r = 0; r < 3; ++r)
                    for (unsigned c = 0; c < 3; ++c) {
                        uint32_t sum = 0;
                        for (unsigned k = 0; k < 3; ++k)
                            sum += uint32_t(parent[r * 3 + k]) * uint32_t(local[k * 3 + c]);
                        int32_t value;
                        std::memcpy(&value, &sum, 4);
                        output[r * 3 + c] = value >> 15;
                    }
            }
            done[node] = 2;
        }
    }
    return true;
}

bool prepare_scene_lighting(const SceneSnapshot &scene, const float *vp, SceneLighting &output,
                            std::string &error, bool require_metadata) {
    error.clear();
    SceneLighting result;
    std::vector<std::vector<size_t>> owner_faces(scene.nodes.size());
    for (size_t i = 0; i < scene.faces.size(); ++i) {
        if (scene.faces[i].owner >= owner_faces.size()) {
            error = "invalid scene face owner";
            return false;
        }
        owner_faces[scene.faces[i].owner].push_back(i);
    }
    const bool has_environment =
        std::any_of(scene.nodes.begin(), scene.nodes.end(), [](const auto &n) {
            return (n.submitted || n.visual_active) && (n.flags & 0x800);
        });
    const bool has_gouraud_lighting =
        std::any_of(scene.faces.begin(), scene.faces.end(), [&](const auto &f) {
            const auto &n = scene.nodes[f.owner];
            return (f.type == 0x16 || f.type == 0x17) && n.light_count &&
                   (n.submitted || n.visual_active);
        });
    std::unordered_map<uint32_t, std::array<int32_t, 2>> uv_versions;
    std::unordered_map<uint32_t, int32_t> normal_dots;
    if (has_gouraud_lighting)
        for (const auto &node : scene.nodes)
            for (const auto &normal : node.vertex_normals)
                normal_dots[normal.address] = normal.dot;
    for (const auto &face : scene.faces) {
        result.shades.push_back(face.shade);
        result.corner_shades.push_back(face.corner_shades);
        result.corners.push_back(face.corners);
        for (unsigned c = 0; has_environment && c < 3; ++c)
            if (face.uv_addresses[c] && (scene.nodes[face.owner].flags & 0x800) &&
                (scene.nodes[face.owner].submitted || scene.nodes[face.owner].visual_active) &&
                environment_type(face.type))
                uv_versions[face.uv_addresses[c]] = face.source_uvs[c];
        for (const auto &normal : face.corner_normals)
            if (has_gouraud_lighting && normal.address)
                normal_dots[normal.address] = normal.dot;
    }
    std::vector<float> world;
    std::vector<int32_t> env_rotations;
    for (size_t owner = 0; owner < scene.nodes.size(); ++owner) {
        const auto &node = scene.nodes[owner];
        if (!node.submitted && !node.visual_active)
            continue;
        if (has_environment)
            for (size_t i : owner_faces[owner])
                for (unsigned c = 0; c < 3; ++c)
                    if (scene.faces[i].uv_addresses[c]) {
                        const auto found = uv_versions.find(scene.faces[i].uv_addresses[c]);
                        if (found == uv_versions.end())
                            continue;
                        const auto &uv = found->second;
                        for (unsigned axis = 0; axis < 2; ++axis)
                            result.corners[i][c].uv[axis] = float(uv[axis]) / (65536.0f * 256);
                    }
        const bool textured =
            std::any_of(owner_faces[owner].begin(), owner_faces[owner].end(), [&](size_t i) {
                od_face_mode mode{};
                uint32_t wrap = 0;
                return textured_mode(scene.faces[i].type, mode, wrap);
            });
        const bool lighting = textured && node.light_count;
        const bool environment = (node.flags & 0x800) != 0;
        if (!lighting && !environment)
            continue;
        if (node.light_count > 8 ||
            (lighting && scene.source_vertices.size() != scene.vertices.size()) ||
            scene.poses.size() != scene.nodes.size()) {
            error = "lit scene lacks exact vertices or valid light bindings";
            return false;
        }
        if (world.empty()) {
            world.resize(scene.poses.size() * 12);
            if (!od_compose_pose(scene.poses.data(), scene.poses.size(), world.data())) {
                error = "invalid lighting hierarchy";
                return false;
            }
        }
        std::array<od_local_light, 8> lights{};
        for (uint32_t i = 0; lighting && i < node.light_count; ++i) {
            const auto index = node.light_indices[i];
            if (index >= scene.light_transform_count) {
                error = "bound light lies beyond refreshed light prefix";
                return false;
            }
            if (index >= scene.lights.size() ||
                (scene.lights[index].type != 1 && scene.lights[index].type != 2)) {
                error = "unsupported or inactive bound light";
                return false;
            }
            const auto &source = scene.lights[index];
            auto &local = lights[i].radial;
            lights[i].type = source.type;
            if (!od_radial_light_local(world.data() + owner * 12, source.position.data(),
                                       local.position)) {
                error = "invalid local light transform";
                return false;
            }
            local.inner_radius = source.inner_radius;
            local.outer_radius = source.outer_radius;
            local.intensity = source.intensity;
            if (source.type == 2) {
                if (!od_oriented_light_axis(world.data() + owner * 12, source.orientation.data(),
                                            lights[i].axis)) {
                    error = "invalid local light orientation";
                    return false;
                }
                for (uint32_t axis = 0; axis < 3; ++axis)
                    result.writes.push_back({0x672770u + uint32_t(index) * 0x94u + axis * 4,
                                             uint32_t(lights[i].axis[axis]), 4, 0x47b3d0});
            }
            for (uint32_t axis = 0; axis < 3; ++axis)
                result.writes.push_back({0x672764u + uint32_t(index) * 0x94u + axis * 4,
                                         uint32_t(local.position[axis]), 4, 0x47b3d0});
        }
        std::vector<uint8_t> visible_faces(scene.faces.size());
        std::vector<uint32_t> block_order;
        std::unordered_map<uint32_t, std::vector<size_t>> blocks;
        for (size_t i : owner_faces[owner]) {
            const auto &face = scene.faces[i];
            od_face_mode mode{};
            uint32_t wrap;
            if (!textured_mode(face.type, mode, wrap) &&
                !(environment && environment_type(face.type)))
                continue;
            auto &block = blocks[face.block];
            if (block.empty())
                block_order.push_back(face.block);
            block.push_back(i);
            float clip[12]{};
            for (int corner = 0; corner < 3; ++corner) {
                const auto &c = face.corners[corner];
                if (c.node >= scene.poses.size() || c.vertex >= scene.vertices.size()) {
                    error = "invalid lighting corner";
                    return false;
                }
                const float *m = world.data() + c.node * 12;
                const float *v = scene.vertices[c.vertex].xyz;
                float p[4] = {0, 0, 0, 1};
                for (int a = 0; a < 3; ++a)
                    p[a] =
                        m[a * 4] * v[0] + m[a * 4 + 1] * v[1] + m[a * 4 + 2] * v[2] + m[a * 4 + 3];
                for (int row = 0; row < 4; ++row)
                    for (int k = 0; k < 4; ++k)
                        clip[corner * 4 + row] += vp[k * 4 + row] * p[k];
            }
            const int visible = od_triangle_visible(clip, 1);
            if (visible < 0) {
                error = "invalid lit geometry";
                return false;
            }
            if (!visible)
                continue; // Preserve the source byte, including culled list heads.
            visible_faces[i] = 1;
        }
        for (auto block : block_order) {
            if (!lighting)
                break;
            const auto &indices = blocks[block];
            const auto type = scene.faces[indices.front()].type;
            od_face_mode shade_mode{};
            uint32_t shade_wrap = 0;
            if (!textured_mode(type, shade_mode, shade_wrap))
                continue;
            // Retail 0x18 uses flat shading; only 0x16/0x17 use this corner branch.
            if (type == 0x16 || type == 0x17) {
                for (auto i : indices)
                    if (visible_faces[i]) {
                        result.corner_shades[i].fill(0);
                        for (unsigned c = 0; c < 3; ++c)
                            result.writes.push_back(
                                {scene.faces[i].address + 0x41 + c, 0, 1, 0x47b7e0});
                    }
                for (uint32_t light = 0; light < node.light_count; ++light) {
                    // Refresh the whole owner pool, including unreferenced normals.
                    // Corner pointers outside it retain their current scratch value.
                    for (const auto &normal : node.vertex_normals) {
                        const int32_t dot = od_light_normal_dot(normal.xyz.data(), &lights[light]);
                        normal_dots[normal.address] = dot;
                        result.writes.push_back({normal.address + 12, uint32_t(dot), 4, 0x47b7e0});
                    }
                    for (auto i : indices) {
                        if (!visible_faces[i])
                            continue;
                        const auto &face = scene.faces[i];
                        for (unsigned c = 0; c < 3; ++c) {
                            const auto &normal = face.corner_normals[c];
                            if (!normal.address) {
                                error = "lit Gouraud lacks captured corner normals";
                                return false;
                            }
                            const int32_t contribution = od_gouraud_light_contribution(
                                scene.source_vertices[face.corners[c].vertex].data(),
                                normal.xyz.data(), normal_dots.at(normal.address), &lights[light]);
                            auto &shade = result.corner_shades[i][c];
                            shade = uint8_t(uint32_t(shade) + uint32_t(contribution));
                            result.writes.push_back({face.address + 0x41 + c, shade, 1, 0x47b7e0});
                        }
                    }
                }
                continue;
            }
            for (auto i : indices) {
                if (!visible_faces[i])
                    continue;
                const auto &face = scene.faces[i];
                if (require_metadata && !face.normal_address) {
                    error = "lit face lacks captured normal address";
                    return false;
                }
                int32_t original[9];
                for (unsigned corner = 0; corner < 3; ++corner)
                    std::copy_n(scene.source_vertices[face.corners[corner].vertex].data(), 3,
                                original + corner * 3);
                if (!od_flat_light_shade(original, face.normal.data(), face.plane_distance,
                                         lights.data(), node.light_count, &result.shades[i])) {
                    error = "flat lighting failed";
                    return false;
                }
                for (uint32_t light = 0; light < node.light_count; ++light)
                    if (face.normal_address) {
                        const auto dot = od_light_normal_dot(face.normal.data(), &lights[light]);
                        if (has_gouraud_lighting)
                            normal_dots[face.normal_address] = dot;
                        result.writes.push_back(
                            {face.normal_address + 12, uint32_t(dot), 4, 0x47b7e0});
                    }
                result.writes.push_back({face.address + 0x40, result.shades[i], 1, 0x47b7e0});
            }
        }
        if (environment) {
            if (env_rotations.empty() && !compose_feedback_rotations(scene, env_rotations)) {
                error = "invalid environment feedback hierarchy";
                return false;
            }
            const int32_t parent = scene.poses[owner].parent;
            if (parent < 0) {
                error = "environment owner lacks its retail parent";
                return false;
            }
            for (auto block : block_order)
                for (auto i : blocks[block]) {
                    const auto &face = scene.faces[i];
                    if (!visible_faces[i] || !environment_type(face.type))
                        continue;
                    for (unsigned c = 0; c < 3; ++c) {
                        if (!face.uv_addresses[c] || !face.corner_normals[c].address) {
                            error = "environment face lacks UV or normal identity";
                            return false;
                        }
                        auto &uv = uv_versions.at(face.uv_addresses[c]);
                        if (!od_environment_uv(env_rotations.data() + size_t(parent) * 9,
                                               face.corner_normals[c].xyz.data(), c, uv.data())) {
                            error = "environment UV calculation failed";
                            return false;
                        }
                        for (unsigned axis = 0; axis < 2; ++axis)
                            result.writes.push_back(
                                {face.uv_addresses[c] + axis * 4, uint32_t(uv[axis]), 4, 0x47e094});
                    }
                }
        }
    }
    // The Glide hook queues translucent BLOCK pointers; their UVs are read by
    // the deferred pass after every object's post-draw environment update.
    for (size_t i = 0; has_environment && i < scene.faces.size(); ++i) {
        od_face_mode mode{};
        uint32_t wrap = 0;
        if (!textured_mode(scene.faces[i].type, mode, wrap) || mode != OD_FACE_TRANSLUCENT)
            continue;
        for (unsigned c = 0; c < 3; ++c)
            if (scene.faces[i].uv_addresses[c]) {
                const auto found = uv_versions.find(scene.faces[i].uv_addresses[c]);
                if (found == uv_versions.end())
                    continue;
                const auto &uv = found->second;
                for (unsigned axis = 0; axis < 2; ++axis)
                    result.corners[i][c].uv[axis] = float(uv[axis]) / (65536.0f * 256);
            }
    }
    output = std::move(result);
    return true;
}

od_render_id SceneDraw::texture(od_renderer *renderer, const SceneMaterial &material,
                                std::string &error) {
    if (material.gpu_mask) {
        for (auto &entry : textures_)
            if (entry.mask == material.gpu_mask && entry.mask_version == material.gpu_version &&
                entry.palette == palette_) {
                entry.last_frame = frame_;
                return entry.texture;
            }
        auto id = od_renderer_resolve_shadow(renderer, material.gpu_mask, palette_.data());
        if (!id) {
            error = od_renderer_error(renderer);
            return 0;
        }
        TextureVersion entry;
        entry.texture = id;
        entry.mask = material.gpu_mask;
        entry.mask_version = material.gpu_version;
        entry.last_frame = frame_;
        entry.palette = palette_;
        textures_.push_back(std::move(entry));
        return id;
    }
    if (material.indices.size() != 65536) {
        error = "material has no complete source page";
        return 0;
    }
    for (auto &entry : textures_)
        if (!entry.mask && entry.palette == palette_ && entry.indices == material.indices) {
            entry.last_frame = frame_;
            return entry.texture;
        }
    std::vector<uint32_t> rgba(128 * 128);
    if (!od_expand_material_page(material.indices.data(), 256, palette_.data(), rgba.data(), 128)) {
        error = "material palette expansion failed";
        return 0;
    }
    const uint64_t hash = hash_pixels(rgba);
    for (auto &entry : textures_)
        if (entry.hash == hash && entry.rgba == rgba) {
            entry.last_frame = frame_;
            return entry.texture;
        }
    auto id = od_renderer_upload_rgba(renderer, 128, 128, rgba.data(), 128 * 4);
    if (!id) {
        error = od_renderer_error(renderer);
        return 0;
    }
    TextureVersion entry;
    entry.hash = hash;
    entry.last_frame = frame_;
    entry.rgba = std::move(rgba);
    entry.indices = material.indices;
    entry.palette = palette_;
    entry.texture = id;
    textures_.push_back(std::move(entry));
    return id;
}

bool SceneDraw::submit(od_renderer *renderer, const SceneSnapshot &scene, od_render_id target,
                       int width, int height, bool hor_plus, std::string &error,
                       std::vector<SceneLightingWrite> *lighting_writes) {
    error.clear();
    float view_projection[16];
    if (!scene.view_projection(width, height, hor_plus, view_projection)) {
        error = "invalid scene camera";
        return false;
    }
    std::vector<float> world;
    SceneLighting lighting;
    if (!prepare_scene_lighting(scene, view_projection, lighting, error, lighting_writes != nullptr))
        return false;
    std::vector<od_scene_triangle> opaque, deferred;
    std::unordered_map<uint32_t, std::vector<const SceneFace *>> blocks;
    std::vector<uint32_t> order, transparent_order;
    for (const auto &face : scene.faces) {
        if (face.owner >= scene.nodes.size()) {
            error = "invalid face owner";
            return false;
        }
        if (!scene.nodes[face.owner].submitted)
            continue;
        if (!face.block) {
            error = "material draw requires a WDS2 capture";
            return false;
        }
        auto &list = blocks[face.block];
        if (list.empty()) {
            const bool translucent = face.type == -7 || face.type == -4 || face.type == -3;
            (translucent ? transparent_order : order).push_back(face.block);
        }
        list.push_back(&face);
    }
    order.insert(order.end(), transparent_order.begin(), transparent_order.end());
    for (auto block : order) {
        const auto &faces = blocks[block];
        const auto &first = *faces.front();
        if (first.type == 1) {
            if (world.empty()) {
                world.resize(scene.poses.size() * 12);
                if (!od_compose_pose(scene.poses.data(), scene.poses.size(), world.data())) {
                    error = "invalid diagnostic hierarchy";
                    return false;
                }
            }
            uint32_t colour = 0;
            for (const auto *face : faces) {
                float clip[12]{};
                for (int c = 0; c < 3; ++c) {
                    const auto &corner = face->corners[c];
                    if (corner.node >= scene.poses.size() ||
                        corner.vertex >= scene.vertices.size()) {
                        error = "invalid diagnostic corner";
                        return false;
                    }
                    const float *m = world.data() + corner.node * 12;
                    const float *v = scene.vertices[corner.vertex].xyz;
                    float p[4] = {0, 0, 0, 1};
                    for (int a = 0; a < 3; ++a)
                        p[a] = m[a * 4] * v[0] + m[a * 4 + 1] * v[1] + m[a * 4 + 2] * v[2] +
                               m[a * 4 + 3];
                    for (int r = 0; r < 4; ++r)
                        for (int k = 0; k < 4; ++k)
                            clip[c * 4 + r] += view_projection[k * 4 + r] * p[k];
                }
                const int visible = od_triangle_visible(clip, 1);
                if (visible < 0) {
                    error = "invalid diagnostic geometry";
                    return false;
                }
                if (!visible)
                    continue;
                od_scene_triangle triangle{};
                std::copy(face->corners.begin(), face->corners.end(), triangle.corners);
                // Glide's constant colour uses ARGB; the shared API uses ABGR.
                triangle.colour = 0xff000000u | ((colour >> 16) & 255u) | (colour & 0xff00u) |
                                  ((colour & 255u) << 16);
                triangle.cull_back = 1;
                opaque.push_back(triangle);
                colour += 0x24bf9;
            }
            continue;
        }
        od_face_mode mode{};
        uint32_t wrap = 0;
        if (!textured_mode(first.type, mode, wrap)) {
            error = "unimplemented scene face type " + std::to_string(first.type);
            return false;
        }
        if (first.material >= scene.materials.size()) {
            error = "unbound scene material";
            return false;
        }
        const auto &material = scene.materials[first.material];
        const uint32_t logical_row = mode == OD_FACE_TRANSLUCENT ? 15
                                     : scene.nodes[first.owner].light_count
                                         ? lighting.shades[size_t(&first - scene.faces.data())]
                                         : std::min(scene.nodes[first.owner].shade, 31u);
        if (logical_row > 31) {
            error = "lit palette row lies outside the captured bank";
            return false;
        }
        // Keep lifted Windows palette generation. Its shade r uses physical
        // row 31-r (neutral 16); the shader receives the resulting colours.
        // Retain the palette snapshot until the PAGE changes, not the row.
        if (material.page != palette_page_) {
            palette_page_ = material.page;
            std::copy_n(material.palette.data() + (31 - logical_row) * 256, 256, palette_.data());
        }
        const auto gpu_texture = texture(renderer, material, error);
        if (!gpu_texture)
            return false;
        for (const auto *face : faces) {
            od_scene_triangle triangle{};
            const auto &corners = lighting.corners[size_t(face - scene.faces.data())];
            std::copy(corners.begin(), corners.end(), triangle.corners);
            triangle.texture = gpu_texture;
            triangle.colour = 0xffffffff;
            triangle.mode = mode;
            triangle.wrap_texture = wrap;
            triangle.cull_back = 1;
            if (face->type >= 0x16 && face->type <= 0x18) {
                triangle.use_corner_brightness = 1;
                for (size_t corner = 0; corner < 3; ++corner)
                    triangle.corner_brightness[corner] = uint8_t(std::min(
                        unsigned(
                            lighting.corner_shades[size_t(face - scene.faces.data())][corner]) *
                            8u,
                        255u));
            }
            (mode == OD_FACE_TRANSLUCENT ? deferred : opaque).push_back(triangle);
        }
    }
    opaque.insert(opaque.end(), deferred.begin(), deferred.end());
    od_scene_packet packet{};
    packet.target = target;
    packet.fog = scene.fog;
    packet.clear = 1;
    packet.clear_colour[3] = 1;
    packet.nodes = scene.poses.data();
    packet.node_count = scene.poses.size();
    packet.vertices = scene.vertices.data();
    packet.vertex_count = scene.vertices.size();
    packet.triangles = opaque.data();
    packet.triangle_count = opaque.size();
    std::copy_n(view_projection, 16, packet.view_projection);
    if (!od_renderer_scene(renderer, &packet)) {
        error = od_renderer_error(renderer);
        return false;
    }
    if (lighting_writes)
        *lighting_writes = std::move(lighting.writes);
    return true;
}
void SceneDraw::finish_frame(od_renderer *renderer) {
    auto it = textures_.begin();
    while (it != textures_.end()) {
        if (it->last_frame != frame_) {
            od_renderer_release(renderer, it->texture);
            it = textures_.erase(it);
        } else
            ++it;
    }
    ++frame_;
}
bool prepare_shadow_input(const SceneSnapshot &scene, od_render_id target,
                          ShadowInput &input, std::string &error) {
    if(scene.source_vertices.size()!=scene.vertices.size() || scene.poses.size()!=scene.nodes.size()) {
        error="shadow lacks original integer vertices";return false;
    }
    auto &nodes=input.nodes;auto &vertices=input.vertices;auto &triangles=input.triangles;
    nodes.resize(scene.nodes.size());
    vertices.resize(scene.vertices.size());
    triangles.clear();
    std::vector<uint8_t> mapped(vertices.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].parent = scene.poses[i].parent;
        std::copy(scene.nodes[i].source_rotation.begin(), scene.nodes[i].source_rotation.end(),
                  nodes[i].rotation);
        std::copy(scene.nodes[i].source_position.begin(), scene.nodes[i].source_position.end(),
                  nodes[i].translation);
        const uint64_t end = uint64_t(scene.nodes[i].first_vertex) + scene.nodes[i].vertex_count;
        if (end > vertices.size()) {
            error = "invalid shadow vertex range";
            return false;
        }
        for (uint32_t j = scene.nodes[i].first_vertex; j < end; ++j) {
            if (mapped[j]) {
                error = "ambiguous shadow vertex owner";
                return false;
            }
            mapped[j] = 1;
            vertices[j].node = uint32_t(i);
            std::copy(scene.source_vertices[j].begin(), scene.source_vertices[j].end(),
                      vertices[j].xyz);
        }
    }
    if (std::find(mapped.begin(), mapped.end(), 0) != mapped.end()) {
        error = "unowned shadow source vertex";
        return false;
    }
    for (const auto &face : scene.faces) {
        if (face.owner >= scene.nodes.size()) {
            error = "invalid shadow face owner";
            return false;
        }
        if (!scene.nodes[face.owner].submitted)
            continue;
        if (face.type != 0x1b) {
            error = "shadow contains a non-mask material mode";
            return false;
        }
        od_shadow_triangle t{};
        for (unsigned c = 0; c < 3; ++c)
            t.vertices[c] = face.corners[c].vertex;
        t.owner = face.owner;
        t.flags = face.flags;
        t.plane = face.plane_distance;
        std::copy(face.normal.begin(), face.normal.end(), t.normal);
        t.retained_dot = face.source_normal_dot;
        const auto &owner = scene.nodes[face.owner];
        t.stale_normal =
            face.normal_address < owner.face_normal_base ||
            uint64_t(face.normal_address) >=
                uint64_t(owner.face_normal_base) + uint64_t(owner.face_normal_count) * 16 ||
            ((face.normal_address - owner.face_normal_base) & 15);
        triangles.push_back(t);
    }
    auto &p = input.packet;
    p = {};
    p.target = target;
    p.nodes = nodes.data();
    p.node_count = nodes.size();
    p.vertices = vertices.data();
    p.vertex_count = vertices.size();
    p.triangles = triangles.data();
    p.triangle_count = triangles.size();
    std::copy(scene.camera.local_rotation.begin(), scene.camera.local_rotation.end(),
              p.camera_rotation);
    std::copy(scene.camera.eye.begin(), scene.camera.eye.end(), p.camera_eye);
    p.focal[0] = scene.camera.focal_x;
    p.focal[1] = scene.camera.focal_y;
    p.center[0] = scene.camera.center_x;
    p.center[1] = scene.camera.center_y;
    p.near_plane = scene.camera.near_plane;
    p.far_plane = scene.camera.far_plane;
    return true;
}
bool SceneDraw::submit_shadow(od_renderer *renderer, const SceneSnapshot &scene,
                              od_render_id target, std::string &error) {
    ShadowInput input;
    if (!prepare_shadow_input(scene, target, input, error))
        return false;
    if (!od_renderer_shadow_mask(renderer, &input.packet)) {
        error = od_renderer_error(renderer);
        return false;
    }
    return true;
}
void SceneDraw::reset(od_renderer *renderer) {
    for (const auto &entry : textures_)
        od_renderer_release(renderer, entry.texture);
    textures_.clear();
    palette_page_ = 0;
    palette_.fill(0);
}
} // namespace wd
