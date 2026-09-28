#include "port/hnm4.h"

#include <algorithm>

namespace od::port {
namespace {

constexpr size_t retail_buffer_bytes = 0x10000; // Each fixed retail frame buffer.

bool fail(std::string& error, const char* message) { error = message; return false; }

// Retail 0x44d9c1: 32-bit little-endian bit words, most significant bit
// first, refilled lazily from the same byte stream that feeds literals.
class LzBits {
public:
    LzBits(const uint8_t* input, size_t size) : input_(input), size_(size) {}
    bool bit(bool& value) {
        if (!left_) {
            if (size_ - pos_ < 4) return false;
            queue_ = static_cast<uint32_t>(input_[pos_]) |
                     (static_cast<uint32_t>(input_[pos_ + 1]) << 8) |
                     (static_cast<uint32_t>(input_[pos_ + 2]) << 16) |
                     (static_cast<uint32_t>(input_[pos_ + 3]) << 24);
            pos_ += 4;
            left_ = 32;
        }
        value = (queue_ & 0x80000000u) != 0;
        queue_ <<= 1;
        --left_;
        return true;
    }
    bool byte(uint8_t& value) {
        if (pos_ >= size_) return false;
        value = input_[pos_++];
        return true;
    }

private:
    const uint8_t* input_;
    size_t size_, pos_ = 0;
    uint32_t queue_ = 0;
    unsigned left_ = 0;
};

bool unpack_lz(const uint8_t* input, size_t size, std::vector<uint8_t>& output,
               std::string& error) {
    LzBits bits(input, size);
    size_t out = 0;
    const size_t capacity = output.size();
    for (;;) {
        bool literal = false;
        if (!bits.bit(literal)) return fail(error, "HNM4 IZ bit stream is truncated");
        if (literal) {
            uint8_t value = 0;
            if (!bits.byte(value)) return fail(error, "HNM4 IZ literal is truncated");
            if (out >= capacity) return fail(error, "HNM4 IZ output exceeds the frame");
            output[out++] = value;
            continue;
        }
        bool long_form = false;
        if (!bits.bit(long_form)) return fail(error, "HNM4 IZ bit stream is truncated");
        size_t count = 0, distance = 0;
        if (long_form) {
            uint8_t low = 0, high = 0;
            if (!bits.byte(low) || !bits.byte(high))
                return fail(error, "HNM4 IZ long match is truncated");
            const unsigned word = low | (static_cast<unsigned>(high) << 8);
            distance = 0x2000u - (word >> 3); // SAR of 0xffff0000|word by 3.
            count = word & 7u;
            if (!count) {
                uint8_t extended = 0;
                if (!bits.byte(extended))
                    return fail(error, "HNM4 IZ match length is truncated");
                if (!extended) return true; // Retail end marker.
                count = extended;
            }
        } else {
            bool b1 = false, b0 = false;
            uint8_t offset = 0;
            if (!bits.bit(b1) || !bits.bit(b0) || !bits.byte(offset))
                return fail(error, "HNM4 IZ short match is truncated");
            count = (b1 ? 2u : 0u) + (b0 ? 1u : 0u);
            distance = 0x100u - offset; // 0xffffff00|byte.
        }
        count += 2;
        if (distance > out) return fail(error, "HNM4 IZ match precedes the frame");
        if (count > capacity - out) return fail(error, "HNM4 IZ output exceeds the frame");
        for (size_t i = 0; i < count; ++i, ++out) output[out] = output[out - distance];
    }
}

// Retail 0x44e676. `out` is the buffer the frame writes and even-mode copies
// read; `prev` is the other buffer, read by odd modes. Positions are byte
// offsets into the interleaved (two rows per 2*width bytes) buffer.
bool unpack_inter(const uint8_t* input, size_t size, std::vector<uint8_t>& out,
                  const std::vector<uint8_t>& prev, size_t width, std::string& error) {
    const size_t limit = out.size();
    const ptrdiff_t line_pair = static_cast<ptrdiff_t>(width) * 2;
    size_t in = 0, pos = 0;
    for (;;) {
        if (in >= size) return fail(error, "HNM4 IU stream has no end code");
        const uint8_t op = input[in];
        auto need = [&](size_t bytes) { return size - in >= bytes; };
        const unsigned count = op & 0x1fu;
        if (count) {
            if (!need(3)) return fail(error, "HNM4 IU copy code is truncated");
            const uint32_t code = op | (static_cast<uint32_t>(input[in + 1]) << 8) |
                                  (static_cast<uint32_t>(input[in + 2]) << 16);
            in += 3;
            const unsigned mode = (code >> 5) & 0xfu;
            const std::vector<uint8_t>& source = (mode & 1) ? prev : out;
            ptrdiff_t ref = static_cast<ptrdiff_t>(pos) +
                            static_cast<ptrdiff_t>((code >> 9) & 0x7fffu) * 2 - 0x8000;
            if (count * 2u > limit - std::min(pos, limit) || pos > limit)
                return fail(error, "HNM4 IU copy exceeds the frame");
            for (unsigned i = 0; i < count; ++i) {
                ptrdiff_t first = ref, second = ref + 1; // lo, hi without mode 2.
                if (mode & 2) { first = ref + 1 - line_pair; second = ref; }
                if (first < 0 || second < 0 || static_cast<size_t>(first) >= limit ||
                    static_cast<size_t>(second) >= limit)
                    return fail(error, "HNM4 IU copy source is outside the frame");
                uint8_t lo = source[static_cast<size_t>(first)];
                uint8_t hi = source[static_cast<size_t>(second)];
                if (mode & 8) std::swap(lo, hi);
                out[pos] = lo;
                out[pos + 1] = hi;
                pos += 2;
                ref += (mode & 4) ? -2 : 2;
            }
            continue;
        }
        switch (op) {
        case 0x00: // Literal word.
            if (!need(3)) return fail(error, "HNM4 IU literal is truncated");
            if (pos > limit || limit - pos < 2)
                return fail(error, "HNM4 IU literal exceeds the frame");
            out[pos] = input[in + 1];
            out[pos + 1] = input[in + 2];
            pos += 2;
            in += 3;
            break;
        case 0x20: // Short skip in words.
            if (!need(2)) return fail(error, "HNM4 IU skip is truncated");
            pos += static_cast<size_t>(input[in + 1]) * 2;
            in += 2;
            break;
        case 0x40: // Long skip in words.
            if (!need(3)) return fail(error, "HNM4 IU skip is truncated");
            pos += (static_cast<size_t>(input[in + 1]) |
                    (static_cast<size_t>(input[in + 2]) << 8)) * 2;
            in += 3;
            break;
        case 0x60: { // Fill words with one index.
            if (!need(3)) return fail(error, "HNM4 IU fill is truncated");
            const size_t words = input[in + 1];
            // Retail decrements the zero count before testing it and would
            // run a 32-bit loop; reject that instead.
            if (!words) return fail(error, "HNM4 IU fill has a zero count");
            if (pos > limit || words * 2 > limit - pos)
                return fail(error, "HNM4 IU fill exceeds the frame");
            std::fill_n(out.begin() + static_cast<ptrdiff_t>(pos), words * 2, input[in + 2]);
            pos += words * 2;
            in += 3;
            break;
        }
        default: // 0x80, 0xa0, 0xc0, 0xe0: retail returns.
            return true;
        }
    }
}

} // namespace

Hnm4Decoder::Hnm4Decoder(uint16_t width, uint16_t height)
    : width_(width), height_(height) {}

bool Hnm4Decoder::update_palette(const uint8_t* payload, size_t size,
                                 std::string& error) {
    error.clear();
    if (!payload && size) return fail(error, "HNM4 PL payload is absent");
    size_t pos = 0;
    // Retail stops at the first pair with either byte 0xff. It writes only
    // the first animated material: its cursor is not rewound, so later
    // materials see the terminator at once.
    while (size - pos >= 2) {
        const size_t start = payload[pos], count_byte = payload[pos + 1];
        if (start == 0xff || count_byte == 0xff) return true;
        pos += 2;
        const size_t count = count_byte ? count_byte : 256; // Byte loop counter.
        if (start + count > 256)
            return fail(error, "HNM4 PL range exceeds the 256-entry palette");
        if (count > (size - pos) / 3) return fail(error, "HNM4 PL entries are truncated");
        ++palette_version_;
        for (size_t i = 0; i < count; ++i, pos += 3) {
            uint8_t* entry = palette_.data() + (start + i) * 3;
            entry[0] = static_cast<uint8_t>(payload[pos] << 2);
            entry[1] = static_cast<uint8_t>(payload[pos + 1] << 2);
            entry[2] = static_cast<uint8_t>(payload[pos + 2] << 2);
        }
    }
    return fail(error, "HNM4 PL palette terminator is missing");
}

bool Hnm4Decoder::decode(uint8_t kind, const uint8_t* payload, size_t size,
                         bool& image_written, std::string& error) {
    error.clear();
    image_written = false;
    if (kind != 'Z' && kind != 'U') {
        ++counter_; // Retail 0x44d93b still advances the parity.
        return true;
    }
    // The retail deinterlace is unrolled for 256-byte texture rows and the
    // frame buffers are fixed at 64 KiB; other sizes would overrun them.
    if (width_ != 256 || height_ != 256)
        return fail(error, "HNM4 frames must be 256x256 for the retail texture path");
    if (!payload && size) return fail(error, "HNM4 image payload is absent");
    const size_t pixels = static_cast<size_t>(width_) * height_;
    if (buffers_[0].empty()) {
        buffers_[0].assign(retail_buffer_bytes, 0);
        buffers_[1].assign(retail_buffer_bytes, 0);
    }
    // Retail 0x44d944: an even counter writes 0x5fce08 and references
    // 0x5ece08; an odd counter swaps them.
    const unsigned output_index = (counter_ & 1) ? 0 : 1;
    auto& out = buffers_[output_index];
    auto& previous = buffers_[1 - output_index];
    if (kind == 'Z') {
        // Skips the payload's width/height/mode word, then refreshes the
        // reference buffer with the whole decoded frame.
        if (size < 4) return fail(error, "HNM4 IZ payload has no header word");
        if (!unpack_lz(payload + 4, size - 4, out, error)) return false;
        std::copy_n(out.begin(), pixels, previous.begin());
    } else if (!unpack_inter(payload, size, out, previous, width_, error)) {
        return false;
    }
    // Retail 0x44da42 with the walker's flag 1: even bytes of each 512-byte
    // pair go to row 2p and odd bytes to row 2p+1 of the texture.
    texture_.resize(pixels);
    for (size_t pair = 0; pair < height_ / 2u; ++pair) {
        const uint8_t* source = out.data() + pair * width_ * 2u;
        uint8_t* even = texture_.data() + pair * 2u * width_;
        uint8_t* odd = even + width_;
        for (size_t x = 0; x < width_; ++x) {
            even[x] = source[x * 2];
            odd[x] = source[x * 2 + 1];
        }
    }
    ++counter_;
    image_written = true;
    return true;
}

} // namespace od::port
