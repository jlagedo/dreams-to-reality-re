#include "port/random.h"

namespace od::port {

int rand_(WatcomRandState& state) {
    state.next = state.next * 0x41c64e6du + 0x3039u;
    return static_cast<int>((state.next >> 16) & 0x7fffu);
}

void srand_(WatcomRandState& state, uint32_t seed) {
    state.next = seed;
}

} // namespace od::port
