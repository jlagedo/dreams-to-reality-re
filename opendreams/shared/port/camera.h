#pragma once

#include <array>
#include <cstdint>

namespace od::port {

struct FollowCamera {
    std::array<int32_t,3> eye{};
    std::array<int32_t,3> target{};
};

// Static snapped entry pose of the retail ground follow camera. The active
// project record supplies preset-0 overrides; motion, easing and collision
// belong to the later live camera tick.
void CAM_StartFollow(FollowCamera& camera,
                     const std::array<int32_t,3>& actor_xyz,
                     int32_t actor_heading, const uint8_t* project_record);

} // namespace od::port
