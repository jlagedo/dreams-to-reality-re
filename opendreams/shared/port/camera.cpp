#include "port/camera.h"

#include <cstddef>
#include <cmath>

namespace od::port {
namespace {

int32_t le32(const uint8_t* data) {
    const uint32_t value = static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
    return static_cast<int32_t>(value);
}

int32_t preset_or_record(const uint8_t* record, size_t offset,
                         int32_t preset) {
    if (!record) return preset;
    const int32_t override_value = le32(record + offset);
    return override_value ? override_value : preset;
}

} // namespace

void CAM_StartFollow(FollowCamera& camera,
                     const std::array<int32_t,3>& actor_xyz,
                     int32_t actor_heading, const uint8_t* project_record) {
    constexpr double tau = 6.283185307179586476925286766559;
    // CAM_LoadPreset(0), CAM_UpdateFollowPos and the snapped ground branch of
    // CAM_ComputeChasePos. The project header can replace each default.
    const int32_t lead_lo = preset_or_record(project_record, 0x120, 1024);
    const int32_t lead_hi = preset_or_record(project_record, 0x124, 1024);
    const int32_t eye_lo = preset_or_record(project_record, 0x128, 528);
    const int32_t eye_hi = preset_or_record(project_record, 0x12c, 528);
    const int32_t height_lo = preset_or_record(project_record, 0x130, -80);
    const int32_t height_hi = preset_or_record(project_record, 0x134, -80);
    const double lead = (static_cast<double>(lead_lo) + lead_hi) * 0.5;
    const double distance = (static_cast<double>(eye_lo) + eye_hi) * 0.5;
    const double yaw = static_cast<double>((4096 - actor_heading) & 4095) *
                       tau / 4096.0;
    const double dx = std::sin(yaw), dz = std::cos(yaw);
    camera.target = {{
        actor_xyz[0] + static_cast<int32_t>(std::lround(lead * dx)),
        actor_xyz[1],
        actor_xyz[2] + static_cast<int32_t>(std::lround(lead * dz))
    }};
    camera.eye = {{
        actor_xyz[0] - static_cast<int32_t>(std::lround(distance * dx)),
        actor_xyz[1] + (height_lo + height_hi) / 2,
        actor_xyz[2] - static_cast<int32_t>(std::lround(distance * dz))
    }};
}

} // namespace od::port
