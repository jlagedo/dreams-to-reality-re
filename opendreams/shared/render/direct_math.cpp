#include "render/direct.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

od_rect od_centered_canvas(int w, int h, int lw, int lh) {
    if (w <= 0 || h <= 0 || lw <= 0 || lh <= 0)
        return {};
    const double scale = std::min(double(w) / lw, double(h) / lh);
    const int cw = std::max(1, int(std::floor(lw * scale)));
    const int ch = std::max(1, int(std::floor(lh * scale)));
    return {(w - cw) / 2, (h - ch) / 2, cw, ch};
}
int od_canvas_point(od_rect v, int lw, int lh, float x, float y, float *ox, float *oy) {
    if (!ox || !oy || v.width <= 0 || v.height <= 0 || lw <= 0 || lh <= 0 || !std::isfinite(x) ||
        !std::isfinite(y))
        return 0;
    *ox = std::clamp((x - v.x) * float(lw) / v.width, 0.0f, float(lw - 1));
    *oy = std::clamp((y - v.y) * float(lh) / v.height, 0.0f, float(lh - 1));
    return x >= v.x && y >= v.y && x < v.x + v.width && y < v.y + v.height;
}
uint16_t od_pack_colour(uint32_t c, od_pixel_format f) {
    const uint32_t r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    return uint16_t(f == OD_RGB555 ? ((c >> 24) == 1 ? 0x8000u : 0u) | ((r >> 3) << 10) |
                                         ((g >> 3) << 5) | (b >> 3)
                                   : ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
uint32_t od_expand_colour(uint16_t c, od_pixel_format f) {
    uint32_t r = (c >> (f == OD_RGB555 ? 10 : 11)) & 31;
    uint32_t g = (c >> 5) & (f == OD_RGB555 ? 31 : 63), b = c & 31;
    r = (r << 3) | (r >> 2);
    b = (b << 3) | (b >> 2);
    g = f == OD_RGB555 ? (g << 3) | (g >> 2) : (g << 2) | (g >> 4);
    // RGB555's unused bit is observable to raw copies/fills. Reserve alpha
    // byte 1 for it; ordinary opaque alpha 255 denotes no padding bit.
    const uint32_t alpha = f == OD_RGB555 && (c & 0x8000) ? 1u : 255u;
    return r | (g << 8) | (b << 16) | (alpha << 24);
}
int od_compose_pose(const od_pose_node *nodes, size_t count, float *out) {
    if ((count && (!nodes || !out)) || count > 1000000)
        return 0;
    std::vector<uint8_t> done(count);
    std::vector<size_t> path;
    for (size_t start = 0; start < count; ++start) {
        size_t n = start;
        while (done[n] != 2) {
            if (done[n] == 1)
                return 0;
            done[n] = 1;
            path.push_back(n);
            const int32_t parent = nodes[n].parent;
            if (parent == -1)
                break;
            if (parent < 0 || size_t(parent) >= count)
                return 0;
            n = size_t(parent);
        }
        while (!path.empty()) {
            n = path.back();
            path.pop_back();
            const float *local = nodes[n].local;
            for (int i = 0; i < 12; ++i)
                if (!std::isfinite(local[i]))
                    return 0;
            float *world = out + n * 12;
            if (nodes[n].parent == -1)
                std::copy_n(local, 12, world);
            else {
                const float *parent = out + size_t(nodes[n].parent) * 12;
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 4; ++c) {
                        float v = c == 3 ? parent[r * 4 + 3] : 0.0f;
                        for (int k = 0; k < 3; ++k)
                            v += parent[r * 4 + k] * local[k * 4 + c];
                        if (!std::isfinite(v))
                            return 0;
                        world[r * 4 + c] = v;
                    }
            }
            done[n] = 2;
        }
    }
    return 1;
}
int od_expand_material_page(const uint8_t *indices, size_t pitch, const uint16_t *palette,
                            uint32_t *rgba, int lod) {
    if (!indices || !palette || !rgba || pitch < 256 || (lod != 128 && lod != 256))
        return 0;
    const unsigned step = 256u / unsigned(lod);
    const uint16_t key = palette[0];
    for (int y = 0; y < lod; ++y)
        for (int x = 0; x < lod; ++x) {
            const uint16_t c = palette[indices[size_t(y) * step * pitch + size_t(x) * step]];
            const uint32_t r = ((c >> 11) & 31u) << 3;
            const uint32_t g = ((c >> 5) & 63u) << 2;
            const uint32_t b = (c & 31u) << 3;
            rgba[size_t(y) * lod + x] = r | (g << 8) | (b << 16) | (c == key ? 0u : 0xff000000u);
        }
    return 1;
}
int od_projection_hor_plus(float fy, float aspect, float near_z, float far_z, float *m) {
    if (!m || !std::isfinite(fy) || !std::isfinite(aspect) || !std::isfinite(near_z) ||
        !std::isfinite(far_z) || fy <= 0 || aspect <= 0 || near_z <= 0 || far_z <= near_z)
        return 0;
    std::fill_n(m, 16, 0.0f);
    m[0] = fy / aspect;
    m[5] = fy;
    m[10] = -near_z / (far_z - near_z);
    m[11] = 1.0f;
    m[14] = near_z * far_z / (far_z - near_z);
    return 1;
}

int od_triangle_visible(const float *vertices, int cull_back) {
    if (!vertices)
        return -1;
    using Point = std::array<double, 4>;
    std::vector<Point> polygon(3), next;
    for (int i = 0; i < 12; ++i) {
        if (!std::isfinite(vertices[i]))
            return -1;
        polygon[i / 4][i % 4] = vertices[i];
    }
    auto distance = [](const Point &p, int plane) {
        switch (plane) {
        case 0:
            return p[3] + p[0];
        case 1:
            return p[3] - p[0];
        case 2:
            return p[3] + p[1];
        case 3:
            return p[3] - p[1];
        case 4:
            return p[2];
        default:
            return p[3] - p[2];
        }
    };
    for (int plane = 0; plane < 6 && !polygon.empty(); ++plane) {
        next.clear();
        Point previous = polygon.back();
        double pd = distance(previous, plane);
        for (const auto &current : polygon) {
            const double cd = distance(current, plane);
            if ((pd < 0) != (cd < 0)) {
                const double t = pd / (pd - cd);
                Point crossing{};
                for (int j = 0; j < 4; ++j)
                    crossing[j] = previous[j] + t * (current[j] - previous[j]);
                next.push_back(crossing);
            }
            if (cd >= 0)
                next.push_back(current);
            previous = current;
            pd = cd;
        }
        polygon.swap(next);
    }
    if (polygon.size() < 3)
        return 0;
    double area = 0;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const auto &a = polygon[i];
        const auto &b = polygon[(i + 1) % polygon.size()];
        if (a[3] <= 0 || b[3] <= 0)
            return 0;
        area += (a[0] * b[1] - b[0] * a[1]) / (a[3] * b[3]);
    }
    return cull_back ? area > 0 : area != 0;
}

