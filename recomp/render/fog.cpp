#include "render/fog.h"

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

} // namespace

float guFogTableIndexToW(int index) {
    return static_cast<float>(std::pow(2.0, 3.0 + static_cast<double>(index >> 2))) /
           static_cast<float>(8 - (index & 3));
}

void guFogGenerateExp(std::array<uint8_t, 64>& table, float density) {
    float dp = density * guFogTableIndexToW(63);
    const float scale = 1.0f / (1.0f - static_cast<float>(std::exp(-dp)));
    for (int i = 0; i < 64; ++i) {
        dp = density * guFogTableIndexToW(i);
        float f = (1.0f - static_cast<float>(std::exp(-dp))) * scale;
        if (f > 1.0f) f = 1.0f;
        else if (f < 0.0f) f = 0.0f;
        f *= 255.0f;
        // (GrFog_t) f truncates; a NaN (zero density) stores 0x80000000 on
        // x87 and keeps its low byte.
        table[static_cast<size_t>(i)] = std::isnan(f) ? 0 : static_cast<uint8_t>(f);
    }
}

std::array<uint32_t, 32> grFogTable_pairs(const std::array<uint8_t, 64>& table) {
    std::array<uint32_t, 32> pairs{};
    for (size_t i = 0; i < 32; ++i) {
        const uint8_t e0 = table[i * 2], e1 = table[i * 2 + 1];
        const uint8_t next = i == 31 ? e1 : table[i * 2 + 2];
        const uint8_t d0 = static_cast<uint8_t>((int(e1) - int(e0)) * 4);
        const uint8_t d1 = static_cast<uint8_t>((int(next) - int(e1)) * 4);
        pairs[i] = (static_cast<uint32_t>(e1) << 24) | (static_cast<uint32_t>(d1) << 16) |
                   (static_cast<uint32_t>(e0) << 8) | d0;
    }
    return pairs;
}

void GLIDE_SetFog(GlideFogState& state, uint32_t color, float density) {
    state.table_mode = true;
    state.color = color;
    state.density = density;
    guFogGenerateExp(state.table, density);
}

float SCENE_SetFog(GlideFogState& state, FogWaterPhase& water, const uint8_t* record,
                   double frame_dt, int32_t mode_151d3c) {
    const int32_t density_word = s32(record + 0x1cc);
    if (density_word != 0) {
        const float stored = static_cast<float>(density_word);
        const uint32_t color = (static_cast<uint32_t>(s32(record + 0x1c0)) << 16) +
            (static_cast<uint32_t>(s32(record + 0x1c4)) << 8) +
            static_cast<uint32_t>(s32(record + 0x1c8));
        GLIDE_SetFog(state, color, static_cast<float>(static_cast<double>(density_word) * 6.25e-6));
        return stored;
    }
    if (s32(record + 0xd4) != 0 && mode_151d3c == 2) {
        const double advanced = frame_dt * 0.02 + water.phase;
        water.phase = static_cast<float>(advanced);
        // FST rounds the stored phase, but retail FCOMP still consumes the
        // unrounded x87 value. Comparing the stored float wraps one step early.
        if (advanced > 3.1415926535897)
            water.phase = -3.14159274f; // 0xc0490fdb
        const float density = static_cast<float>(std::sin(static_cast<double>(water.phase)) *
                                                 4.0e-5 + 1.8e-4);
        GLIDE_SetFog(state, 0x6080u, density);
        return density;
    }
    GLIDE_SetFog(state, 0, 0.0f);
    return 0.0f; // EBX, the zero +0x1cc word
}

} // namespace od::port
