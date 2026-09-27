#pragma once

#include "inspect/catalog.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port { struct PortraitSprite; }

namespace od::inspect {

// Source-scoped decoded pixels for one selected sprite, glyph or texture.
// Indexed bytes and palette rows remain separate so the viewer can inspect
// source palette entries and transparency without reopening the disc.
struct StillImage {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> indices;
    std::vector<uint8_t> coverage; // Present for two-byte .ALP texels.
    std::vector<std::array<uint8_t, 4>> colors; // 256 entries per row.
    uint32_t default_row = 0;
    bool transparent_zero = false;
    bool pyramid_commands = false;
    std::string note;

    uint32_t palette_rows() const { return static_cast<uint32_t>(colors.size() / 256); }
    bool render(uint32_t row, bool hide_zero, std::vector<uint8_t>& rgba,
                std::string& error) const;
    bool palette_rgba(uint32_t row, std::array<uint8_t, 1024>& rgba) const;
};

bool load_still_image(const Source& source, const Row& row,
                      StillImage& image, std::string& error);
bool portrait_still_image(const port::PortraitSprite& portrait,
                          StillImage& image, std::string& error);

} // namespace od::inspect
