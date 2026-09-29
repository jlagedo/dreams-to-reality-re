#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/fog.h"
#include "port/level_materials.h"
#include "port/palette_lighting.h"
#include "port/random.h"
#include "port/scene.h"
#include "port/video.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace od::port;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

uint16_t row_color(const std::vector<uint8_t>& bank, unsigned row, size_t index) {
    const size_t at = palette_row_offset(row) + index * 4 + 2;
    return static_cast<uint16_t>(bank[at] | (bank[at + 1] << 8));
}

ModelMaterial make_material(const std::string& name, uint16_t seed) {
    ModelMaterial material;
    material.name = name;
    material.bank.assign(0x18014u, 0);
    for (size_t i = 0; i < 256; ++i) {
        const uint16_t color = static_cast<uint16_t>(seed + i * 0x0841u);
        const size_t at = palette_row_offset(15) + i * 4 + 2;
        material.bank[at] = static_cast<uint8_t>(color);
        material.bank[at + 1] = static_cast<uint8_t>(color >> 8);
    }
    return material;
}

uint16_t pack565(uint32_t bgr0, int delta) {
    const auto add = [&](uint32_t c) { return std::clamp(static_cast<int>(c) + 2 * delta, 0, 255); };
    const int b = add(bgr0 & 0xff), g = add((bgr0 >> 8) & 0xff), r = add((bgr0 >> 16) & 0xff);
    return static_cast<uint16_t>((b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8));
}

void test_rand() {
    WatcomRandState state;
    // Watcom 10.6 rand_ from the thread-data seed 1 (the ANSI example LCG).
    const int expected[5]{16838, 5758, 10113, 17515, 31051};
    for (int value : expected) check(rand_(state) == value, "rand_ sequence from seed 1");
    srand_(state, 1);
    check(rand_(state) == 16838, "srand_(1) restarts the sequence");
    srand_(state, 0);
    check(rand_(state) == ((0x3039u >> 16) & 0x7fff), "srand_(0) first draw");
}

void test_clamp_and_offsets() {
    std::vector<uint8_t> table;
    REND_InitAddClampTable(table);
    check(table.size() == 0x10000u, "clamp table size");
    check(table[(0x7fu << 8) | 200] == 255, "clamp table saturates high");
    check(table[(0x81u << 8) | 10] == 0, "clamp table saturates low");
    check(table[(0x01u << 8) | 5] == 7, "clamp table adds twice the delta");
    check(table[(0xffu << 8) | 5] == 3, "clamp table negative delta");
    std::array<uint32_t, 256> source{};
    source[0] = 0x00f80408u; // R 248, G 4, B 8
    source[1] = 0x00102030u;
    std::vector<uint8_t> row(0x400, 0xaa);
    REND_ApplyPaletteOffsets(table, 0, 0, 0, source, row.data());
    check(row[0] == 0 && row[1] == 0, "ApplyOffsets clears the low word");
    check((row[2] | (row[3] << 8)) == 0xf821, "ApplyOffsets packs RGB565 in the high word");
    REND_ApplyPaletteOffsets(table, 300, -300, 5, source, row.data());
    // Deltas clamp to +-127 before the table.
    const int r = std::min(0x10 + 254, 255), g = std::max(0x20 - 254, 0), b = 0x30 + 10;
    const uint16_t expected = static_cast<uint16_t>((b >> 3) | ((g & 0xfc) << 3) | ((r & 0xf8) << 8));
    check((row[6] | (row[7] << 8)) == expected, "ApplyOffsets clamps deltas to 127");
}

