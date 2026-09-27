#include "port/lz.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

bool matches(const std::vector<uint8_t>& result, const char* expected) {
    return std::string(result.begin(), result.end()) == expected;
}

} // namespace

int main() {
    std::vector<uint8_t> result;
    std::string error;

    // Three literals, then the zero-length long code that ends a record.
    const std::array<uint8_t, 10> literals{{0, 0, 0, 0xe8,
                                             'A', 'B', 'C', 0, 0, 0}};
    if (!od::port::LZ_Unpack(literals.data(), literals.size(), result, error) ||
        !matches(result, "ABC")) return 1;

    // A short back-reference to the byte just written overlaps its own output.
    const std::array<uint8_t, 9> overlap{{0, 0, 0, 0x92,
                                           'A', 0xff, 0, 0, 0}};
    if (!od::port::LZ_Unpack(overlap.data(), overlap.size(), result, error) ||
        !matches(result, "AAAAA")) return 2;

    if (od::port::LZ_Unpack(literals.data(), 5, result, error) || error.empty())
        return 3;
    if (od::port::LZ_Unpack(overlap.data(), overlap.size(), result, error, 4) ||
        error.empty()) return 4;
    if (od::port::LZ_Unpack(nullptr, 1, result, error) || error.empty())
        return 5;
    return 0;
}
