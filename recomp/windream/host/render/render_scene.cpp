#include "render_scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace wd {
namespace {
constexpr size_t max_nodes = 10000, max_vertices = 1000000, max_faces = 1000000;
struct Read {
    SceneReader source;
    std::string &error;
    bool bytes(uint32_t address, void *destination, size_t size) {
        if (uint64_t(address) + size > 0x100000000ull || !source.read ||
            !source.read(source.context, address, destination, size)) {
            char message[100];
            std::snprintf(message, sizeof message, "unmapped scene input %08x + %zu", address,
                          size);
            error = message;
            return false;
        }
        return true;
    }
};
uint32_t word(const uint8_t *p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int32_t signed_word(const uint8_t *p) {
    const uint32_t value = word(p);
    int32_t result;
    std::memcpy(&result, &value, sizeof result);
    return result;
}
float float_word(const uint8_t *p) {
    const uint32_t value = word(p);
    float result;
    std::memcpy(&result, &value, sizeof result);
    return result;
}
bool flat(int32_t type) {
    return type == -2 || type == 1 || type == 4 || type == 0x11 || type == 0x1b;
}
bool fail(std::string &error, const char *message) {
    error = message;
    return false;
}
} // namespace

bool SceneSnapshot::view_projection(int width, int height, bool hor_plus, float out[16]) const {
    if (!out || width <= 0 || height <= 0 || !camera.screen_width || !camera.screen_height)
        return false;
    float projection[16]{};
    float fx = 2 * camera.focal_x / float(camera.screen_width);
    const float fy = 2 * camera.focal_y / float(camera.screen_height);
    if (hor_plus)
        fx *= (float(camera.screen_width) / float(camera.screen_height)) / (float(width) / height);
    if (!std::isfinite(fx) || fx <= 0)
        return false;
    if (!od_projection_hor_plus(fy, 1, float(camera.near_plane), float(camera.far_plane),
                                projection))
        return false;
    projection[0] = fx;
    projection[5] = -fy; // original camera-space Y increases down the screen
    projection[8] = 2 * float(camera.center_x) / float(camera.screen_width) - 1;
    projection[9] = 1 - 2 * float(camera.center_y) / float(camera.screen_height);
    // The view inverse is row-major affine. Projection/output are column-major.
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float value = col == 3 ? projection[12 + row] : 0;
            for (int k = 0; k < 3; ++k)
                value += projection[k * 4 + row] * camera.view[k * 4 + col];
            if (!std::isfinite(value))
                return false;
            out[col * 4 + row] = value;
        }
    }
    return true;
}

