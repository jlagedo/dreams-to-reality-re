#include "render_interp.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace wd {
namespace {
// A step larger than these is a cut or a teleport, not motion: drawn as the
// newer scene has it. Scene units per game step (Duncan walks about 30).
constexpr float node_jump = 2048, camera_jump = 1024;
// Quaternion dot products: cos(half the angle) of 90 degrees for a node,
// 45 degrees for the camera.
constexpr float node_turn = 0.7071f, camera_turn = 0.9239f;

struct Quat {
    float w, x, y, z;
};
// Row-major 3x3 m[r * stride + c]. False when the matrix is not a rotation
// (scaled, sheared or mirrored): those are blended element by element.
bool to_quat(const float *m, int stride, Quat &q) {
    const auto at = [&](int r, int c) { return m[r * stride + c]; };
    for (int c = 0; c < 3; ++c) {
        const float length =
            std::sqrt(at(0, c) * at(0, c) + at(1, c) * at(1, c) + at(2, c) * at(2, c));
        if (std::fabs(length - 1) > 0.03f)
            return false;
    }
    const float det = at(0, 0) * (at(1, 1) * at(2, 2) - at(1, 2) * at(2, 1)) -
                      at(0, 1) * (at(1, 0) * at(2, 2) - at(1, 2) * at(2, 0)) +
                      at(0, 2) * (at(1, 0) * at(2, 1) - at(1, 1) * at(2, 0));
    if (det < 0.9f)
        return false;
    const float trace = at(0, 0) + at(1, 1) + at(2, 2);
    if (trace > 0) {
        const float s = std::sqrt(trace + 1) * 2;
        q = {0.25f * s, (at(2, 1) - at(1, 2)) / s, (at(0, 2) - at(2, 0)) / s,
             (at(1, 0) - at(0, 1)) / s};
    } else if (at(0, 0) > at(1, 1) && at(0, 0) > at(2, 2)) {
        const float s = std::sqrt(1 + at(0, 0) - at(1, 1) - at(2, 2)) * 2;
        q = {(at(2, 1) - at(1, 2)) / s, 0.25f * s, (at(0, 1) + at(1, 0)) / s,
             (at(0, 2) + at(2, 0)) / s};
    } else if (at(1, 1) > at(2, 2)) {
        const float s = std::sqrt(1 + at(1, 1) - at(0, 0) - at(2, 2)) * 2;
        q = {(at(0, 2) - at(2, 0)) / s, (at(0, 1) + at(1, 0)) / s, 0.25f * s,
             (at(1, 2) + at(2, 1)) / s};
    } else {
        const float s = std::sqrt(1 + at(2, 2) - at(0, 0) - at(1, 1)) * 2;
        q = {(at(1, 0) - at(0, 1)) / s, (at(0, 2) + at(2, 0)) / s, (at(1, 2) + at(2, 1)) / s,
             0.25f * s};
    }
    const float n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    q = {q.w / n, q.x / n, q.y / n, q.z / n};
    return true;
}
void from_quat(const Quat &q, float *m, int stride) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    const float r[9] = {1 - 2 * (yy + zz), 2 * (xy - wz),     2 * (xz + wy),
                        2 * (xy + wz),     1 - 2 * (xx + zz), 2 * (yz - wx),
                        2 * (xz - wy),     2 * (yz + wx),     1 - 2 * (xx + yy)};
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c)
            m[row * stride + c] = r[row * 3 + c];
}
float dot(const Quat &a, const Quat &b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }
Quat slerp(Quat a, const Quat &b, float t) {
    float d = dot(a, b);
    if (d < 0) {
        a = {-a.w, -a.x, -a.y, -a.z};
        d = -d;
    }
    float wa = 1 - t, wb = t;
    if (d < 0.9995f) {
        const float angle = std::acos(d), s = std::sin(angle);
        wa = std::sin((1 - t) * angle) / s;
        wb = std::sin(t * angle) / s;
    }
    Quat q{wa * a.w + wb * b.w, wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z};
    const float n = std::sqrt(dot(q, q));
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}
// The rotation of `out` (stride 4 or 3) between a and b; false when the
// turn is larger than `limit` allows.
bool blend_rotation(const float *a, const float *b, int stride, float t, float limit, float *out) {
    Quat qa, qb;
    if (to_quat(a, stride, qa) && to_quat(b, stride, qb)) {
        if (std::fabs(dot(qa, qb)) < limit)
            return false;
        from_quat(slerp(qa, qb, t), out, stride);
        return true;
    }
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * stride + c] = a[r * stride + c] + (b[r * stride + c] - a[r * stride + c]) * t;
    return true;
}
float distance(const float *a, const float *b, int stride) {
    float sum = 0;
    for (int i = 0; i < 3; ++i)
        sum += (a[i * stride] - b[i * stride]) * (a[i * stride] - b[i * stride]);
    return std::sqrt(sum);
}
// SceneCamera::view as capture_scene derives it, from a float eye and a
// row-major local rotation (Q15 scaled to 1).
void camera_view(const float eye[3], const float rotation[9], std::array<float, 12> &view) {
    for (int row = 0; row < 3; ++row) {
        float translation = 0;
        for (int col = 0; col < 3; ++col) {
            const float value = rotation[col * 3 + row];
            view[row * 4 + col] = value;
            translation -= value * eye[col];
        }
        view[row * 4 + 3] = translation;
    }
}
} // namespace

