#pragma once
#include "render_scene.h"

namespace wd {
struct UiRegisters {
    uint32_t eax = 0, edx = 0, ebx = 0, ecx = 0, esi = 0, edi = 0, esp = 0, ebp = 0;
};
struct UiMemoryWrite {
    uint32_t address, value, bytes;
};
struct UiBatch {
    uint32_t target_address = 0;
    od_draw_kind kind = OD_DRAW_KEEP;
    od_pixel_format format = OD_RGB565;
    od_rect rect{};
    uint32_t parameter = 0;
    std::vector<uint32_t> pixels; // source RGB565 plus normalized coverage
    std::vector<uint32_t> lookup; // immutable blend-table window, indices -3072..4095
    std::vector<UiMemoryWrite> metadata;
};
// Pixel-producing leaves only. Reads CPU sources/controls, never destination
// pixels; returns a GPU operation plus exact-width non-pixel scratch stores.
bool normalize_sprite(SceneReader, const UiRegisters &, bool faded, UiBatch &, std::string &);
bool normalize_gauge(SceneReader, const UiRegisters &, UiBatch &, std::string &);
bool normalize_masked64(SceneReader, const UiRegisters &, UiBatch &, std::string &);
} // namespace wd
