#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace od {

// The movie-reachable part of the 3dfx LFB contract. This is a transfer
// surface, not a CPU rasterizer for scene geometry.
class GlideCompat {
public:
    struct LfbInfo {
        uint16_t* pixels = nullptr;
        uint32_t pitch_bytes = 0;
    };

    GlideCompat(uint32_t width = 640, uint32_t height = 480);

    bool grLfbLock(LfbInfo& info, std::string& error);
    bool grLfbUnlock(std::string& error);
    bool grClipWindow(uint32_t left, uint32_t top, uint32_t right,
                      uint32_t bottom, std::string& error);
    bool grBufferClear(uint16_t rgb565, std::string& error);
    bool grBufferSwap(std::string& error);

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t lfb_pitch_bytes() const;
    uint64_t frame_number() const { return frame_number_; }
    const std::vector<uint16_t>& front_buffer() const { return front_; }
    bool locked() const { return locked_; }

private:
    uint32_t width_ = 0, height_ = 0;
    uint32_t clip_left_ = 0, clip_top_ = 0, clip_right_ = 0, clip_bottom_ = 0;
    std::vector<uint16_t> back_, front_;
    uint64_t frame_number_ = 0;
    bool locked_ = false;
};

} // namespace od