void test_color_table() {
    PaletteLightingState state;
    LightingSlot slot;
    ModelMaterial material = make_material("A", 0);
    const size_t at = palette_row_offset(15) + 2;
    material.bank[at] = 0xff;
    material.bank[at + 1] = 0xff;
    material.bank[at + 4] = static_cast<uint8_t>(0x6327u); // R 12, G 25, B 7
    material.bank[at + 5] = static_cast<uint8_t>(0x6327u >> 8);
    MDL_BuildPaletteColorTable(state, slot, material.bank);
    check(slot.source[0] == 0x00f8fcf8u, "row-15 565 expands R/B x8, G x4");
    check(slot.source[1] == ((12u * 8u) << 16 | (25u * 4u) << 8 | 7u * 8u), "565 channel order");
    state.contrast = 128;
    slot.name = "E11_EAU";
    MDL_BuildPaletteColorTable(state, slot, material.bank);
    check(slot.source[0] == 0x00ffffffu, "contrast saturates");
    // (96 - 127) * 128 / 64 + 127 = 65 (truncating division).
    check(((slot.source[1] >> 16) & 0xff) == 65u, "contrast stretches around 127");
    state.contrast_filter = 1;
    state.scene_actor_name = "H18ANGKR.DSN";
    MDL_BuildPaletteColorTable(state, slot, material.bank);
    check(slot.source[0] == 0x00f8fcf8u, "+0x1ec limits contrast to the scene prefix");
    state.scene_actor_name = "e11_angk.dsn";
    MDL_BuildPaletteColorTable(state, slot, material.bank);
    check(slot.source[0] == 0x00ffffffu, "+0x1ec prefix compare is upper case");
}

std::vector<uint8_t> zero_record() { return std::vector<uint8_t>(0x2200, 0); }

void put32(std::vector<uint8_t>& record, size_t offset, int32_t value) {
    std::memcpy(record.data() + offset, &value, 4);
}

void test_slots_and_rows() {
    ModelGraph graph;
    for (int i = 0; i < 70; ++i)
        graph.materials.push_back(make_material("M" + std::to_string(i),
                                                static_cast<uint16_t>(i * 97)));
    auto record = zero_record();
    put32(record, 0x138, 16); // keep `current`
    PaletteLightingState state;
    init_level_palette(state, record.data(), graph.materials.size());
    load_level_palette(state, record.data());
    check(state.scale_actor == 8 && state.scale_nonactor == 2, "default row scales");
    std::vector<MaterialGroup> groups{{0, graph.materials.size(), false}};
    const auto cache = build_material_cache(graph, groups, {});
    int registered = 0;
    for (const auto& entry : cache)
        registered += MDL_RegisterLightingMaterial(state, cache, graph.materials, entry.name);
    check(registered == 64, "64-slot cap");
    check(state.slots[63].name == "M63" && state.slots[0].name == "M0", "cache order fills slots");
    for (size_t i = 0; i < 256; ++i) {
        const uint16_t c = row_color(graph.materials[5].bank, 15, i);
        const uint32_t expected = (c & 0x1fu) * 8u | (c & 0x7e0u) << 5 | (c & 0xf800u) << 8;
        if (state.slots[5].source[i] != expected) {
            check(false, "slot seed equals row-15 expansion");
            break;
        }
    }
    const auto original = graph.materials[5].bank;
    SCENE_InitPaletteLighting(state, graph.materials);
    // Cursor 0: row 31 gets ((0+1)*2)>>2 = 0, neutral row 15 gets 0 (3dfx).
    bool ok = true;
    for (size_t i = 0; i < 256; ++i) {
        ok &= row_color(graph.materials[5].bank, 31, i) == pack565(state.slots[5].source[i], 0);
        ok &= row_color(graph.materials[5].bank, 15, i) == row_color(original, 15, i);
    }
    check(ok, "init pass writes row 31 and neutral row 15");
    check(row_color(graph.materials[5].bank, 30, 3) == 0, "other rows wait for later ticks");
    check(state.dirty_rows[5] == ((1u << 31) | (1u << 15)), "dirty rows 31 and 15");
    check(state.dirty_rows[65] == 0, "unregistered material untouched");
    // Rolling cursor 1 -> row 30 with ((1+1)*2)>>2 = 1.
    state.cursor = 1;
    state.current = {0, 0, 0};
    REND_UpdatePaletteRows(state, 5, 0, 0, 0, graph.materials);
    ok = true;
    for (size_t i = 0; i < 256; ++i)
        ok &= row_color(graph.materials[5].bank, 30, i) == pack565(state.slots[5].source[i], 1);
    check(ok, "rolling row 31-c with ((c+1)*S)>>2");
    // Windows neutral row is 16.
    state.variant = PaletteRowVariant::windream;
    REND_UpdatePaletteRows(state, 5, 10, 10, 10, graph.materials);
    check(row_color(graph.materials[5].bank, 16, 7) == pack565(state.slots[5].source[7], 10),
          "WINDREAM neutral row 16");
    // 3dfx +0xc8 quarter rule: (C + 12*S) >> 2.
    state.variant = PaletteRowVariant::dreamsfx;
    state.project_c8 = 1;
    REND_UpdatePaletteRows(state, 5, 40, 40, 40, graph.materials);
    check(row_color(graph.materials[5].bank, 15, 9) == pack565(state.slots[5].source[9], (40 + 24) >> 2),
          "+0xc8 neutral row (C + 12S) >> 2");
    state.project_c8 = 0;
    // Effect lights with loading pending rebuild all 32 rows.
    state.effect_lights_active = true;
    state.textures_pending = true;
    state.dirty_rows[5] = 0;
    REND_UpdatePaletteRows(state, 5, 0, 0, 0, graph.materials);
    check(state.dirty_rows[5] == 0xffffffffu, "full rebuild writes 32 rows");
    check(row_color(graph.materials[5].bank, 0, 11) == pack565(state.slots[5].source[11], (31 * 2) >> 2),
          "full rebuild row 31-i with (i*S)>>2");
}