int32_t od_radial_normal_dot(const int32_t *normal, const od_radial_light *light) {
    if (!normal || !light)
        return INT32_MIN;
    double value = 0;
    for (int axis = 0; axis < 3; ++axis)
        value += double(normal[axis]) * light->position[axis];
    value /= 32768.0;
    return !std::isfinite(value) || value >= 2147483648.0 || value < -2147483648.0 ? INT32_MIN
                                                                                   : int32_t(value);
}
int od_radial_flat_shade(const int32_t *vertices, const int32_t *normal, int32_t plane,
                         const od_radial_light *lights, size_t count, uint8_t *shade) {
    if (!vertices || !normal || !shade || (count && !lights) || count > 8)
        return 0;
    auto signed32 = [](uint32_t value) {
        int32_t result;
        std::memcpy(&result, &value, 4);
        return result;
    };
    auto chop = [](double value) {
        if (!std::isfinite(value) || value >= 2147483648.0 || value < -2147483648.0)
            return INT32_MIN;
        return int32_t(value);
    };
    int32_t center[3];
    for (int axis = 0; axis < 3; ++axis)
        center[axis] = signed32(uint32_t(vertices[axis]) + uint32_t(vertices[3 + axis]) +
                                uint32_t(vertices[6 + axis])) /
                       3;
    uint32_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto &light = lights[i];
        double squared = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const int32_t delta = signed32(uint32_t(center[axis]) - uint32_t(light.position[axis]));
            squared += double(delta) * delta;
        }
        const int32_t normal_dot = od_radial_normal_dot(normal, &light);
        const float distance = float(std::sqrt(squared));
        const int32_t product =
            signed32((uint32_t(plane) - uint32_t(normal_dot)) * uint32_t(light.intensity));
        int32_t contribution = chop(double(product) / distance);
        if (distance > double(light.inner_radius)) {
            if (distance >= float(light.outer_radius))
                contribution = 0;
            else
                contribution = chop(((double(light.outer_radius) - distance) / light.outer_radius) *
                                    double(contribution));
        }
        if (contribution < 0)
            sum += uint32_t(contribution);
    }
    int32_t total = signed32(sum);
    if (total < -31)
        total = -31;
    *shade = uint8_t(0u - uint32_t(total));
    return 1;
}
int od_radial_light_local(const float *world, const int32_t *position, int32_t *local) {
    if (!world || !position || !local)
        return 0;
    for (int i = 0; i < 12; ++i)
        if (!std::isfinite(world[i]))
            return 0;
    int32_t result[3];
    for (int axis = 0; axis < 3; ++axis) {
        double value = 0;
        for (int k = 0; k < 3; ++k)
            value += double(world[k * 4 + axis]) * (double(position[k]) - world[k * 4 + 3]);
        if (!std::isfinite(value) || value >= 2147483648.0 || value < -2147483648.0)
            return 0;
        result[axis] = int32_t(value);
    }
    std::copy_n(result, 3, local);
    return 1;
}
