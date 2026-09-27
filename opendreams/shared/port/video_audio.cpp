#include "port/video_audio.h"

namespace od::port {

void VideoDpcm::reset() {
    deltas_.fill(0);
    predictor_.fill(0);
    initialized_ = false;
}

bool VideoDpcm::decode_sd(const uint8_t* payload, size_t size,
                          std::vector<int16_t>& pcm, std::string& error) {
    error.clear();
    pcm.clear();
    if (!payload && size) {
        error = "SD sound payload is missing";
        return false;
    }
    if (!initialized_) {
        if (size < 512) {
            error = "first SD sound payload has no 512-byte delta table";
            return false;
        }
        for (size_t i = 0; i < deltas_.size(); ++i) {
            const uint16_t bits = static_cast<uint16_t>(payload[i * 2]) |
                static_cast<uint16_t>(payload[i * 2 + 1] << 8);
            deltas_[i] = static_cast<int16_t>(bits);
        }
        payload += 512;
        size -= 512;
        initialized_ = true;
    }
    // The retail helper updates left and right accumulators independently,
    // wraps at 16 bits, and writes interleaved signed PCM.
    pcm.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        auto& channel = predictor_[i & 1];
        channel = static_cast<uint16_t>(channel + deltas_[payload[i]]);
        pcm.push_back(static_cast<int16_t>(channel));
    }
    return true;
}

} // namespace od::port
