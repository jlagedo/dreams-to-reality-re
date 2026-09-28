#include "render/boot_menu.h"

#include <imgui.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <vector>

namespace od {
namespace {

ImVec2 point(ImVec2 origin, float scale, float x, float y) {
    return {origin.x + scale * x, origin.y + scale * y};
}

} // namespace

bool BootMenuCanvas::upload(const port::SpriteSet& set,
                            const port::SpriteDescriptor& descriptor,
                            Picture& picture, std::string& error) {
    if (descriptor.status != port::SpriteSlotStatus::loaded ||
        descriptor.width == 0 || descriptor.height == 0 ||
        descriptor.width > 2048 || descriptor.height > 2048) {
        error = "menu sprite descriptor is unavailable";
        return false;
    }
    const size_t count = static_cast<size_t>(descriptor.width) * descriptor.height;
    if (descriptor.retail_buffer.size() < count * set.bytes_per_pixel) {
        error = "menu sprite pixels are truncated";
        return false;
    }
    std::vector<uint8_t> rgba(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t index = descriptor.retail_buffer[i * set.bytes_per_pixel];
        const uint16_t color = set.palette[index];
        rgba[4*i] = static_cast<uint8_t>((((color >> 10) & 31) * 255) / 31);
        rgba[4*i+1] = static_cast<uint8_t>((((color >> 5) & 31) * 255) / 31);
        rgba[4*i+2] = static_cast<uint8_t>(((color & 31) * 255) / 31);
        uint8_t alpha = index == 0 ? 0 : 255;
        if (alpha && set.bytes_per_pixel == 2) {
            const uint8_t coverage = descriptor.retail_buffer[i * 2 + 1];
            alpha = coverage >= 63 ? 255 :
                static_cast<uint8_t>(((coverage >> 1) * 255) / 32);
        }
        rgba[4*i+3] = alpha;
    }
    sg_image_desc image{};
    image.width = static_cast<int>(descriptor.width);
    image.height = static_cast<int>(descriptor.height);
    image.pixel_format = SG_PIXELFORMAT_RGBA8;
    image.data.mip_levels[0].ptr = rgba.data();
    image.data.mip_levels[0].size = rgba.size();
    image.label = "retail menu sprite";
    picture.image = sg_make_image(&image);
    sg_view_desc view{};
    view.texture.image = picture.image;
    picture.view = sg_make_view(&view);
    if (sg_query_image_state(picture.image) != SG_RESOURCESTATE_VALID ||
        sg_query_view_state(picture.view) != SG_RESOURCESTATE_VALID) {
        error = "GPU upload of a menu sprite failed";
        return false;
    }
    picture.width = image.width;
    picture.height = image.height;
    picture.anchor_x = descriptor.field_0c;
    picture.anchor_y = descriptor.field_10;
    return true;
}

bool BootMenuCanvas::load(const port::SpriteState& sprites, std::string& error) {
    shutdown();
    error.clear();
    if (!port::MENU_PlaceCornerIcons(640, 480, placement_)) {
        error = "retail menu corner names could not be resolved";
        return false;
    }
    for (size_t i = 0; i < corners_.size(); ++i) {
        const auto& item = placement_[i];
        const auto* bank = sprites.icon_bank(static_cast<size_t>(item.bank));
        if (!bank || static_cast<size_t>(item.slot) >= bank->descriptors.size() ||
            !upload(*bank, bank->descriptors[static_cast<size_t>(item.slot)],
                    corners_[i], error)) {
            shutdown();
            return false;
        }
    }
    const auto* font = sprites.font(0);
    const auto* set = sprites.set(0);
    if (!font || !font->active || !set) {
        error = "HI640 menu font is unavailable";
        shutdown();
        return false;
    }
    advances_ = font->advances;
    std::array<bool, 256> needed{};
    for (int choice = 0; choice < 4; ++choice) {
        port::BootMenuState selected;
        selected.selected = choice;
        for (unsigned char ch : port::MENU_SelectedLabel(selected))
            if (ch != ' ') needed[ch] = true;
    }
    for (size_t ch = 0; ch < needed.size(); ++ch) {
        if (!needed[ch]) continue;
        if (ch >= set->descriptors.size() ||
            !upload(*set, set->descriptors[ch], glyphs_[ch], error)) {
            shutdown();
            return false;
        }
    }
    return true;
}

void BootMenuCanvas::draw(ImDrawList* list, const port::BootMenuState& state,
                          ImVec2 origin, float scale) const {
    if (!list || scale <= 0.0f) return;
    for (size_t i = 0; i < 4; ++i) {
        const size_t appearance = i == static_cast<size_t>(state.selected) ? i + 4 : i;
        const Picture& picture = corners_[appearance];
        const port::MenuCorner& place = placement_[appearance];
        if (!picture.view.id) continue;
        // SPR_BlitSprite uses (x - descriptor+0x0c, y - descriptor+0x10).
        // The north INTERF halves are 63 high with +0x10 == -1, so this
        // closes their one-row seam against the south halves at y=149.
        const int x = place.x - picture.anchor_x;
        const int y = place.y - picture.anchor_y;
        list->AddImage(simgui_imtextureid(picture.view),
                       point(origin, scale, static_cast<float>(x),
                             static_cast<float>(y)),
                       point(origin, scale, static_cast<float>(x + picture.width),
                             static_cast<float>(y + picture.height)));
    }
    const std::string_view label = port::MENU_SelectedLabel(state);
    // MENU_Draw at 0x435f65 pushes x=0x626e10 (50 at 640) and
    // y=0x626e14 + 24/scaleY (369 at 480) into TEXT_Print.
    float x = 50.0f;
    constexpr float y = 369.0f;
    for (unsigned char ch : label) {
        const Picture& glyph = glyphs_[ch];
        if (glyph.view.id) {
            list->AddImage(simgui_imtextureid(glyph.view),
                           point(origin, scale, x, y),
                           point(origin, scale, x + glyph.width, y + glyph.height));
        }
        x += static_cast<float>(std::max(0, advances_[ch]));
    }
}

void BootMenuCanvas::shutdown() {
    const auto release = [](Picture& picture) {
        if (picture.view.id) sg_destroy_view(picture.view);
        if (picture.image.id) sg_destroy_image(picture.image);
        picture = {};
    };
    for (auto& picture : corners_) release(picture);
    for (auto& picture : glyphs_) release(picture);
    placement_ = {};
    advances_ = {};
}

} // namespace od
