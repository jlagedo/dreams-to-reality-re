#include "render/model_preview.h"

#include <model_preview.glsl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace od {
namespace {

constexpr int target_size = 512;

void preview_matrix(const ModelView& view, float (&matrix)[16]) {
    const float yaw_c=std::cos(view.yaw), yaw_s=std::sin(view.yaw);
    const float pitch_c=std::cos(view.pitch), pitch_s=std::sin(view.pitch);
    constexpr float focal = 2.2f, near_plane = 0.1f, far_plane = 100.0f;
    const float a = far_plane / (far_plane - near_plane);
    const float b = -far_plane * near_plane / (far_plane - near_plane);
    const float x_offset=-yaw_c*view.target[0]+yaw_s*view.target[2];
    const float y_offset=pitch_s*yaw_s*view.target[0]-
        pitch_c*view.target[1]+pitch_s*yaw_c*view.target[2];
    const float z_offset=view.distance-pitch_c*yaw_s*view.target[0]-
        pitch_s*view.target[1]-pitch_c*yaw_c*view.target[2];
    const float rows[4][4] = {
        {focal * yaw_c, 0, -focal * yaw_s, focal*x_offset},
        {-focal * pitch_s * yaw_s, focal * pitch_c,
         -focal * pitch_s * yaw_c, focal*y_offset},
        {a * pitch_c * yaw_s, a * pitch_s, a * pitch_c * yaw_c, a*z_offset+b},
        {pitch_c * yaw_s, pitch_s, pitch_c * yaw_c, z_offset},
    };
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            matrix[column * 4 + row] = rows[row][column];
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

    sg_view_desc attachment{};
    attachment.color_attachment.image = color_;
    color_attachment_ = sg_make_view(&attachment);
    attachment = {};
    attachment.depth_stencil_attachment.image = depth_;
    depth_attachment_ = sg_make_view(&attachment);
    attachment = {};
    attachment.texture.image = color_;
    color_texture_view_ = sg_make_view(&attachment);

    shader_ = sg_make_shader(model_preview_shader_desc(sg_query_backend()));
    sg_pipeline_desc pipeline{};
    pipeline.shader = shader_;
    pipeline.layout.buffers[0].stride = sizeof(port::GlideModelVertex);
    pipeline.layout.attrs[ATTR_model_preview_position].format = SG_VERTEXFORMAT_FLOAT3;
    pipeline.layout.attrs[ATTR_model_preview_texcoord].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline.layout.attrs[ATTR_model_preview_texcoord].offset = 3 * sizeof(float);
    pipeline.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    pipeline.depth.pixel_format = SG_PIXELFORMAT_DEPTH;
    pipeline.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    pipeline.depth.write_enabled = true;
    pipeline.label = "model preview opaque";
    opaque_pipeline_ = sg_make_pipeline(&pipeline);
    pipeline.colors[0].blend.enabled = true;
    pipeline.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    pipeline.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    pipeline.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
    pipeline.label = "model preview deferred alpha";
    alpha_pipeline_ = sg_make_pipeline(&pipeline);

    sg_sampler_desc sampler{};
    sampler.min_filter = SG_FILTER_LINEAR;
    sampler.mag_filter = SG_FILTER_LINEAR;
    sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler.label = "model preview clamp";
    clamp_sampler_ = sg_make_sampler(&sampler);
    sampler.wrap_u = SG_WRAP_REPEAT;
    sampler.wrap_v = SG_WRAP_REPEAT;
    sampler.label = "model preview wrap";
    wrap_sampler_ = sg_make_sampler(&sampler);

    if (!valid(color_) || !valid(depth_) || !valid(color_attachment_) ||
        !valid(depth_attachment_) || !valid(color_texture_view_) ||
        sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(opaque_pipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(alpha_pipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_sampler_state(clamp_sampler_) != SG_RESOURCESTATE_VALID ||
        sg_query_sampler_state(wrap_sampler_) != SG_RESOURCESTATE_VALID) {
        error = "sokol could not create the model preview GPU resources";
        shutdown();
        return false;
    }
    return true;
}

bool ModelPreview::load(const port::PreviewActor& actor, std::string& error) {
    if (!actor.attached_to_camera_root) {
        error="selected model is not attached to the preview root";
        return false;
    }
    return load(actor.model,error);
}

bool ModelPreview::load(const port::ModelGraph& graph, std::string& error) {
    error.clear();
    clear_model();
    if (!port::GLIDE_DrawObjectFaces(graph,draw_,error)) return false;
    float extent = 0;
    for (size_t axis=0; axis<3; ++axis)
        extent = std::max(extent,draw_.maximum[axis]-draw_.minimum[axis]);
    if (!(extent > 0)) {
        error = "selected model has zero-size geometry";
        clear_model();
        return false;
    }
    const float scale=1.8f/extent;
    for (auto& vertex : draw_.vertices)
        for (size_t axis=0; axis<3; ++axis)
            vertex.position[axis] = (vertex.position[axis]-
                (draw_.minimum[axis]+draw_.maximum[axis])*0.5f)*scale;

    sg_buffer_desc vertex_desc{};
    vertex_desc.data.ptr=draw_.vertices.data();
    vertex_desc.data.size=draw_.vertices.size()*sizeof(port::GlideModelVertex);
    vertex_desc.label="model preview triangles";
    vertices_=sg_make_buffer(&vertex_desc);
    if (sg_query_buffer_state(vertices_) != SG_RESOURCESTATE_VALID) {
        error="sokol could not upload model triangles";
        clear_model();
        return false;
    }
    for (const auto& batch : draw_.batches) {
        const auto found=std::find_if(textures_.begin(),textures_.end(),
            [&](const GpuTexture& texture) {
                return texture.material==batch.material && texture.row==batch.palette_row;
            });
        if (found != textures_.end()) {
            batch_textures_.push_back(static_cast<size_t>(found-textures_.begin()));
            continue;
        }
        std::vector<uint8_t> rgba;
        const auto& material=graph.materials[batch.material];
        const unsigned lod=material.preview_lod;
        const bool uploaded=lod==128 ?
            port::model_texture_lod(material.bank,batch.palette_row,rgba,error) :
            lod==256 ?
            port::scene_texture_page(material.bank,batch.palette_row,rgba,error) : false;
        if (!uploaded) {
            if (error.empty()) error="scene material has an unsupported texture size";
            clear_model();
            return false;
        }
        GpuTexture texture;
        texture.material=batch.material;
        texture.row=batch.palette_row;
        sg_image_desc image_desc{};
        image_desc.width=static_cast<int>(lod);
        image_desc.height=static_cast<int>(lod);
        image_desc.pixel_format=SG_PIXELFORMAT_RGBA8;
        image_desc.data.mip_levels[0].ptr=rgba.data();
        image_desc.data.mip_levels[0].size=rgba.size();
        image_desc.label="model P8 material LOD";
        texture.image=sg_make_image(&image_desc);
        sg_view_desc view_desc{};
        view_desc.texture.image=texture.image;
        texture.view=sg_make_view(&view_desc);
        textures_.push_back(texture);
        batch_textures_.push_back(textures_.size()-1);
        if (!valid(texture.image) || !valid(texture.view)) {
            error="sokol could not upload model material";
            clear_model();
            return false;
        }
    }
    return true;
}

void ModelPreview::draw(const ModelView& view) const {
    if (!has_model()) return;
    sg_pass pass{};
    pass.attachments.colors[0]=color_attachment_;
    pass.attachments.depth_stencil=depth_attachment_;
    pass.action.colors[0].load_action=SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value={0.045f,0.055f,0.08f,1.0f};
    pass.action.depth.load_action=SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value=1.0f;
    sg_begin_pass(&pass);
    float matrix[16]{};
    preview_matrix(view,matrix);
    sg_range vs_uniforms{matrix,sizeof(matrix)};
    for (size_t index=0; index<draw_.batches.size(); ++index) {
        const auto& batch=draw_.batches[index];
        const bool alpha=batch.mode==port::GlideFaceMode::translucent;
        sg_apply_pipeline(alpha ? alpha_pipeline_ : opaque_pipeline_);
        sg_bindings bindings{};
        bindings.vertex_buffers[0]=vertices_;
        bindings.views[VIEW_image_tex]=textures_[batch_textures_[index]].view;
        bindings.samplers[SMP_image_smp]=batch.mode==port::GlideFaceMode::clamp
            ? clamp_sampler_ : wrap_sampler_;
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_vs_params,&vs_uniforms);
        const std::array<float,4> mode{{
            batch.mode==port::GlideFaceMode::chroma ? 1.0f : 0.0f,
            alpha ? 0.5f : 1.0f,0.0f,0.0f}};
        sg_range fs_uniforms{mode.data(),sizeof(mode)};
        sg_apply_uniforms(UB_fs_params,&fs_uniforms);
        sg_draw(static_cast<int>(batch.first),static_cast<int>(batch.count),1);
    }
    sg_end_pass();
}

void ModelPreview::clear_model() {
    for (auto& texture : textures_) {
        if (texture.view.id) sg_destroy_view(texture.view);
        if (texture.image.id) sg_destroy_image(texture.image);
    }
    textures_.clear();
    batch_textures_.clear();
    if (vertices_.id) sg_destroy_buffer(vertices_);
    vertices_={};
    draw_={};
}

void ModelPreview::shutdown() {
    clear_model();
    if (wrap_sampler_.id) sg_destroy_sampler(wrap_sampler_);
    if (clamp_sampler_.id) sg_destroy_sampler(clamp_sampler_);
    if (alpha_pipeline_.id) sg_destroy_pipeline(alpha_pipeline_);
    if (opaque_pipeline_.id) sg_destroy_pipeline(opaque_pipeline_);
    if (shader_.id) sg_destroy_shader(shader_);
    if (color_texture_view_.id) sg_destroy_view(color_texture_view_);
    if (depth_attachment_.id) sg_destroy_view(depth_attachment_);
    if (color_attachment_.id) sg_destroy_view(color_attachment_);
    if (depth_.id) sg_destroy_image(depth_);
    if (color_.id) sg_destroy_image(color_);
    wrap_sampler_={}; clamp_sampler_={}; alpha_pipeline_={}; opaque_pipeline_={};
    shader_={}; color_texture_view_={}; depth_attachment_={}; color_attachment_={};
    depth_={}; color_={};
}

} // namespace od