void test_tick_and_interpolation() {
    ModelGraph graph;
    graph.materials.push_back(make_material("A", 0x1234));
    auto record = zero_record();
    put32(record, 0x138, 1);          // mode 1: start at -128
    put32(record, 0x24, 127);         // R variation
    put32(record, 0xfc, 1);           // one second of timed base
    put32(record, 0xf4, 30);          // target G
    PaletteLightingState state;
    init_level_palette(state, record.data(), 1);
    load_level_palette(state, record.data());
    check(state.current[0] == -128 && state.mode_triplet[1] == -64, "mode 1 start bias");
    const auto cache = build_material_cache(graph, {{0, 1, false}}, {});
    MDL_RegisterLightingMaterial(state, cache, graph.materials, "a");
    SCENE_InitPaletteLighting(state, graph.materials);
    check(state.remaining == 30.0f && state.rate[1] == 1.0f, "timed base setup x30");
    WatcomRandState random;
    WatcomRandState reference;
    palette_lighting_tick_0x42f024(state, graph.materials, random, 1.0f);
    const int draw = rand_(reference) & 0x7f;
    rand_(reference);
    rand_(reference);
    check(random.next == reference.next, "three rand_ calls per tick");
    const int goal = (draw * 127) >> 7;
    check(state.current[0] == -128 + ((goal + 128) >> 3), "ease by (t - c) >> 3");
    check(state.base[1] == 1 && state.cursor == 1, "timed base and cursor advance");
    for (int i = 0; i < 14; ++i) palette_lighting_tick_0x42f024(state, graph.materials, random, 1.0f);
    check(state.base[1] == 15, "timed base after 15 ticks");
    for (int i = 0; i < 20; ++i) palette_lighting_tick_0x42f024(state, graph.materials, random, 1.0f);
    check(state.base[1] == 30 && state.remaining == 0.0f, "timed base reaches target");
    // From below, >>3 stops 1..7 short.
    PaletteLightingState low;
    low.current = {-128, -128, -128};
    for (int i = 0; i < 200; ++i) REND_TickPaletteLighting(low, graph.materials, random);
    check(low.current[1] >= -7 && low.current[1] <= -1, "ease from below settles below target");
    check(low.cursor == (200u & 31u), "cursor wraps at 32");
}

void test_actor_palette() {
    PaletteLightingState state;
    state.slots[0].name = "XH_IMG_B";
    state.slots[0].flags = 2;
    state.slots[1].name = "WALL";
    state.slots[1].flags = 2;
    state.ambient = {152, 168, 126};
    state.actors.push_back({"xh_", {}, 0, 0, true});
    MDL_BindActorPalette(state, 0);
    check((state.slots[0].flags & 0x10) && state.slots[0].actor == 0, "actor prefix binds slot");
    check(!(state.slots[1].flags & 0x10), "other slots stay unbound");
    check(state.actors[0].offset[0] == 64 && state.actors[0].fallback == 0xffffu, "mode != 1 start +64");
    ENT_AdaptActorColor(state, state.actors[0], {0, 0, 0, 0});
    // fallback 0x00ffff: R 0, G 255, B 255, k = 8.
    check(state.actors[0].offset[0] == ((((0 - 152) >> 8) + 64) >> 1), "adapt R");
    check(state.actors[0].offset[1] == ((((255 - 168) >> 8) + 64) >> 1), "adapt G");
    state.project_c8 = 1;
    ENT_AdaptActorColor(state, state.actors[0], {0x10, 0x20, 0x30, 0x40});
    check(state.actors[0].fallback == 0x28u, "sample average stored before forcing zero");
}

