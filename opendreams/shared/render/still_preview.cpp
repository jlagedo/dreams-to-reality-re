#include "render/still_preview.h"

#include <array>
#include <vector>

namespace od {

bool StillPreview::load(const inspect::StillImage& image, uint32_t palette_row,
                        bool transparent_zero, std::string& error) {
    clear();
    std::vector<uint8_t> rgba;
    if (!image.render(palette_row,transparent_zero,rgba,error)) return false;
    std::array<uint8_t,1024> palette_rgba{};
    if (!image.palette_rgba(palette_row,palette_rgba)) {
        error = "selected image has no palette row";
        return false;
    }
    sg_image_desc picture{};
    picture.width=static_cast<int>(image.width);
    picture.height=static_cast<int>(image.height);
    picture.pixel_format=SG_PIXELFORMAT_RGBA8;
    picture.data.mip_levels[0].ptr=rgba.data();
    picture.data.mip_levels[0].size=rgba.size();
    picture.label="selected still image";
    image_=sg_make_image(&picture);
    sg_view_desc image_view{};
    image_view.texture.image=image_;
    image_view_=sg_make_view(&image_view);

    sg_image_desc swatch{};
    swatch.width=16;
    swatch.height=16;
    swatch.pixel_format=SG_PIXELFORMAT_RGBA8;
    swatch.data.mip_levels[0].ptr=palette_rgba.data();
    swatch.data.mip_levels[0].size=palette_rgba.size();
    swatch.label="selected source palette";
    palette_=sg_make_image(&swatch);
    sg_view_desc palette_view{};
    palette_view.texture.image=palette_;
    palette_view_=sg_make_view(&palette_view);
    if (sg_query_image_state(image_)!=SG_RESOURCESTATE_VALID ||
        sg_query_view_state(image_view_)!=SG_RESOURCESTATE_VALID ||
        sg_query_image_state(palette_)!=SG_RESOURCESTATE_VALID ||
        sg_query_view_state(palette_view_)!=SG_RESOURCESTATE_VALID) {
        error="sokol could not upload the selected image or palette";
        clear();
        return false;
    }
    return true;
}

void StillPreview::clear() {
    if (image_view_.id) sg_destroy_view(image_view_);
    if (image_.id) sg_destroy_image(image_);
    if (palette_view_.id) sg_destroy_view(palette_view_);
    if (palette_.id) sg_destroy_image(palette_);
    image_view_={}; image_={}; palette_view_={}; palette_={};
}

} // namespace od
