#include "render/glide_compat.h"

#include <iostream>
#include <string>

int main() {
    od::GlideCompat glide(4,4);
    od::GlideCompat::LfbInfo lock;
    std::string error;
    if (!glide.grLfbLock(lock,error) || lock.pitch_bytes!=8 ||
        !glide.grClipWindow(0,0,4,4,error) ||
        !glide.grBufferClear(0,error)) {
        std::cerr << "movie LFB lock/clear failed: " << error << '\n'; return 1;
    }
    lock.pixels[1*4+2]=0xf800;
    if (glide.grBufferSwap(error) || !glide.grLfbUnlock(error) ||
        !glide.grBufferSwap(error) || glide.front_buffer()[6]!=0xf800) {
        std::cerr << "movie LFB swap did not freeze the written frame\n"; return 1;
    }
    if (!glide.grLfbLock(lock,error)) return 1;
    lock.pixels[6]=0x07e0;
    if (glide.front_buffer()[6]!=0xf800 || !glide.grLfbUnlock(error)) {
        std::cerr << "front buffer changed before the next swap\n"; return 1;
    }
    return 0;
}
