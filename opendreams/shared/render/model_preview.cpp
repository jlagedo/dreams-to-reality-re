#include "render/model_preview.h"

#include <model_preview.glsl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace od {
namespace {

constexpr int target_size = 512;

struct Vertex {
    float position[3];
    float uv[2];
};

void preview_matrix(float (&matrix)[16]) {
    constexpr float yaw_c = 0.70710678f, yaw_s = 0.70710678f;
    constexpr float pitch_c = 0.93969262f, pitch_s = 0.34202014f;
    constexpr float focal = 2.2f, near_plane = 0.1f, far_plane = 100.0f;
    const float a = far_plane / (far_plane - near_plane);
    const float b = -far_plane * near_plane / (far_plane - near_plane);
    const float rows[4][4] = {
        {focal * yaw_c, 0, -focal * yaw_s, 0},
        {-focal * pitch_s * yaw_s, focal * pitch_c,
         -focal * pitch_s * yaw_c, 0},
        {a * pitch_c * yaw_s, a * pitch_s, a * pitch_c * yaw_c, a * 3.5f + b},
        {pitch_c * yaw_s, pitch_s, pitch_c * yaw_c, 3.5f},
    };
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            matrix[column * 4 + row] = rows[row][column];
}

std::array<float, 3> world_point(const port::ModelGraph& graph,
                                  const port::ModelCorner& corner) {
    const auto& node = graph.nodes[corner.node];
    const auto& source = node.vertices[corner.vertex];
    std::array<float, 3> result{};
    for (size_t row = 0; row < 3; ++row) {
        int64_t value = 0;
        for (size_t column = 0; column < 3; ++column)
            value += static_cast<int64_t>(node.local_rot[row * 3 + column]) *
                     source[column];
        result[row] = static_cast<float>((value >> 15) + node.local_xyz[row]);
    }
    result[1] = -result[1]; // Retail Y points down; the viewer's Y points up.
    return result;
}

std::vector<uint8_t> palette_texture(const std::vector<uint8_t>& bank, unsigned row) {
    std::vector<uint8_t> rgba(256u * 256u * 4u);
    const size_t palette = 0x14u + static_cast<size_t>(row) * 0x400u;
    const size_t page = 0x8014u;
    for (size_t pixel = 0; pixel < 256u * 256u; ++pixel) {
        const uint8_t index = bank[page + pixel];
        const size_t entry = palette + static_cast<size_t>(index) * 4u + 2u;
        const uint16_t color = static_cast<uint16_t>(bank[entry]) |
            static_cast<uint16_t>(bank[entry + 1] << 8);
        rgba[pixel * 4] = static_cast<uint8_t>(((color >> 11) & 31u) * 8u);
        rgba[pixel * 4 + 1] = static_cast<uint8_t>(((color >> 5) & 63u) * 4u);
        rgba[pixel * 4 + 2] = static_cast<uint8_t>((color & 31u) * 8u);
        rgba[pixel * 4 + 3] = 255;
    }
    return rgba;
}

bool valid(sg_image image) { return sg_query_image_state(image) == SG_RESOURCESTATE_VALID; }
bool valid(sg_view view) { return sg_query_view_state(view) == SG_RESOURCESTATE_VALID; }

} // namespace

