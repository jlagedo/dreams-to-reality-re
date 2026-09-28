#pragma once

#include "port/model.h"
#include "port/random.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

// REND_UpdatePaletteRows exists in two builds. DREAMSFX (0x3fca4) writes the
// zero-offset neutral row at page-0x4400 (row 15) and, for non-actor slots of
// a project with +0xc8 set, offsets that row by (C + 12*S) >> 2. WINDREAM
// (0x42e8b1) writes the neutral row at page-0x4000 (row 16) with C. The GPU
// path uses the 3dfx rule, because its unlit row selection is `row = shade`
// (15) while the software rasterizer uses `31 - shade` (16).
enum class PaletteRowVariant { dreamsfx, windream };

// One 0x420-byte record of the 64-slot table at WINDREAM 0x615ad8.
struct LightingSlot {
    std::string name;                  // +0x08, upper case
    std::vector<size_t> materials;     // +0x04 page; every graph copy of it
    uint32_t flags = 0;                // +0x18: 1 HNM4, 2 lit, 8 sprite, 0x10 actor
    int actor = -1;                    // +0x1c, index into PaletteLightingState::actors
    std::array<uint32_t, 256> source{}; // +0x20 BGR0 colours seeded from row 15
};

// The fields of the retail actor record that palette code reads or writes.
struct PaletteActor {
    std::string name;                  // +0x8c
    std::array<int32_t, 3> offset{};   // +0x98/+0x9c/+0xa0 RGB
    uint32_t fallback = 0;             // +0xa4 last adapted sample
    int32_t radius = 0;                // +0x10c sampling offset
    bool adapts = false;               // +0xa9 & 2
};

// One entry of the material cache at 0x661ee0, in insertion order. Retail
// shares one bank per name; the host graph may hold several copies of it.
struct MaterialCacheEntry {
    std::string name;                  // upper case
    std::vector<size_t> materials;     // first entry is the canonical bank
};

// Source-scoped copy of the palette globals (WINDREAM addresses; DREAMSFX
// pairs in docs/lighting.md). One level at a time.
struct PaletteLightingState {
    std::array<int32_t, 3> base{};            // 0x4a0f88
    std::array<int32_t, 3> target{};          // 0x4a0f94, project +0xf0
    std::array<int32_t, 3> variation{};       // 0x4a0fa0
    std::array<int32_t, 3> current{};         // 0x4a0fac, zero at boot
    std::array<int32_t, 3> mode_triplet{};    // 0x4a0fc4, written but never read
    std::array<int32_t, 3> ambient{};         // 0x62630c/08/04
    std::array<int32_t, 3> underwater_bias{}; // 0x6262f8/f0/ec
    int32_t underwater_range = 0;             // 0x6262fc
    bool underwater = false;                  // 0x626310
    int32_t duration = 0;                     // 0x626318, project +0xfc seconds
    float remaining = 0.0f;                   // 0x626314, 30 Hz units
    std::array<float, 3> rate{};              // 0x6262dc/e0/d8
    std::array<float, 3> accumulator{};       // 0x4a0fb8..c0
    int32_t scale_actor = 8;                  // 0x6262e8
    int32_t scale_nonactor = 2;               // 0x626300
    bool effect_lights_active = false;        // 0x4a0f64
    // Statically 1 in WINDREAM's data with no writer: 0x4a0f68 gates the
    // HNM4 open (SCENE_StartAnimTexture, SCENE_HasAnimTexture); 0x4a0f70 and
    // 0x4a0f6c gate the page scroll and page blend in 0x42f024.
    bool palette_lighting_enabled = true;     // 0x4a0f68
    bool page_scroll_enabled = true;          // 0x4a0f70
    bool page_blend_enabled = true;           // 0x4a0f6c
    // The two 64 KiB page-blend copies (0x4d7410 shifted, 0x4e7510 rolled)
    // and the untouched BSS byte at 0x4e7410 that 0x403f68 reads past the
    // shifted copy's end. One pair for the whole process, as in retail.
    std::vector<uint8_t> blend_shifted;       // 0x4d7410
    std::vector<uint8_t> blend_rolled;        // 0x4e7510
    uint8_t blend_gap = 0;                    // 0x4e7410, no reader or writer
    uint32_t cursor = 0;                      // 0x4a0fd4, 5-bit rolling row
    float exit_countdown = 0.0f;              // 0x5e5480
    bool textures_pending = false;            // 0x5df49c through 0x417590
    int32_t mode = 0;                         // project +0x138
    int32_t project_c8 = 0;                   // project +0xc8
    int32_t contrast = 0;                     // project +0x1e8
    int32_t contrast_filter = 0;              // project +0x1ec
    std::string scene_actor_name;             // 0x4fbdd4: actor 2 (OBJET0) +0x8c
    PaletteRowVariant variant = PaletteRowVariant::dreamsfx;
    std::array<LightingSlot, 64> slots{};
    std::vector<PaletteActor> actors;
    std::vector<uint8_t> add_clamp;           // 0x4c7010, 64 KiB
    // Host bookkeeping: per graph material, the rows written since the
    // renderer last consumed them (bit r = row r).
    std::vector<uint32_t> dirty_rows;
    // Pages rewritten by the scroll/blend effects since the last hand-off.
    std::vector<uint8_t> dirty_pages;
};

