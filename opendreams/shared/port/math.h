#pragma once

#include <array>
#include <cstdint>

namespace od::port {

using Mat3 = std::array<int32_t, 9>;
using Vec3 = std::array<int32_t, 3>;

// Fixed tables that WINDREAM.EXE builds in .bss during RES_InitArena
// (unnamed generator at 0x45b040). Angles use 4096 units per turn.
//   cos/sin  (0x665ff0/0x669ff0): trunc(32768 * cos|sin(i * step)), where
//            step is the stored double 0x3f5921fb60000002 (float pi / 2048)
//            accumulated by repeated addition, so sin[1024] is 32767.
//   acos     (0x661ff0): trunc(acos((i - 2048) / 2048) * 2048 * k), with k
//            the stored double 0x3fd45f306dc9c92d (about 1/pi) and Watcom's
//            acos (pi/2 - atan(x / sqrt(1 - x*x)), FLDPI at x = -1).
struct MathTrigTables {
    std::array<int32_t, 4096> acos{};
    std::array<int32_t, 4096> cos{};
    std::array<int32_t, 4096> sin{};
};

// Host accessor; the tables are generated once on first use.
const MathTrigTables& math_trig_tables();

// Q15 operations used by retail's parent-first model transform walk.
void MATH_MulMat3(const Mat3& left, const Mat3& right, Mat3& result);
void MATH_MulMat3Vec3(const Mat3& matrix, const Vec3& vector, Vec3& result);

} // namespace od::port
