#pragma once
#include "render_scene.h"

// Display interpolation between two consecutive captured scenes (WD_INTERPOLATE).
// Host-only presentation: the blended values never reach the guest. Retail
// draws each game frame once; this draws extra frames between two of them.
namespace wd {

struct SceneMotion {
    // Per node of the newer scene: its index in the older one, or -1 when it
    // is drawn as captured (new node, other parent, or a jump: see blend).
    std::vector<int32_t> previous;
    bool camera = false; // false: a cut, the newer camera as captured
    size_t nodes = 0, vertices = 0;
    // The newer scene's own values, restored by restore_scene_motion.
    std::vector<od_pose_node> poses;
    std::vector<od_scene_vertex> source_vertices;
    std::array<float, 12> view{};
};

// Matches the newer scene's nodes to the older one's by node address and
// parent address, and saves what blend_scene overwrites.
void prepare_scene_motion(const SceneSnapshot &older, const SceneSnapshot &newer,
                          SceneMotion &motion);
// Overwrites newer's poses, vertices and camera view with the state alpha of
// the way from older to newer (0 = older, 1 = newer). Faces, materials,
// lights and the camera's projection stay the newer scene's.
void blend_scene(const SceneSnapshot &older, SceneSnapshot &newer, const SceneMotion &motion,
                 float alpha);
void restore_scene_motion(SceneSnapshot &newer, const SceneMotion &motion);

// A camera as blend_scene draws it: eye and row-major local rotation (the
// retail Q15 values scaled to 1).
struct CameraPose {
    float eye[3]{};
    float rotation[9]{};
};
// The camera blend_scene gives alpha of the way (the newer one at a cut).
CameraPose blended_camera(const SceneSnapshot &older, const SceneSnapshot &newer,
                          const SceneMotion &motion, float alpha);
void apply_camera(SceneSnapshot &scene, const CameraPose &camera);
// Moves `pose` toward `target`: the eye by `fraction` of the gap, the rotation
// by the same fraction of the arc. False when they are too far apart to be
// one camera's motion (a cut): then `pose` is left alone.
bool follow_camera(CameraPose &pose, const CameraPose &target, float fraction);

} // namespace wd
