#include "port/lz.h"

namespace od::port {

bool LZ_Unpack(const uint8_t* source, size_t source_size,
               std::vector<uint8_t>& output, std::string& error,
               size_t output_limit) {
    output.clear();
    error.clear();
    if (source_size && !source) {
        error = "null LZ input";
        return false;
    }

    size_t position = 0;
    uint32_t control = 0;
    unsigned remaining_bits = 0;
    const auto bit = [&](unsigned& value) -> bool {
        if (!remaining_bits) {
            if (source_size - position < 4) {
                error = "LZ control word ends past input";
                return false;
            }
            control = static_cast<uint32_t>(source[position]) |
                (static_cast<uint32_t>(source[position + 1]) << 8) |
                (static_cast<uint32_t>(source[position + 2]) << 16) |
                (static_cast<uint32_t>(source[position + 3]) << 24);
            position += 4;
            remaining_bits = 32;
        }
        value = (control >> --remaining_bits) & 1u;
        return true;
    };

    while (true) {
        unsigned tag = 0;
        if (!bit(tag)) return false;
        if (tag) {
            if (position == source_size) {
                error = "LZ literal ends past input";
                return false;
            }
            if (output.size() == output_limit) {
                error = "LZ output exceeds its limit";
                return false;
            }
            output.push_back(source[position++]);
            continue;
        }

        if (!bit(tag)) return false;
        size_t length = 0;
        size_t back = 0;
        if (tag) {
            if (source_size - position < 2) {
                error = "LZ long match ends past input";
                return false;
            }
            const uint16_t code = static_cast<uint16_t>(source[position]) |
                static_cast<uint16_t>(source[position + 1] << 8);
            position += 2;
            back = 8192u - (code >> 3);
            if (code & 7u) {
                length = (code & 7u) + 2u;
            } else {
                if (position == source_size) {
                    error = "LZ extended length ends past input";
                    return false;
                }
                length = source[position++];
                if (!length) return true;
                length += 2;
            }
        } else {
            unsigned b1 = 0, b2 = 0;
            if (!bit(b1) || !bit(b2)) return false;
            if (position == source_size) {
                error = "LZ short displacement ends past input";
                return false;
            }
            back = 256u - source[position++];
            length = 2u + 2u * b1 + b2;
        }

        if (back > output.size()) {
            error = "LZ match starts before output";
            return false;
        }
        if (length > output_limit - output.size()) {
            error = "LZ output exceeds its limit";
            return false;
        }
        for (size_t i = 0; i < length; ++i)
            output.push_back(output[output.size() - back]);
    }
}

} // namespace od::port
