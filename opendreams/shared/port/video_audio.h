#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

// The SD path's 256 signed deltas and two 16-bit stereo predictors.
class VideoDpcm {
public:
    void reset();
    bool decode_sd(const uint8_t* payload, size_t size,
                   std::vector<int16_t>& pcm, std::string& error);
    bool initialized() const { return initialized_; }

private:
    std::array<int16_t, 256> deltas_{};
    std::array<uint16_t, 2> predictor_{};
    bool initialized_ = false;
};

} // namespace od::port
