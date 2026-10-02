#include "render_scene.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace wd {
namespace {
struct Stream {
    FILE *file;
    bool writing, ok = true;
    void u32(uint32_t &value) {
        uint8_t b[4];
        if (writing) {
            for (int i = 0; i < 4; ++i)
                b[i] = uint8_t(value >> (8 * i));
            ok = std::fwrite(b, 1, 4, file) == 4 && ok;
        } else {
            if (std::fread(b, 1, 4, file) != 4) {
                ok = false;
                return;
            }
            value =
                uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        }
    }
    void i32(int32_t &value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        u32(bits);
        std::memcpy(&value, &bits, 4);
    }
    void f32(float &value) {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        u32(bits);
        std::memcpy(&value, &bits, 4);
    }
};
bool transfer(Stream &io, SceneSnapshot &s) {
    const bool lit_rows = std::any_of(s.materials.begin(), s.materials.end(),
                                      [](const auto &m) { return !m.lit_palette.empty(); });
    uint32_t magic = s.source_vertices.size() != s.vertices.size() ? 0x33534457
                     : s.light_views && s.dos_palette && lit_rows   ? 0x44534457
                     : s.light_views && s.dos_palette               ? 0x43534457
                     : s.light_views                                ? 0x42534457
                                                                    : 0x41534457,
             nodes = uint32_t(s.nodes.size()), vertices = uint32_t(s.vertices.size()),
             faces = uint32_t(s.faces.size());
    io.u32(magic);
    io.u32(nodes);
    io.u32(vertices);
    io.u32(faces);
    if (!io.ok ||
        (magic != 0x31534457 && magic != 0x32534457 && magic != 0x33534457 && magic != 0x34534457 &&
         magic != 0x35534457 && magic != 0x36534457 && magic != 0x37534457 && magic != 0x38534457 &&
         magic != 0x39534457 && magic != 0x41534457 && magic != 0x42534457 &&
         magic != 0x43534457 && magic != 0x44534457) ||
        nodes > 10000 || vertices > 1000000 || faces > 1000000)
        return false;
    const bool materials_version = magic != 0x31534457;
    if (!io.writing) {
        s.nodes.resize(nodes);
        s.poses.resize(nodes);
        s.vertices.resize(vertices);
        s.vertex_addresses.resize(vertices);
        s.faces.resize(faces);
        s.cross_node_faces = 0;
    }
    auto &c = s.camera;
    io.u32(c.address);
    for (auto &v : c.eye)
        io.i32(v);
    for (auto &v : c.local_rotation)
        io.i32(v);
    for (auto &v : c.view)
        io.f32(v);
    io.u32(c.screen_width);
    io.u32(c.screen_height);
    io.u32(c.viewport_x);
    io.u32(c.viewport_y);
    io.u32(c.viewport_width);
    io.u32(c.viewport_height);
    io.f32(c.focal_x);
    io.f32(c.focal_y);
    io.i32(c.center_x);
    io.i32(c.center_y);
    io.i32(c.near_plane);
    io.i32(c.far_plane);
    for (size_t i = 0; i < nodes; ++i) {
        auto &n = s.nodes[i];
        auto &p = s.poses[i];
        uint32_t active = n.submitted;
        io.u32(n.address);
        io.u32(n.flags);
        io.u32(n.first_vertex);
        io.u32(n.vertex_count);
        io.u32(active);
        n.submitted = active != 0;
        io.i32(p.parent);
        for (auto &v : p.local)
            io.f32(v);
        if (materials_version) {
            io.u32(n.shade);
            io.u32(n.light_count);
        }
        if (active > 1 || uint64_t(n.first_vertex) + n.vertex_count > vertices)
            return false;
    }
    for (size_t i = 0; i < vertices; ++i) {
        io.u32(s.vertex_addresses[i]);
        for (auto &v : s.vertices[i].xyz)
            io.f32(v);
    }
    for (auto &f : s.faces) {
        uint32_t shade = f.shade;
        io.u32(f.address);
        io.u32(f.owner);
        io.u32(f.flags);
        io.u32(f.material_slot);
        io.u32(f.colour);
        io.i32(f.type);
        io.u32(shade);
        f.shade = uint8_t(shade);
        if (f.owner >= nodes || shade > 255)
            return false;
        for (auto &v : f.normal)
            io.i32(v);
        io.i32(f.plane_distance);
        for (auto &corner : f.corners) {
            io.u32(corner.node);
            io.u32(corner.vertex);
            io.f32(corner.uv[0]);
            io.f32(corner.uv[1]);
            if (corner.node >= nodes || corner.vertex >= vertices)
                return false;
            const auto &owner = s.nodes[corner.node];
            if (corner.vertex < owner.first_vertex ||
                uint64_t(corner.vertex) >= uint64_t(owner.first_vertex) + owner.vertex_count)
                return false;
        }
        if (!io.writing)
            s.cross_node_faces +=
                f.corners[0].node != f.corners[1].node || f.corners[0].node != f.corners[2].node;
        if (materials_version) {
            io.u32(f.block);
            io.u32(f.material);
        }
    }
    if (materials_version) {
        uint32_t count = uint32_t(s.materials.size());
        io.u32(count);
        if (!io.ok || count > 1024)
            return false;
        if (!io.writing)
            s.materials.resize(count);
        for (auto &material : s.materials) {
            io.u32(material.slot);
            io.u32(material.page);
            for (auto &colour : material.palette) {
                uint32_t value = colour;
                io.u32(value);
                if (value > 65535)
                    return false;
                colour = uint16_t(value);
            }
            if (!io.writing)
                material.indices.resize(65536);
            if (material.indices.size() != 65536)
                return false;
            const size_t bytes = io.writing
                                     ? std::fwrite(material.indices.data(), 1, 65536, io.file)
                                     : std::fread(material.indices.data(), 1, 65536, io.file);
            io.ok = io.ok && bytes == 65536;
        }
        for (const auto &face : s.faces)
            if (face.material != UINT32_MAX && face.material >= count)
                return false;
    }
    if (magic >= 0x33534457) {
        io.u32(s.fog.enabled);
        io.u32(s.fog.colour);
        if (s.fog.enabled > 1)
            return false;
        for (auto &entry : s.fog.table) {
            uint32_t value = entry;
            io.u32(value);
            if (value > 255)
                return false;
            entry = uint8_t(value);
        }
    }
    if (magic >= 0x34534457) {
        if (!io.writing)
            s.source_vertices.resize(vertices);
        for (auto &vertex : s.source_vertices)
            for (auto &value : vertex)
                io.i32(value);
        for (auto &node : s.nodes)
            for (auto &index : node.light_indices) {
                uint32_t value = index;
                io.u32(value);
                if (value > 255)
                    return false;
                index = uint8_t(value);
            }
        for (auto &light : s.lights) {
            io.u32(light.type);
            for (auto &value : light.position)
                io.i32(value);
            for (auto &value : light.orientation)
                io.i32(value);
            io.i32(light.inner_radius);
            io.i32(light.outer_radius);
            io.i32(light.intensity);
        }
        for (const auto &node : s.nodes) {
            if (node.light_count > 8)
                return false;
            for (uint32_t i = 0; i < node.light_count; ++i)
                if (node.light_indices[i] >= 100)
                    return false;
        }
    }
    if (magic >= 0x35534457)
        for (auto &face : s.faces)
            io.u32(face.normal_address);
    if (magic >= 0x36534457) {
        io.u32(s.light_transform_count);
        if (s.light_transform_count > 100)
            return false;
    }
    if (magic >= 0x37534457)
        for (auto &face : s.faces)
            for (auto &shade : face.corner_shades) {
                uint32_t value = shade;
                io.u32(value);
                if (value > 255)
                    return false;
                shade = uint8_t(value);
            }
    if (!io.writing && magic < 0x37534457)
        for (const auto &face : s.faces)
            if (face.type >= 0x16 && face.type <= 0x18)
                return false; // Old snapshots omitted the three source shade bytes.
    if (magic >= 0x38534457) {
        auto normal = [&](SceneNormal &n) {
            io.u32(n.address);
            for (auto &value : n.xyz)
                io.i32(value);
            io.i32(n.dot);
        };
        size_t total = 0;
        for (auto &node : s.nodes) {
            uint32_t count = uint32_t(node.vertex_normals.size());
            io.u32(count);
            if (!io.ok || count > 1000000 - total)
                return false;
            total += count;
            if (!io.writing)
                node.vertex_normals.resize(count);
            for (auto &n : node.vertex_normals) {
                normal(n);
                if (!n.address || uint64_t(n.address) + 16 > 0x100000000ull)
                    return false;
            }
        }
        for (auto &face : s.faces)
            for (auto &n : face.corner_normals) {
                normal(n);
                if (uint64_t(n.address) + 16 > 0x100000000ull)
                    return false;
            }
    } else if (!io.writing)
        for (const auto &face : s.faces)
            if ((face.type == 0x16 || face.type == 0x17) && s.nodes[face.owner].light_count)
                return false; // Lit Gouraud requires pool identity and retained corner dots.
    if (magic >= 0x39534457) {
        for (auto &node : s.nodes) {
            uint32_t active = node.visual_active;
            io.u32(active);
            if (active > 1)
                return false;
            node.visual_active = active != 0;
            for (auto &value : node.source_rotation)
                io.i32(value);
        }
        for (auto &face : s.faces)
            for (unsigned c = 0; c < 3; ++c) {
                io.u32(face.uv_addresses[c]);
                if (uint64_t(face.uv_addresses[c]) + 8 > 0x100000000ull)
                    return false;
                for (auto &value : face.source_uvs[c])
                    io.i32(value);
            }
    } else if (!io.writing)
        for (const auto &node : s.nodes)
            if (node.flags & 0x800)
                return false; // Environment mapping requires exact rotations and UV alias identity.
    if (magic >= 0x41534457) {
        for (auto &node : s.nodes) {
            for (auto &value : node.source_position)
                io.i32(value);
            io.u32(node.face_normal_base);
            io.u32(node.face_normal_count);
            if (node.face_normal_count > 1000000 ||
                uint64_t(node.face_normal_base) + uint64_t(node.face_normal_count) * 16 > 0x100000000ull)
                return false;
        }
        for (auto &face : s.faces)
            io.i32(face.source_normal_dot);
    } else if (!io.writing)
        for (const auto &face : s.faces)
            if (face.type == 0x1b)
                return false; // Exact mask projection requires the original integer translations.
    // Older snapshots lack the retained view transforms; lighting rejects a
    // bound slot past the refreshed prefix in them instead of guessing.
    s.light_views = magic >= 0x42534457;
    if (s.light_views)
        for (auto &light : s.lights) {
            for (auto &value : light.view_position)
                io.i32(value);
            for (auto &value : light.view_orientation)
                io.i32(value);
        }
    // WDSC: the same layout; the material banks hold the DOS 3dfx rows.
    s.dos_palette = magic >= 0x43534457;
    // WDSD: per material, the rows of lit faces (indexed by shade) or none.
    if (magic >= 0x44534457)
        for (auto &material : s.materials) {
            uint32_t present = !material.lit_palette.empty();
            io.u32(present);
            if (!io.ok || present > 1)
                return false;
            if (!io.writing)
                material.lit_palette.assign(present ? 32 * 256 : 0, 0);
            for (auto &colour : material.lit_palette) {
                uint32_t value = colour;
                io.u32(value);
                if (value > 65535)
                    return false;
                colour = uint16_t(value);
            }
        }
    return io.ok;
}
} // namespace
bool write_scene(const SceneSnapshot &input, const char *path, std::string &error) {
    if (input.nodes.size() != input.poses.size() ||
        (!input.source_vertices.empty() && input.source_vertices.size() != input.vertices.size()) ||
        input.vertices.size() != input.vertex_addresses.size() || input.nodes.size() > 10000 ||
        input.vertices.size() > 1000000 || input.faces.size() > 1000000 ||
        input.materials.size() > 1024) {
        error = "inconsistent scene arrays";
        return false;
    }
    if (!path) {
        error = "missing scene output path";
        return false;
    }
    FILE *file = std::fopen(path, "wb");
    if (!file) {
        error = "cannot create scene capture";
        return false;
    }
    // transfer never changes values in write mode except assigning identical
    // values; use a copy to keep the public snapshot immutable.
    SceneSnapshot copy = input;
    Stream io{file, true};
    const bool ok = transfer(io, copy);
    const int closed = std::fclose(file);
    if (!ok || closed) {
        error = "scene capture write failed";
        return false;
    }
    return true;
}
bool read_scene(const char *path, SceneSnapshot &output, std::string &error) {
    if (!path) {
        error = "missing scene input path";
        return false;
    }
    FILE *file = std::fopen(path, "rb");
    if (!file) {
        error = "cannot open scene capture";
        return false;
    }
    SceneSnapshot value;
    Stream io{file, false};
    const bool ok = transfer(io, value) && std::fgetc(file) == EOF;
    std::fclose(file);
    std::vector<float> world(value.poses.size() * 12);
    if (!ok || !od_compose_pose(value.poses.data(), value.poses.size(), world.data())) {
        error = "invalid/truncated scene capture";
        return false;
    }
    float matrix[16];
    if (!value.view_projection(640, 480, true, matrix)) {
        error = "invalid captured projection";
        return false;
    }
    output = std::move(value);
    return true;
}
} // namespace wd

extern "C" int wd_capture_scene_file(bool (*reader)(void *, uint32_t, void *, size_t),
                                     void *context, uint32_t root, const char *path, char *message,
                                     size_t capacity) {
    wd::SceneSnapshot scene;
    std::string error;
    const bool ok = wd::capture_scene({context, reader}, root, scene, error) &&
                    wd::write_scene(scene, path, error);
    if (message && capacity)
        std::snprintf(message, capacity, "%s", error.c_str());
    return ok ? 1 : 0;
}
