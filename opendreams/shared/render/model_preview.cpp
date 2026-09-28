#include "render/model_preview.h"

#include <model_preview.glsl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace od {
namespace {

void preview_matrix(const ModelView& view, float* matrix) {
    if (view.explicit_eye) {
        float forward[3]{view.target[0]-view.eye[0],
                         view.target[1]-view.eye[1],
                         view.target[2]-view.eye[2]};
        const float length=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+
                                     forward[2]*forward[2]);
        if (length>0.0f)
            for (float& value : forward) value/=length;
        else forward[2]=1.0f;
        float right[3]{forward[2],0.0f,-forward[0]};
        const float horizontal=std::sqrt(right[0]*right[0]+right[2]*right[2]);
        if (horizontal>0.0f) {
            right[0]/=horizontal; right[2]/=horizontal;
        } else right[0]=1.0f;
        const float up[3]{
            forward[1]*right[2]-forward[2]*right[1],
            forward[2]*right[0]-forward[0]*right[2],
            forward[0]*right[1]-forward[1]*right[0]};
        const auto eye_dot=[&](const float (&axis)[3]) {
            return axis[0]*view.eye[0]+axis[1]*view.eye[1]+
                   axis[2]*view.eye[2];
        };
        const float a=view.far_plane/(view.far_plane-view.near_plane);
        const float b=-view.far_plane*view.near_plane/
                      (view.far_plane-view.near_plane);
        const float rows[4][4]={{
            view.focal_x*right[0],view.focal_x*right[1],view.focal_x*right[2],
            -view.focal_x*eye_dot(right)},
            {view.focal_y*up[0],view.focal_y*up[1],view.focal_y*up[2],
             -view.focal_y*eye_dot(up)},
            {a*forward[0],a*forward[1],a*forward[2],b-a*eye_dot(forward)},
            {forward[0],forward[1],forward[2],-eye_dot(forward)}};
        for (size_t row=0;row<4;++row)
            for (size_t column=0;column<4;++column)
                matrix[column*4+row]=rows[row][column];
        return;
    }
    const float yaw_c=std::cos(view.yaw), yaw_s=std::sin(view.yaw);
    const float pitch_c=std::cos(view.pitch), pitch_s=std::sin(view.pitch);
    const float near_plane = view.near_plane, far_plane = view.far_plane;
    const float a = far_plane / (far_plane - near_plane);
    const float b = -far_plane * near_plane / (far_plane - near_plane);
    const float x_offset=-yaw_c*view.target[0]+yaw_s*view.target[2];
    const float y_offset=pitch_s*yaw_s*view.target[0]-
        pitch_c*view.target[1]+pitch_s*yaw_c*view.target[2];
    const float z_offset=view.distance-pitch_c*yaw_s*view.target[0]-
        pitch_s*view.target[1]-pitch_c*yaw_c*view.target[2];
    const float rows[4][4] = {
        {view.focal_x * yaw_c, 0, -view.focal_x * yaw_s,
         view.focal_x*x_offset},
        {-view.focal_y * pitch_s * yaw_s, view.focal_y * pitch_c,
         -view.focal_y * pitch_s * yaw_c, view.focal_y*y_offset},
        {a * pitch_c * yaw_s, a * pitch_s, a * pitch_c * yaw_c, a*z_offset+b},
        {pitch_c * yaw_s, pitch_s, pitch_c * yaw_c, z_offset},
    };
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            matrix[column * 4 + row] = rows[row][column];
}

// GLIDE_ConvertPalette (DREAMSFX 0x66cb0): RGB565 high word of each row
// entry, R/B x8 and G x4.
void stage_palette(std::vector<uint8_t>& rgba, size_t material, uint32_t rows,
                   const std::vector<uint8_t>& bank) {
    for (unsigned row=0; row<32; ++row) {
        if (!(rows & (1u<<row))) continue;
        uint8_t* out=rgba.data()+(material*32u+row)*256u*4u;
        for (size_t index=0; index<256; ++index) {
            const size_t at=0x14u+static_cast<size_t>(row)*0x400u+index*4u+2u;
            const uint16_t color=static_cast<uint16_t>(bank[at] | (bank[at+1]<<8));
            out[index*4]=static_cast<uint8_t>(((color>>11)&31u)*8u);
            out[index*4+1]=static_cast<uint8_t>(((color>>5)&63u)*4u);
            out[index*4+2]=static_cast<uint8_t>((color&31u)*8u);
            out[index*4+3]=255;
        }
    }
}

