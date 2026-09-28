#include "port/palette_lighting.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace od::port {
namespace {

int32_t s32(const uint8_t* bytes) {
    const uint32_t value = static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
    int32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

// 32-bit two's-complement arithmetic as the retail IMUL/ADD produce it.
int32_t wrap(int64_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    int32_t result = 0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::string upper_ascii(std::string_view input) {
    std::string result(input);
    for (char& ch : result)
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - ('a' - 'A'));
    return result;
}

// C strncmp over NUL-terminated views of the host strings.
int strncmp_cells(std::string_view a, std::string_view b, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const unsigned char ca = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
        const unsigned char cb = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
        if (ca != cb) return ca < cb ? -1 : 1;
        if (!ca) return 0;
    }
    return 0;
}

void mark_dirty(PaletteLightingState& state, size_t material, unsigned row) {
    if (material < state.dirty_rows.size()) state.dirty_rows[material] |= 1u << row;
}

void apply_row(PaletteLightingState& state, const LightingSlot& slot,
               int32_t red, int32_t green, int32_t blue, unsigned row,
               std::vector<ModelMaterial>& materials) {
    for (const size_t material : slot.materials) {
        if (material >= materials.size() || materials[material].bank.size() != 0x18014u)
            continue;
        REND_ApplyPaletteOffsets(state.add_clamp, red, green, blue, slot.source,
                                 materials[material].bank.data() + palette_row_offset(row));
        mark_dirty(state, material, row);
    }
}

} // namespace

void REND_InitAddClampTable(std::vector<uint8_t>& table) {
    table.resize(0x10000u);
    for (uint32_t index = 0; index < 0x10000u; ++index) {
        const int32_t delta = static_cast<int8_t>(static_cast<uint8_t>(index >> 8));
        const int32_t value = static_cast<int32_t>(index & 0xffu) + delta * 2;
        table[index] = static_cast<uint8_t>(std::clamp(value, 0, 255));
    }
}

void REND_ApplyPaletteOffsets(const std::vector<uint8_t>& add_clamp,
                              int32_t red, int32_t green, int32_t blue,
                              const std::array<uint32_t, 256>& source,
                              uint8_t* row) {
    const auto delta = [](int32_t value) {
        return static_cast<uint32_t>(static_cast<uint8_t>(std::clamp(value, -0x7f, 0x7f))) << 8;
    };
    const uint32_t dr = delta(red), dg = delta(green), db = delta(blue);
    for (size_t i = 0; i < 256; ++i) {
        const uint32_t color = source[i];
        const uint32_t b = add_clamp[db | (color & 0xffu)];
        const uint32_t g = add_clamp[dg | ((color >> 8) & 0xffu)];
        const uint32_t r = add_clamp[dr | ((color >> 16) & 0xffu)];
        const uint32_t packed = (b >> 3) | ((g & 0xfcu) << 3) | ((r & 0xf8u) << 8);
        row[i * 4] = 0;
        row[i * 4 + 1] = 0;
        row[i * 4 + 2] = static_cast<uint8_t>(packed);
        row[i * 4 + 3] = static_cast<uint8_t>(packed >> 8);
    }
}

void init_level_palette(PaletteLightingState& state, const uint8_t* record,
                        size_t graph_materials) {
    state.scale_actor = 8;
    state.scale_nonactor = 2;
    state.underwater = false;
    if (s32(record + 0xc0)) state.scale_actor = s32(record + 0xc0);
    if (s32(record + 0xc4)) state.scale_nonactor = s32(record + 0xc4);
    REND_InitAddClampTable(state.add_clamp);
    for (auto& slot : state.slots) slot.flags = 0; // 0x42d7d3
    state.effect_lights_active = false;             // ENT_ResetEffectLights
    state.actors.clear();
    state.dirty_rows.assign(graph_materials, 0);
    state.dirty_pages.assign(graph_materials, 0);
}

void load_level_palette(PaletteLightingState& state, const uint8_t* record) {
    state.duration = s32(record + 0xfc);
    for (size_t c = 0; c < 3; ++c) {
        state.target[c] = s32(record + 0xf0 + c * 4);
        state.ambient[c] = s32(record + 0x30 + c * 4);
        state.base[c] = s32(record + 0x18 + c * 4);
        state.underwater_bias[c] = s32(record + 0xe0 + c * 4);
        state.variation[c] = s32(record + 0x24 + c * 4);
    }
    state.underwater_range = s32(record + 0xec);
    state.mode = s32(record + 0x138);
    state.project_c8 = s32(record + 0xc8);
    state.contrast = s32(record + 0x1e8);
    state.contrast_filter = s32(record + 0x1ec);
    // 0x41fc70..0x41fcd8, after DSN_LoadTextures step 0.
    if (state.mode == 0) {
        state.mode_triplet = {0x40, 0x40, 0x40};
        state.current = {0x80, 0x80, 0x80};
    }
    if (state.mode == 1) {
        state.mode_triplet = {-0x40, -0x40, -0x40};
        state.current = {-0x80, -0x80, -0x80};
    }
}

