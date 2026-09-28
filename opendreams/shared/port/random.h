#pragma once

#include <cstdint>

namespace od::port {

// Watcom 10.6 C runtime random generator (module rand; WINDREAM 0x460842 /
// 0x460866, DREAMSFX 0x77993 / 0x779b7). The seed is the thread-data word at
// +0xc, initialised to 1 when the thread data is created. The game never
// calls srand_, so every run draws the same sequence from launch.
// The retail stream is process-wide and shared by all 21 callers; ported
// callers must share one state object and keep their original call order.
struct WatcomRandState {
    uint32_t next = 1;
};

// seed = seed * 0x41c64e6d + 0x3039; return (seed >> 16) & 0x7fff.
int rand_(WatcomRandState& state);
void srand_(WatcomRandState& state, uint32_t seed);

} // namespace od::port