// The 256x256 page at bank +0x8014, point-subsampled for the 128 model LOD.
void stage_indices(std::vector<uint8_t>& indices, unsigned lod,
                   const std::vector<uint8_t>& bank) {
    const size_t step=256u/lod;
    indices.resize(static_cast<size_t>(lod)*lod);
    for (size_t y=0; y<lod; ++y)
        for (size_t x=0; x<lod; ++x)
            indices[y*lod+x]=bank[0x8014u+(y*step)*256u+x*step];
}

bool valid(sg_image image) { return sg_query_image_state(image) == SG_RESOURCESTATE_VALID; }
bool valid(sg_view view) { return sg_query_view_state(view) == SG_RESOURCESTATE_VALID; }

} // namespace

bool ModelPreview::create_target(int width, int height, std::string& error) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        error = "model target dimensions are outside the GPU range";
        return false;
    }
    sg_image_desc color_desc{};
    color_desc.usage.color_attachment = true;
    color_desc.width = width;
    color_desc.height = height;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.sample_count = 1;
    color_desc.label = "model preview color";
    color_ = sg_make_image(&color_desc);

    sg_image_desc depth_desc{};
    depth_desc.usage.depth_stencil_attachment = true;
    depth_desc.width = width;
    depth_desc.height = height;
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
    if (!valid(color_) || !valid(depth_) || !valid(color_attachment_) ||
        !valid(depth_attachment_) || !valid(color_texture_view_)) {
        destroy_target();
        error = "sokol could not create the model render target";
        return false;
    }
    target_width_ = width;
    target_height_ = height;
    return true;
}

void ModelPreview::destroy_target() {
    if (color_texture_view_.id) sg_destroy_view(color_texture_view_);
    if (depth_attachment_.id) sg_destroy_view(depth_attachment_);
    if (color_attachment_.id) sg_destroy_view(color_attachment_);
    if (depth_.id) sg_destroy_image(depth_);
    if (color_.id) sg_destroy_image(color_);
    color_texture_view_={}; depth_attachment_={}; color_attachment_={};
    depth_={}; color_={};
    target_width_ = target_height_ = 0;
}

bool ModelPreview::resize_target(int width, int height, std::string& error) {
    error.clear();
    if (width == target_width_ && height == target_height_ && color_.id) return true;
    destroy_target();
    return create_target(width, height, error);
}