// 0x4021e1: table[(d << 8) | c] = clamp(c + 2 * (int8)d, 0, 255).
void REND_InitAddClampTable(std::vector<uint8_t>& table);

// REND_ApplyPaletteOffsets (WINDREAM 0x4031c3, DREAMSFX 0x2f933, 565 branch):
// clamps each delta to [-127, 127], adds it twice through the clamp table to
// the BGR0 source and stores RGB565 in the high word of each 0x400-byte row
// entry (low word zero).
void REND_ApplyPaletteOffsets(const std::vector<uint8_t>& add_clamp,
                              int32_t red, int32_t green, int32_t blue,
                              const std::array<uint32_t, 256>& source,
                              uint8_t* row);

// Host split of SCENE_InitLevel (0x41f42e), palette part: scales 8/2 with project
// +0xc0/+0xc4 overrides, underwater flag clear, REND_InitAddClampTable,
// 0x42d7d3 (free all 64 slots) and ENT_ResetEffectLights.
void init_level_palette(PaletteLightingState& state, const uint8_t* record,
                            size_t graph_materials);
// Host split of SCENE_LoadLevel (0x41f9db), palette part before material registration: the
// project copies and the mode 0/1 start biases (+128 / -128). Mode 16 leaves
// `current` as the previous level left it.
void load_level_palette(PaletteLightingState& state, const uint8_t* record);

// MDL_FindMaterial (0x466040) over the host cache; nullptr when absent.
const MaterialCacheEntry* MDL_FindMaterial(const std::vector<MaterialCacheEntry>& cache,
                                           std::string_view name);
// MDL_RegisterLightingMaterial (0x42d96d): first free slot of 64 (flags == 0),
// flag 2, source seeded by MDL_BuildPaletteColorTable. False when the name is
// empty, not cached or the table is full.
bool MDL_RegisterLightingMaterial(PaletteLightingState& state,
                                  const std::vector<MaterialCacheEntry>& cache,
                                  const std::vector<ModelMaterial>& materials,
                                  std::string_view name);
// MDL_BuildPaletteColorTable (0x42e126): row 15 RGB565 -> BGR0 (R/B x8,
// G x4), with project contrast +0x1e8 ((C - 127) * k / 64 + 127, clamped);
// +0x1ec restricts contrast to slots sharing the scene actor's 3-char prefix.
void MDL_BuildPaletteColorTable(const PaletteLightingState& state,
                                LightingSlot& slot, const std::vector<uint8_t>& bank);
// MDL_BindActorPalette (0x42dc42, WINDREAM variant): every slot whose name
// starts with the actor name gets flag 0x10 and the actor; the actor offsets
// start at -64 (mode 1) or +64, its fallback sample at 0 or 0xffff.
void MDL_BindActorPalette(PaletteLightingState& state, size_t actor);
// SCENE_InitPaletteLighting (0x42d824): timed base interpolation setup, zero
// `current` when effect lights exist, one rolling pass with cursor 0. The
// 0x42ddb9 sprite slot and SCENE_AddActorEffectLights are not ported.
void SCENE_InitPaletteLighting(PaletteLightingState& state,
                               std::vector<ModelMaterial>& materials);
