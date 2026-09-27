#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

// One instance is owned by one open HNM6/HNS6 video. The caller supplies the
// previous and destination 640x304 RGB565 frames, as at the retail IX call.
class Hnm6Decoder {
public:
    Hnm6Decoder(uint16_t width = 640, uint16_t height = 304)
        : width_(width), height_(height) {}
    bool decode(const uint8_t* payload, size_t size,
                const std::vector<uint16_t>& previous,
                std::vector<uint16_t>& destination, std::string& error);

private:
    std::array<int16_t, 64> luma_quant_{}, chroma_quant_{};
    int quality_ = 0;
    uint16_t width_ = 640, height_ = 304;
    void set_quality(int quality);
};

} // namespace od::port
