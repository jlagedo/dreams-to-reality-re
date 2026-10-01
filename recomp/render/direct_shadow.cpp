#include "render/direct_shadow.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
int32_t signed32(uint32_t bits) {
    int32_t v;
    std::memcpy(&v, &bits, 4);
    return v;
}
int32_t chop(double v) {
    return !std::isfinite(v) || v >= 2147483648.0 || v < -2147483648.0 ? INT32_MIN : int32_t(v);
}
bool project(const od_shadow_packet &p, std::vector<int32_t> &poses,
             std::vector<std::array<float, 3>> &view, std::vector<std::array<int32_t, 2>> &screen) {
    if (!p.nodes || !p.node_count || p.node_count > 10000 || p.vertex_count > 1000000 ||
        (p.vertex_count && !p.vertices) || p.near_plane <= 0 || p.far_plane <= p.near_plane ||
        !std::isfinite(p.focal[0]) || !std::isfinite(p.focal[1]) || p.focal[0] <= 0 ||
        p.focal[1] <= 0 || p.nodes[0].parent != -1)
        return false;
    poses.resize(p.node_count * 12);
    auto *camera = poses.data();
    for (unsigned r = 0; r < 3; ++r) {
        uint32_t sum = 0;
        for (unsigned c = 0; c < 3; ++c) {
            camera[3 + r * 3 + c] = p.camera_rotation[c * 3 + r];
            sum += uint32_t(camera[3 + r * 3 + c]) * uint32_t(p.camera_eye[c]);
        }
        camera[r] = signed32(0u - uint32_t(signed32(sum) >> 15));
    }
    // Captured source hierarchy is pre-order, including hidden corner owners.
    for (size_t i = 1; i < p.node_count; ++i) {
        const auto &node = p.nodes[i];
        if (node.parent < 0 || size_t(node.parent) >= i)
            return false;
        const int32_t *parent = poses.data() + size_t(node.parent) * 12;
        int32_t *out = poses.data() + i * 12;
        for (unsigned r = 0; r < 3; ++r) {
            double value = 0;
            for (unsigned k = 0; k < 3; ++k)
                value += double(parent[3 + r * 3 + k]) * node.translation[k];
            out[r] = signed32(uint32_t(parent[r]) + uint32_t(chop(value / 32768.0)));
            for (unsigned c = 0; c < 3; ++c) {
                uint32_t sum = 0;
                for (unsigned k = 0; k < 3; ++k)
                    sum += uint32_t(parent[3 + r * 3 + k]) * uint32_t(node.rotation[k * 3 + c]);
                out[3 + r * 3 + c] = signed32(sum) >> 15;
            }
        }
    }
    view.resize(p.vertex_count);
    screen.resize(p.vertex_count);
    for (size_t i = 0; i < p.vertex_count; ++i) {
        const auto &v = p.vertices[i];
        if (v.node >= p.node_count)
            return false;
        const auto *m = poses.data() + size_t(v.node) * 12;
        for (unsigned r = 0; r < 3; ++r) {
            // Retail spills normalized rotation, local XYZ and translation to
            // float, then evaluates this sum on the 53-bit x87 stack.
            view[i][r] = float(double(float(m[r])) +
                               double(float(v.xyz[2])) * float(double(m[3 + r * 3 + 2]) / 32768.0) +
                               double(float(v.xyz[0])) * float(double(m[3 + r * 3]) / 32768.0) +
                               double(float(v.xyz[1])) * float(double(m[3 + r * 3 + 1]) / 32768.0));
        }
        for (unsigned axis = 0; axis < 2; ++axis)
            screen[i][axis] =
                chop(double(view[i][axis]) * p.focal[axis] / view[i][2] + p.center[axis]);
    }
    return true;
}
} // namespace
int od_shadow_project(const od_shadow_packet *p, int32_t *poses, float *view, int32_t *screen) {
    if (!p)
        return 0;
    std::vector<int32_t> m;
    std::vector<std::array<float, 3>> v;
    std::vector<std::array<int32_t, 2>> xy;
    if (!project(*p, m, v, xy))
        return 0;
    if (poses)
        std::copy(m.begin(), m.end(), poses);
    for (size_t i = 0; i < v.size(); ++i) {
        if (view)
            std::copy(v[i].begin(), v[i].end(), view + i * 3);
        if (screen)
            std::copy(xy[i].begin(), xy[i].end(), screen + i * 2);
    }
    return 1;
}
namespace od {
bool prepare_shadow(const od_shadow_packet &p, std::vector<ShadowTriangle> &out,
                    std::string &error) {
    std::vector<int32_t> poses;
    std::vector<std::array<float, 3>> view;
    std::vector<std::array<int32_t, 2>> screen;
    if (!project(p, poses, view, screen) || p.triangle_count > 1000000 ||
        (p.triangle_count && !p.triangles)) {
        error = "invalid source shadow packet";
        return false;
    }
    for (size_t i = 0; i < p.triangle_count; ++i) {
        const auto &t = p.triangles[i];
        if (t.owner >= p.node_count || t.vertices[0] >= view.size() ||
            t.vertices[1] >= view.size() || t.vertices[2] >= view.size()) {
            error = "invalid shadow corner/owner";
            return false;
        }
        const auto *m = poses.data() + size_t(t.owner) * 12;
        if (!(t.flags & 8)) {
            uint32_t dot = uint32_t(t.retained_dot);
            if (!t.stale_normal) {
                dot = 0;
                for (unsigned c = 0; c < 3; ++c) {
                    double value = 0;
                    for (unsigned r = 0; r < 3; ++r)
                        value += double(m[3 + r * 3 + c]) * m[r];
                    const auto eye = chop(-value / 32768.0);
                    dot += uint32_t(signed32(uint32_t(eye) * uint32_t(t.normal[c])) / 32768);
                }
            }
            if (signed32(dot - uint32_t(t.plane)) < 0)
                continue;
        } else {
            const auto &a = view[t.vertices[0]], &b = view[t.vertices[1]], &c = view[t.vertices[2]];
            const float u[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
            const float v[3] = {b[0] - c[0], b[1] - c[1], b[2] - c[2]};
            const double facing = (double(u[1]) * v[2] - double(u[2]) * v[1]) * a[0] +
                                  (double(u[2]) * v[0] - double(u[0]) * v[2]) * a[1] +
                                  (double(u[0]) * v[1] - double(u[1]) * v[0]) * a[2];
            if (facing >= 0)
                continue;
        }
        std::vector<std::array<float, 3>> polygon;
        for (auto index : t.vertices) {
            if (chop(view[index][2]) >= p.far_plane) {
                polygon.clear();
                break;
            }
            polygon.push_back(view[index]);
        }
        if (polygon.empty())
            continue;
        std::vector<std::array<float, 3>> clipped;
        auto previous = polygon.back();
        for (const auto &current : polygon) {
            const bool a = previous[2] >= p.near_plane, b = current[2] >= p.near_plane;
            if (a != b) {
                const double fraction =
                    (double(p.near_plane) - previous[2]) / (double(current[2]) - previous[2]);
                std::array<float, 3> crossing;
                for (unsigned k = 0; k < 2; ++k)
                    crossing[k] =
                        float(previous[k] + fraction * (double(current[k]) - previous[k]));
                crossing[2] = float(p.near_plane);
                clipped.push_back(crossing);
            }
            if (b)
                clipped.push_back(current);
            previous = current;
        }
        std::vector<std::array<int32_t, 2>> projected;
        for (const auto &v : clipped) {
            std::array<int32_t, 2> xy;
            for (unsigned a = 0; a < 2; ++a)
                xy[a] = chop(double(v[a]) * p.focal[a] / v[2] + p.center[a]);
            projected.push_back(xy);
        }
        for (size_t j = 1; j + 1 < projected.size(); ++j) {
            ShadowTriangle triangle{projected[0], projected[j], projected[j + 1]};
            const auto &a = triangle[0], &b = triangle[1], &c = triangle[2];
            const int32_t area =
                signed32((uint32_t(b[0]) - uint32_t(a[0])) * (uint32_t(c[1]) - uint32_t(b[1])) -
                         (uint32_t(c[0]) - uint32_t(b[0])) * (uint32_t(b[1]) - uint32_t(a[1])));
            if (area < 0)
                out.push_back(triangle);
        }
    }
    return true;
}
} // namespace od
