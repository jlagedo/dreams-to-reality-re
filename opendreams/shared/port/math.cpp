#include "port/math.h"

#include <cmath>
#include <cstring>

namespace od::port {
namespace {

int32_t q15_sum(int64_t a, int64_t b, int64_t c) {
    // Watcom used 32-bit integer products and sums followed by SAR 15.
    const uint32_t bits = static_cast<uint32_t>(a + b + c);
    int32_t signed_sum = 0;
    std::memcpy(&signed_sum, &bits, sizeof(bits));
    return signed_sum >> 15;
}

double stored_double(uint64_t bits) {
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// 0x45b040 runs on the x87 stack and truncates each product through __CHP.
// Double arithmetic reproduces every entry: the nearest non-integral product
// is 3e-11 from an integer, and a 32-bit x87 replay of the retail loop at
// 53- and 64-bit precision control yields the same three tables.
MathTrigTables build_trig_tables() {
    MathTrigTables tables;
    const double scale = stored_double(0x40e0000000000000ull);      // 0x4c6074
    const double step = stored_double(0x3f5921fb60000002ull);       // 0x4c6094
    double angle = 0.0;
    for (size_t i = 0; i < 4096; ++i) {
        tables.cos[i] = static_cast<int32_t>(std::cos(angle) * scale);
        tables.sin[i] = static_cast<int32_t>(std::sin(angle) * scale);
        angle += step;
    }
    const double inverse_pi = stored_double(0x3fd45f306dc9c92dull); // 0x4c608c
    const double half_pi = stored_double(0x3ff921fb54442d18ull);    // 0x4ac8d8
    constexpr double pi = 3.141592653589793238462643383279502884;   // FLDPI
    for (int32_t i = -2048; i < 2048; ++i) {
        const double x = static_cast<double>(i) * (1.0 / 2048.0);   // 0x4c607c
        const double one_minus_square = 1.0 - x * x;
        // Watcom acos_ (0x47e952): |x| == 1 loads 0 or FLDPI, otherwise
        // pi/2 - FPATAN(x, sqrt(1 - x*x)).
        const double radians = one_minus_square == 0.0 ? (x < 0.0 ? pi : 0.0) :
            half_pi - std::atan(x / std::sqrt(one_minus_square));
        tables.acos[static_cast<size_t>(i + 2048)] =
            static_cast<int32_t>(radians * 2048.0 * inverse_pi);
    }
    return tables;
}

} // namespace

const MathTrigTables& math_trig_tables() {
    static const MathTrigTables tables = build_trig_tables();
    return tables;
}

void MATH_MulMat3(const Mat3& left, const Mat3& right, Mat3& result) {
    Mat3 output{};
    for (size_t row = 0; row < 3; ++row)
        for (size_t column = 0; column < 3; ++column)
            output[row * 3 + column] = q15_sum(
                static_cast<int64_t>(left[row * 3]) * right[column],
                static_cast<int64_t>(left[row * 3 + 1]) * right[3 + column],
                static_cast<int64_t>(left[row * 3 + 2]) * right[6 + column]);
    result = output;
}

void MATH_MulMat3Vec3(const Mat3& matrix, const Vec3& vector, Vec3& result) {
    Vec3 output{};
    for (size_t row = 0; row < 3; ++row)
        output[row] = q15_sum(
            static_cast<int64_t>(matrix[row * 3]) * vector[0],
            static_cast<int64_t>(matrix[row * 3 + 1]) * vector[1],
            static_cast<int64_t>(matrix[row * 3 + 2]) * vector[2]);
    result = output;
}

} // namespace od::port