const MaterialCacheEntry* MDL_FindMaterial(const std::vector<MaterialCacheEntry>& cache,
                                           std::string_view name) {
    for (const auto& entry : cache)
        if (entry.name == name) return &entry;
    return nullptr;
}

bool MDL_RegisterLightingMaterial(PaletteLightingState& state,
                                  const std::vector<MaterialCacheEntry>& cache,
                                  const std::vector<ModelMaterial>& materials,
                                  std::string_view name) {
    if (name.empty()) return false;
    const std::string upper = upper_ascii(name);
    const MaterialCacheEntry* entry = MDL_FindMaterial(cache, upper);
    if (!entry || entry->materials.empty() ||
        entry->materials.front() >= materials.size()) return false;
    for (auto& slot : state.slots) {
        if (slot.flags != 0) continue;
        slot.name = upper;
        slot.materials = entry->materials;
        slot.flags |= 2u;
        MDL_BuildPaletteColorTable(state, slot, materials[entry->materials.front()].bank);
        return true;
    }
    return false;
}

void MDL_BuildPaletteColorTable(const PaletteLightingState& state,
                                LightingSlot& slot, const std::vector<uint8_t>& bank) {
    const bool sized = bank.size() == 0x18014u;
    const bool contrast = state.contrast != 0 && (state.contrast_filter == 0 ||
        strncmp_cells(upper_ascii(slot.name), upper_ascii(state.scene_actor_name), 3) == 0);
    const auto stretch = [&](uint32_t channel) {
        // IDIV by 64 truncates toward zero.
        const int32_t scaled = wrap(static_cast<int64_t>(static_cast<int32_t>(channel) - 0x7f) *
                                    state.contrast) / 64 + 0x7f;
        return static_cast<uint32_t>(std::clamp(scaled, 0, 0xff));
    };
    for (size_t i = 0; i < 256; ++i) {
        const size_t at = palette_row_offset(15) + i * 4 + 2;
        const uint32_t color = sized ? static_cast<uint32_t>(bank[at]) |
            (static_cast<uint32_t>(bank[at + 1]) << 8) : 0;
        const uint32_t blue = (color & 0x1fu) * 8u;
        uint32_t value = blue | ((color & 0x7e0u) << 5) | ((color & 0xf800u) << 8);
        if (contrast) {
            const uint32_t red = stretch((color & 0xf800u) >> 8);
            const uint32_t green = stretch((color & 0x7e0u) >> 3);
            value = stretch(blue) | (green << 8) | (red << 16);
        }
        slot.source[i] = value;
    }
}

void MDL_BindActorPalette(PaletteLightingState& state, size_t actor_index) {
    if (actor_index >= state.actors.size()) return;
    PaletteActor& actor = state.actors[actor_index];
    actor.name = upper_ascii(actor.name);
    const size_t length = actor.name.size();
    for (auto& slot : state.slots) {
        if (strncmp_cells(slot.name, actor.name, length) != 0) continue;
        slot.flags |= 0x10u;
        slot.actor = static_cast<int>(actor_index);
        if (state.mode == 1) {
            actor.fallback = 0;
            actor.offset = {-0x40, -0x40, -0x40};
        } else {
            actor.fallback = 0xffffu;
            actor.offset = {0x40, 0x40, 0x40};
        }
    }
}

