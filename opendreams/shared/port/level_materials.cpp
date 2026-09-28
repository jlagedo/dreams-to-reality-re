#include "port/level_materials.h"

#include "port/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace od::port {
namespace {

bool fail(std::string& error, std::string message) {
    error = std::move(message);
    return false;
}

std::string cell(const uint8_t* bytes, size_t length) {
    size_t used = 0;
    while (used < length && bytes[used]) ++used;
    return std::string(reinterpret_cast<const char*>(bytes), used);
}

std::string upper_ascii(std::string_view input) {
    std::string result(input);
    for (char& ch : result)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
    return result;
}

void seed_decoder(AnimTextureBinding& binding, const PaletteLightingState& lighting) {
    if (!binding.video || binding.slot >= lighting.slots.size()) return;
    std::array<uint8_t, 768> seed{};
    const auto& source = lighting.slots[binding.slot].source;
    for (size_t i = 0; i < 256; ++i) {
        seed[i * 3] = static_cast<uint8_t>(source[i] >> 16);
        seed[i * 3 + 1] = static_cast<uint8_t>(source[i] >> 8);
        seed[i * 3 + 2] = static_cast<uint8_t>(source[i]);
    }
    binding.video->hnm4().seed_palette(seed);
}

// MGM 0x17 (VID_Open) and 0x18. A failed open leaves the slot flagged and
// the page showing its authored texture.
bool open_anim(AnimTextureBinding& binding, const PaletteLightingState& lighting,
               std::string& error) {
    binding.open = false;
    if (!binding.video) return true;
    VideoError video_error;
    if (VID_Open(*binding.video, binding.path, video_error) == 0 ||
        binding.video->family() != VideoFamily::hnm4) {
        VID_Close(*binding.video);
        error = video_error.message;
        return true;
    }
    seed_decoder(binding, lighting);
    binding.open = true;
    return true;
}

} // namespace

bool bind_anim_texture_0x42dae2(AnimTextureBinding& binding, PaletteLightingState& lighting,
                                const std::vector<MaterialCacheEntry>& cache,
                                std::vector<ModelMaterial>& materials,
                                const uint8_t* record,
                                std::shared_ptr<const disc::Image> scene_image,
                                std::string& error) {
    error.clear();
    binding.material_name = cell(record + 0x4c, 16);
    binding.file_name = cell(record + 0x5c, 16);
    binding.bound = false;
    binding.open = false;
    binding.pixels_dirty.assign(materials.size(), 0);
    if (binding.material_name.empty()) return true;
    const MaterialCacheEntry* entry = MDL_FindMaterial(cache, binding.material_name);
    if (!entry || entry->materials.empty()) return true; // MDL_FindMaterial returned 0
    for (size_t slot = 0; slot < lighting.slots.size(); ++slot) {
        // strcmp over all 64 names; the flags are not tested.
        if (lighting.slots[slot].name != binding.material_name) continue;
        lighting.slots[slot].materials = entry->materials;
        binding.pixel_materials = entry->materials; // 0x42db9e
        lighting.slots[slot].flags |= 1u;
        binding.slot = slot;
        binding.bound = true;
        // sprintf("%sdata\\anim\\%s", FILE_GetInstallRoot(), +0x5c).
        binding.path = "DATA/ANIM/" + upper_ascii(binding.file_name);
        // 0x42dbeb: MGM 0x17/0x18 only while palette lighting is on.
        if (!lighting.palette_lighting_enabled) return true;
        if (!scene_image) return fail(error, "animated material has no scene disc");
        binding.vfs = std::make_unique<VfsContext>(std::move(scene_image));
        binding.video = std::make_unique<VideoState>(*binding.vfs, false);
        std::string open_error;
        return open_anim(binding, lighting, open_error);
    }
    return true;
}

void retarget_effect_page_0x42dfa0(AnimTextureBinding& binding, PaletteLightingState& lighting,
                                   const std::vector<MaterialCacheEntry>& cache,
                                   const std::vector<ModelMaterial>& materials,
                                   const uint8_t* record) {
    const std::string name = cell(record + 0x7c, 16);
    if (name.empty()) return;
    const MaterialCacheEntry* entry = MDL_FindMaterial(cache, name);
    if (!entry || entry->materials.empty()) return;
    for (auto& slot : lighting.slots) {
        if (slot.name != name) continue;
        slot.materials = entry->materials;
        binding.pixel_materials = entry->materials; // 0x42e05e
        binding.pixels_retargeted = true;
        slot.flags |= 0x40u;
        // 0x403f3b seeds both blend copies from the page.
        for (const size_t material : entry->materials) {
            if (material >= materials.size() || materials[material].bank.size() != 0x18014u)
                continue;
            page_blend_seed_0x403f3b(lighting, materials[material].bank.data() + 0x8014);
            break;
        }
        return;
    }
}