void test_fog() {
    check(guFogTableIndexToW(0) == 1.0f && std::fabs(guFogTableIndexToW(63) - 52428.8f) < 0.01f,
          "Glide fog W breakpoints");
    std::array<uint8_t, 64> table{};
    guFogGenerateExp(table, 0.0f);
    check(std::all_of(table.begin(), table.end(), [](uint8_t v) { return v == 0; }),
          "zero density gives an empty table");
    guFogGenerateExp(table, static_cast<float>(63 * 6.25e-6)); // Project39
    check(table[63] == 255 && table[0] == 0, "P39 table ends");
    check(std::is_sorted(table.begin(), table.end()), "P39 table is monotonic");
    // W = 1000 lies between entries 27 (896) and 28 (1024).
    const float w0 = guFogTableIndexToW(27), w1 = guFogTableIndexToW(28);
    const float at1000 = table[27] + (1000.0f - w0) / (w1 - w0) * (table[28] - table[27]);
    check(std::fabs(at1000 - 82.0f) <= 2.0f, "P39 fog near 82/255 at W=1000");
    for (int i = 0; i < 64; ++i) {
        const double dp = 63 * 6.25e-6 * guFogTableIndexToW(i);
        const double expected = (1.0 - std::exp(-dp)) / (1.0 - std::exp(-63 * 6.25e-6 * 52428.8)) * 255.0;
        if (std::fabs(table[static_cast<size_t>(i)] - expected) > 1.0) {
            check(false, "P39 table matches the exponential");
            break;
        }
    }
    std::array<uint8_t, 64> ramp{};
    for (size_t i = 0; i < 64; ++i) ramp[i] = static_cast<uint8_t>(i * 4);
    const auto pairs = grFogTable_pairs(ramp);
    check(pairs[0] == ((4u << 24) | (16u << 16) | (0u << 8) | 16u), "grFogTable pair encoding");
    check(pairs[31] == ((252u << 24) | (0u << 16) | (248u << 8) | 16u), "grFogTable last pair");
    auto record = zero_record();
    put32(record, 0x1c0, 242688);
    put32(record, 0x1c4, 110848);
    put32(record, 0x1c8, 97280);
    put32(record, 0x1cc, 10);
    GlideFogState fog;
    FogWaterPhase water;
    SCENE_SetFog(fog, water, record.data(), 1.0, 0);
    check(fog.color == 0xb5b27c00u, "P66 unmasked colour packing");
    check(fog.table_mode && std::fabs(fog.density - 6.25e-5f) < 1e-9f, "density scale");
    put32(record, 0x1cc, 0);
    put32(record, 0xd4, 100);
    SCENE_SetFog(fog, water, record.data(), 1.0, 2);
    check(fog.color == 0x6080u && std::fabs(fog.density - static_cast<float>(std::sin(0.02) * 4e-5 + 1.8e-4)) < 1e-9f,
          "water fog branch");
    SCENE_SetFog(fog, water, record.data(), 1.0, 0);
    check(fog.color == 0 && fog.density == 0.0f, "no fog branch");
    water.phase=3.1f;
    SCENE_SetFog(fog,water,record.data(),(3.1415926535897-1e-8-water.phase)/.02,2);
    check(water.phase>0,"water phase compares unrounded x87 value before wrapping");
}

// Independent byte-level model of the page effects (not the dword code).
struct BlendModel {
    std::vector<uint8_t> shifted, rolled;
    uint8_t gap = 0;
    void seed(const uint8_t* page) {
        shifted.assign(page, page + 0x10000);
        rolled.assign(page, page + 0x10000);
    }
    void step(uint8_t* out) {
        std::vector<uint8_t> next_rolled(0x10000);
        for (size_t r = 0; r < 256; ++r)
            std::copy_n(rolled.begin() + ((r + 1) % 256) * 256, 256, next_rolled.begin() + r * 256);
        rolled.swap(next_rolled);
        std::vector<uint8_t> read(0x10000), next_shifted(0x10000);
        for (size_t r = 0; r < 256; ++r)
            for (size_t c = 0; c < 256; ++c) {
                const size_t at = r * 256 + c;
                // Byte 255 reads the following row's first (still old) byte.
                read[at] = c < 255 ? shifted[at + 1] : (r < 255 ? shifted[at + 1] : gap);
                next_shifted[at] = c < 255 ? shifted[at + 1] : shifted[r * 256];
            }
        shifted.swap(next_shifted);
        for (size_t d = 0; d < 0x10000; d += 4) {
            unsigned carry = 0;
            for (size_t j = 0; j < 4; ++j) {
                const unsigned sum = read[d + j] + rolled[d + j] + carry;
                carry = sum >> 8;
                out[d + j] = static_cast<uint8_t>((sum & 0xfeu) >> 1);
            }
        }
    }
};

