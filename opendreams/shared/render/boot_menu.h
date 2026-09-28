#pragma once

#include "port/boot_menu.h"
#include "port/sprite.h"

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ImDrawList;
struct ImVec2;

namespace od {

// GPU destination for the retail menu's eight INTERF corners and the
// TEXT_Print glyphs of font slots 0 (HI640) and 1 (HI480). The source remains
// SpriteState; this adapter owns only uploaded textures.
class BootMenuCanvas {
public:
    static constexpr size_t font_count = 2;
    bool load(const port::SpriteState& sprites, std::string& error);
    void draw(ImDrawList* list, const port::BootMenuState& state,
              ImVec2 origin, float scale) const;
    void draw_text(ImDrawList* list, const port::MenuTextLine& line,
                   ImVec2 origin, float scale) const;
    void shutdown();

private:
    struct Picture {
        sg_image image{};
        sg_view view{};
        int width = 0;
        int height = 0;
        int anchor_x = 0;
        int anchor_y = 0;
    };
    static bool convert(const port::SpriteSet& set,
                        const port::SpriteDescriptor& descriptor,
                        std::vector<uint8_t>& rgba, std::string& error,
                        bool dim = false);
    static bool upload(const port::SpriteSet& set,
                       const port::SpriteDescriptor& descriptor,
                       Picture& picture, std::string& error);
    struct Glyph {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int anchor_x = 0; // descriptor +0x0c
        int anchor_y = 0; // descriptor +0x10
    };
    // One atlas per font for normal and darkened TEXT_Print rows.
    struct Font {
        std::array<Picture, 2> atlas{};
        std::array<Glyph, 256> glyphs{};
        std::array<int32_t, 256> advances{};
    };
    bool load_font(const port::SpriteSet& set, const port::FontState& state,
                   Font& font, std::string& error);
    std::array<port::MenuCorner, 8> placement_{};
    std::array<Picture, 8> corners_{};
    std::array<Font, font_count> fonts_{};
};

} // namespace od
