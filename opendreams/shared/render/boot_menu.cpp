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

bool BootMenuCanvas::convert(const port::SpriteSet& set,
                             const port::SpriteDescriptor& descriptor,
                             std::vector<uint8_t>& rgba, std::string& error,
                             bool dim) {
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
    rgba.assign(count * 4, 0);
    for (size_t i = 0; i < count; ++i) {
        const uint8_t index = descriptor.retail_buffer[i * set.bytes_per_pixel];
        const uint16_t color = set.palette[index];
        // A darkened TEXT_Print row halves each channel, (c & 0xF7DE) >> 1.
        const int shift = dim ? 1 : 0;
        rgba[4*i] = static_cast<uint8_t>(((((color >> 10) & 31) >> shift) * 255) / 31);
        rgba[4*i+1] = static_cast<uint8_t>(((((color >> 5) & 31) >> shift) * 255) / 31);
        rgba[4*i+2] = static_cast<uint8_t>((((color & 31) >> shift) * 255) / 31);
        uint8_t alpha = index == 0 ? 0 : 255;
        if (alpha && set.bytes_per_pixel == 2) {
            const uint8_t coverage = descriptor.retail_buffer[i * 2 + 1];
            alpha = coverage >= 63 ? 255 :
                static_cast<uint8_t>(((coverage >> 1) * 255) / 32);
        }
        rgba[4*i+3] = alpha;
    }
    return true;
}

bool BootMenuCanvas::upload(const port::SpriteSet& set,
                            const port::SpriteDescriptor& descriptor,
                            Picture& picture, std::string& error) {
    std::vector<uint8_t> rgba;
    if (!convert(set, descriptor, rgba, error)) return false;
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
    for (size_t slot = 0; slot < fonts_.size(); ++slot) {
        const auto* font = sprites.font(slot);
        const auto* set = font && font->active ? sprites.set(font->sprite_slot) : nullptr;
        if (!set || !load_font(*set, *font, fonts_[slot], error)) {
            if (error.empty())
                error = slot == 0 ? "HI640 menu font is unavailable" :
                                    "HI480 save-slot font is unavailable";
            shutdown();
            return false;
        }
    }
    return true;
}

bool BootMenuCanvas::load_font(const port::SpriteSet& set,
                               const port::FontState& state, Font& font,
                               std::string& error) {
    font.advances = state.advances;
    // Shelf-pack printable ASCII, which covers every menu label and slot name.
    constexpr int atlas_width = 1024;
    int x = 0, y = 0, shelf = 0;
    for (size_t ch = 33; ch < 127 && ch < set.descriptors.size(); ++ch) {
        const auto& descriptor = set.descriptors[ch];
        if (descriptor.status != port::SpriteSlotStatus::loaded ||
            !descriptor.width || !descriptor.height ||
            descriptor.width > atlas_width) continue;
        const int width = static_cast<int>(descriptor.width);
        const int height = static_cast<int>(descriptor.height);
        if (x + width > atlas_width) { x = 0; y += shelf + 1; shelf = 0; }
        font.glyphs[ch] = {x, y, width, height, descriptor.field_0c,
                           descriptor.field_10};
        x += width + 1;
        shelf = std::max(shelf, height);
    }
    const int atlas_height = std::max(1, y + shelf);
    for (size_t variant = 0; variant < font.atlas.size(); ++variant) {
        std::vector<uint8_t> atlas(static_cast<size_t>(atlas_width) * atlas_height * 4, 0);
        std::vector<uint8_t> rgba;
        for (size_t ch = 33; ch < 127 && ch < set.descriptors.size(); ++ch) {
            const Glyph& glyph = font.glyphs[ch];
            if (!glyph.width) continue;
            if (!convert(set, set.descriptors[ch], rgba, error, variant == 1)) return false;
            for (int row = 0; row < glyph.height; ++row)
                std::copy_n(rgba.data() + static_cast<size_t>(row) * glyph.width * 4,
                            static_cast<size_t>(glyph.width) * 4,
                            atlas.data() + (static_cast<size_t>(glyph.y + row) *
                                            atlas_width + glyph.x) * 4);
        }
        sg_image_desc image{};
        image.width = atlas_width;
        image.height = atlas_height;
        image.pixel_format = SG_PIXELFORMAT_RGBA8;
        image.data.mip_levels[0].ptr = atlas.data();
        image.data.mip_levels[0].size = atlas.size();
        image.label = variant == 1 ? "retail menu font (darkened)" : "retail menu font";
        Picture& picture = font.atlas[variant];
        picture.image = sg_make_image(&image);
        sg_view_desc view{};
        view.texture.image = picture.image;
        picture.view = sg_make_view(&view);
        if (sg_query_image_state(picture.image) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(picture.view) != SG_RESOURCESTATE_VALID) {
            error = "GPU upload of a menu font atlas failed";
            return false;
        }
        picture.width = atlas_width;
        picture.height = atlas_height;
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
    // MENU_Draw at 0x435f65 prints the selected label with font 0, dim 0 at
    // x=0x626e10 (50 at 640) and y=0x626e14 + 24/scaleY (369 at 480).
    port::MenuTextLine label;
    label.x = 50;
    label.y = 369;
    label.text = std::string(port::MENU_SelectedLabel(state));
    draw_text(list, label, origin, scale);
}

void BootMenuCanvas::draw_text(ImDrawList* list, const port::MenuTextLine& line,
                               ImVec2 origin, float scale) const {
    if (!list || scale <= 0.0f || line.font < 0 ||
        static_cast<size_t>(line.font) >= fonts_.size()) return;
    const Font& font = fonts_[static_cast<size_t>(line.font)];
    float x = static_cast<float>(line.x);
    const float y = static_cast<float>(line.y);
    const Picture& atlas = font.atlas[line.dim ? 1 : 0];
    if (!atlas.view.id) return;
    const float u_scale = 1.0f / static_cast<float>(atlas.width);
    const float v_scale = 1.0f / static_cast<float>(atlas.height);
    for (unsigned char ch : line.text) {
        const Glyph& glyph = font.glyphs[ch];
        if (glyph.width) {
            // TEXT_DrawGlyphStyled blits through SPR_BlitSprite, which draws at
            // (x - descriptor+0x0c, y - descriptor+0x10).
            const float left = x - static_cast<float>(glyph.anchor_x);
            const float top = y - static_cast<float>(glyph.anchor_y);
            list->AddImage(simgui_imtextureid(atlas.view),
                           point(origin, scale, left, top),
                           point(origin, scale, left + glyph.width, top + glyph.height),
                           {glyph.x * u_scale, glyph.y * v_scale},
                           {(glyph.x + glyph.width) * u_scale,
                            (glyph.y + glyph.height) * v_scale});
        }
        x += static_cast<float>(std::max(0, font.advances[ch]));
    }
}

void BootMenuCanvas::shutdown() {
    const auto release = [](Picture& picture) {
        if (picture.view.id) sg_destroy_view(picture.view);
        if (picture.image.id) sg_destroy_image(picture.image);
        picture = {};
    };
    for (auto& picture : corners_) release(picture);
    for (auto& font : fonts_) {
        for (auto& picture : font.atlas) release(picture);
        font = {};
    }
    placement_ = {};
}

} // namespace od
