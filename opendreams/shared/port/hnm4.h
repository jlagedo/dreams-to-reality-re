#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

// Retail WINDREAM HNM4 animated-texture codec state. The original keeps two
// fixed 64 KiB interleaved frame buffers (0x5ece08 and 0x5fce08, installed by
// the init at 0x44d8e8), a 16-bit frame counter whose low bit selects the
// output buffer, and writes the deinterlaced 256x256 indexed frame straight
// into the bound animated material's texture pixels (0x5e5494). PL chunks
// rewrite that material's palette with six-bit channels shifted left by two.
// One decoder belongs to one open HNM4/HNS4 file.
class Hnm4Decoder {
public:
    Hnm4Decoder() = default;
    Hnm4Decoder(uint16_t width, uint16_t height);

    // PL payload, as parsed by the retail material palette update 0x42ed30.
    bool update_palette(const uint8_t* payload, size_t size, std::string& error);
    // Retail PL runs overwrite the bound material slot's palette, which level
    // load seeded from the material's DSN row 15; entries a run never touches
    // keep that seed. A scene binding installs it here before decoding.
    void seed_palette(const std::array<uint8_t, 768>& seed) {
        palette_ = seed;
        ++palette_version_;
    }
    // Incremented by every palette change, for GPU refresh bookkeeping.
    uint32_t palette_version() const { return palette_version_; }
    // I? payload from the retail frame entry 0x44d921. `kind` is the tag's
    // second byte: 'Z' (LZ key frame), 'U' (inter frame); other kinds only
    // advance the frame counter. Returns false with a bounded error for
    // malformed data; `image_written` reports whether the texture changed.
    bool decode(uint8_t kind, const uint8_t* payload, size_t size,
                bool& image_written, std::string& error);

    uint16_t width() const { return width_; }
    uint16_t height() const { return height_; }
    uint16_t frame_counter() const { return counter_; }
    // Deinterlaced 8-bit indices, 256 bytes per row, as retail writes them to
    // the animated material's texture. Empty until the first Z/U frame.
    const std::vector<uint8_t>& texture() const { return texture_; }
    // R, G, B per entry, each the retail `channel << 2` byte. Entries a PL
    // chunk never touched stay zero; a scene binding seeds them from its
    // material palette as retail's material record already holds them.
    const std::array<uint8_t, 768>& palette() const { return palette_; }

private:
    std::array<std::vector<uint8_t>, 2> buffers_; // [0]=0x5ece08, [1]=0x5fce08
    std::vector<uint8_t> texture_;
    std::array<uint8_t, 768> palette_{};
    uint16_t width_ = 0, height_ = 0, counter_ = 0;
    uint32_t palette_version_ = 0;
};

} // namespace od::port
