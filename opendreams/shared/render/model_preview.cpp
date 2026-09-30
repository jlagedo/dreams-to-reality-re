#include "render/model_preview.h"

#include "render/direct_sokol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <map>

namespace od {
namespace {

void preview_matrix(const ModelView &view, float *matrix) {
    if (view.explicit_eye) {
        float forward[3]{view.target[0] - view.eye[0], view.target[1] - view.eye[1],
                         view.target[2] - view.eye[2]};
        const float length =
            std::sqrt(forward[0] * forward[0] + forward[1] * forward[1] + forward[2] * forward[2]);
        if (length > 0.0f)
            for (float &value : forward)
                value /= length;
        else
            forward[2] = 1.0f;
        float right[3]{forward[2], 0.0f, -forward[0]};
        const float horizontal = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        if (horizontal > 0.0f) {
            right[0] /= horizontal;
            right[2] /= horizontal;
        } else
            right[0] = 1.0f;
        const float up[3]{forward[1] * right[2] - forward[2] * right[1],
                          forward[2] * right[0] - forward[0] * right[2],
                          forward[0] * right[1] - forward[1] * right[0]};
        const auto eye_dot = [&](const float (&axis)[3]) {
            return axis[0] * view.eye[0] + axis[1] * view.eye[1] + axis[2] * view.eye[2];
        };
        const float a = -view.near_plane / (view.far_plane - view.near_plane);
        const float b = view.far_plane * view.near_plane / (view.far_plane - view.near_plane);
        const float rows[4][4] = {
            {view.focal_x * right[0], view.focal_x * right[1], view.focal_x * right[2],
             -view.focal_x * eye_dot(right)},
            {view.focal_y * up[0], view.focal_y * up[1], view.focal_y * up[2],
             -view.focal_y * eye_dot(up)},
            {a * forward[0], a * forward[1], a * forward[2], b - a * eye_dot(forward)},
            {forward[0], forward[1], forward[2], -eye_dot(forward)}};
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                matrix[column * 4 + row] = rows[row][column];
        return;
    }
    const float yaw_c = std::cos(view.yaw), yaw_s = std::sin(view.yaw);
    const float pitch_c = std::cos(view.pitch), pitch_s = std::sin(view.pitch);
    const float near_plane = view.near_plane, far_plane = view.far_plane;
    const float a = -near_plane / (far_plane - near_plane);
    const float b = far_plane * near_plane / (far_plane - near_plane);
    const float x_offset = -yaw_c * view.target[0] + yaw_s * view.target[2];
    const float y_offset = pitch_s * yaw_s * view.target[0] - pitch_c * view.target[1] +
                           pitch_s * yaw_c * view.target[2];
    const float z_offset = view.distance - pitch_c * yaw_s * view.target[0] -
                           pitch_s * view.target[1] - pitch_c * yaw_c * view.target[2];
    const float rows[4][4] = {
        {view.focal_x * yaw_c, 0, -view.focal_x * yaw_s, view.focal_x * x_offset},
        {-view.focal_y * pitch_s * yaw_s, view.focal_y * pitch_c, -view.focal_y * pitch_s * yaw_c,
         view.focal_y * y_offset},
        {a * pitch_c * yaw_s, a * pitch_s, a * pitch_c * yaw_c, a * z_offset + b},
        {pitch_c * yaw_s, pitch_s, pitch_c * yaw_c, z_offset},
    };
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            matrix[column * 4 + row] = rows[row][column];
}

bool face_mode(int32_t type, od_face_mode &mode, uint32_t &wrap) {
    wrap = 1;
    switch (type) {
    case 1:
    case 2:
    case 3:
    case 0x16:
    case 0x17:
    case 0x18:
        mode = OD_FACE_OPAQUE;
        wrap = 0;
        return true;
    case 9:
        mode = OD_FACE_OPAQUE;
        return true;
    case -5:
    case -6:
        mode = OD_FACE_CHROMA;
        return true;
    case -7:
    case -4:
    case -3:
        mode = OD_FACE_TRANSLUCENT;
        return true;
    default:
        return false;
    }
}
std::vector<const port::ModelFace *> preview_faces(const port::ModelGraph &graph) {
    std::vector<const port::ModelFace *> faces;
    faces.reserve(graph.faces.size() + graph.flat_faces.size());
    for (const auto &face : graph.faces)
        faces.push_back(&face);
    for (const auto &face : graph.flat_faces)
        if (face.type == 1)
            faces.push_back(&face);
    return faces;
}
} // namespace

void ModelPreview::committed(void *context) {
    auto &self = *static_cast<ModelPreview *>(context);
    auto it = self.textures_.begin();
    while (it != self.textures_.end()) {
        if (it->last_frame != self.frame_) {
            od_renderer_release(self.renderer_, it->texture);
            it = self.textures_.erase(it);
        } else
            ++it;
    }
    od_renderer_frame_complete(self.renderer_);
    ++self.frame_;
}
bool ModelPreview::create_target(int width, int height, std::string &error) {
    auto scene = od_renderer_target(renderer_, width, height, width, height);
    auto output = scene ? od_renderer_target(renderer_, width, height, width, height) : 0;
    if (!scene || !output) {
        error = od_renderer_error(renderer_);
        if (scene)
            od_renderer_release(renderer_, scene);
        return false;
    }
    destroy_target();
    scene_target_ = scene;
    output_target_ = output;
    target_width_ = width;
    target_height_ = height;
    return true;
}
void ModelPreview::destroy_target() {
    if (scene_target_)
        od_renderer_release(renderer_, scene_target_);
    if (output_target_)
        od_renderer_release(renderer_, output_target_);
    scene_target_ = output_target_ = 0;
    target_width_ = target_height_ = 0;
}
bool ModelPreview::resize_target(int width, int height, std::string &error) {
    error.clear();
    if (!renderer_) {
        error = "preview renderer is not initialized";
        return false;
    }
    return (width == target_width_ && height == target_height_ && scene_target_) ||
           create_target(width, height, error);
}
bool ModelPreview::init(std::string &error, int width, int height) {
    error.clear();
    if (renderer_) {
        error = "preview renderer already initialized";
        return false;
    }
    renderer_ = od_renderer_create();
    if (!renderer_) {
        error = "cannot create shared preview renderer";
        return false;
    }
    listening_ = sg_add_commit_listener({committed, this});
    if (!listening_ || !create_target(width, height, error)) {
        if (error.empty())
            error = "cannot register preview frame retirement";
        shutdown();
        return false;
    }
    return true;
}
bool ModelPreview::stage_graph(const port::ModelGraph &graph, bool fit, std::string &error) {
    if (graph.nodes.empty() || graph.nodes.size() > 10000 || graph.faces.size() > 1000000 ||
        graph.flat_faces.size() > 1000000 - graph.faces.size() ||
        graph.materials.size() > 512) {
        error = "invalid preview graph size";
        return false;
    }
    std::vector<od_pose_node> poses(graph.nodes.size() + 1);
    poses[0] = {-1, {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0}};
    std::vector<od_scene_vertex> vertices;
    std::vector<uint32_t> bases;
    for (size_t i = 0; i < graph.nodes.size(); ++i) {
        const auto &source = graph.nodes[i];
        if (source.parent < -1 || source.parent >= int(graph.nodes.size()) ||
            vertices.size() + source.vertices.size() > 1000000) {
            error = "invalid preview hierarchy or vertex count";
            return false;
        }
        auto &node = poses[i + 1];
        node.parent = source.parent + 1;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                node.local[r * 4 + c] = float(source.local_rot[r * 3 + c]) / 32768;
            node.local[r * 4 + 3] = float(source.local_xyz[r]);
        }
        bases.push_back(uint32_t(vertices.size()));
        for (auto v : source.vertices)
            vertices.push_back({{float(v[0]), float(v[1]), float(v[2])}});
    }
    std::vector<float> world(poses.size() * 12);
    if (!od_compose_pose(poses.data(), poses.size(), world.data())) {
        error = "invalid preview posed hierarchy";
        return false;
    }
    std::array<float, 3> minimum{}, maximum{};
    bool first = true;
    for (const auto *source_face : preview_faces(graph)) {
        const auto &face = *source_face;
        od_face_mode mode{};
        uint32_t wrap;
        if (!face_mode(face.type, mode, wrap) ||
            (face.type != 1 && (face.material_index >= graph.materials.size() ||
                               graph.materials[face.material_index].bank.size() != 0x18014)))
            continue;
        if (face.owner_node >= graph.nodes.size()) {
            error = "invalid preview face owner";
            return false;
        }
        if (face.type != 1 && graph.nodes[face.owner_node].light_count) {
            error = "preview per-object lighting is not implemented";
            return false;
        }
        const auto lod = face.type == 1 ? 128 : graph.materials[face.material_index].preview_lod;
        if (lod != 128 && lod != 256) {
            error = "invalid preview material LOD";
            return false;
        }
        for (const auto &corner : face.corners) {
            if (corner.node >= graph.nodes.size() ||
                corner.vertex >= graph.nodes[corner.node].vertices.size()) {
                error = "invalid preview face corner";
                return false;
            }
            const auto &v = vertices[bases[corner.node] + corner.vertex];
            const float *m = world.data() + (corner.node + 1) * 12;
            for (int axis = 0; axis < 3; ++axis) {
                const float p = m[axis * 4] * v.xyz[0] + m[axis * 4 + 1] * v.xyz[1] +
                                m[axis * 4 + 2] * v.xyz[2] + m[axis * 4 + 3];
                if (first)
                    minimum[axis] = maximum[axis] = p;
                else {
                    minimum[axis] = std::min(minimum[axis], p);
                    maximum[axis] = std::max(maximum[axis], p);
                }
            }
            first = false;
        }
    }
    float scale = frame_scale_;
    std::array<float, 3> center{frame_center_[0], frame_center_[1], frame_center_[2]};
    if (fit) {
        float extent = 0;
        for (int i = 0; i < 3; ++i)
            extent = std::max(extent, maximum[i] - minimum[i]);
        if (first || !(extent > 0) || !std::isfinite(extent)) {
            error = "preview has no finite drawable geometry";
            return false;
        }
        scale = 1.8f / extent;
        if (!std::isfinite(scale)) {
            error = "preview framing scale overflow";
            return false;
        }
        for (int i = 0; i < 3; ++i)
            center[i] = minimum[i] * .5f + maximum[i] * .5f;
    }
    poses[0].local[0] = poses[0].local[10] = scale;
    poses[0].local[5] = -scale;
    for (int i = 0; i < 3; ++i)
        poses[0].local[i * 4 + 3] = -center[i] * scale;
    std::vector<GpuMaterial> materials;
    for (size_t i = 0; i < graph.materials.size(); ++i) {
        const auto &m = graph.materials[i];
        const bool same = !fit && i < graph_.materials.size() && i < materials_.size() &&
                          m.bank == graph_.materials[i].bank &&
                          m.preview_lod == graph_.materials[i].preview_lod &&
                          m.cache_slot == graph_.materials[i].cache_slot &&
                          m.name == graph_.materials[i].name;
        if (same)
            materials.push_back(materials_[i]);
        else
            materials.push_back(
                {m.preview_lod, next_version_++,
                 m.cache_slot == SIZE_MAX ? uint64_t(i) * 2 : uint64_t(m.cache_slot) * 2 + 1,
                 m.bank});
    }
    graph_ = graph;
    materials_ = std::move(materials);
    poses_ = std::move(poses);
    vertices_ = std::move(vertices);
    vertex_bases_ = std::move(bases);
    frame_scale_ = scale;
    std::copy(center.begin(), center.end(), frame_center_);
    loaded_ = true;
    return true;
}
bool ModelPreview::load(const port::PreviewActor &actor, std::string &error) {
    if (!actor.attached_to_camera_root) {
        error = "selected model is not attached to preview root";
        return false;
    }
    return load(actor.model, error);
}
bool ModelPreview::load(const port::ModelGraph &graph, std::string &error) {
    error.clear();
    clear_model();
    if (!renderer_) {
        error = "preview renderer is not initialized";
        return false;
    }
    return stage_graph(graph, true, error);
}
bool ModelPreview::update_pose(const port::ModelGraph &graph, std::string &error) {
    error.clear();
    if (!has_model()) {
        error = "no model is loaded for animation";
        return false;
    }
    return stage_graph(graph, false, error);
}
void ModelPreview::update_palette_rows(size_t material, uint32_t rows,
                                       const std::vector<uint8_t> &bank) {
    if (material >= materials_.size() || bank.size() != 0x18014 ||
        materials_[material].bank.size() != bank.size())
        return;
    for (unsigned row = 0; row < 32; ++row)
        if (rows & (1u << row))
            std::copy_n(bank.data() + 0x14 + row * 0x400, 0x400,
                        materials_[material].bank.data() + 0x14 + row * 0x400);
}
void ModelPreview::update_material_pixels(size_t material, const std::vector<uint8_t> &bank) {
    if (material >= materials_.size() || bank.size() != 0x18014 ||
        materials_[material].bank.size() != bank.size())
        return;
    std::copy_n(bank.data() + 0x8014, 65536, materials_[material].bank.data() + 0x8014);
    materials_[material].version = next_version_++;
}
od_render_id ModelPreview::texture(size_t material, uint32_t row, std::string &error) {
    const auto &source = materials_[material];
    if (!palette_bound_ || palette_identity_ != source.identity) {
        palette_bound_ = true;
        palette_identity_ = source.identity;
        for (size_t i = 0; i < 256; ++i) {
            const auto at = 0x14 + row * 0x400 + i * 4 + 2;
            palette_[i] = uint16_t(source.bank[at] | uint16_t(source.bank[at + 1]) << 8);
        }
    }
    for (auto &entry : textures_)
        if (entry.material == material && entry.version == source.version &&
            entry.palette == palette_) {
            entry.last_frame = frame_;
            return entry.texture;
        }
    std::vector<uint32_t> pixels(size_t(source.lod) * source.lod);
    if (!od_expand_material_page(source.bank.data() + 0x8014, 256, palette_.data(), pixels.data(),
                                 int(source.lod))) {
        error = "preview palette expansion failed";
        return 0;
    }
    const auto id = od_renderer_upload_rgba(renderer_, int(source.lod), int(source.lod),
                                            pixels.data(), source.lod * 4);
    if (!id) {
        error = od_renderer_error(renderer_);
        return 0;
    }
    textures_.push_back({material, source.version, frame_, palette_, id});
    return id;
}
bool ModelPreview::draw(const ModelView &view, std::string &error) {
    error.clear();
    if (!has_model()) {
        error = "no preview model";
        return false;
    }
    std::vector<od_scene_triangle> triangles;
    const auto faces = preview_faces(graph_);
    std::vector<float> world;
    float view_projection[16];
    preview_matrix(view, view_projection);
    std::map<std::pair<size_t, uint32_t>, uint32_t> diagnostic_colours;
    for (int pass = 0; pass < 2; ++pass)
        for (const auto *source_face : faces) {
            const auto &face = *source_face;
            od_face_mode mode{};
            uint32_t wrap;
            if (!face_mode(face.type, mode, wrap) ||
                (face.type != 1 && (face.material_index >= materials_.size() ||
                                   materials_[face.material_index].bank.size() != 0x18014)))
                continue;
            if ((mode == OD_FACE_TRANSLUCENT) != (pass == 1))
                continue;
            od_scene_triangle triangle{};
            triangle.mode = mode;
            triangle.colour = 0xffffffff;
            if (face.type != 1) {
                const auto &material = graph_.materials[face.material_index];
                const uint32_t row = material.static_palette_row15 || mode == OD_FACE_TRANSLUCENT
                                         ? 15u
                                         : std::min(graph_.nodes[face.owner_node].shade, 31u);
                triangle.texture = texture(face.material_index, row, error);
                if (!triangle.texture)
                    return false;
            }
            triangle.wrap_texture = wrap;
            triangle.cull_back = 1;
            if (face.type >= 0x16 && face.type <= 0x18) {
                triangle.use_corner_brightness = 1;
                for (size_t corner = 0; corner < 3; ++corner)
                    triangle.corner_brightness[corner] = uint8_t(
                        std::min(unsigned(face.corner_shades[corner]) * 8u, 255u));
            }
            for (int i = 0; i < 3; ++i) {
                const auto &c = face.corners[i];
                triangle.corners[i] = {
                    uint32_t(c.node + 1),
                    vertex_bases_[c.node] + uint32_t(c.vertex),
                    {float(c.u) / (65536.0f * 256), float(c.v) / (65536.0f * 256)}};
            }
            if (face.type == 1) {
                if (world.empty()) {
                    world.resize(poses_.size() * 12);
                    if (!od_compose_pose(poses_.data(), poses_.size(), world.data())) {
                        error = "invalid diagnostic preview hierarchy";
                        return false;
                    }
                }
                float clip[12]{};
                for (unsigned c = 0; c < 3; ++c) {
                    const auto &corner = triangle.corners[c];
                    const float *m = world.data() + corner.node * 12;
                    const float *v = vertices_[corner.vertex].xyz;
                    float p[4] = {0, 0, 0, 1};
                    for (unsigned axis = 0; axis < 3; ++axis)
                        p[axis] = m[axis * 4] * v[0] + m[axis * 4 + 1] * v[1] +
                                  m[axis * 4 + 2] * v[2] + m[axis * 4 + 3];
                    for (unsigned row = 0; row < 4; ++row)
                        for (unsigned k = 0; k < 4; ++k)
                            clip[c * 4 + row] += view_projection[k * 4 + row] * p[k];
                }
                const int visible = od_triangle_visible(clip, 1);
                if (visible < 0) {
                    error = "invalid diagnostic preview geometry";
                    return false;
                }
                if (!visible)
                    continue;
                auto &colour = diagnostic_colours[{face.owner_node, face.source_block}];
                triangle.colour = 0xff000000u | ((colour >> 16) & 255u) | (colour & 0xff00u) |
                                  ((colour & 255u) << 16);
                colour += 0x24bf9;
            }
            triangles.push_back(triangle);
        }
    od_scene_packet packet{};
    packet.target = scene_target_;
    packet.nodes = poses_.data();
    packet.node_count = poses_.size();
    packet.vertices = vertices_.data();
    packet.vertex_count = vertices_.size();
    packet.triangles = triangles.data();
    packet.triangle_count = triangles.size();
    packet.clear = 1;
    packet.clear_colour[0] = .045f;
    packet.clear_colour[1] = .055f;
    packet.clear_colour[2] = .08f;
    packet.clear_colour[3] = 1;
    std::copy_n(view_projection, 16, packet.view_projection);
    packet.fog.enabled = fog_.table_mode;
    packet.fog.colour =
        ((fog_.color >> 16) & 255) | (fog_.color & 0xff00) | ((fog_.color & 255) << 16);
    std::copy(fog_.table.begin(), fog_.table.end(), packet.fog.table);
    packet.fog_depth_scale = 1 / frame_scale_;
    if (!od_renderer_scene(renderer_, &packet)) {
        error = od_renderer_error(renderer_);
        return false;
    }
    const bool ok =
        od_renderer_output_target(renderer_, scene_target_, output_target_, output_gamma_) != 0;
    if (!ok)
        error = od_renderer_error(renderer_);
    return ok;
}
sg_view ModelPreview::texture_view() const { return od_renderer_view(renderer_, output_target_); }
od_render_stats ModelPreview::renderer_stats() const { return od_renderer_stats(renderer_); }
bool ModelPreview::project_joint(const std::array<int32_t, 3> &world_xyz, const ModelView &view,
                                 float &u, float &v) const {
    if (!has_model())
        return false;
    const float point[3]{(static_cast<float>(world_xyz[0]) - frame_center_[0]) * frame_scale_,
                         (-static_cast<float>(world_xyz[1]) - frame_center_[1]) * frame_scale_,
                         (static_cast<float>(world_xyz[2]) - frame_center_[2]) * frame_scale_};
    float matrix[16]{};
    preview_matrix(view, matrix);
    const float x = matrix[0] * point[0] + matrix[4] * point[1] + matrix[8] * point[2] + matrix[12];
    const float y = matrix[1] * point[0] + matrix[5] * point[1] + matrix[9] * point[2] + matrix[13];
    const float z =
        matrix[2] * point[0] + matrix[6] * point[1] + matrix[10] * point[2] + matrix[14];
    const float w =
        matrix[3] * point[0] + matrix[7] * point[1] + matrix[11] * point[2] + matrix[15];
    if (!(w > 0.0f) || z < 0.0f || z > w)
        return false;
    u = 0.5f + 0.5f * x / w;
    v = 0.5f - 0.5f * y / w;
    return std::isfinite(u) && std::isfinite(v);
}

std::array<float, 3> ModelPreview::world_to_view(const std::array<int32_t, 3> &xyz) const {
    return {{(static_cast<float>(xyz[0]) - frame_center_[0]) * frame_scale_,
             (-static_cast<float>(xyz[1]) - frame_center_[1]) * frame_scale_,
             (static_cast<float>(xyz[2]) - frame_center_[2]) * frame_scale_}};
}

void ModelPreview::clear_model() {
    for (auto &entry : textures_)
        od_renderer_release(renderer_, entry.texture);
    textures_.clear();
    materials_.clear();
    graph_ = {};
    poses_.clear();
    vertices_.clear();
    vertex_bases_.clear();
    palette_bound_ = false;
    loaded_ = false;
    frame_scale_ = 1;
    std::fill_n(frame_center_, 3, 0.0f);
}
void ModelPreview::shutdown() {
    if (!renderer_)
        return;
    if (listening_)
        sg_remove_commit_listener({committed, this});
    listening_ = false;
    clear_model();
    destroy_target();
    od_renderer_destroy(renderer_);
    renderer_ = nullptr;
}
} // namespace od
