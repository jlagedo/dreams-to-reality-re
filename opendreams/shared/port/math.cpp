#include "port/math.h"

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

} // namespace

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