bool ModelPreview::init(std::string& error, int width, int height) {
    error.clear();
    if (!create_target(width, height, error)) return false;

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
    // REND_CullFaces rejects back-facing source triangles before the 3dfx
    // object hook. Source Y is inverted for the GPU view, making the retained
    // front-facing triangle winding counter-clockwise in clip space.
    pipeline.cull_mode = SG_CULLMODE_BACK;
    pipeline.face_winding = SG_FACEWINDING_CCW;
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
    sampler.min_filter = SG_FILTER_NEAREST;
    sampler.mag_filter = SG_FILTER_NEAREST;
    sampler.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler.label = "model preview texel fetch";
    texel_sampler_ = sg_make_sampler(&sampler);

    if (sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(opaque_pipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(alpha_pipeline_) != SG_RESOURCESTATE_VALID ||
        sg_query_sampler_state(texel_sampler_) != SG_RESOURCESTATE_VALID) {
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
    frame_scale_=1.8f/extent;
    for (size_t axis=0; axis<3; ++axis)
        frame_center_[axis]=(draw_.minimum[axis]+draw_.maximum[axis])*0.5f;
    for (auto& vertex : draw_.vertices)
        for (size_t axis=0; axis<3; ++axis)
            vertex.position[axis]=(vertex.position[axis]-frame_center_[axis])*
                frame_scale_;

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
    if (graph.materials.size()*32u > 16384u) {
        error="model has too many materials for one palette texture";
        clear_model();
        return false;
    }
    materials_.resize(graph.materials.size());
    palette_rgba_.assign(graph.materials.size()*32u*256u*4u,0);
    for (size_t material=0; material<graph.materials.size(); ++material)
        if (graph.materials[material].bank.size()==0x18014u)
            stage_palette(palette_rgba_,material,0xffffffffu,graph.materials[material].bank);
    palette_dirty_=true;
    sg_image_desc palette_desc{};
    palette_desc.usage.dynamic_update=true;
    palette_desc.width=256;
    palette_desc.height=static_cast<int>(graph.materials.size()*32u);
    palette_desc.pixel_format=SG_PIXELFORMAT_RGBA8;
    palette_desc.label="model palette rows";
    palette_image_=sg_make_image(&palette_desc);
    sg_view_desc palette_view{};
    palette_view.texture.image=palette_image_;
    palette_view_=sg_make_view(&palette_view);
    if (!valid(palette_image_) || !valid(palette_view_)) {
        error="sokol could not create the model palette texture";
        clear_model();
        return false;
    }
    for (const auto& batch : draw_.batches) {
        GpuMaterial& gpu=materials_[batch.material];
        if (gpu.image.id) continue;
        const auto& material=graph.materials[batch.material];
        const unsigned lod=material.preview_lod;
        if (lod!=128 && lod!=256) {
            error="scene material has an unsupported texture size";
            clear_model();
            return false;
        }
        gpu.lod=lod;
        stage_indices(gpu.indices,lod,material.bank);
        sg_image_desc image_desc{};
        image_desc.width=static_cast<int>(lod);
        image_desc.height=static_cast<int>(lod);
        image_desc.pixel_format=SG_PIXELFORMAT_R8;
        image_desc.data.mip_levels[0].ptr=gpu.indices.data();
        image_desc.data.mip_levels[0].size=gpu.indices.size();
        image_desc.label="model P8 material indices";
        gpu.image=sg_make_image(&image_desc);
        sg_view_desc view_desc{};
        view_desc.texture.image=gpu.image;
        gpu.view=sg_make_view(&view_desc);
        if (!valid(gpu.image) || !valid(gpu.view)) {
            error="sokol could not upload model material";
            clear_model();
            return false;
        }
    }
    return true;
}

void ModelPreview::update_palette_rows(size_t material, uint32_t rows,
                                       const std::vector<uint8_t>& bank) {
    if (!rows || material>=materials_.size() || bank.size()!=0x18014u) return;
    stage_palette(palette_rgba_,material,rows,bank);
    palette_dirty_=true;
}

void ModelPreview::update_material_pixels(size_t material,
                                          const std::vector<uint8_t>& bank) {
    if (material>=materials_.size() || bank.size()!=0x18014u) return;
    GpuMaterial& gpu=materials_[material];
    if (!gpu.image.id) return; // No batch samples this material.
    stage_indices(gpu.indices,gpu.lod,bank);
    if (!gpu.streaming) {
        // DREAMSFX re-downloads animated pages on the first bind of each
        // frame; the host switches the page to a streamed image once.
        sg_destroy_view(gpu.view);
        sg_destroy_image(gpu.image);
        sg_image_desc image_desc{};
        image_desc.usage.dynamic_update=true;
        image_desc.width=static_cast<int>(gpu.lod);
        image_desc.height=static_cast<int>(gpu.lod);
        image_desc.pixel_format=SG_PIXELFORMAT_R8;
        image_desc.label="animated P8 material indices";
        gpu.image=sg_make_image(&image_desc);
        sg_view_desc view_desc{};
        view_desc.texture.image=gpu.image;
        gpu.view=sg_make_view(&view_desc);
        gpu.streaming=true;
    }
    gpu.dirty=true;
}

void ModelPreview::upload_staged() {
    if (palette_dirty_ && palette_image_.id) {
        sg_image_data data{};
        data.mip_levels[0].ptr=palette_rgba_.data();
        data.mip_levels[0].size=palette_rgba_.size();
        sg_update_image(palette_image_,&data);
        palette_dirty_=false;
    }
    for (auto& gpu : materials_) {
        if (!gpu.dirty || !gpu.streaming || !gpu.image.id) continue;
        sg_image_data data{};
        data.mip_levels[0].ptr=gpu.indices.data();
        data.mip_levels[0].size=gpu.indices.size();
        sg_update_image(gpu.image,&data);
        gpu.dirty=false;
    }
}

bool ModelPreview::update_pose(const port::ModelGraph& graph, std::string& error) {
    error.clear();
    if (!has_model()) {
        error="no model is loaded for animation";
        return false;
    }
    port::GlideModelDraw next;
    if (!port::GLIDE_DrawObjectFaces(graph,next,error)) return false;
    if (next.vertices.size()!=draw_.vertices.size() ||
        next.batches.size()!=draw_.batches.size()) {
        error="animation changed the model's face layout";
        return false;
    }
    for (size_t i=0; i<next.batches.size(); ++i) {
        const auto& a=next.batches[i];
        const auto& b=draw_.batches[i];
        if (a.first!=b.first || a.count!=b.count ||
            a.material!=b.material || a.palette_row!=b.palette_row ||
            a.mode!=b.mode) {
            error="animation changed a material draw batch";
            return false;
        }
    }
    for (auto& vertex : next.vertices)
        for (size_t axis=0; axis<3; ++axis)
            vertex.position[axis]=(vertex.position[axis]-frame_center_[axis])*
                frame_scale_;
    if (!dynamic_vertices_) {
        sg_destroy_buffer(vertices_);
        sg_buffer_desc desc{};
        desc.size=next.vertices.size()*sizeof(port::GlideModelVertex);
        desc.usage.dynamic_update=true;
        desc.label="animated model triangles";
        vertices_=sg_make_buffer(&desc);
        if (sg_query_buffer_state(vertices_)!=SG_RESOURCESTATE_VALID) {
            error="sokol could not allocate animated model vertices";
            clear_model();
            return false;
        }
        dynamic_vertices_=true;
    }
    sg_range range{next.vertices.data(),
                   next.vertices.size()*sizeof(port::GlideModelVertex)};
    sg_update_buffer(vertices_,&range);
    draw_.vertices=std::move(next.vertices);
    return true;
}

void ModelPreview::draw(const ModelView& view) {
    if (!has_model()) return;
    upload_staged();
    sg_pass pass{};
    pass.attachments.colors[0]=color_attachment_;
    pass.attachments.depth_stencil=depth_attachment_;
    pass.action.colors[0].load_action=SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value={0.045f,0.055f,0.08f,1.0f};
    pass.action.depth.load_action=SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value=1.0f;
    sg_begin_pass(&pass);
    vs_params_t vs{};
    preview_matrix(view,vs.mvp);
    vs.view_params[0]=frame_scale_>0.0f ? 1.0f/frame_scale_ : 1.0f;
    sg_range vs_uniforms{&vs,sizeof(vs)};
    fs_params_t fs{};
    // grFogColorValue takes ARGB; fog ignores the alpha byte.
    fs.fog_color[0]=static_cast<float>((fog_.color>>16)&0xffu)/255.0f;
    fs.fog_color[1]=static_cast<float>((fog_.color>>8)&0xffu)/255.0f;
    fs.fog_color[2]=static_cast<float>(fog_.color&0xffu)/255.0f;
    fs.fog_color[3]=fog_.table_mode ? 1.0f : 0.0f;
    fs.output_mode[0]=output_exponent_;
    for (size_t i=0; i<64; ++i)
        fs.fog_table[i/4][i%4]=static_cast<float>(fog_.table[i]);
    for (size_t index=0; index<draw_.batches.size(); ++index) {
        const auto& batch=draw_.batches[index];
        const bool alpha=batch.mode==port::GlideFaceMode::translucent;
        sg_apply_pipeline(alpha ? alpha_pipeline_ : opaque_pipeline_);
        sg_bindings bindings{};
        bindings.vertex_buffers[0]=vertices_;
        bindings.views[VIEW_index_tex]=materials_[batch.material].view;
        bindings.views[VIEW_palette_tex]=palette_view_;
        bindings.samplers[SMP_texel_smp]=texel_sampler_;
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(UB_vs_params,&vs_uniforms);
        fs.draw_mode[0]=batch.mode==port::GlideFaceMode::chroma ? 1.0f : 0.0f;
        fs.draw_mode[1]=alpha ? 0.5f : 1.0f;
        fs.draw_mode[2]=static_cast<float>(batch.material*32u+batch.palette_row);
        // Types 2/3 clamp; 9, -5/-6 and the deferred blocks wrap.
        fs.draw_mode[3]=batch.mode==port::GlideFaceMode::clamp ? 0.0f : 1.0f;
        sg_range fs_uniforms{&fs,sizeof(fs)};
        sg_apply_uniforms(UB_fs_params,&fs_uniforms);
        sg_draw(static_cast<int>(batch.first),static_cast<int>(batch.count),1);
    }
    sg_end_pass();
}

bool ModelPreview::project_joint(const std::array<int32_t,3>& world_xyz,
                                 const ModelView& view, float& u, float& v) const {
    if (!has_model()) return false;
    const float point[3]{
        (static_cast<float>(world_xyz[0])-frame_center_[0])*frame_scale_,
        (-static_cast<float>(world_xyz[1])-frame_center_[1])*frame_scale_,
        (static_cast<float>(world_xyz[2])-frame_center_[2])*frame_scale_};
    float matrix[16]{};
    preview_matrix(view,matrix);
    const float x=matrix[0]*point[0]+matrix[4]*point[1]+
                  matrix[8]*point[2]+matrix[12];
    const float y=matrix[1]*point[0]+matrix[5]*point[1]+
                  matrix[9]*point[2]+matrix[13];
    const float z=matrix[2]*point[0]+matrix[6]*point[1]+
                  matrix[10]*point[2]+matrix[14];
    const float w=matrix[3]*point[0]+matrix[7]*point[1]+
                  matrix[11]*point[2]+matrix[15];
    if (!(w>0.0f) || z<0.0f || z>w) return false;
    u=0.5f+0.5f*x/w;
    v=0.5f-0.5f*y/w;
    return std::isfinite(u) && std::isfinite(v);
}

std::array<float,3> ModelPreview::world_to_view(
    const std::array<int32_t,3>& xyz) const {
    return {{
        (static_cast<float>(xyz[0])-frame_center_[0])*frame_scale_,
        (-static_cast<float>(xyz[1])-frame_center_[1])*frame_scale_,
        (static_cast<float>(xyz[2])-frame_center_[2])*frame_scale_
    }};
}

void ModelPreview::clear_model() {
    for (auto& material : materials_) {
        if (material.view.id) sg_destroy_view(material.view);
        if (material.image.id) sg_destroy_image(material.image);
    }
    materials_.clear();
    if (palette_view_.id) sg_destroy_view(palette_view_);
    if (palette_image_.id) sg_destroy_image(palette_image_);
    palette_view_={};
    palette_image_={};
    palette_rgba_.clear();
    palette_dirty_=false;
    if (vertices_.id) sg_destroy_buffer(vertices_);
    vertices_={};
    draw_={};
    frame_scale_=1.0f;
    frame_center_[0]=frame_center_[1]=frame_center_[2]=0.0f;
    dynamic_vertices_=false;
}

void ModelPreview::shutdown() {
    clear_model();
    if (texel_sampler_.id) sg_destroy_sampler(texel_sampler_);
    if (alpha_pipeline_.id) sg_destroy_pipeline(alpha_pipeline_);
    if (opaque_pipeline_.id) sg_destroy_pipeline(opaque_pipeline_);
    if (shader_.id) sg_destroy_shader(shader_);
    destroy_target();
    texel_sampler_={}; alpha_pipeline_={}; opaque_pipeline_={};
    shader_={};
}

} // namespace od