bool capture_scene(SceneReader source, uint32_t root, SceneSnapshot &output, std::string &error) {
    error.clear();
    if (!root)
        return fail(error, "render root is null");
    SceneSnapshot result;
    std::array<bool, 100> needed_lights{};
    Read read{source, error};
    uint8_t light_count_bytes[4];
    if (!read.bytes(0x4ac758, light_count_bytes, 4))
        return false;
    result.light_transform_count = word(light_count_bytes);
    if (result.light_transform_count > 100)
        return fail(error, "invalid light transform count");
    uint8_t camera[0xd4], viewport[0x4c];
    if (!read.bytes(root, camera, sizeof camera) ||
        !read.bytes(0x661e90, viewport, sizeof viewport))
        return false;
    if (!(word(camera + 0x0c) & 0x400))
        return fail(error, "render root is not a verified camera node");
    auto &c = result.camera;
    c.address = root;
    for (int i = 0; i < 3; ++i)
        c.eye[i] = signed_word(camera + 0x1c + i * 4);
    for (int i = 0; i < 9; ++i)
        c.local_rotation[i] = signed_word(camera + 0x28 + i * 4);
    for (int row = 0; row < 3; ++row) {
        float translation = 0;
        for (int col = 0; col < 3; ++col) {
            const float value = float(c.local_rotation[col * 3 + row]) / 32768;
            c.view[row * 4 + col] = value;
            translation -= value * float(c.eye[col]);
        }
        c.view[row * 4 + 3] = translation;
    }
    c.focal_y = float_word(viewport);
    c.focal_x = float_word(viewport + 4);
    c.center_y = signed_word(viewport + 8);
    c.center_x = signed_word(viewport + 12);
    c.viewport_y = word(viewport + 0x18);
    c.viewport_x = word(viewport + 0x1c);
    c.far_plane = signed_word(viewport + 0x20);
    c.screen_width = word(viewport + 0x2c);
    c.viewport_height = word(viewport + 0x30);
    c.screen_height = word(viewport + 0x38);
    c.near_plane = signed_word(viewport + 0x3c);
    c.viewport_width = word(viewport + 0x48);
    if (!c.screen_width || !c.screen_height || c.screen_width > 16384 || c.screen_height > 16384 ||
        !std::isfinite(c.focal_x) || !std::isfinite(c.focal_y) || c.focal_x <= 0 ||
        c.focal_y <= 0 || c.near_plane <= 0 || c.far_plane <= c.near_plane)
        return fail(error, "invalid retail camera projection");
    struct Pending {
        uint32_t address, parent;
        bool parent_visible;
    };
    std::vector<Pending> pending{{root, 0, true}};
    std::unordered_map<uint32_t, uint32_t> node_indices;
    std::vector<uint32_t> face_blocks;
    size_t normal_count = 0;
    auto capture_normal = [&](uint32_t address, SceneNormal &normal) {
        normal.address = address;
        if (!address)
            return true;
        uint8_t record[16];
        if (!read.bytes(address, record, sizeof record))
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
            normal.xyz[axis] = signed_word(record + axis * 4);
        normal.dot = signed_word(record + 12);
        return true;
    };
    while (!pending.empty()) {
        auto entry = pending.back();
        pending.pop_back();
        if (!entry.address)
            continue;
        if (result.nodes.size() >= max_nodes || node_indices.count(entry.address))
            return fail(error, "cyclic, duplicate or oversized node tree");
        // Capture the consumed retail prefix only. The demo's 0xdc-byte
        // object adds virtual-vertex fields at +d4/+d8; both are zero in
        // our retail captures and its relocator does not relocate +d8.
        // See debug-renderer-contracts.md; do not infer an extra vertex pool.
        uint8_t node[0xd4];
        if (!read.bytes(entry.address, node, sizeof node))
            return false;
        const uint32_t flags = word(node + 12), parent = word(node + 16);
        if (entry.address != root && parent != entry.parent)
            return fail(error, "hierarchy parent does not match child/sibling ownership");
        const bool visible = entry.address == root || (entry.parent_visible && !(flags & 1));
        const uint32_t index = uint32_t(result.nodes.size());
        node_indices.emplace(entry.address, index);
        od_pose_node pose{};
        pose.parent = -1;
        if (entry.address == root) {
            pose.local[0] = pose.local[5] = pose.local[10] = 1;
        } else {
            auto found = node_indices.find(parent);
            if (found == node_indices.end())
                return fail(error, "unresolved parent node");
            pose.parent = int32_t(found->second);
            for (int r = 0; r < 3; ++r) {
                for (int col = 0; col < 3; ++col)
                    pose.local[r * 4 + col] =
                        float(signed_word(node + 0x28 + (r * 3 + col) * 4)) / 32768;
                pose.local[r * 4 + 3] = float(signed_word(node + 0x1c + r * 4));
            }
        }
        const uint32_t count = word(node + 0x7c), start = word(node + 0x80);
        if (count > max_vertices - result.vertices.size() || (count && word(node + 0xc0) != 40))
            return fail(error, "invalid original vertex count/stride");
        SceneNode info{entry.address, flags & ~uint32_t(2 | 8 | 0x20 | 0x40),
                       uint32_t(result.vertices.size()), count,
                       entry.address != root && visible && !(flags & (4 | 0x1000))};
        info.visual_active = entry.address != root && visible && !(flags & 4);
        for (unsigned i = 0; i < 9; ++i)
            info.source_rotation[i] = signed_word(node + 0x28 + i * 4);
        for (unsigned i = 0; i < 3; ++i)
            info.source_position[i] = signed_word(node + 0x1c + i * 4);
        info.face_normal_count = word(node + 0x94);
        info.face_normal_base = word(node + 0x98);
        info.light_count = word(node + 0xc4);
        if (info.light_count > 8)
            return fail(error, "invalid node light count");
        std::copy_n(node + 0xc8, 8, info.light_indices.begin());
        for (uint32_t i = 0; i < info.light_count; ++i)
            if (info.light_indices[i] >= 100)
                return fail(error, "invalid node light index");
            else if (info.visual_active)
                needed_lights[info.light_indices[i]] = true;
        info.shade = word(node + 0xd0);
        const uint32_t normals = word(node + 0x8c), normal_base = word(node + 0x90);
        if (normals > max_vertices - normal_count || (normals && !normal_base))
            return fail(error, "invalid original vertex normal pool");
        normal_count += normals;
        info.vertex_normals.resize(normals);
        for (uint32_t i = 0; i < normals; ++i) {
            const uint64_t address = uint64_t(normal_base) + uint64_t(i) * 16;
            if (address + 16 > 0x100000000ull)
                return fail(error, "vertex normal range overflow");
            if (!capture_normal(uint32_t(address), info.vertex_normals[i]))
                return false;
        }
        result.nodes.push_back(info);
        result.poses.push_back(pose);
        face_blocks.push_back(word(node + 0xa4));
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t address = uint64_t(start) + i * 40ull;
            if (address + 40 > 0x100000000ull)
                return fail(error, "vertex range overflow");
            uint8_t vertex[16];
            if (!read.bytes(uint32_t(address), vertex, sizeof vertex))
                return false;
            od_scene_vertex v{};
            std::array<int32_t, 3> source_vertex{};
            for (int axis = 0; axis < 3; ++axis)
                v.xyz[axis] = float(source_vertex[axis] = signed_word(vertex + 4 + axis * 4));
            result.vertices.push_back(v);
            result.source_vertices.push_back(source_vertex);
            result.vertex_addresses.push_back(uint32_t(address));
        }
        // Pre-order matches the hierarchy's child, sibling, parent walk. Hidden
        // nodes remain available to resolve corners but cannot submit faces.
        if (entry.address != root && word(node + 24))
            pending.push_back({word(node + 24), entry.parent, entry.parent_visible});
        if (word(node + 20))
            pending.push_back({word(node + 20), entry.address, visible});
    }
    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> vertices;
    for (uint32_t n = 0; n < result.nodes.size(); ++n) {
        const auto &node = result.nodes[n];
        for (uint32_t i = node.first_vertex; i < node.first_vertex + node.vertex_count; ++i)
            if (!vertices.emplace(result.vertex_addresses[i], std::make_pair(n, i)).second)
                return fail(error, "ambiguous source vertex owner");
    }
    for (uint32_t n = 0; n < result.nodes.size(); ++n) {
        std::unordered_set<uint32_t> seen;
        for (uint32_t block = face_blocks[n]; block;) {
            if (!seen.insert(block).second || seen.size() > 10000)
                return fail(error, "cyclic face blocks");
            uint8_t b[0x34];
            if (!read.bytes(block, b, sizeof b))
                return false;
            const int32_t type = signed_word(b + 4);
            const uint32_t count = word(b + 0x1c), start = word(b + 0x20), stride = word(b + 0x2c);
            if ((stride != 56 && stride != 68) || count > max_faces - result.faces.size() ||
                (count && type != 0x1b && flat(type) != (stride == 56)))
                return fail(error, "invalid face count/type/stride");
            for (uint32_t i = 0; i < count; ++i) {
                const uint64_t address = uint64_t(start) + uint64_t(i) * stride;
                if (address + stride > 0x100000000ull)
                    return fail(error, "face range overflow");
                uint8_t f[68]{};
                if (!read.bytes(uint32_t(address), f, stride))
                    return false;
                SceneFace face;
                face.address = uint32_t(address);
                face.block = block;
                face.owner = n;
                face.type = type;
                // REND_CullFaces resets surviving records with flags &= 8.
                // Bits 1/2 are cull/near-clip output from the previous render,
                // not source visibility. Only the dynamic-normal bit persists.
                face.flags = word(f) & 8;
                face.plane_distance = signed_word(f + 0x30);
                if (flat(type))
                    face.colour = word(b + 8);
                else
                    face.material_slot = word(b + 8);
                face.shade = stride == 68 ? f[0x40] : 0;
                if (stride == 68)
                    std::copy_n(f + 0x41, 3, face.corner_shades.begin());
                for (int corner = 0; corner < 3; ++corner) {
                    auto found = vertices.find(word(f + 8 + corner * 12));
                    if (found == vertices.end())
                        return fail(error, "unresolved face corner owner");
                    auto &c = face.corners[corner];
                    c.node = found->second.first;
                    c.vertex = found->second.second;
                    if ((type == 0x16 || type == 0x17 || (result.nodes[n].flags & 0x800)) &&
                        !capture_normal(word(f + 0x0c + corner * 12), face.corner_normals[corner]))
                        return false;
                    if (stride == 68) {
                        uint8_t uv[8];
                        face.uv_addresses[corner] = word(f + 0x34 + corner * 4);
                        if (!read.bytes(face.uv_addresses[corner], uv, sizeof uv))
                            return false;
                        face.source_uvs[corner] = {signed_word(uv), signed_word(uv + 4)};
                        c.uv[0] = float(signed_word(uv)) / (65536.0f * 256);
                        c.uv[1] = float(signed_word(uv + 4)) / (65536.0f * 256);
                    }
                }
                const auto plane = word(f + 0x2c);
                face.normal_address = plane;
                if (plane) {
                    uint8_t normal[16];
                    if (!read.bytes(plane, normal, sizeof normal))
                        return false;
                    for (int axis = 0; axis < 3; ++axis)
                        face.normal[axis] = signed_word(normal + axis * 4);
                    face.source_normal_dot = signed_word(normal + 12);
                }
                result.cross_node_faces += face.corners[0].node != face.corners[1].node ||
                                           face.corners[0].node != face.corners[2].node;
                result.faces.push_back(face);
            }
            block = word(b);
        }
    }
    // Snapshot CPU-mutated pages/palettes now, not by guest address at GPU
    // submission time. Only submitted textured faces require live bindings.
    std::unordered_map<uint32_t, uint32_t> material_slots;
    for (auto &face : result.faces) {
        if (!result.nodes[face.owner].submitted || flat(face.type))
            continue;
        auto found = material_slots.find(face.material_slot);
        if (found != material_slots.end()) {
            face.material = found->second;
            continue;
        }
        if (result.materials.size() >= 1024)
            return fail(error, "too many scene materials");
        uint8_t pointer[4];
        if (!read.bytes(face.material_slot, pointer, sizeof pointer))
            return false;
        SceneMaterial material;
        material.slot = face.material_slot;
        material.page = word(pointer);
        if (material.page < 0x8000)
            return fail(error, "invalid material palette/page address");
        if (source.gpu_image)
            material.gpu_mask =
                source.gpu_image(source.context, material.page, &material.gpu_version);
        if (!material.gpu_mask)
            material.indices.resize(256 * 256);
        std::array<uint8_t, 0x8000> palette{};
        if ((!material.gpu_mask &&
             !read.bytes(material.page, material.indices.data(), material.indices.size())) ||
            !read.bytes(material.page - 0x8000, palette.data(), palette.size()))
            return false;
        for (size_t i = 0; i < material.palette.size(); ++i)
            material.palette[i] = uint16_t(word(palette.data() + i * 4) >> 16);
        face.material = uint32_t(result.materials.size());
        material_slots.emplace(material.slot, face.material);
        result.materials.push_back(std::move(material));
    }
    for (size_t i = 0; i < result.lights.size(); ++i) {
        if (!needed_lights[i])
            continue;
        uint8_t record[0x94];
        if (!read.bytes(0x672700 + uint32_t(i) * 0x94, record, sizeof record))
            return false;
        auto &light = result.lights[i];
        light.type = word(record);
        for (int a = 0; a < 3; ++a)
            light.position[a] = signed_word(record + 4 + a * 4);
        for (int a = 0; a < 9; ++a)
            light.orientation[a] = signed_word(record + 0x10 + a * 4);
        light.inner_radius = signed_word(record + 0x88);
        light.outer_radius = signed_word(record + 0x8c);
        light.intensity = signed_word(record + 0x90);
    }
    output = std::move(result);
    return true;
}
} // namespace wd
