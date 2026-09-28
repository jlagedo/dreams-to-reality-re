#pragma once

#include "port/fog.h"
#include "port/palette_lighting.h"
#include "port/random.h"
#include "port/video.h"
#include "port/vfs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace od::port {

class PreviewLevelContext;

// Project-bound HNM4 animated material (research-animtex). Retail keeps one
// video stream, the slot flag bit 0 and the raw page pointer 0x5e5494.
struct AnimTextureBinding {
    std::string material_name;          // project +0x4c
    std::string file_name;              // project +0x5c
    std::string path;                   // data\anim\<+0x5c> on the scene disc
    size_t slot = SIZE_MAX;             // slot with flag 1 (PL target)
    std::vector<size_t> pixel_materials; // 0x5e5494: graph copies of the page
    std::unique_ptr<VfsContext> vfs;
    std::unique_ptr<VideoState> video;
    bool bound = false;                 // 0x42dae2 matched a slot
    bool open = false;                  // VID_Open succeeded
    // SCENE_StartPageBlend overwrote 0x5e5494 after SCENE_StartAnimTexture
    // (the Project 51 ESSAI case): pixels go to the +0x7c page while PL
    // colours still go to the +0x4c slot.
    bool pixels_retargeted = false;
    uint32_t decoded_frames = 0;
    uint32_t reopen_count = 0;
    double clock = 0.0;                 // host 15 Hz accumulator
    std::vector<uint8_t> pixels_dirty;  // per graph material
};

// SCENE_StartAnimTexture (0x42dae2, DREAMSFX 0x3f290; 0x4a0f68 gates the
// open): resolve project +0x4c through the
// cache and the lighting slots, set slot flag 1, target the page and open
// data\anim\<+0x5c> (MGM 0x17 -> VID_Open, then 0x18). Returns false only
// for host errors; "no binding" is `binding.bound == false`.
bool bind_anim_texture_0x42dae2(AnimTextureBinding& binding, PaletteLightingState& lighting,
                                const std::vector<MaterialCacheEntry>& cache,
                                std::vector<ModelMaterial>& materials,
                                const uint8_t* record,
                                std::shared_ptr<const disc::Image> scene_image,
                                std::string& error);
// SCENE_StartPageBlend (0x42dfa0, +0x7c effect material): when the name
// resolves to a registered slot it sets flag 0x40, retargets 0x5e5494 to
// that page (the Project 51 ESSAI case, kept as retail does it) and seeds the
// two blend copies (0x403f3b). The per-tick blend is page_blend_0x403f68.
void retarget_effect_page_0x42dfa0(AnimTextureBinding& binding, PaletteLightingState& lighting,
                                   const std::vector<MaterialCacheEntry>& cache,
                                   const std::vector<ModelMaterial>& materials,
                                   const uint8_t* record);
// SCENE_HasAnimTexture (0x42da77): 0x4a0f68 set and any slot has flag 1.
// GAME_TickFrame's 0x3f handler reopens the HNM4 only when this is true.
bool SCENE_HasAnimTexture(const PaletteLightingState& lighting);
// One GAME_TickFrame 0x3e message: VID_DecodeFrame -> VID_DecodeHnm4Frame.
// The deinterlaced page is copied into every target bank; PL runs (0x42ed30)
// update the flagged slot's source colours and REND_UpdatePaletteRows(slot,
// 0, 0, 0). At the last frame the file is closed and reopened (0x3f).
bool anim_texture_decode_frame(AnimTextureBinding& binding, PaletteLightingState& lighting,
                               std::vector<ModelMaterial>& materials, std::string& error);

// One live entry of the level material cache, in slot order. Faces reach
// it through ModelMaterial::cache_slot; `bank` also covers entries no drawn
// face uses (shadow, shot or preview player materials), which still take a
// lighting slot in retail.
struct LevelCacheSlot {
    size_t slot = SIZE_MAX;
    std::string name;
    bool page = false;          // MDL_FindMaterial returns a non-zero page
    ModelMaterial bank;
};

struct LevelMaterialSetup {
    const uint8_t* record = nullptr;             // DREAMS.DAT project record
    std::shared_ptr<const disc::Image> scene_image;
    // The level cache in retail slot order. When empty, `cache_groups`
    // approximates it (standalone graphs without a level cache).
    std::vector<LevelCacheSlot> cache_slots;
    std::vector<MaterialGroup> cache_groups;
    std::vector<std::string> excluded_names;
    std::string scene_actor_name;                // OBJET0 asset name (0x4fbdd4)
    // Actors bound by MDL_BindActorPalette (the runtime player XH_).
    std::vector<PaletteActor> actors;
    int32_t fog_mode_151d3c = 0;                 // DREAMSFX state word, see SCENE_SetFog
};

// Setup for a loaded preview level: the level material cache in slot order
// (shadows, player, shots, OBJET1..15, OBJET0). The player arguments are
// retained for callers; the runtime player now enters through the cache.
LevelMaterialSetup level_material_setup(const PreviewLevelContext& level,
                                        size_t player_first_material = SIZE_MAX,
                                        size_t player_material_count = 0);

// Host session: the material banks of one rendered graph, the palette
// lighting state, the Glide fog state and the HNM4 binding. It advances on a
// deterministic 30 Hz game tick (retail runs GAME_Tick once per pumped frame
// with a measured dt; see adaptation notes) with at most one HNM4 frame per
// host frame and per game tick at 15 Hz.
class LevelMaterialSession {
public:
    static constexpr double tick_seconds = 1.0 / 30.0;
    static constexpr double hnm4_seconds = 1.0 / 15.0;
    static constexpr int max_ticks_per_frame = 8;

    bool start(const ModelGraph& graph, const LevelMaterialSetup& setup, std::string& error);
    // Runs the due 30 Hz ticks for one host frame.
    bool advance(double elapsed_seconds, std::string& error);
    // One game tick: an optional HNM4 frame, then 0x42f024 with dt = 1.
    bool tick(bool decode_hnm4, std::string& error);
    void clear();

    bool active() const { return active_; }
    std::vector<ModelMaterial>& materials() { return materials_; }
    const std::vector<ModelMaterial>& materials() const { return materials_; }
    const PaletteLightingState& lighting() const { return lighting_; }
    PaletteLightingState& lighting() { return lighting_; }
    const AnimTextureBinding& anim() const { return anim_; }
    const GlideFogState& fog() const { return fog_; }
    const std::vector<MaterialCacheEntry>& cache() const { return cache_; }
    const WatcomRandState& random() const { return random_; }
    uint64_t ticks() const { return ticks_; }
    // Renderer hand-off: rows and pages changed since the previous call.
    uint32_t take_dirty_rows(size_t material);
    bool take_dirty_pixels(size_t material);

private:
    std::vector<ModelMaterial> materials_;
    std::vector<MaterialCacheEntry> cache_;
    PaletteLightingState lighting_;
    AnimTextureBinding anim_;
    GlideFogState fog_;
    FogWaterPhase water_;
    WatcomRandState random_;
    double tick_clock_ = 0.0;
    uint64_t ticks_ = 0;
    bool anim_started_ = false;
    bool active_ = false;
    std::array<uint8_t, 0x2200> record_{};
    std::shared_ptr<const disc::Image> scene_image_;
};

} // namespace od::port
