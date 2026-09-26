#include "render/background.h"

#include <background.glsl.h>

namespace od {

bool Background::init(std::string& error) {
    // One oversized triangle covers the drawable without an index buffer.
    static const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    sg_buffer_desc buffer_desc{};
    buffer_desc.data.ptr = vertices;
    buffer_desc.data.size = sizeof(vertices);
    buffer_desc.label = "background vertices";
    vertices_ = sg_make_buffer(&buffer_desc);
    shader_ = sg_make_shader(background_shader_desc(sg_query_backend()));

    sg_pipeline_desc pipeline_desc{};
    pipeline_desc.shader = shader_;
    pipeline_desc.layout.attrs[ATTR_background_position].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc.depth.write_enabled = false;
    pipeline_desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
    pipeline_desc.label = "background pipeline";
    pipeline_ = sg_make_pipeline(&pipeline_desc);

    if (sg_query_buffer_state(vertices_) != SG_RESOURCESTATE_VALID ||
        sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID ||
        sg_query_pipeline_state(pipeline_) != SG_RESOURCESTATE_VALID) {
        error = "sokol could not create the background shader or pipeline";
        return false;
    }
    return true;
}

void Background::draw() const {
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertices_;
    sg_apply_pipeline(pipeline_);
    sg_apply_bindings(&bindings);
    sg_draw(0, 3, 1);
}

void Background::shutdown() {
    if (pipeline_.id != 0) sg_destroy_pipeline(pipeline_);
    if (shader_.id != 0) sg_destroy_shader(shader_);
    if (vertices_.id != 0) sg_destroy_buffer(vertices_);
    pipeline_ = {};
    shader_ = {};
    vertices_ = {};
}

} // namespace od
