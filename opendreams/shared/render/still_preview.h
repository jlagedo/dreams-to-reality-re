#pragma once

#include "inspect/still_preview.h"

#include <sokol_gfx.h>

#include <cstdint>
#include <string>

namespace od {

class StillPreview {
public:
    bool load(const inspect::StillImage& image, uint32_t palette_row,
              bool transparent_zero, std::string& error);
    void clear();
    bool has_image() const { return image_view_.id != 0; }
    sg_view image_view() const { return image_view_; }
    sg_view palette_view() const { return palette_view_; }

private:
    sg_image image_{};
    sg_view image_view_{};
    sg_image palette_{};
    sg_view palette_view_{};
};

} // namespace od