void prepare_scene_motion(const SceneSnapshot &older, const SceneSnapshot &newer,
                          SceneMotion &motion) {
    motion.poses = newer.poses;
    motion.source_vertices = newer.vertices;
    motion.view = newer.camera.view;
    motion.previous.assign(newer.nodes.size(), -1);
    motion.nodes = motion.vertices = 0;
    std::unordered_map<uint32_t, int32_t> index;
    index.reserve(older.nodes.size());
    for (size_t i = 0; i < older.nodes.size(); ++i)
        index.emplace(older.nodes[i].address, int32_t(i));
    const auto parent_address = [](const SceneSnapshot &scene, int32_t parent) {
        return parent < 0 ? 0u : scene.nodes[size_t(parent)].address;
    };
    for (size_t i = 0; i < newer.nodes.size(); ++i) {
        const auto found = index.find(newer.nodes[i].address);
        if (found == index.end() || i >= newer.poses.size() ||
            size_t(found->second) >= older.poses.size())
            continue;
        const auto &a = older.poses[size_t(found->second)], &b = newer.poses[i];
        if (parent_address(older, a.parent) != parent_address(newer, b.parent))
            continue;
        if (distance(a.local + 3, b.local + 3, 4) > node_jump)
            continue;
        float probe[12];
        if (!blend_rotation(a.local, b.local, 4, 0.5f, node_turn, probe))
            continue;
        motion.previous[i] = found->second;
        ++motion.nodes;
        const auto &na = older.nodes[size_t(found->second)], &nb = newer.nodes[i];
        if (na.vertex_count == nb.vertex_count && nb.vertex_count &&
            older.vertex_addresses[na.first_vertex] == newer.vertex_addresses[nb.first_vertex])
            for (uint32_t k = 0; k < nb.vertex_count; ++k) {
                const auto &va = older.vertices[na.first_vertex + k].xyz;
                const auto &vb = newer.vertices[nb.first_vertex + k].xyz;
                if (va[0] != vb[0] || va[1] != vb[1] || va[2] != vb[2])
                    ++motion.vertices;
            }
    }
    const auto &ca = older.camera, &cb = newer.camera;
    float ra[9], rb[9], probe[9];
    for (int i = 0; i < 9; ++i) {
        ra[i] = float(ca.local_rotation[i]) / 32768;
        rb[i] = float(cb.local_rotation[i]) / 32768;
    }
    const float ea[3] = {float(ca.eye[0]), float(ca.eye[1]), float(ca.eye[2])};
    const float eb[3] = {float(cb.eye[0]), float(cb.eye[1]), float(cb.eye[2])};
    motion.camera = ca.address == cb.address && distance(ea, eb, 1) <= camera_jump &&
                    blend_rotation(ra, rb, 3, 0.5f, camera_turn, probe);
}

void blend_scene(const SceneSnapshot &older, SceneSnapshot &newer, const SceneMotion &motion,
                 float alpha) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    for (size_t i = 0; i < motion.previous.size(); ++i) {
        const int32_t j = motion.previous[i];
        if (j < 0)
            continue;
        const auto &a = older.poses[size_t(j)], &b = motion.poses[i];
        auto &out = newer.poses[i];
        blend_rotation(a.local, b.local, 4, alpha, -1, out.local);
        for (int r = 0; r < 3; ++r)
            out.local[r * 4 + 3] = a.local[r * 4 + 3] + (b.local[r * 4 + 3] - a.local[r * 4 + 3]) * alpha;
        if (!motion.vertices)
            continue;
        const auto &na = older.nodes[size_t(j)], &nb = newer.nodes[i];
        if (na.vertex_count != nb.vertex_count || !nb.vertex_count ||
            older.vertex_addresses[na.first_vertex] != newer.vertex_addresses[nb.first_vertex])
            continue;
        for (uint32_t k = 0; k < nb.vertex_count; ++k) {
            const auto &va = older.vertices[na.first_vertex + k].xyz;
            const auto &vb = motion.source_vertices[nb.first_vertex + k].xyz;
            auto &vo = newer.vertices[nb.first_vertex + k].xyz;
            for (int axis = 0; axis < 3; ++axis)
                vo[axis] = va[axis] + (vb[axis] - va[axis]) * alpha;
        }
    }
    apply_camera(newer, blended_camera(older, newer, motion, alpha));
}

CameraPose blended_camera(const SceneSnapshot &older, const SceneSnapshot &newer,
                          const SceneMotion &motion, float alpha) {
    const auto &ca = older.camera, &cb = newer.camera;
    CameraPose pose;
    float ra[9], rb[9];
    for (int i = 0; i < 9; ++i) {
        ra[i] = float(ca.local_rotation[i]) / 32768;
        rb[i] = float(cb.local_rotation[i]) / 32768;
    }
    if (!motion.camera) {
        std::copy_n(rb, 9, pose.rotation);
        for (int i = 0; i < 3; ++i)
            pose.eye[i] = float(cb.eye[i]);
        return pose;
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    blend_rotation(ra, rb, 3, alpha, -1, pose.rotation);
    for (int i = 0; i < 3; ++i)
        pose.eye[i] = float(ca.eye[i]) + float(cb.eye[i] - ca.eye[i]) * alpha;
    return pose;
}

void apply_camera(SceneSnapshot &scene, const CameraPose &camera) {
    camera_view(camera.eye, camera.rotation, scene.camera.view);
}

bool follow_camera(CameraPose &pose, const CameraPose &target, float fraction) {
    if (distance(pose.eye, target.eye, 1) > camera_jump)
        return false;
    float rotation[9];
    if (!blend_rotation(pose.rotation, target.rotation, 3, fraction, camera_turn, rotation))
        return false;
    std::copy_n(rotation, 9, pose.rotation);
    for (int i = 0; i < 3; ++i)
        pose.eye[i] += (target.eye[i] - pose.eye[i]) * fraction;
    return true;
}

void restore_scene_motion(SceneSnapshot &newer, const SceneMotion &motion) {
    newer.poses = motion.poses;
    newer.vertices = motion.source_vertices;
    newer.camera.view = motion.view;
}

} // namespace wd
