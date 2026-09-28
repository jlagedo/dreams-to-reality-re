#pragma once

#include "port/fog.h"
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

// The retail projection fixes its horizontal focal length; a destination of
// another aspect keeps that term and derives the vertical one.
inline ModelView with_horizontal_focal(ModelView view, float focal_x,
                                       int width, int height) {
    if (width > 0 && height > 0) {
        view.focal_x = focal_x;
        view.focal_y = focal_x * static_cast<float>(width) / static_cast<float>(height);
    }
    return view;
}

// Viewer orbit framing keeps the chosen focal length on the shorter side, so
// a model stays inside a wide or tall preview pane.
inline ModelView fit_to_target(ModelView view, int width, int height) {
    if (width > 0 && height > 0) {
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        if (aspect >= 1.0f) view.focal_x = view.focal_y / aspect;
        else view.focal_y = view.focal_x * aspect;
    }
    return view;
}

// Static model GPU path using the DREAMSFX face submission contract. It owns
// one actor mesh and a color/depth destination. ODViewer sizes that target to
// its preview pane and ODRuntime to the whole drawable, both in physical
// pixels; Shell still owns the one swapchain pass, commit and present.
//
// Materials are uploaded as 8-bit index pages (128x128 for DAN models, the
// 256x256 scene page for DSN banks) plus one palette texture holding the 32
// rows of every material, so palette lighting and HNM4 updates rewrite rows
// or pages instead of re-expanding RGBA textures. Each batch selects its
// row as GLIDE_DrawObjectFaces bound it.
class ModelPreview {
public:
    bool init(std::string& error, int width = 512, int height = 512);
    bool load(const port::PreviewActor& actor, std::string& error);
    bool load(const port::ModelGraph& graph, std::string& error);
    bool update_pose(const port::ModelGraph& graph, std::string& error);
    // Recreates the destination attachments when the size changes.
    bool resize_target(int width, int height, std::string& error);
    // Stage palette rows (bit r = row r) of one material from its bank.
    void update_palette_rows(size_t material, uint32_t rows,
                             const std::vector<uint8_t>& bank);
    // Stage the 256x256 page of one material (an animated material).
    void update_material_pixels(size_t material, const std::vector<uint8_t>& bank);
    // DREAMSFX table fog (SCENE_SetFog / GLIDE_SetFog state) and the
    // scan-out gamma of grGammaCorrectionValue (out = in^(1/gamma)).
    void set_fog(const port::GlideFogState& fog) { fog_ = fog; }
    void set_output_gamma(float glide_gamma) {
        output_exponent_ = glide_gamma > 0.0f ? 1.0f / glide_gamma : 1.0f;
    }
    // Uploads staged palette rows and pages (at most one sg_update_image per
    // image), then renders. Call once per sokol frame.
    void draw(const ModelView& view);
    bool project_joint(const std::array<int32_t,3>& world_xyz,
                       const ModelView& view, float& u, float& v) const;
    std::array<float,3> world_to_view(const std::array<int32_t,3>& xyz) const;
    float scaled_distance(float source_units) const { return source_units * frame_scale_; }
    void clear_model();
    void shutdown();
    bool has_model() const { return vertices_.id != 0 && !draw_.batches.empty(); }
    sg_view texture_view() const { return color_texture_view_; }
    int target_width() const { return target_width_; }
    int target_height() const { return target_height_; }

private:
    bool create_target(int width, int height, std::string& error);
    void destroy_target();
    void upload_staged();
    int target_width_ = 0;
    int target_height_ = 0;
    sg_image color_{};
    sg_image depth_{};
    sg_view color_attachment_{};
    sg_view depth_attachment_{};
    sg_view color_texture_view_{};
    sg_shader shader_{};
    sg_pipeline opaque_pipeline_{};
    sg_pipeline alpha_pipeline_{};
    sg_sampler texel_sampler_{};
    sg_buffer vertices_{};
    struct GpuMaterial {
        unsigned lod = 0;
        bool streaming = false;
        bool dirty = false;
        std::vector<uint8_t> indices;
        sg_image image{};
        sg_view view{};
    };
    std::vector<GpuMaterial> materials_;
    std::vector<uint8_t> palette_rgba_;
    bool palette_dirty_ = false;
    sg_image palette_image_{};
    sg_view palette_view_{};
    port::GlideFogState fog_;
    float output_exponent_ = 1.0f;
    port::GlideModelDraw draw_;
    float frame_scale_ = 1.0f;
    float frame_center_[3]{};
    bool dynamic_vertices_ = false;
};

} // namespace od
