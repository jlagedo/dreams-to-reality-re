#pragma once

#include "port/boot_menu.h"
#include "port/sprite.h"

#include <sokol_gfx.h>

#include <array>
#include <cstdint>
#include <string>

struct ImDrawList;
struct ImVec2;

namespace od {

// GPU destination for the retail menu's eight INTERF corners and HI640 glyphs.
// The source remains SpriteState; this adapter owns only uploaded textures.
class BootMenuCanvas {
public:
    bool load(const port::SpriteState& sprites, std::string& error);
    void draw(ImDrawList* list, const port::BootMenuState& state,
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
    static bool upload(const port::SpriteSet& set,
                       const port::SpriteDescriptor& descriptor,
                       Picture& picture, std::string& error);
    std::array<port::MenuCorner, 8> placement_{};
    std::array<Picture, 8> corners_{};
    std::array<Picture, 256> glyphs_{};
    std::array<int32_t, 256> advances_{};
};

} // namespace od
