#pragma once

#include "port/scene.h"

#include <sokol_gfx.h>

#include <cstddef>
#include <string>

namespace od {

// Viewer-only first GPU material path. It owns one static actor mesh and a
// square offscreen color/depth target sampled by the ImGui preview pane.
class ModelPreview {
public:
    bool init(std::string& error);
    bool load(const port::PreviewActor& actor, std::string& error);
    void draw() const;
    void clear_model();
    void shutdown();
    bool has_model() const { return vertices_.id != 0 && vertex_count_ != 0; }
    sg_view texture_view() const { return color_texture_view_; }

private:
    sg_image color_{};
    sg_image depth_{};
    sg_view color_attachment_{};
    sg_view depth_attachment_{};
    sg_view color_texture_view_{};
    sg_shader shader_{};
    sg_pipeline pipeline_{};
    sg_sampler sampler_{};
    sg_buffer vertices_{};
    sg_image material_{};
    sg_view material_view_{};
    size_t vertex_count_ = 0;
};

} // namespace od
