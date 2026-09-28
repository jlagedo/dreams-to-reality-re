#pragma once

#include "port/scene.h"
#include "port/glide_model.h"

#include <sokol_gfx.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace od {

struct ModelView {
    float yaw = 0.78539816f;
    float pitch = 0.34906585f;
    float distance = 3.5f;
    float target[3]{};
    bool explicit_eye = false;
    float eye[3]{};
    float focal_x = 2.2f;
    float focal_y = 2.2f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
};

// Static model GPU path using the DREAMSFX face submission contract. It owns
// one actor mesh and an offscreen color/depth target for the viewer pane.
class ModelPreview {
public:
    bool init(std::string& error, int width = 512, int height = 512);
    bool load(const port::PreviewActor& actor, std::string& error);
    bool load(const port::ModelGraph& graph, std::string& error);
    bool update_pose(const port::ModelGraph& graph, std::string& error);
    void draw(const ModelView& view) const;
    bool project_joint(const std::array<int32_t,3>& world_xyz,
                       const ModelView& view, float& u, float& v) const;
    std::array<float,3> world_to_view(const std::array<int32_t,3>& xyz) const;
    float scaled_distance(float source_units) const { return source_units * frame_scale_; }
    void clear_model();
    void shutdown();
    bool has_model() const { return vertices_.id != 0 && !draw_.batches.empty(); }
    sg_view texture_view() const { return color_texture_view_; }

private:
    sg_image color_{};
    sg_image depth_{};
    sg_view color_attachment_{};
    sg_view depth_attachment_{};
    sg_view color_texture_view_{};
    sg_shader shader_{};
    sg_pipeline opaque_pipeline_{};
    sg_pipeline alpha_pipeline_{};
    sg_sampler clamp_sampler_{};
    sg_sampler wrap_sampler_{};
    sg_buffer vertices_{};
    struct GpuTexture {
        size_t material = 0;
        uint32_t row = 0;
        sg_image image{};
        sg_view view{};
    };
    std::vector<GpuTexture> textures_;
    std::vector<size_t> batch_textures_;
    port::GlideModelDraw draw_;
    float frame_scale_ = 1.0f;
    float frame_center_[3]{};
    bool dynamic_vertices_ = false;
};

} // namespace od
