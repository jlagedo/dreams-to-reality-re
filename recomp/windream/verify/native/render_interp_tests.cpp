// Display interpolation's blend (host/render/render_interp.cpp) on synthetic
// scenes: endpoints, midpoints, and what is drawn unblended.
#include "render_interp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#define CHECK(e)                                                                                   \
    do {                                                                                           \
        if (!(e)) {                                                                                \
            std::fprintf(stderr, "line %d: %s\n", __LINE__, #e);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
namespace {
bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }
// A pose turned `degrees` about Y (row-major), at `x` along X.
od_pose_node pose(int32_t parent, float degrees, float x, float scale = 1) {
    const float r = degrees * 3.14159265f / 180;
    od_pose_node p{};
    p.parent = parent;
    const float m[12] = {std::cos(r) * scale, 0, std::sin(r) * scale, x,
                         0, scale, 0, 0,
                         -std::sin(r) * scale, 0, std::cos(r) * scale, 0};
    std::copy(m, m + 12, p.local);
    return p;
}
// Root (the camera node) and one child node at `child`, under `parent`.
wd::SceneSnapshot scene(uint32_t child, uint32_t parent, od_pose_node child_pose, int eye_x,
                        int yaw_q15_sin = 0) {
    wd::SceneSnapshot s;
    s.camera.address = 0x1000;
    s.camera.eye = {eye_x, 0, 0};
    const int c = int(std::lround(std::sqrt(32768.0 * 32768 - double(yaw_q15_sin) * yaw_q15_sin)));
    s.camera.local_rotation = {c, 0, yaw_q15_sin, 0, 32768, 0, -yaw_q15_sin, 0, c};
    for (int row = 0; row < 3; ++row) { // as capture_scene derives it
        float translation = 0;
        for (int col = 0; col < 3; ++col) {
            const float value = float(s.camera.local_rotation[col * 3 + row]) / 32768;
            s.camera.view[row * 4 + col] = value;
            translation -= value * float(s.camera.eye[col]);
        }
        s.camera.view[row * 4 + 3] = translation;
    }
    wd::SceneNode root{}, node{};
    root.address = 0x1000;
    s.nodes.push_back(root);
    s.poses.push_back(pose(-1, 0, 0));
    if (parent != 0x1000) {
        wd::SceneNode middle{};
        middle.address = parent;
        s.nodes.push_back(middle);
        s.poses.push_back(pose(0, 0, 0));
    }
    node.address = child;
    s.nodes.push_back(node);
    child_pose.parent = int32_t(s.nodes.size()) - 2;
    s.poses.push_back(child_pose);
    return s;
}
float angle_of(const od_pose_node &p) { return std::atan2(p.local[2], p.local[0]) * 180 / 3.14159265f; }
} // namespace

int main() {
    // Matched node: translation and rotation blended; 0 and 1 are the ends.
    {
        auto older = scene(0x2000, 0x1000, pose(0, 0, 0), 0);
        auto newer = scene(0x2000, 0x1000, pose(0, 90, 100), 30);
        wd::SceneMotion motion;
        wd::prepare_scene_motion(older, newer, motion);
        CHECK(motion.nodes == 2 && motion.previous[1] == 1 && motion.camera);
        wd::blend_scene(older, newer, motion, 0.5f);
        CHECK(near(newer.poses[1].local[3], 50));
        CHECK(near(angle_of(newer.poses[1]), 45, 1e-3f));
        CHECK(near(newer.camera.view[3], -15)); // eye x 15: view translation -15
        wd::blend_scene(older, newer, motion, 0);
        CHECK(near(newer.poses[1].local[3], 0) && near(angle_of(newer.poses[1]), 0, 1e-3f));
        wd::blend_scene(older, newer, motion, 1);
        CHECK(near(newer.poses[1].local[3], 100) && near(angle_of(newer.poses[1]), 90, 1e-3f));
        wd::restore_scene_motion(newer, motion);
        CHECK(newer.poses[1].local[3] == 100 && near(newer.camera.view[3], -30));
    }
    // A jump, a new node and a new parent are drawn as the newer scene has them.
    {
        auto older = scene(0x2000, 0x1000, pose(0, 0, 0), 0);
        auto jumped = scene(0x2000, 0x1000, pose(0, 0, 5000), 0);
        auto added = scene(0x2100, 0x1000, pose(0, 0, 10), 0);
        auto moved = scene(0x2000, 0x3000, pose(0, 0, 10), 0);
        for (auto *newer : {&jumped, &added, &moved}) {
            wd::SceneMotion motion;
            wd::prepare_scene_motion(older, *newer, motion);
            CHECK(motion.previous.back() == -1);
            const float x = newer->poses.back().local[3];
            wd::blend_scene(older, *newer, motion, 0.5f);
            CHECK(newer->poses.back().local[3] == x);
        }
    }
    // A turn over 90 degrees in one step is not blended either.
    {
        auto older = scene(0x2000, 0x1000, pose(0, 0, 0), 0);
        auto newer = scene(0x2000, 0x1000, pose(0, 120, 0), 0);
        wd::SceneMotion motion;
        wd::prepare_scene_motion(older, newer, motion);
        CHECK(motion.previous[1] == -1);
    }
    // A scaled matrix is not a rotation: element by element.
    {
        auto older = scene(0x2000, 0x1000, pose(0, 0, 0, 1), 0);
        auto newer = scene(0x2000, 0x1000, pose(0, 0, 0, 2), 0);
        wd::SceneMotion motion;
        wd::prepare_scene_motion(older, newer, motion);
        wd::blend_scene(older, newer, motion, 0.5f);
        CHECK(near(newer.poses[1].local[0], 1.5f) && near(newer.poses[1].local[5], 1.5f));
    }
    // A camera cut (eye jump or a turn over 45 degrees) shows the newer camera.
    {
        auto older = scene(0x2000, 0x1000, pose(0, 0, 0), 0);
        auto far = scene(0x2000, 0x1000, pose(0, 0, 0), 5000);
        auto turned = scene(0x2000, 0x1000, pose(0, 0, 0), 0, 30000);
        for (auto *newer : {&far, &turned}) {
            wd::SceneMotion motion;
            wd::prepare_scene_motion(older, *newer, motion);
            CHECK(!motion.camera);
            const auto view = newer->camera.view;
            wd::blend_scene(older, *newer, motion, 0.5f);
            for (size_t i = 0; i < view.size(); ++i)
                CHECK(near(newer->camera.view[i], view[i]));
        }
    }
    // Following a camera: a fraction of the gap; too far is refused.
    {
        wd::CameraPose pose{}, target{};
        for (int i = 0; i < 9; i += 4)
            pose.rotation[i] = target.rotation[i] = 1;
        target.eye[0] = 100;
        CHECK(wd::follow_camera(pose, target, 0.25f) && near(pose.eye[0], 25));
        target.eye[0] = 10000;
        CHECK(!wd::follow_camera(pose, target, 0.25f) && near(pose.eye[0], 25));
    }
    std::puts("render interpolation tests passed");
    return 0;
}