std::vector<uint8_t> scrolled_model(const uint8_t* page, int ticks) {
    std::vector<uint8_t> out(0x10000);
    const size_t rows = static_cast<size_t>(ticks) * 2;
    for (size_t r = 0; r < 256; ++r)
        std::copy_n(page + ((r + rows) % 256) * 256, 256, out.begin() + r * 256);
    return out;
}

void test_page_effects() {
    std::vector<uint8_t> page(0x10000);
    uint32_t lcg = 12345;
    for (auto& b : page) { lcg = lcg * 1103515245u + 12345u; b = static_cast<uint8_t>(lcg >> 16); }
    std::vector<uint8_t> scrolled = page;
    for (int i = 0; i < 3; ++i) page_scroll_0x403fb9(scrolled.data());
    check(scrolled == scrolled_model(page.data(), 3), "page scroll: two rows per call");
    for (int i = 0; i < 125; ++i) page_scroll_0x403fb9(scrolled.data());
    check(scrolled == page, "page scroll: 128 calls wrap the page");

    PaletteLightingState state;
    std::vector<uint8_t> blended = page;
    page_blend_seed_0x403f3b(state, blended.data());
    BlendModel model;
    model.seed(page.data());
    std::vector<uint8_t> expected(0x10000);
    for (int i = 0; i < 5; ++i) {
        page_blend_0x403f68(state, blended.data());
        model.step(expected.data());
    }
    check(blended == expected, "page blend matches the byte model after 5 ticks");
    check(state.blend_shifted == model.shifted && state.blend_rolled == model.rolled,
          "page blend copies match the byte model");
    // 0xff + 0x01 in byte 0 wraps to 0 and carries into byte 1.
    PaletteLightingState carry;
    std::vector<uint8_t> out(0x10000, 0);
    carry.blend_shifted.assign(0x10000, 0);
    carry.blend_rolled.assign(0x10000, 0);
    carry.blend_shifted[1] = 0xff; // read at byte 0 of row 0
    carry.blend_rolled[256] = 0x01; // rolled row 1 becomes row 0
    carry.blend_rolled[257] = 0x01;
    page_blend_0x403f68(carry, out.data());
    check(out[0] == 0 && out[1] == 1, "page blend: byte overflow wraps and carries");

    // Slot plumbing: flag 0x20 / 0x40 slots on graph copies, 0x4a0f70/6c gates.
    std::vector<ModelMaterial> materials{make_material("SCROLL", 1), make_material("SCROLL", 1),
                                         make_material("BLEND", 2)};
    for (auto& material : materials)
        std::copy(page.begin(), page.end(), material.bank.begin() + 0x8014);
    std::vector<MaterialCacheEntry> cache{{"SCROLL", {0, 1}}, {"BLEND", {2}}};
    std::vector<uint8_t> record(0x2200);
    std::memcpy(record.data() + 0x6c, "SCROLL", 6);
    PaletteLightingState slots;
    init_level_palette(slots, record.data(), materials.size());
    load_level_palette(slots, record.data());
    MDL_RegisterLightingMaterial(slots, cache, materials, "SCROLL");
    MDL_RegisterLightingMaterial(slots, cache, materials, "BLEND");
    check(SCENE_StartPageScroll(slots, cache, record.data()) && slots.slots[0].flags == 0x22u,
          "SCENE_StartPageScroll sets flag 0x20 on the +0x6c slot");
    std::vector<uint8_t> other(0x2200);
    std::memcpy(other.data() + 0x6c, "NOPE", 4);
    check(!SCENE_StartPageScroll(slots, cache, other.data()), "unknown +0x6c binds nothing");
    AnimTextureBinding binding;
    std::memcpy(record.data() + 0x7c, "BLEND", 5);
    retarget_effect_page_0x42dfa0(binding, slots, cache, materials, record.data());
    check(slots.slots[1].flags == 0x42u && binding.pixels_retargeted &&
          binding.pixel_materials == std::vector<size_t>{2}, "SCENE_StartPageBlend retargets 0x5e5494");
    WatcomRandState random;
    palette_lighting_tick_0x42f024(slots, materials, random, 1.0f);
    const auto page_of = [&](size_t m) {
        return std::vector<uint8_t>(materials[m].bank.begin() + 0x8014, materials[m].bank.end());
    };
    check(page_of(0) == scrolled_model(page.data(), 1) && page_of(1) == page_of(0),
          "tick scrolls every graph copy once");
    BlendModel slot_model;
    slot_model.seed(page.data());
    slot_model.step(expected.data());
    check(page_of(2) == expected, "tick blends the +0x7c page once");
    check(slots.dirty_pages[0] && slots.dirty_pages[1] && slots.dirty_pages[2],
          "page effects mark pages for upload");
    slots.page_scroll_enabled = false;
    slots.page_blend_enabled = false;
    const auto before = page_of(0);
    palette_lighting_tick_0x42f024(slots, materials, random, 1.0f);
    check(page_of(0) == before, "0x4a0f70 clear stops the scroll");
    slots.slots[0].flags |= 1u;
    check(SCENE_HasAnimTexture(slots), "SCENE_HasAnimTexture sees flag 1");
    slots.palette_lighting_enabled = false;
    check(!SCENE_HasAnimTexture(slots), "0x4a0f68 clear hides the animated slot");
}