// REND_UpdatePaletteRows (DREAMSFX 0x3fca4 / WINDREAM 0x42e8b1).
void REND_UpdatePaletteRows(PaletteLightingState& state, size_t slot,
                            int32_t red, int32_t green, int32_t blue,
                            std::vector<ModelMaterial>& materials);
// REND_TickPaletteLighting (0x42e5f2): three rand_ calls (R, G, B), ease
// `current` by (target - current) >> 3, update every flag-2 slot, advance
// the cursor.
void REND_TickPaletteLighting(PaletteLightingState& state,
                              std::vector<ModelMaterial>& materials,
                              WatcomRandState& random);
// ENT_AdaptActorColor (0x41c0a6). `samples` are the four projected pixel
// reads as 0x00RRGGBB (WINDREAM's reader 0x4020a8 is a RET stub and
// DREAMSFX reads its 2D frame buffer); any zero sample selects the stored
// fallback +0xa4.
void ENT_AdaptActorColor(PaletteLightingState& state, PaletteActor& actor,
                         const std::array<uint32_t, 4>& samples);
// Unnamed 0x403fb9: rotates a 256x256 page up by two rows (the first 0x200
// bytes go through the 0x4f7510 buffer to the end).
void page_scroll_0x403fb9(uint8_t* page);
// Unnamed 0x403f3b: copies the page into both blend buffers.
void page_blend_seed_0x403f3b(PaletteLightingState& state, const uint8_t* page);
// Unnamed 0x403f68: 0x403ef0 rolls the 0x4e7510 copy up one row (its first
// row passes through 0x4f7510, contiguous with the copy's end); then, for each
// of 256 rows and 64 dwords, the 0x4d7410 copy shifts left one byte (reading
// the unaligned dword at +1) and the page dword becomes
// ((shifted + rolled) & 0xfefefefe) >> 1. The row's first byte wraps to its
// last byte after the row, so byte 255 of each output row averages the next
// row's first byte (the 0x4e7410 byte after the last row).
void page_blend_0x403f68(PaletteLightingState& state, uint8_t* page);
// SCENE_StartPageScroll (0x42dea3): project +0x6c resolves through
// MDL_FindMaterial and a strcmp over the 64 slot names; the slot gets the
// page and flag 0x20. False when nothing binds.
bool SCENE_StartPageScroll(PaletteLightingState& state,
                           const std::vector<MaterialCacheEntry>& cache,
                           const uint8_t* record);
// Unnamed 0x42e7fb / 0x42e856: 0x403fb9 on every flag-0x20 slot page and
// 0x403f68 on every flag-0x40 slot page, in slot order. Each retail page is
// one bank; the host writes the effect into the first graph copy and copies
// the page to the others.
void page_scroll_tick_0x42e7fb(PaletteLightingState& state,
                               std::vector<ModelMaterial>& materials);
void page_blend_tick_0x42e856(PaletteLightingState& state,
                              std::vector<ModelMaterial>& materials);
// Unnamed 0x42f024, called by GAME_Tick before the frame draw: timed base
// interpolation (dt in 30 Hz units, x87 chop), 0x42e7fb page scroll
// (0x4a0f70), 0x42e856 page blend (0x4a0f6c), 0x42dd54 ->
// ENT_AdaptActorColor for adapting actors, then REND_TickPaletteLighting.
// Effect-light motion (0x42ef2e, gated by 0x6262f4) is not ported.
void palette_lighting_tick_0x42f024(PaletteLightingState& state,
                                    std::vector<ModelMaterial>& materials,
                                    WatcomRandState& random, float dt);

// Host: approximate retail material-cache insertion order. Retail loads the
// player (SCENE_InitLevel), OBJET1..15 and then the OBJET0 scene, adding each
// model's materials in first-face-use order and sharing one bank per name.
struct MaterialGroup {
    size_t first = 0;
    size_t count = 0;
    bool face_order = false; // DSN banks enter the cache as faces bind them
};
std::vector<MaterialCacheEntry> build_material_cache(
    const ModelGraph& graph, const std::vector<MaterialGroup>& groups,
    const std::vector<std::string>& excluded_names);

// Row helpers shared with tests: row r of a bank starts at 0x14 + r * 0x400.
constexpr size_t palette_row_offset(unsigned row) { return 0x14u + row * 0x400u; }

} // namespace od::port
