#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

struct WavePcm {
    uint32_t rate = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    bool repaired_format_tag = false;
    std::vector<uint8_t> samples;
    size_t bytes_per_frame() const { return static_cast<size_t>(channels) * bits / 8; }
    uint64_t frame_count() const {
        const size_t frame = bytes_per_frame();
        return frame ? samples.size() / frame : 0;
    }
};

// Retail DSOUND_LoadWav advances past the 44-byte WAVE header and fills a
// DirectSound buffer. This source-scoped adaptation validates the RIFF chunks
// and gives SDL the same PCM bytes, including the known FSB tag-3 defect.
bool DSOUND_LoadWav(const uint8_t* wave, size_t size,
                    WavePcm& pcm, std::string& error);

} // namespace od::port