bool ModelPreview::init(std::string& error) {
    error.clear();
    sg_image_desc color_desc{};
    color_desc.usage.color_attachment = true;
    color_desc.width = target_size;
    color_desc.height = target_size;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.sample_count = 1;
    color_desc.label = "model preview color";
    color_ = sg_make_image(&color_desc);

    sg_image_desc depth_desc{};
    depth_desc.usage.depth_stencil_attachment = true;
    depth_desc.width = target_size;
    depth_desc.height = target_size;
    depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
    depth_desc.sample_count = 1;
    depth_desc.label = "model preview depth";
    depth_ = sg_make_image(&depth_desc);

    sg_view_desc color_attachment_desc{};
    color_attachment_desc.color_attachment.image = color_;
    color_attachment_ = sg_make_view(&color_attachment_desc);
    sg_view_desc depth_attachment_desc{};
    depth_attachment_desc.depth_stencil_attachment.image = depth_;
    depth_attachment_ = sg_make_view(&depth_attachment_desc);
    sg_view_desc color_texture_desc{};
    color_texture_desc.texture.image = color_;
    color_texture_view_ = sg_make_view(&color_texture_desc);

    shader_ = sg_make_shader(model_preview_shader_desc(sg_query_backend()));
    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shader_;
    pipeline_desc.layout.buffers[0].stride = sizeof(Vertex);
    pipeline_desc.layout.attrs[ATTR_model_preview_position].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline_desc.layout.attrs[ATTR_model_preview_texcoord].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc.layout.attrs[ATTR_model_preview_texcoord].offset = 3 * sizeof(float);
    pipeline_desc.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    pipeline_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    pipeline_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.label = "model preview pipeline";
    pipeline_ = sg_make_pipeline(&pipeline_desc);

    sg_sampler_desc sampler_desc{};
    sampler_desc.min_filter = SG_FILTER_LINEAR;
    sampler_desc.mag_filter = SG_FILTER_LINEAR;
    sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.label = "model preview sampler";
    sampler_ = sg_make_sampler(&sampler_desc);

    if (!valid(color_) || !valid(depth_) || !valid(color_attachment_) ||
        !valid(depth_attachment_) || !valid(color_texture_view_) ||
        sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(pipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_sampler_state(sampler_) != SG_RESOURCESTATE_VALID) {
        error = "sokol could not create the model preview GPU resources";
        shutdown();
        return false;
    }
    return true;
}

bool ModelPreview::load(const port::PreviewActor& actor, std::string& error) {
    error.clear();
    clear_model();
    const auto& graph = actor.model;
    if (!actor.attached_to_camera_root || graph.nodes.empty() ||
        graph.faces.empty() || graph.materials.size() != 1 ||
        graph.materials[0].bank.size() < 0x18014u) {
        error = "selected model has no supported one-material 3D preview";
        return false;
    }
    std::vector<Vertex> draw_vertices;
    draw_vertices.reserve(graph.faces.size() * 3u);
    std::array<float, 3> minimum{
        std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float, 3> maximum{
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()};
    for (const auto& face : graph.faces) {
        if (face.material_index != 0 || (face.type != 2 && face.type != 3)) {
            error = "selected model has a face mode outside the initial GPU path";
            return false;
        }
        for (const auto& corner : face.corners) {
            if (corner.node >= graph.nodes.size() ||
                corner.vertex >= graph.nodes[corner.node].vertices.size()) {
                error = "selected model has an invalid face corner";
                return false;
            }
            const auto point = world_point(graph, corner);
            Vertex vertex{{point[0], point[1], point[2]},
                          {static_cast<float>(corner.u) / (65536.0f * 256.0f),
                           static_cast<float>(corner.v) / (65536.0f * 256.0f)}};
            for (size_t axis = 0; axis < 3; ++axis) {
                minimum[axis] = std::min(minimum[axis], point[axis]);
                maximum[axis] = std::max(maximum[axis], point[axis]);
            }
            draw_vertices.push_back(vertex);
        }
    }
    float extent = 0;
    for (size_t axis = 0; axis < 3; ++axis)
        extent = std::max(extent, maximum[axis] - minimum[axis]);
    if (extent <= 0) {
        error = "selected model has zero-size geometry";
        return false;
    }
    const float scale = 1.8f / extent;
    for (auto& vertex : draw_vertices)
        for (size_t axis = 0; axis < 3; ++axis)
            vertex.position[axis] = (vertex.position[axis] -
                (minimum[axis] + maximum[axis]) * 0.5f) * scale;

    sg_buffer_desc vertex_desc{};
    vertex_desc.data.ptr = draw_vertices.data();
    vertex_desc.data.size = draw_vertices.size() * sizeof(Vertex);
    vertex_desc.label = "model preview triangles";
    vertices_ = sg_make_buffer(&vertex_desc);
    vertex_count_ = draw_vertices.size();

    const unsigned shade = std::min(graph.nodes[0].shade, 31u);
    const auto rgba = palette_texture(graph.materials[0].bank, shade);
    sg_image_desc material_desc{};
    material_desc.width = 256;
    material_desc.height = 256;
    material_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    material_desc.data.mip_levels[0].ptr = rgba.data();
    material_desc.data.mip_levels[0].size = rgba.size();
    material_desc.label = "model preview indexed material";
    material_ = sg_make_image(&material_desc);
    sg_view_desc material_view_desc{};
    material_view_desc.texture.image = material_;
    material_view_ = sg_make_view(&material_view_desc);
    if (sg_query_buffer_state(vertices_) != SG_RESOURCESTATE_VALID ||
        !valid(material_) || !valid(material_view_)) {
        error = "sokol could not upload the model preview mesh or material";
        clear_model();
        return false;
    }
    return true;
}

void ModelPreview::draw() const {
    if (!has_model()) return;
    sg_pass pass{};
    pass.attachments.colors[0] = color_attachment_;
    pass.attachments.depth_stencil = depth_attachment_;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.045f, 0.055f, 0.08f, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    sg_begin_pass(&pass);
    sg_apply_pipeline(pipeline_);
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertices_;
    bindings.views[VIEW_image_tex] = material_view_;
    bindings.samplers[SMP_image_smp] = sampler_;
    sg_apply_bindings(&bindings);
    float matrix[16]{};
    preview_matrix(matrix);
    sg_range uniforms{matrix, sizeof(matrix)};
    sg_apply_uniforms(UB_vs_params, &uniforms);
    sg_draw(0, static_cast<int>(vertex_count_), 1);
    sg_end_pass();
}

void ModelPreview::clear_model() {
    if (material_view_.id) sg_destroy_view(material_view_);
    if (material_.id) sg_destroy_image(material_);
    if (vertices_.id) sg_destroy_buffer(vertices_);
    material_view_ = {};
    material_ = {};
    vertices_ = {};
    vertex_count_ = 0;
}

void ModelPreview::shutdown() {
    clear_model();
    if (sampler_.id) sg_destroy_sampler(sampler_);
    if (pipeline_.id) sg_destroy_pipeline(pipeline_);
    if (shader_.id) sg_destroy_shader(shader_);
    if (color_texture_view_.id) sg_destroy_view(color_texture_view_);
    if (depth_attachment_.id) sg_destroy_view(depth_attachment_);
    if (color_attachment_.id) sg_destroy_view(color_attachment_);
    if (depth_.id) sg_destroy_image(depth_);
    if (color_.id) sg_destroy_image(color_);
    sampler_ = {};
    pipeline_ = {};
    shader_ = {};
    color_texture_view_ = {};
    depth_attachment_ = {};
    color_attachment_ = {};
    depth_ = {};
    color_ = {};
}

} // namespace od
