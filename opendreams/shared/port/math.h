#pragma once

#include <array>
#include <cstdint>

namespace od::port {

using Mat3 = std::array<int32_t, 9>;
using Vec3 = std::array<int32_t, 3>;

// Q15 operations used by retail's parent-first model transform walk.
void MATH_MulMat3(const Mat3& left, const Mat3& right, Mat3& result);
void MATH_MulMat3Vec3(const Mat3& matrix, const Vec3& vector, Vec3& result);

} // namespace od::port
