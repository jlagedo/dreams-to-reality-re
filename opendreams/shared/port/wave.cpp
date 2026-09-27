#include "port/wave.h"

#include <cstring>

namespace od::port {
namespace {
uint16_t le16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}
}

bool DSOUND_LoadWav(const uint8_t* wave, size_t size,
                    WavePcm& pcm, std::string& error) {
    error.clear();
    pcm = {};
    if (!wave || size < 44 || size > 32u * 1024u * 1024u ||
        std::memcmp(wave,"RIFF",4)!=0 || std::memcmp(wave+8,"WAVE",4)!=0)
        return fail(error,"sound sample has no bounded RIFF/WAVE header");
    const uint64_t riff_end = static_cast<uint64_t>(le32(wave+4)) + 8;
    if (riff_end < 44 || riff_end > size)
        return fail(error,"RIFF length exceeds the selected sound sample");
    bool have_format=false, have_data=false;
    uint32_t byte_rate=0;
    uint16_t block_align=0, format_tag=0;
    size_t data_at=0, data_size=0;
    for (size_t at=12; at+8<=riff_end;) {
        const uint32_t length=le32(wave+at+4);
        const uint64_t body=static_cast<uint64_t>(at)+8;
        if (length > riff_end-body)
            return fail(error,"RIFF subchunk exceeds the selected sound sample");
        if (std::memcmp(wave+at,"fmt ",4)==0 && !have_format) {
            if (length < 16) return fail(error,"WAVE format chunk is truncated");
            const uint8_t* fmt=wave+at+8;
            format_tag=le16(fmt);
            pcm.channels=le16(fmt+2);
            pcm.rate=le32(fmt+4);
            byte_rate=le32(fmt+8);
            block_align=le16(fmt+12);
            pcm.bits=le16(fmt+14);
            have_format=true;
        } else if (std::memcmp(wave+at,"data",4)==0 && !have_data) {
            data_at=static_cast<size_t>(body);
            data_size=length;
            have_data=true;
        }
        const uint64_t next=body+length+(length&1u);
        if (next > riff_end) {
            if (body+length==riff_end) break; // Final odd data byte has no pad.
            return fail(error,"RIFF padding exceeds the selected sound sample");
        }
        at=static_cast<size_t>(next);
    }
    if (!have_format || !have_data || pcm.channels<1 || pcm.channels>2 ||
        pcm.rate<8000 || pcm.rate>192000 || (pcm.bits!=8 && pcm.bits!=16))
        return fail(error,"WAVE format or data chunk is unsupported");
    const size_t expected_align=pcm.bytes_per_frame();
    if (!expected_align || block_align!=expected_align ||
        byte_rate!=pcm.rate*expected_align || data_size%expected_align!=0)
        return fail(error,"WAVE byte rate or frame alignment is invalid");
    if (format_tag==3) {
        // FSB clip 12 says IEEE float/16-bit but its byte rate and PCM words
        // are ordinary signed 16-bit samples. Retail ignores the tag.
        pcm.repaired_format_tag=true;
    } else if (format_tag!=1) {
        return fail(error,"WAVE compression is not supported by the retail PCM path");
    }
    pcm.samples.assign(wave+data_at,wave+data_at+data_size);
    return true;
}

} // namespace od::port