void REND_UpdatePaletteRows(PaletteLightingState& state, size_t slot_index,
                            int32_t red, int32_t green, int32_t blue,
                            std::vector<ModelMaterial>& materials) {
    if (slot_index >= state.slots.size()) return;
    const LightingSlot& slot = state.slots[slot_index];
    const int32_t step = static_cast<int32_t>(state.cursor + 1);
    const unsigned rolling_row = 31u - (state.cursor & 31u);
    const unsigned neutral_row = state.variant == PaletteRowVariant::dreamsfx ? 15u : 16u;
    const bool rebuild_all = state.textures_pending || state.exit_countdown > 0.0f;
    const auto rolling = [&](int32_t r, int32_t g, int32_t b, int32_t scale) {
        const int32_t delta = wrap(static_cast<int64_t>(step) * scale) >> 2;
        apply_row(state, slot, wrap(int64_t{r} + delta), wrap(int64_t{g} + delta),
                  wrap(int64_t{b} + delta), rolling_row, materials);
    };
    const auto full = [&](int32_t r, int32_t g, int32_t b, int32_t scale) {
        for (int32_t i = 0; i < 32; ++i) {
            const int32_t delta = wrap(static_cast<int64_t>(i) * scale) >> 2;
            apply_row(state, slot, wrap(int64_t{r} + delta), wrap(int64_t{g} + delta),
                      wrap(int64_t{b} + delta), 31u - static_cast<unsigned>(i), materials);
        }
    };
    if (slot.flags & 0x10u) {
        if (slot.actor < 0 || static_cast<size_t>(slot.actor) >= state.actors.size()) return;
        const auto& offset = state.actors[static_cast<size_t>(slot.actor)].offset;
        const int32_t scale = state.scale_actor;
        if (!state.effect_lights_active) {
            rolling(offset[0], offset[1], offset[2], scale);
            apply_row(state, slot, offset[0], offset[1], offset[2], neutral_row, materials);
        } else if (rebuild_all) {
            full(offset[0], offset[1], offset[2], scale);
        } else {
            rolling(offset[0], offset[1], offset[2], scale);
        }
        return;
    }
    const int32_t scale = state.scale_nonactor;
    if (!state.effect_lights_active) {
        if (scale != 0) rolling(red, green, blue, scale);
        if (state.variant == PaletteRowVariant::dreamsfx && state.project_c8 != 0) {
            // 0x3fe43..0x3ff2e: (C + 12 * S) >> 2 per channel.
            const int32_t twelve = wrap(int64_t{scale} * 12);
            apply_row(state, slot, wrap(int64_t{red} + twelve) >> 2,
                      wrap(int64_t{green} + twelve) >> 2,
                      wrap(int64_t{blue} + twelve) >> 2, neutral_row, materials);
        } else {
            apply_row(state, slot, red, green, blue, neutral_row, materials);
        }
        return;
    }
    if (scale == 0) return;
    if (rebuild_all) full(red, green, blue, scale);
    else rolling(red, green, blue, scale);
}

void SCENE_InitPaletteLighting(PaletteLightingState& state,
                               std::vector<ModelMaterial>& materials) {
    // FILD +0xfc, FMUL 30.0 (0x4c504c), FSTP float.
    state.remaining = static_cast<float>(static_cast<double>(state.duration) * 30.0);
    if (std::fabs(state.remaining) != 0.0f) {
        for (size_t c = 0; c < 3; ++c) {
            state.accumulator[c] = static_cast<float>(state.base[c]);
            state.rate[c] = static_cast<float>(
                (static_cast<double>(static_cast<float>(state.target[c])) -
                 static_cast<double>(state.accumulator[c])) /
                static_cast<double>(state.remaining));
        }
    }
    if (state.effect_lights_active) state.current = {0, 0, 0};
    state.cursor = 0;
    for (size_t slot = 0; slot < state.slots.size(); ++slot)
        if (state.slots[slot].flags & 2u)
            REND_UpdatePaletteRows(state, slot, state.current[0], state.current[1],
                                   state.current[2], materials);
}

void REND_TickPaletteLighting(PaletteLightingState& state,
                              std::vector<ModelMaterial>& materials,
                              WatcomRandState& random) {
    std::array<int32_t, 3> goal{};
    for (size_t c = 0; c < 3; ++c) {
        const uint32_t draw = static_cast<uint32_t>(rand_(random)) & 0x7fu;
        if (!state.underwater) {
            goal[c] = wrap(int64_t{state.base[c]} +
                           (wrap(int64_t{draw} * state.variation[c]) >> 7));
        } else {
            const int32_t range = wrap(int64_t{state.variation[c]} + state.underwater_range);
            goal[c] = wrap(int64_t{state.base[c]} + state.underwater_bias[c] +
                           (wrap(int64_t{draw} * range) >> 7));
        }
    }
    for (size_t c = 0; c < 3; ++c)
        state.current[c] = wrap(int64_t{state.current[c]} +
                                (wrap(int64_t{goal[c]} - state.current[c]) >> 3));
    for (size_t slot = 0; slot < state.slots.size(); ++slot)
        if (state.slots[slot].flags & 2u)
            REND_UpdatePaletteRows(state, slot, state.current[0], state.current[1],
                                   state.current[2], materials);
    state.cursor = (state.cursor + 1u) & 0x1fu;
}

