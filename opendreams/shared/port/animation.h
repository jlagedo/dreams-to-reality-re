#pragma once

#include "port/math.h"
#include "port/model.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace od::port {

using Quat4 = std::array<int32_t,4>;

struct RotationKey {
    int32_t frame = 0;
    Quat4 quaternion{};
    float ease_out = 0;
    float ease_in = 0;
    Quat4 out_control{};
    Quat4 in_control{};
    bool has_out_control = false;
    bool has_in_control = false;
};

struct TranslationKey {
    int32_t frame = 0;
    Vec3 position{};
    float ease_out = 0;
    float ease_in = 0;
    Vec3 in_tangent{};
    Vec3 out_tangent{};
};

struct AnimationTrack {
    uint32_t duration = 0;
    uint32_t rotation_stride = 0;
    std::vector<RotationKey> rotations;
    std::vector<TranslationKey> translations;
};

struct AnimationClip {
    std::string name;
    uint32_t resource_type = 0; // 4: linear, 6: spline.
    uint32_t duration = 0;
    std::vector<AnimationTrack> tracks; // Original node-directory order.
};

// Checked host records replace retail's in-place 32-bit track/key pointers.
bool ANIM_RelocLinearTracks(const std::vector<uint8_t>& bytes,
                            AnimationClip& clip, std::string& error);
bool ANIM_RelocSplineTracks(const std::vector<uint8_t>& bytes,
                            AnimationClip& clip, std::string& error);
bool ANIM_DecodeClip(const std::vector<uint8_t>& bytes, std::string_view name,
                     AnimationClip& clip, std::string& error);

// Q15 matrix conversion follows WINDREAM's integer products and SAR 14.
void MATH_QuatToMatrix(const Quat4& quaternion, Mat3& matrix);
// Retail fixed-table slerp at weight 0..256 using math_trig_tables().
Quat4 MATH_QuatSlerp(const Quat4& left, const Quat4& right, int32_t weight256);
float ANIM_ApplyEase(float t, float left_out, float right_in);
// Retail flags: bit 0 skips rotation, bit 1 skips translation. A skipped
// channel or a track with fewer than two keys leaves has_* false, meaning
// the node keeps its current value.
bool ANIM_EvalTrackLinear(const AnimationTrack& track, float frame,
                          unsigned flags, Mat3& rotation, Vec3& position,
                          bool& has_rotation, bool& has_position,
                          std::string& error);
bool ANIM_EvalTrackSpline(const AnimationTrack& track, float frame,
                          unsigned flags, Mat3& rotation, Vec3& position,
                          bool& has_rotation, bool& has_position,
                          std::string& error);
bool ANIM_ApplyModelLinear(const AnimationClip& clip, float frame,
                           const ModelGraph& bind, ModelGraph& pose,
                           bool follow_root_motion, std::string& error);
bool ANIM_ApplyModelSpline(const AnimationClip& clip, float frame,
                           const ModelGraph& bind, ModelGraph& pose,
                           bool follow_root_motion, std::string& error);

} // namespace od::port
