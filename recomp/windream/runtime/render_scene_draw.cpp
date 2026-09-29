#include "render_scene_draw.h"
#include <algorithm>
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
} // namespace

bool prepare_radial_lighting(const SceneSnapshot &scene, const float *vp, SceneLighting &output,
                             std::string &error, bool require_metadata) {
    error.clear();
    SceneLighting result;
    for (const auto &face : scene.faces)
        result.shades.push_back(face.shade);
    std::vector<float> world;
    for (size_t owner = 0; owner < scene.nodes.size(); ++owner) {
        const auto &node = scene.nodes[owner];
        if (!node.submitted || !node.light_count)
            continue;
        const bool textured =
            std::any_of(scene.faces.begin(), scene.faces.end(), [owner](const SceneFace &face) {
                od_face_mode mode{};
                uint32_t wrap = 0;
                return face.owner == owner && textured_mode(face.type, mode, wrap);
            });
        if (!textured)
            continue;
        if (node.light_count > 8 || scene.source_vertices.size() != scene.vertices.size() ||
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
        std::array<od_radial_light, 8> lights{};
        for (uint32_t i = 0; i < node.light_count; ++i) {
            const auto index = node.light_indices[i];
            if (index >= scene.light_transform_count) {
                error = "bound light lies beyond refreshed light prefix";
                return false;
            }
            if (index >= scene.lights.size() || scene.lights[index].type != 1) {
                error = "unsupported non-radial or inactive bound light";
                return false;
            }
            const auto &source = scene.lights[index];
            auto &local = lights[i];
            if (!od_radial_light_local(world.data() + owner * 12, source.position.data(),
                                       local.position)) {
                error = "invalid local light transform";
                return false;
            }
            local.inner_radius = source.inner_radius;
            local.outer_radius = source.outer_radius;
            local.intensity = source.intensity;
            for (uint32_t axis = 0; axis < 3; ++axis)
                result.writes.push_back({0x672764u + uint32_t(index) * 0x94u + axis * 4,
                                         uint32_t(local.position[axis]), 4, 0x47b3d0});
        }
        for (size_t i = 0; i < scene.faces.size(); ++i) {
            const auto &face = scene.faces[i];
            od_face_mode mode{};
            uint32_t wrap;
            if (face.owner != owner || !textured_mode(face.type, mode, wrap))
                continue;
            float clip[12]{};
            int32_t original[9];
            for (int corner = 0; corner < 3; ++corner) {
                const auto &c = face.corners[corner];
                if (c.node >= scene.poses.size() || c.vertex >= scene.vertices.size()) {
                    error = "invalid lighting corner";
                    return false;
                }
                std::copy_n(scene.source_vertices[c.vertex].data(), 3, original + corner * 3);
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
            if (require_metadata && !face.normal_address) {
                error = "lit face lacks captured normal address";
                return false;
            }
            if (!od_radial_flat_shade(original, face.normal.data(), face.plane_distance,
                                      lights.data(), node.light_count, &result.shades[i])) {
                error = "radial shading failed";
                return false;
            }
            for (uint32_t light = 0; light < node.light_count; ++light)
                if (face.normal_address)
                    result.writes.push_back(
                        {face.normal_address + 12,
                         uint32_t(od_radial_normal_dot(face.normal.data(), &lights[light])), 4,
                         0x47b7e0});
            result.writes.push_back({face.address + 0x40, result.shades[i], 1, 0x47b7e0});
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
    if (!prepare_radial_lighting(scene, view_projection, lighting, error,
                                 lighting_writes != nullptr))
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
            std::copy(face->corners.begin(), face->corners.end(), triangle.corners);
            triangle.texture = gpu_texture;
            triangle.colour = 0xffffffff;
            triangle.mode = mode;
            triangle.wrap_texture = wrap;
            triangle.cull_back = 1;
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
bool SceneDraw::submit_shadow(od_renderer *renderer, const SceneSnapshot &scene,
                              od_render_id target, std::string &error) {
    std::vector<od_scene_triangle> triangles;
    for (const auto &face : scene.faces) {
        if (!scene.nodes[face.owner].submitted)
            continue;
        if (face.type != 0x1b) {
            error = "shadow contains a non-mask material mode";
            return false;
        }
        od_scene_triangle t{};
        std::copy(face.corners.begin(), face.corners.end(), t.corners);
        t.colour = 0xff010101;
        t.cull_back = 1;
        triangles.push_back(t);
    }
    od_scene_packet p{};
    p.target = target;
    p.nodes = scene.poses.data();
    p.node_count = scene.poses.size();
    p.vertices = scene.vertices.data();
    p.vertex_count = scene.vertices.size();
    p.triangles = triangles.data();
    p.triangle_count = triangles.size();
    p.clear = 1;
    p.clear_colour[3] = 1;
    if (!scene.view_projection(128, 256, false, p.view_projection)) {
        error = "invalid shadow camera";
        return false;
    }
    if (!od_renderer_scene(renderer, &p)) {
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