void ENT_AdaptActorColor(PaletteLightingState& state, PaletteActor& actor,
                         const std::array<uint32_t, 4>& samples) {
    uint32_t sample = 0;
    if (!samples[0] || !samples[1] || !samples[2] || !samples[3]) {
        sample = actor.fallback;
    } else {
        const uint32_t mask = 0xfefefeu;
        const uint32_t left = (((samples[0] & mask) + (samples[1] & mask)) >> 1) & mask;
        const uint32_t right = (((samples[2] & mask) + (samples[3] & mask)) >> 1) & mask;
        sample = (left + right) >> 1;
    }
    actor.fallback = sample;
    int shift = 8;
    if (state.project_c8 != 0 || state.contrast != 0) {
        sample = 0;
        shift = 2;
    }
    if (state.exit_countdown > 0.0f) shift = 1;
    const int32_t channels[3]{
        static_cast<int32_t>((sample >> 16) & 0xffu),
        static_cast<int32_t>((sample >> 8) & 0xffu),
        static_cast<int32_t>(sample & 0xffu)};
    for (size_t c = 0; c < 3; ++c)
        actor.offset[c] = wrap(int64_t{wrap(int64_t{channels[c]} - state.ambient[c]) >> shift} +
                               actor.offset[c]) >> 1;
}

namespace {

uint32_t load32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
}

void store32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
    bytes[2] = static_cast<uint8_t>(value >> 16);
    bytes[3] = static_cast<uint8_t>(value >> 24);
}

std::string cell16(const uint8_t* bytes) {
    size_t used = 0;
    while (used < 16 && bytes[used]) ++used;
    return std::string(reinterpret_cast<const char*>(bytes), used);
}

constexpr size_t page_offset = 0x8014u;
constexpr size_t page_size = 0x10000u;

bool has_page(const std::vector<ModelMaterial>& materials, size_t material) {
    return material < materials.size() && materials[material].bank.size() == 0x18014u;
}

// Runs one page effect on the slot's canonical bank and mirrors the page to
// the other graph copies of the same retail bank.
template <typename Effect>
void apply_page_effect(PaletteLightingState& state, const LightingSlot& slot,
                       std::vector<ModelMaterial>& materials, Effect effect) {
    size_t first = SIZE_MAX;
    for (const size_t material : slot.materials)
        if (has_page(materials, material)) { first = material; break; }
    if (first == SIZE_MAX) return;
    uint8_t* page = materials[first].bank.data() + page_offset;
    effect(page);
    for (const size_t material : slot.materials) {
        if (!has_page(materials, material)) continue;
        if (material != first)
            std::copy(page, page + page_size, materials[material].bank.begin() + page_offset);
        if (material < state.dirty_pages.size()) state.dirty_pages[material] = 1;
    }
}

} // namespace

void page_scroll_0x403fb9(uint8_t* page) {
    std::array<uint8_t, 0x200> held{};                 // 0x4f7510
    std::copy(page, page + 0x200, held.begin());
    std::memmove(page, page + 0x200, 0xfe00);          // REP MOVSD 0x3f80
    std::copy(held.begin(), held.end(), page + 0xfe00);
}

void page_blend_seed_0x403f3b(PaletteLightingState& state, const uint8_t* page) {
    state.blend_shifted.assign(page, page + page_size);
    state.blend_rolled.assign(page, page + page_size);
}

void page_blend_0x403f68(PaletteLightingState& state, uint8_t* page) {
    if (state.blend_shifted.size() != page_size) state.blend_shifted.assign(page_size, 0);
    if (state.blend_rolled.size() != page_size) state.blend_rolled.assign(page_size, 0);
    // 0x403ef0: 0x4e7510..+0x10100 moves down 0x100 bytes; the top 0x100 of
    // the source is 0x4f7510, which just received the first row.
    auto& rolled = state.blend_rolled;
    std::array<uint8_t, 0x100> first_row{};
    std::copy(rolled.begin(), rolled.begin() + 0x100, first_row.begin());
    std::memmove(rolled.data(), rolled.data() + 0x100, page_size - 0x100);
    std::copy(first_row.begin(), first_row.end(), rolled.end() - 0x100);
    // The shifted copy plus the one BSS byte that follows it in memory.
    auto& shifted = state.blend_shifted;
    const auto byte_at = [&](size_t at) -> uint8_t {
        return at < page_size ? shifted[at] : state.blend_gap;
    };
    for (size_t row = 0; row < 256; ++row) {
        const size_t base = row * 256;
        const uint8_t head = shifted[base]; // DH
        for (size_t dword = 0; dword < 64; ++dword) {
            const size_t at = base + dword * 4;
            const uint32_t next = static_cast<uint32_t>(byte_at(at + 1)) |
                (static_cast<uint32_t>(byte_at(at + 2)) << 8) |
                (static_cast<uint32_t>(byte_at(at + 3)) << 16) |
                (static_cast<uint32_t>(byte_at(at + 4)) << 24);
            store32(shifted.data() + at, next);
            const uint32_t sum = next + load32(rolled.data() + at);
            store32(page + at, (sum & 0xfefefefeu) >> 1);
        }
        shifted[base + 255] = head;
    }
}

