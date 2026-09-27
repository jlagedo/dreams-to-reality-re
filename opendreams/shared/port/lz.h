#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace od::port {

// Adapted WINDREAM/GDIDREAM LZ_Unpack. The retail routine trusts its source and
// destination pointers; the port bounds both and returns a recoverable error.
bool LZ_Unpack(const uint8_t* source, size_t source_size,
               std::vector<uint8_t>& output, std::string& error,
               size_t output_limit = 1u << 26);

} // namespace od::port
