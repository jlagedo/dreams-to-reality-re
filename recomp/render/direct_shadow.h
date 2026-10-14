#pragma once
#include "render/direct.h"
#include <array>
#include <string>
#include <vector>
namespace od {
using ShadowTriangle = std::array<std::array<int32_t, 2>, 3>;
// The same triangles before the retail integer truncation, in mask pixels.
using ShadowDetailTriangle = std::array<std::array<float, 2>, 3>;
bool prepare_shadow(const od_shadow_packet &, std::vector<ShadowTriangle> &, std::string &,
                    std::vector<ShadowDetailTriangle> *detail = nullptr);
} // namespace od