bool SCENE_HasAnimTexture(const PaletteLightingState& lighting) {
    if (!lighting.palette_lighting_enabled) return false; // 0x4a0f68
    for (const auto& slot : lighting.slots)
        if (slot.flags & 1u) return true;
    return false;
}

bool anim_texture_decode_frame(AnimTextureBinding& binding, PaletteLightingState& lighting,
                               std::vector<ModelMaterial>& materials, std::string& error) {
    error.clear();
    if (!binding.open || !binding.video) return true;
    VideoStep step;
    VideoError video_error;
    if (!VID_DecodeFrame(*binding.video, step, video_error))
        return fail(error, "HNM4 " + binding.path + ": " + video_error.message);
    ++binding.decoded_frames;
    const Hnm4Decoder& decoder = binding.video->hnm4();
    if (step.palette_chunks && binding.slot < lighting.slots.size()) {
        // 0x42ed30: PL runs land in the slot source as channel << 2 (BGR0).
        auto& source = lighting.slots[binding.slot].source;
        const auto& palette = decoder.palette();
        for (size_t i = 0; i < 256; ++i)
            source[i] = (static_cast<uint32_t>(palette[i * 3]) << 16) |
                        (static_cast<uint32_t>(palette[i * 3 + 1]) << 8) |
                        palette[i * 3 + 2];
        REND_UpdatePaletteRows(lighting, binding.slot, 0, 0, 0, materials);
    }
    if (step.image_ready && decoder.texture().size() == 0x10000u) {
        // 0x44da42 deinterlaces straight into the 256x256 page at bank +0x8014.
        for (const size_t material : binding.pixel_materials) {
            if (material >= materials.size() || materials[material].bank.size() != 0x18014u)
                continue;
            std::copy(decoder.texture().begin(), decoder.texture().end(),
                      materials[material].bank.begin() + 0x8014);
            if (material < binding.pixels_dirty.size()) binding.pixels_dirty[material] = 1;
        }
    }
    if (step.ended) {
        // VID_Close, MGM 0x3f; GAME_TickFrame reopens when 0x42da77 sees a
        // flag-1 slot. The slot palette keeps its PL colours.
        VID_Close(*binding.video);
        binding.open = false;
        if (!SCENE_HasAnimTexture(lighting)) return true;
        ++binding.reopen_count;
        std::string open_error;
        return open_anim(binding, lighting, open_error);
    }
    return true;
}

LevelMaterialSetup level_material_setup(const PreviewLevelContext& level,
                                        size_t, size_t) {
    LevelMaterialSetup setup;
    setup.record = level.project_record().data();
    setup.scene_image = level.scene_image();
    const auto& slots = level.material_cache().slots;
    for (size_t slot = 0; slot < slots.size(); ++slot) {
        // Entries zeroed by RES_InitArena have an empty name and are skipped
        // by MDL_RegisterLightingMaterial.
        if (!slots[slot].refcount || slots[slot].record.name.empty()) continue;
        LevelCacheSlot entry;
        entry.slot = slot;
        entry.name = slots[slot].record.name;
        entry.page = slots[slot].page == MaterialPage::loaded;
        if (entry.page) entry.bank = slots[slot].bank;
        setup.cache_slots.push_back(std::move(entry));
    }
    setup.scene_actor_name = cell(setup.record + 0x600 + 0xc, 16);
    return setup;
}

