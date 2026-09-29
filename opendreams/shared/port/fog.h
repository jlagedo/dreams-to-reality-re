#pragma once

#include <array>
#include <cstdint>

namespace od::port {

// Glide 2 fog state as DREAMSFX leaves it: grFogMode(GR_FOG_WITH_TABLE),
// grFogColorValue and the 64-entry table from guFogGenerateExp.
struct GlideFogState {
    bool table_mode = false;          // grFogMode(2) was issued
    uint32_t color = 0;               // GrColor_t, ARGB; alpha ignored by fog
    float density = 0.0f;             // guFogGenerateExp argument
    std::array<uint8_t, 64> table{};  // GrFog_t entries
};

// Water-branch state of SCENE_SetFog: the phase float at 0x159fa0.
struct FogWaterPhase {
    float phase = 0.0f;
};

// Glide 2 SST1 library (gu.c): W of table entry i, 2^(3 + i/4) / (8 - i%4).
float guFogTableIndexToW(int index);
// Glide 2 SST1 library (gu.c): exponential table normalised to the last
// entry. Zero density divides 0 by 0; the x87 float-to-byte store of NaN
// yields 0, so the table is all zero (unverified on hardware). The renderer
// applies the table mode's blend bias; a zero table is not a disabled mode.
void guFogGenerateExp(std::array<uint8_t, 64>& table, float density);
// grFogTable's hardware form: 32 pairs, deltas (e1-e0)<<2 in 8 bits.
std::array<uint32_t, 32> grFogTable_pairs(const std::array<uint8_t, 64>& table);

// GLIDE_SetFog (DREAMSFX 0x671d0).
void GLIDE_SetFog(GlideFogState& state, uint32_t color, float density);
// SCENE_SetFog (DREAMSFX 0x29288) for the current project record:
//   +0x1cc != 0: colour (+0x1c0 << 16) + (+0x1c4 << 8) + +0x1c8, unmasked
//                32-bit, density +0x1cc * 6.25e-6;
//   else +0xd4 != 0 and 0x151d3c == 2: colour 0x6080, phase += dt * 0.02,
//                wrapped to -pi past pi, density sin(phase) * 4e-5 + 1.8e-4;
//   else colour 0, density 0.
// `frame_dt` is the double 0x159e20; `mode_151d3c` the unnamed DREAMSFX
// game-state word the water branch tests. Returns the density that retail
// stores at 0x159f9c.
float SCENE_SetFog(GlideFogState& state, FogWaterPhase& water, const uint8_t* record,
                   double frame_dt, int32_t mode_151d3c);

} // namespace od::port
