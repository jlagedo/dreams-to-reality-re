#include "render/text.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

extern const unsigned char od_text_font_data[];
extern const std::size_t od_text_font_size;

namespace od {
uint32_t text_cp1252(uint8_t code) {
    static const uint16_t high[32] = {
        0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
        0x2039, 0x0152, 0,      0x017d, 0,      0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022,
        0x2013, 0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};
    if (code < 0x20)
        return 0;
    if (code >= 0x80 && code < 0xa0)
        return high[code - 0x80];
    return code;
}
bool build_text_atlas(float cap_height, TextAtlas &out, std::string &error) {
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, od_text_font_data, stbtt_GetFontOffsetForIndex(od_text_font_data, 0))) {
        error = "embedded text font does not parse";
        return false;
    }
    int x0, y0, x1, y1;
    if (!stbtt_GetCodepointBox(&font, 'H', &x0, &y0, &x1, &y1) || y1 <= 0 || !(cap_height > 0)) {
        error = "text font has no capital H";
        return false;
    }
    const float scale = cap_height / float(y1);
    // Shelf packing, 1 texel of clear padding round each glyph.
    const int width = std::clamp(int(std::ceil(cap_height * 24)), 64, 4096);
    int pen_x = 1, pen_y = 1, shelf = 0;
    out = TextAtlas{};
    struct Pending {
        int code, glyph, w, h, bx, by;
    };
    std::vector<Pending> pending;
    for (int code = 0; code < 256; ++code) {
        const uint32_t unicode = text_cp1252(uint8_t(code));
        const int glyph = unicode ? stbtt_FindGlyphIndex(&font, int(unicode)) : 0;
        if (!glyph || unicode == ' ')
            continue;
        int bx0, by0, bx1, by1;
        stbtt_GetGlyphBitmapBox(&font, glyph, scale, scale, &bx0, &by0, &bx1, &by1);
        const int w = bx1 - bx0, h = by1 - by0;
        if (w <= 0 || h <= 0)
            continue;
        if (pen_x + w + 1 > width) {
            pen_x = 1;
            pen_y += shelf + 1;
            shelf = 0;
        }
        auto &g = out.glyphs[size_t(code)];
        g.present = true;
        g.x = pen_x;
        g.y = pen_y;
        g.width = w;
        g.height = h;
        g.left = float(bx0);
        g.top = float(by0);
        int gx0, gy0, gx1, gy1;
        stbtt_GetGlyphBox(&font, glyph, &gx0, &gy0, &gx1, &gy1);
        g.ink_centre = (gx0 + gx1) * 0.5f * scale;
        pending.push_back({code, glyph, w, h, pen_x, pen_y});
        pen_x += w + 1;
        shelf = std::max(shelf, h);
    }
    out.width = width;
    out.height = pen_y + shelf + 1;
    std::vector<unsigned char> coverage(size_t(out.width) * out.height);
    for (const auto &p : pending)
        stbtt_MakeGlyphBitmap(&font, coverage.data() + size_t(p.by) * out.width + p.bx, p.w, p.h,
                              out.width, scale, scale, p.glyph);
    out.rgba.resize(coverage.size());
    for (size_t i = 0; i < coverage.size(); ++i)
        out.rgba[i] = 0x00ffffffu | (uint32_t(coverage[i]) << 24);
    return true;
}
} // namespace od