std::shared_ptr<const od::disc::Image> open_disc(const char* env) {
    const char* cue = std::getenv(env);
    if (!cue || !*cue) return {};
    od::disc::Error error;
    auto image = od::disc::Image::open(std::filesystem::u8path(cue), error);
    if (!image) std::cerr << env << ": " << error.message << '\n';
    return std::shared_ptr<const od::disc::Image>(std::move(image));
}

int check_binding(const std::shared_ptr<const od::disc::Image>& primary,
                  const std::shared_ptr<const od::disc::Image>& secondary,
                  const char* project, const char* material_name, const char* path,
                  uint32_t fog_color, int ticks) {
    std::string error;
    PreviewLevelContext level(primary, secondary);
    if (!level.select_project_scene(project, error) || !SCENE_LoadLevel(level, error)) {
        std::cerr << project << ": " << error << '\n';
        return 1;
    }
    LevelMaterialSession session;
    if (!session.start(level.render_graph(), level_material_setup(level), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    check(session.fog().table_mode && session.fog().color == fog_color, "project fog colour");
    const auto& graph = level.render_graph();
    for (int i = 0; i < ticks; ++i)
        if (!session.tick(i % 2 == 0, error)) {
            std::cerr << error << '\n';
            return 1;
        }
    const auto& anim = session.anim();
    check(anim.bound && anim.open, "animated material binds and opens");
    check(anim.path == path, "anim path from +0x5c");
    check(anim.decoded_frames == static_cast<uint32_t>((ticks + 1) / 2), "one decode per requested tick");
    check(!anim.pixel_materials.empty(), "pixel target resolved");
    if (anim.slot >= 64 || anim.pixel_materials.empty()) return 1;
    const size_t material = anim.pixel_materials.front();
    check(graph.materials[material].name == material_name, "target material name");
    check(session.lighting().slots[anim.slot].flags == 3u, "slot flags 2 | 1");
    // Independent decode of the same frames with a sentinel seed.
    od::port::VfsContext vfs(primary);
    od::port::VideoState video(vfs, false);
    od::port::VideoError video_error;
    if (od::port::VID_Open(video, path, video_error) == 0) {
        std::cerr << video_error.message << '\n';
        return 1;
    }
    std::array<uint8_t, 768> sentinel{};
    sentinel.fill(1); // PL writes channel << 2, never 1.
    video.hnm4().seed_palette(sentinel);
    for (uint32_t frame = 0; frame < anim.decoded_frames; ++frame) {
        od::port::VideoStep step;
        if (!od::port::VID_DecodeFrame(video, step, video_error)) {
            std::cerr << video_error.message << '\n';
            return 1;
        }
    }
    const auto& bank = session.materials()[material].bank;
    check(std::equal(video.hnm4().texture().begin(), video.hnm4().texture().end(),
                     bank.begin() + 0x8014), "material page equals the decoder texture");
    const auto& source = session.lighting().slots[anim.slot].source;
    const auto& original = graph.materials[material].bank;
    size_t touched = 0, untouched = 0;
    bool seed_ok = true, pl_ok = true;
    for (size_t i = 0; i < 256; ++i) {
        const uint8_t* rgb = video.hnm4().palette().data() + i * 3;
        const uint16_t c = row_color(original, 15, i);
        const uint32_t seed = (c & 0x1fu) * 8u | (c & 0x7e0u) << 5 | (c & 0xf800u) << 8;
        if (rgb[0] == 1 && rgb[1] == 1 && rgb[2] == 1) {
            ++untouched;
            seed_ok &= source[i] == seed;
        } else {
            ++touched;
            pl_ok &= source[i] == ((uint32_t(rgb[0]) << 16) | (uint32_t(rgb[1]) << 8) | rgb[2]);
        }
    }
    const auto& cur = session.lighting().current;
    std::cout << project << ' ' << material_name << ": " << touched << " PL entries, "
              << untouched << " seed entries, slot " << anim.slot << ", current "
              << cur[0] << ',' << cur[1] << ',' << cur[2] << '\n';
    check(pl_ok, "PL entries equal decoder palette");
    check(seed_ok, "untouched entries keep the row-15 seed");
    // Untouched entries of the neutral row 15 are the seed plus `current`.
    std::vector<uint8_t> neutral(0x400);
    REND_ApplyPaletteOffsets(session.lighting().add_clamp, cur[0], cur[1], cur[2], source,
                             neutral.data());
    check(std::equal(neutral.begin(), neutral.end(), bank.begin() + palette_row_offset(15)),
          "row 15 = source + current");
    return 0;
}

// Project page effect after `ticks` session ticks against the byte models.
int check_page_effect(const std::shared_ptr<const od::disc::Image>& primary,
                      const std::shared_ptr<const od::disc::Image>& secondary,
                      const char* project, const char* name, bool blend, int ticks) {
    std::string error;
    PreviewLevelContext level(primary, secondary);
    if (!level.select_project_scene(project, error) || !SCENE_LoadLevel(level, error)) {
        std::cerr << project << ": " << error << '\n';
        return 1;
    }
    LevelMaterialSession session;
    if (!session.start(level.render_graph(), level_material_setup(level), error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const MaterialCacheEntry* entry = MDL_FindMaterial(session.cache(), name);
    check(entry && !entry->materials.empty(), "page effect material is cached");
    if (!entry || entry->materials.empty()) return 1;
    const size_t material = entry->materials.front();
    const std::vector<uint8_t> original(session.materials()[material].bank.begin() + 0x8014,
                                        session.materials()[material].bank.end());
    for (int i = 0; i < ticks; ++i)
        if (!session.tick(false, error)) {
            std::cerr << error << '\n';
            return 1;
        }
    std::vector<uint8_t> expected;
    if (blend) {
        BlendModel model;
        model.seed(original.data());
        expected.resize(0x10000);
        for (int i = 0; i < ticks; ++i) model.step(expected.data());
    } else {
        expected = scrolled_model(original.data(), ticks);
    }
    size_t slot = SIZE_MAX;
    for (size_t i = 0; i < 64; ++i)
        if (session.lighting().slots[i].name == name) slot = i;
    const uint32_t flag = blend ? 0x40u : 0x20u;
    check(slot < 64 && (session.lighting().slots[slot].flags & flag), "page effect slot flag");
    const std::vector<uint8_t> page(session.materials()[material].bank.begin() + 0x8014,
                                    session.materials()[material].bank.end());
    check(page == expected, blend ? "page blend bytes after N ticks" : "page scroll bytes after N ticks");
    check(page != original, "page effect changed the page");
    for (const size_t copy : entry->materials)
        check(std::equal(page.begin(), page.end(),
                         session.materials()[copy].bank.begin() + 0x8014), "graph copies share the page");
    std::cout << project << ' ' << name << (blend ? " blend" : " scroll") << ": slot " << slot
              << ", " << entry->materials.size() << " graph copies, " << ticks << " ticks\n";
    return 0;
}

int test_disc() {
    auto disc1 = open_disc("DREAMS_CUE1");
    auto disc2 = open_disc("DREAMS_CUE2");
    if (!disc1) {
        std::cout << "SKIP: set DREAMS_CUE1 (and DREAMS_CUE2) for the Project39 binding test\n";
        return 77;
    }
    if (check_binding(disc1, disc2, "Project39", "E11_EAU", "DATA/ANIM/E11_EAU.HNM",
                      (139u << 16) | (159u << 8) | 189u, 64)) return 1;
    if (disc2 && check_binding(disc2, disc1, "Project13", "F03HNM1", "DATA/ANIM/TF_ALL.HNM",
                               0, 41)) return 1;
    std::string error;
    PreviewLevelContext level(disc1, disc2);
    if (!level.select_project_scene("Project39", error) || !SCENE_LoadLevel(level, error)) return 1;
    // Host clock: 60 frames at 60 Hz give 15 decodes; 10 frames at 5 Hz give 10.
    LevelMaterialSession clock;
    if (!clock.start(level.render_graph(), level_material_setup(level), error)) return 1;
    for (int i = 0; i < 60; ++i) clock.advance(1.0 / 60.0, error);
    check(clock.ticks() == 30, "30 Hz ticks over one second");
    check(clock.anim().decoded_frames == 15, "15 Hz decodes over one second");
    LevelMaterialSession slow;
    if (!slow.start(level.render_graph(), level_material_setup(level), error)) return 1;
    for (int i = 0; i < 10; ++i) slow.advance(0.2, error);
    check(slow.anim().decoded_frames == 10, "at most one decode per host frame");
    // Reopen at the end: 75 frames, then frame counter restarts.
    LevelMaterialSession loop;
    if (!loop.start(level.render_graph(), level_material_setup(level), error)) return 1;
    for (int i = 0; i < 80; ++i) loop.tick(true, error);
    check(loop.anim().reopen_count == 1 && loop.anim().open, "E11_EAU reopens after 75 frames");

    // Project36: empty +0x4c never opens M05FEU_H.
    PreviewLevelContext p36(disc1, disc2);
    if (!p36.select_project_scene("Project36", error) || !SCENE_LoadLevel(p36, error)) {
        std::cerr << "Project36: " << error << '\n';
        return 1;
    }
    LevelMaterialSession s36;
    if (!s36.start(p36.render_graph(), level_material_setup(p36), error) || !s36.tick(true, error))
        return 1;
    check(!s36.anim().bound && !s36.anim().open, "Project36 empty +0x4c opens nothing");

    // +0x6c page scroll (Project36 M04FEU) and +0x7c page blend (Project9 E04_NRJ).
    if (check_page_effect(disc1, disc2, "Project36", "M04FEU", false, 37)) return 1;
    if (check_page_effect(disc1, disc2, "Project9", "E04_NRJ", true, 23)) return 1;
    // Project51: report whether ESSAI (+0x7c) is resident and takes 0x5e5494.
    if (disc2) {
        PreviewLevelContext p51(disc2, disc1);
        if (!p51.select_project_scene("Project51", error) || !SCENE_LoadLevel(p51, error)) {
            std::cerr << "Project51: " << error << '\n';
            return 1;
        }
        LevelMaterialSession s51;
        if (!s51.start(p51.render_graph(), level_material_setup(p51), error) ||
            !s51.tick(false, error)) return 1;
        const bool cached = MDL_FindMaterial(s51.cache(), "ESSAI") != nullptr;
        bool slot = false;
        for (const auto& entry : s51.lighting().slots) slot |= entry.name == "ESSAI";
        std::cout << "Project51 ESSAI: cached " << cached << ", lighting slot " << slot
                  << ", 0x5e5494 retargeted " << s51.anim().pixels_retargeted
                  << ", PL slot " << s51.anim().slot << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    test_rand();
    test_clamp_and_offsets();
    test_color_table();
    test_slots_and_rows();
    test_tick_and_interpolation();
    test_actor_palette();
    test_fog();
    test_page_effects();
    int disc = 0;
    if (argc > 1 && std::strcmp(argv[1], "--disc") == 0) disc = test_disc();
    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    if (disc == 77) return 77;
    if (disc) return disc;
    std::cout << "palette lighting tests passed\n";
    return 0;
}
