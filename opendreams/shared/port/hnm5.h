#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

// Retail HNM5 uses two 640x304 indexed frames and a palette-expanded RGB565
// lookup for its LFB blit. One decoder belongs to one open UBB2/UBS2 file.
class Hnm5Decoder {
public:
    Hnm5Decoder(uint16_t width = 640, uint16_t height = 304)
        : width_(width), height_(height) {}
    bool update_palette(const uint8_t* payload, size_t size, std::string& error);
    bool decode(const uint8_t* payload, size_t size,
                std::vector<uint16_t>& rgb565, std::string& error);
    void expand(std::vector<uint16_t>& rgb565) const;

private:
    std::array<std::vector<uint8_t>, 2> indexed_;
    std::array<uint16_t, 256> palette_{};
    unsigned previous_ = 0;
    uint16_t width_ = 640, height_ = 304;
};

} // namespace od::port