bool SCENE_StartPageScroll(PaletteLightingState& state,
                           const std::vector<MaterialCacheEntry>& cache,
                           const uint8_t* record) {
    const std::string name = cell16(record + 0x6c);
    if (name.empty()) return false;
    const MaterialCacheEntry* entry = MDL_FindMaterial(cache, name);
    if (!entry || entry->materials.empty()) return false;
    for (auto& slot : state.slots) {
        if (slot.name != name) continue; // strcmp_ over all 64 names
        slot.materials = entry->materials;
        slot.flags |= 0x20u;
        return true;
    }
    return false;
}

void page_scroll_tick_0x42e7fb(PaletteLightingState& state,
                               std::vector<ModelMaterial>& materials) {
    for (const auto& slot : state.slots)
        if (slot.flags & 0x20u)
            apply_page_effect(state, slot, materials, page_scroll_0x403fb9);
}

void page_blend_tick_0x42e856(PaletteLightingState& state,
                              std::vector<ModelMaterial>& materials) {
    for (const auto& slot : state.slots)
        if (slot.flags & 0x40u)
            apply_page_effect(state, slot, materials,
                              [&](uint8_t* page) { page_blend_0x403f68(state, page); });
}

void palette_lighting_tick_0x42f024(PaletteLightingState& state,
                                    std::vector<ModelMaterial>& materials,
                                    WatcomRandState& random, float dt) {
    if (std::fabs(state.remaining) != 0.0f) {
        state.remaining = static_cast<float>(static_cast<double>(state.remaining) - dt);
        if (state.remaining < 0.0f) state.remaining = 0.0f;
        for (size_t c = 0; c < 3; ++c) {
            state.accumulator[c] = static_cast<float>(
                static_cast<double>(state.rate[c]) * dt + state.accumulator[c]);
            state.base[c] = static_cast<int32_t>(std::trunc(state.accumulator[c])); // __CHP
        }
    }
    // 0x42ef2e (effect-light motion, 0x6262f4) is not ported.
    if (state.page_scroll_enabled) page_scroll_tick_0x42e7fb(state, materials);
    if (state.page_blend_enabled) page_blend_tick_0x42e856(state, materials);
    // 0x4a0f74 (static 1): 0x42dd54, actors with +0xa9 & 2 in active-list order.
    for (auto& actor : state.actors)
        if (actor.adapts) ENT_AdaptActorColor(state, actor, {0, 0, 0, 0});
    REND_TickPaletteLighting(state, materials, random);
}

std::vector<MaterialCacheEntry> build_material_cache(
    const ModelGraph& graph, const std::vector<MaterialGroup>& groups,
    const std::vector<std::string>& excluded_names) {
    std::vector<MaterialCacheEntry> cache;
    std::vector<std::string> excluded;
    for (const auto& name : excluded_names) excluded.push_back(upper_ascii(name));
    std::vector<uint8_t> seen(graph.materials.size());
    const auto add = [&](size_t material) {
        if (material >= graph.materials.size() || seen[material]) return;
        seen[material] = 1;
        const std::string name = upper_ascii(graph.materials[material].name);
        if (name.empty() ||
            std::find(excluded.begin(), excluded.end(), name) != excluded.end()) return;
        const auto found = std::find_if(cache.begin(), cache.end(),
            [&](const MaterialCacheEntry& entry) { return entry.name == name; });
        if (found != cache.end()) found->materials.push_back(material);
        else cache.push_back({name, {material}});
    };
    for (const auto& group : groups) {
        const size_t end = std::min(group.first + group.count, graph.materials.size());
        if (group.face_order) {
            for (const auto& face : graph.faces)
                if (face.material_index >= group.first && face.material_index < end)
                    add(face.material_index);
        } else {
            for (size_t material = group.first; material < end; ++material) add(material);
        }
    }
    return cache;
}

} // namespace od::port
