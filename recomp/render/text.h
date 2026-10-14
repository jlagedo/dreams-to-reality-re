// The port's TrueType text (port option, not retail): the game's bitmap font
// glyphs drawn instead from Reddit Sans Condensed Bold (SIL OFL 1.1, embedded
// by RenderCore.cmake), rasterized at the display's own resolution.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace od {
struct TextGlyph {
    bool present = false;
    int x = 0, y = 0, width = 0, height = 0; // atlas texels
    float left = 0, top = 0;                 // bitmap's top-left from the pen on the baseline, px
    float ink_centre = 0;                    // ink's horizontal centre from the pen, px
};
struct TextAtlas {
    int width = 0, height = 0;
    std::vector<uint32_t> rgba; // white, coverage in alpha
    std::array<TextGlyph, 256> glyphs{};
};
// The 256 codes of a game font as Windows-1252 (the HI*.SPR layout), as
// Unicode; 0 for codes 1252 leaves undefined.
uint32_t text_cp1252(uint8_t code);
// Every code whose Unicode the font has, with capitals cap_height px tall.
bool build_text_atlas(float cap_height, TextAtlas &out, std::string &error);
} // namespace od
