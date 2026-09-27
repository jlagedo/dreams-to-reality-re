#include "render/glide_compat.h"

#include <algorithm>

namespace od {
namespace {
bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}
}

GlideCompat::GlideCompat(uint32_t width, uint32_t height)
    : width_(width), height_(height), clip_right_(width), clip_bottom_(height),
      back_(static_cast<size_t>(width) * height),
      front_(static_cast<size_t>(width) * height) {}

uint32_t GlideCompat::lfb_pitch_bytes() const { return width_ * 2; }

bool GlideCompat::grLfbLock(LfbInfo& info, std::string& error) {
    error.clear();
    info = {};
    if (locked_) return fail(error, "Glide back buffer is already locked");
    if (!width_ || !height_) return fail(error, "Glide back buffer has no pixels");
    locked_ = true;
    info = {back_.data(), lfb_pitch_bytes()};
    return true;
}

bool GlideCompat::grLfbUnlock(std::string& error) {
    error.clear();
    if (!locked_) return fail(error, "Glide back buffer is not locked");
    locked_ = false;
    return true;
}

bool GlideCompat::grClipWindow(uint32_t left, uint32_t top, uint32_t right,
                               uint32_t bottom, std::string& error) {
    error.clear();
    if (left > right || top > bottom || right > width_ || bottom > height_)
        return fail(error, "Glide clip window is outside the back buffer");
    clip_left_ = left;
    clip_top_ = top;
    clip_right_ = right;
    clip_bottom_ = bottom;
    return true;
}

bool GlideCompat::grBufferClear(uint16_t rgb565, std::string& error) {
    error.clear();
    for (uint32_t y = clip_top_; y < clip_bottom_; ++y)
        std::fill(back_.begin() + static_cast<size_t>(y) * width_ + clip_left_,
                  back_.begin() + static_cast<size_t>(y) * width_ + clip_right_, rgb565);
    return true;
}

bool GlideCompat::grBufferSwap(std::string& error) {
    error.clear();
    if (locked_) return fail(error, "Glide swap while back buffer is locked");
    front_ = back_; // Freeze the frame before another lock can modify it.
    ++frame_number_;
    return true;
}

} // namespace od
