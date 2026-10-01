#pragma once
#include "render/direct.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace od {
namespace edge_detail {
using Point = std::array<double, 3>;
inline Point subtract(const Point &a, const Point &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
inline double dot(const Point &a, const Point &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline double distance2(const Point &a, const Point &b) {
    const auto difference = subtract(a, b);
    return dot(difference, difference);
}
struct Vertex {
    Point source{}, anchor{}, lower{}, upper{};
    size_t parent = 0;
    bool anchored = false;
};
struct Edge {
    size_t a, b, triangle;
    uint32_t root;
    Point normal;
};
struct Grid {
    int64_t x, y, z;
    uint32_t root;
    bool operator==(const Grid &other) const {
        return x == other.x && y == other.y && z == other.z && root == other.root;
    }
};
struct GridHash {
    size_t operator()(const Grid &value) const {
        uint64_t hash = uint64_t(value.x) * 0x9e3779b185ebca87ULL;
        hash ^= uint64_t(value.y) * 0xc2b2ae3d27d4eb4fULL;
        hash ^= uint64_t(value.z) * 0x165667b19e3779f9ULL;
        return size_t(hash ^ (uint64_t(value.root) * 0x85ebca77c2b2ae63ULL));
    }
};
inline bool model_roots(const od_scene_packet &packet, std::vector<uint32_t> &roots,
                         std::string &error) {
    roots.assign(packet.node_count, UINT32_MAX);
    std::vector<size_t> path;
    for (size_t start = 0; start < roots.size(); ++start) {
        size_t current = start;
        path.clear();
        while (roots[current] == UINT32_MAX) {
            if (path.size() >= packet.node_count) {
                error = "cyclic source edge hierarchy";
                return false;
            }
            path.push_back(current);
            const int32_t parent = packet.nodes[current].parent;
            if (parent < -1 || (parent >= 0 && size_t(parent) >= packet.node_count)) {
                error = "invalid source edge parent";
                return false;
            }
            if (parent == -1 || packet.nodes[parent].parent == -1) {
                roots[current] = uint32_t(current);
                break;
            }
            current = size_t(parent);
        }
        for (size_t node : path)
            roots[node] = roots[current];
    }
    return true;
}
} // namespace edge_detail

// Posed positions are the existing shared renderer's corner stream. Only XYZ
// changes; stride can include UVs/brightness. This is source-edge geometry work,
// never screen coverage/readback. Outputs are published only after validation.
inline bool stitch_source_edges(const od_scene_packet &packet, float *positions, size_t stride,
                                 size_t &joined, std::string &error,
                                 std::unordered_map<uint32_t, size_t> *joined_roots = nullptr) {
    using namespace edge_detail;
    joined = 0;
    const double quantum = packet.source_edge_quantum;
    if (!std::isfinite(quantum) || quantum < 0) {
        error = "invalid source edge quantum";
        return false;
    }
    if (!quantum || !packet.triangle_count)
        return true;
    if (!positions || stride < 3 || packet.node_count > 1000000 ||
        packet.triangle_count > 1000000 || !packet.nodes || !packet.triangles) {
        error = "invalid source edge arrays";
        return false;
    }
    std::vector<uint32_t> roots;
    if (!model_roots(packet, roots, error))
        return false;
    std::vector<Vertex> vertices;
    std::vector<size_t> corners(packet.triangle_count * 3);
    std::unordered_map<uint64_t, size_t> identities;
    std::unordered_map<Grid, size_t, GridHash> coincident;
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto &corner = packet.triangles[i / 3].corners[i % 3];
        if (corner.node >= packet.node_count) {
            error = "invalid source edge corner";
            return false;
        }
        const uint64_t key = (uint64_t(corner.node) << 32) | corner.vertex;
        const auto known = identities.find(key);
        if (known != identities.end()) {
            corners[i] = known->second;
        } else {
            const float *source = positions + i * stride;
            Point point{source[0], source[1], source[2]};
            if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2])) {
                error = "nonfinite source edge position";
                return false;
            }
            uint32_t bits[3];
            for (unsigned axis = 0; axis < 3; ++axis) {
                const float value = source[axis] == 0 ? 0 : source[axis];
                std::memcpy(bits + axis, &value, 4);
            }
            const auto alias = coincident.emplace(
                Grid{bits[0], bits[1], bits[2], roots[corner.node]}, vertices.size());
            corners[i] = alias.first->second;
            identities.emplace(key, corners[i]);
            if (alias.second)
                vertices.push_back({point, point, point, point, vertices.size(), false});
        }
    }
    auto root_of = [&](size_t id) {
        size_t root = id;
        while (vertices[root].parent != root)
            root = vertices[root].parent;
        while (vertices[id].parent != id) {
            const size_t next = vertices[id].parent;
            vertices[id].parent = root;
            id = next;
        }
        return root;
    };
    std::vector<Edge> edges;
    std::vector<bool> protected_vertices(vertices.size());
    std::unordered_map<uint64_t, size_t> edge_counts;
    const double tolerance2 = quantum * quantum * 1.000001;
    const auto edge_key = [](size_t a, size_t b) {
        return (uint64_t(std::min(a, b)) << 32) | uint64_t(std::max(a, b));
    };
    for (size_t i = 0; i < packet.triangle_count; ++i) {
        const auto &triangle = packet.triangles[i];
        if (triangle.mode != OD_FACE_OPAQUE) {
            for (unsigned c = 0; c < 3; ++c)
                protected_vertices[corners[i * 3 + c]] = true;
            continue;
        }
        const auto a = vertices[corners[i * 3]].source;
        const auto b = vertices[corners[i * 3 + 1]].source;
        const auto c = vertices[corners[i * 3 + 2]].source;
        const auto ab = subtract(b, a), ac = subtract(c, a);
        Point normal{ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2],
                     ab[0] * ac[1] - ab[1] * ac[0]};
        const double area2 = std::sqrt(dot(normal, normal));
        const double longest = std::sqrt(std::max({distance2(a, b), distance2(a, c), distance2(b, c)}));
        // Protect tiny details and guarantee a bounded endpoint movement cannot
        // collapse a triangle. Parallel/layered faces are rejected below.
        if (!(area2 > 4 * quantum * longest)) {
            for (unsigned c = 0; c < 3; ++c)
                protected_vertices[corners[i * 3 + c]] = true;
            continue;
        }
        for (double &component : normal)
            component /= area2;
        for (size_t c = 0; c < 3; ++c) {
            const size_t x = corners[i * 3 + c], y = corners[i * 3 + (c + 1) % 3];
            const auto ra = roots[triangle.corners[c].node];
            const auto rb = roots[triangle.corners[(c + 1) % 3].node];
            if (ra != rb)
                continue;
            edges.push_back({x, y, i, ra, normal});
            ++edge_counts[edge_key(x, y)];
        }
    }
    std::unordered_map<Grid, std::vector<size_t>, GridHash> buckets;
    for (size_t index = 0; index < edges.size(); ++index) {
        const auto &edge = edges[index];
        if (protected_vertices[edge.a] || protected_vertices[edge.b])
            continue;
        if (edge_counts[edge_key(edge.a, edge.b)] != 1)
            continue;
        const auto &a = vertices[edge.a].source, &b = vertices[edge.b].source;
        double midpoint[3];
        for (unsigned axis = 0; axis < 3; ++axis) {
            midpoint[axis] = std::floor((a[axis] + b[axis]) / (2 * quantum));
            if (std::abs(midpoint[axis]) > double(INT64_MAX / 2)) {
                error = "source edge coordinates exceed grid range";
                return false;
            }
        }
        const Grid key{int64_t(midpoint[0]), int64_t(midpoint[1]), int64_t(midpoint[2]), edge.root};
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z) {
                    const auto found = buckets.find({key.x + x, key.y + y, key.z + z, key.root});
                    if (found == buckets.end())
                        continue;
                    for (size_t candidate : found->second) {
                        const auto &other = edges[candidate];
                        const auto &c = vertices[other.b].source, &d = vertices[other.a].source;
                        const double normal_dot = dot(edge.normal, other.normal);
                        // Coherent opposite edge directions, distinct planes,
                        // and two nearby endpoints establish the authored join.
                        if (other.triangle == edge.triangle || std::abs(normal_dot) > .95 ||
                            distance2(a, c) > tolerance2 || distance2(b, d) > tolerance2)
                            continue;
                        Point anchors[2];
                        const size_t ids[2][2] = {{edge.a, other.b}, {edge.b, other.a}};
                        bool valid = true;
                        for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
                            const auto &p = vertices[ids[endpoint][0]].source;
                            const auto &q = vertices[ids[endpoint][1]].source;
                            Point middle;
                            for (unsigned axis = 0; axis < 3; ++axis)
                                middle[axis] = (p[axis] + q[axis]) * .5;
                            const double pa = dot(edge.normal, subtract(p, middle));
                            const double pb = dot(other.normal, subtract(q, middle));
                            const double scale = 1 / (1 - normal_dot * normal_dot);
                            const double u = (pa - normal_dot * pb) * scale;
                            const double v = (pb - normal_dot * pa) * scale;
                            auto &anchor = anchors[endpoint];
                            for (unsigned axis = 0; axis < 3; ++axis)
                                anchor[axis] = middle[axis] + u * edge.normal[axis] + v * other.normal[axis];
                            for (size_t id : ids[endpoint]) {
                                const auto &group = vertices[root_of(id)];
                                if (group.anchored) {
                                    if (distance2(anchor, group.anchor) > tolerance2 * 1e-8)
                                        valid = false;
                                    anchor = group.anchor;
                                }
                                if (distance2(anchor, vertices[id].source) > tolerance2)
                                    valid = false;
                            }
                            const auto &ga = vertices[root_of(ids[endpoint][0])];
                            const auto &gb = vertices[root_of(ids[endpoint][1])];
                            double maximum_move2 = 0;
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                const double lo = std::min(ga.lower[axis], gb.lower[axis]);
                                const double hi = std::max(ga.upper[axis], gb.upper[axis]);
                                const double extent = std::max(std::abs(anchor[axis] - lo),
                                                               std::abs(anchor[axis] - hi));
                                maximum_move2 += extent * extent;
                            }
                            if (maximum_move2 > tolerance2)
                                valid = false;
                        }
                        if (!valid)
                            continue;
                        for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
                            const auto aroot = root_of(ids[endpoint][0]);
                            const auto broot = root_of(ids[endpoint][1]);
                            vertices[broot].parent = aroot;
                            vertices[aroot].anchor = anchors[endpoint];
                            vertices[aroot].anchored = true;
                            for (unsigned axis = 0; axis < 3; ++axis) {
                                vertices[aroot].lower[axis] = std::min(vertices[aroot].lower[axis],
                                                                      vertices[broot].lower[axis]);
                                vertices[aroot].upper[axis] = std::max(vertices[aroot].upper[axis],
                                                                      vertices[broot].upper[axis]);
                            }
                        }
                        ++joined;
                        if (joined_roots)
                            ++(*joined_roots)[edge.root];
                    }
                }
        // Bounded local candidate list prevents dense duplicate geometry from
        // degrading to a quadratic search; ambiguous overflow stays unchanged.
        auto &bucket = buckets[key];
        if (bucket.size() < 32)
            bucket.push_back(index);
    }
    for (size_t i = 0; i < corners.size(); ++i) {
        const auto &vertex = vertices[root_of(corners[i])];
        if (vertex.anchored)
            for (unsigned axis = 0; axis < 3; ++axis)
                positions[i * stride + axis] = float(vertex.anchor[axis]);
    }
    return true;
}
} // namespace od