bool LevelMaterialSession::start(const ModelGraph& graph, const LevelMaterialSetup& setup,
                                 std::string& error) {
    error.clear();
    clear();
    if (!setup.record) return fail(error, "level materials need a project record");
    std::memcpy(record_.data(), setup.record, record_.size());
    scene_image_ = setup.scene_image;
    materials_ = graph.materials;
    if (!setup.cache_slots.empty()) {
        // Retail pointer identity: every graph copy of a cache entry's bank.
        // Entries no drawn face uses get an undrawn host copy so that their
        // lighting slot is taken in retail order.
        cache_.clear();
        for (const auto& slot : setup.cache_slots) {
            MaterialCacheEntry entry{upper_ascii(slot.name), {}};
            if (slot.page) {
                for (size_t material = 0; material < graph.materials.size(); ++material)
                    if (graph.materials[material].cache_slot == slot.slot)
                        entry.materials.push_back(material);
                if (entry.materials.empty()) {
                    entry.materials.push_back(materials_.size());
                    materials_.push_back(slot.bank);
                    materials_.back().cache_slot = slot.slot;
                }
            }
            cache_.push_back(std::move(entry));
        }
    } else {
        cache_ = build_material_cache(graph, setup.cache_groups, setup.excluded_names);
    }
    lighting_ = {};
    init_level_palette(lighting_, record_.data(), materials_.size());
    load_level_palette(lighting_, record_.data());
    lighting_.scene_actor_name = setup.scene_actor_name;
    // SCENE_LoadLevel 0x41fd0a: every cache entry, first free slot of 64.
    for (const auto& entry : cache_)
        MDL_RegisterLightingMaterial(lighting_, cache_, materials_, entry.name);
    lighting_.actors = setup.actors;
    for (size_t actor = 0; actor < lighting_.actors.size(); ++actor)
        MDL_BindActorPalette(lighting_, actor);
    SCENE_InitPaletteLighting(lighting_, materials_);
    // DREAMSFX SCENE_LoadLevel 0x296ff, after GLIDE_UploadAllTextures.
    SCENE_SetFog(fog_, water_, record_.data(), 1.0, setup.fog_mode_151d3c);
    anim_.pixels_dirty.assign(materials_.size(), 0);
    active_ = true;
    return true;
}

bool LevelMaterialSession::tick(bool decode_hnm4, std::string& error) {
    error.clear();
    if (!active_) return true;
    if (!anim_started_) {
        // GAME_StartLevel (0x42f166) on the first gameplay GAME_Tick:
        // SCENE_StartAnimTexture, SCENE_StartPageScroll, SCENE_StartPageBlend.
        anim_started_ = true;
        if (!bind_anim_texture_0x42dae2(anim_, lighting_, cache_, materials_, record_.data(),
                                        scene_image_, error)) return false;
        SCENE_StartPageScroll(lighting_, cache_, record_.data());
        retarget_effect_page_0x42dfa0(anim_, lighting_, cache_, materials_, record_.data());
    }
    if (decode_hnm4 && !anim_texture_decode_frame(anim_, lighting_, materials_, error))
        return false;
    palette_lighting_tick_0x42f024(lighting_, materials_, random_, 1.0f);
    ++ticks_;
    return true;
}

bool LevelMaterialSession::advance(double elapsed_seconds, std::string& error) {
    error.clear();
    if (!active_) return true;
    const double elapsed = std::clamp(elapsed_seconds, 0.0, 0.25);
    constexpr double epsilon = 1e-9;
    tick_clock_ += elapsed;
    anim_.clock += elapsed;
    bool decode_budget = true;
    int ticks = 0;
    while (tick_clock_ + epsilon >= tick_seconds && ticks < max_ticks_per_frame) {
        tick_clock_ -= tick_seconds;
        bool decode = false;
        if (decode_budget && anim_.clock + epsilon >= hnm4_seconds) {
            // One 0x3b per pump; elapsed periods beyond one are dropped.
            anim_.clock = std::fmod(anim_.clock + epsilon, hnm4_seconds);
            decode_budget = false;
            decode = true;
        }
        if (!tick(decode, error)) return false;
        ++ticks;
    }
    if (ticks == max_ticks_per_frame) tick_clock_ = std::fmod(tick_clock_, tick_seconds);
    if (tick_clock_ < 0.0) tick_clock_ = 0.0;
    return true;
}

void LevelMaterialSession::clear() {
    if (anim_.video) VID_Close(*anim_.video);
    anim_.video.reset();
    anim_.vfs.reset();
    anim_ = {};
    materials_.clear();
    cache_.clear();
    lighting_ = {};
    fog_ = {};
    water_ = {};
    random_ = {};
    tick_clock_ = 0.0;
    ticks_ = 0;
    anim_started_ = false;
    active_ = false;
    record_.fill(0);
    scene_image_.reset();
}

uint32_t LevelMaterialSession::take_dirty_rows(size_t material) {
    if (material >= lighting_.dirty_rows.size()) return 0;
    const uint32_t rows = lighting_.dirty_rows[material];
    lighting_.dirty_rows[material] = 0;
    return rows;
}

bool LevelMaterialSession::take_dirty_pixels(size_t material) {
    bool dirty = false;
    if (material < anim_.pixels_dirty.size() && anim_.pixels_dirty[material]) {
        anim_.pixels_dirty[material] = 0;
        dirty = true;
    }
    if (material < lighting_.dirty_pages.size() && lighting_.dirty_pages[material]) {
        lighting_.dirty_pages[material] = 0;
        dirty = true;
    }
    return dirty;
}

} // namespace od::port
