#include "port/model.h"

#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace od::port {
namespace {

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

bool range(const std::vector<uint8_t>& bytes, size_t offset, size_t length) {
    return offset <= bytes.size() && length <= bytes.size() - offset;
}

uint32_t u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint32_t>(bytes[offset]) |
        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

int32_t i32(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<int32_t>(u32(bytes, offset));
}

std::string fixed_text(const std::vector<uint8_t>& bytes, size_t offset, size_t length) {
    size_t end = 0;
    while (end < length && bytes[offset + end]) ++end;
    return std::string(reinterpret_cast<const char*>(bytes.data() + offset), end);
}

bool address_offset(uint32_t address, int64_t delta,
                    const std::vector<uint8_t>& record, size_t length,
                    size_t& offset) {
    const int64_t translated = static_cast<int64_t>(address) + delta;
    if (translated < 0 || static_cast<uint64_t>(translated) > record.size()) return false;
    offset = static_cast<size_t>(translated);
    return range(record, offset, length);
}

} // namespace

bool MDL_RelocPrimitives(const std::vector<uint8_t>& record, int64_t delta,
                         size_t owner, uint32_t first_block,
                         const std::vector<uint32_t>& vertex_bases,
                         ModelGraph& graph, std::string& error) {
    std::unordered_set<uint32_t> visited;
    for (uint32_t block = first_block; block;) {
        if (!visited.insert(block).second)
            return fail(error, "model face blocks form a cycle");
        size_t block_offset = 0;
        if (!address_offset(block, delta, record, 0x34, block_offset))
            return fail(error, "model face block is outside its record");
        const int32_t type = i32(record, block_offset + 4);
        const std::string material = fixed_text(record, block_offset + 0xc, 16);
        const uint32_t count = u32(record, block_offset + 0x1c);
        const uint32_t first_face = u32(record, block_offset + 0x20);
        const uint32_t stride = u32(record, block_offset + 0x2c);
        const bool diagnostic = type == -2 || type == 1 || type == 4 ||
                                type == 0x11 || type == 0x1b;
        if (count > 20000 || (count && stride != (diagnostic ? 56u : 68u)))
            return fail(error, "model face block has an unsupported count or stride");
        size_t face_offset = 0;
        if (count && !address_offset(first_face, delta, record,
                                     diagnostic ? 0x30 : 0x40, face_offset))
            return fail(error, "model face list is outside its record");
        // Retail relocates the 56-byte diagnostic blocks too. The textured
        // model graph keeps only the 68-byte triangles used by the GPU
        // material path; diagnostic submission remains a separate slice.
        if (diagnostic) {
            if (count && !range(record,face_offset+static_cast<size_t>(count-1)*stride,
                                0x30))
                return fail(error,"model diagnostic face list is truncated");
            block = u32(record, block_offset);
            continue;
        }
        for (uint32_t index = 0; index < count; ++index) {
            const size_t at = face_offset + static_cast<size_t>(index) * stride;
            if (!range(record, at, 0x40))
                return fail(error, "model face ends outside its record");
            ModelFace face;
            face.owner_node = owner;
            face.type = type;
            face.material_name = material;
            if (range(record, at, 0x41)) face.shade = record[at + 0x40];
            constexpr std::array<size_t, 3> vertex_fields{{8, 0x14, 0x20}};
            constexpr std::array<size_t, 3> uv_fields{{0x34, 0x38, 0x3c}};
            for (size_t corner = 0; corner < 3; ++corner) {
                const uint32_t ref = u32(record, at + vertex_fields[corner]);
                bool found = false;
                for (size_t node = 0; node < graph.nodes.size(); ++node) {
                    const uint64_t base = vertex_bases[node];
                    const uint64_t end = base + graph.nodes[node].vertices.size() * 40u;
                    if (ref < base || ref >= end || ((ref - base) % 40u)) continue;
                    face.corners[corner].node = node;
                    face.corners[corner].vertex = (ref - base) / 40u;
                    found = true;
                    break;
                }
                if (!found) return fail(error, "model face has an invalid vertex reference");
                size_t uv_offset = 0;
                if (!address_offset(u32(record, at + uv_fields[corner]), delta,
                                    record, 8, uv_offset))
                    return fail(error, "model face has an invalid UV reference");
                face.corners[corner].u = i32(record, uv_offset);
                face.corners[corner].v = i32(record, uv_offset + 4);
            }
            graph.faces.push_back(std::move(face));
        }
        block = u32(record, block_offset);
    }
    return true;
}

bool RES_RelocOffsetTable(const std::vector<uint8_t>& record,
                          std::vector<uint32_t>& node_offsets, std::string& error) {
    error.clear();
    node_offsets.clear();
    if (!range(record, 0x14, 4))
        return fail(error, "model record has no node table count");
    const uint32_t count = u32(record, 0x14);
    if (!count || count > 10000 || !range(record, 0x18, static_cast<size_t>(count) * 4u))
        return fail(error, "model node offset table is invalid");
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t offset = u32(record, 0x18 + static_cast<size_t>(index) * 4u);
        if (!range(record, offset, 0xf0))
            return fail(error, "model node offset is outside its record");
        node_offsets.push_back(offset);
    }
    return true;
}

bool MDL_RelocNodeTree(const std::vector<uint8_t>& record,
                       const std::vector<uint32_t>& node_offsets,
                       ModelGraph& graph, std::string& error) {
    error.clear();
    graph = {};
    if (node_offsets.empty()) return fail(error, "model has no node offsets");
    int64_t delta = std::numeric_limits<int64_t>::min();
    std::vector<uint32_t> vertex_bases;
    vertex_bases.reserve(node_offsets.size());
    graph.nodes.reserve(node_offsets.size());
    for (uint32_t source_offset : node_offsets) {
        const size_t off = source_offset;
        if (!range(record, off, 0xf0))
            return fail(error, "model node header is truncated");
        const uint32_t count = u32(record, off + 0x90);
        const uint32_t base = u32(record, off + 0x94);
        if (count > 100000 || !range(record, off + 0xf0,
                                     static_cast<size_t>(count) * 40u) ||
            (count && u32(record, off + 0x9c) !=
                      base + static_cast<uint64_t>(count) * 40u))
            return fail(error, "model node vertex array is invalid");
        // Zero-vertex connector nodes have no usable vertex base. Retail still
        // includes them in the node table and hierarchy (AR0.DAN is one case).
        if (count) {
            const int64_t this_delta = static_cast<int64_t>(off + 0xf0) - base;
            if (delta == std::numeric_limits<int64_t>::min()) delta = this_delta;
            else if (delta != this_delta)
                return fail(error, "model nodes disagree about their address delta");
        }
        ModelNode node;
        node.source_offset = source_offset;
        for (size_t axis = 0; axis < 3; ++axis)
            node.local_xyz[axis] = i32(record, off + 0x30 + axis * 4u);
        for (size_t value = 0; value < 9; ++value)
            node.local_rot[value] = i32(record, off + 0x3c + value * 4u);
        node.light_count = u32(record, off + 0x14 + 0xc4);
        node.shade = u32(record, off + 0x14 + 0xd0);
        node.vertices.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            const size_t at = off + 0xf4 + static_cast<size_t>(index) * 40u;
            node.vertices.push_back({i32(record, at), i32(record, at + 4),
                                     i32(record, at + 8)});
        }
        vertex_bases.push_back(base);
        graph.nodes.push_back(std::move(node));
    }
    if (delta == std::numeric_limits<int64_t>::min())
        return fail(error, "model has no vertex-bearing node for relocation");
    for (size_t index = 0; index < graph.nodes.size(); ++index) {
        if (!graph.nodes[index].vertices.empty()) continue;
        const size_t off = node_offsets[index];
        const int64_t expected = static_cast<int64_t>(off + 0xf0) - delta;
        const uint32_t end = u32(record, off + 0x9c);
        if (u32(record, off + 0x94) != 0 ||
            (end != 0 && (expected < 0 || expected > UINT32_MAX ||
                          end != static_cast<uint32_t>(expected))))
            return fail(error, "empty model connector has invalid address fields");
    }
    std::unordered_map<uint32_t, size_t> by_address;
    for (size_t index = 0; index < graph.nodes.size(); ++index)
        by_address.emplace(static_cast<uint32_t>(
            static_cast<int64_t>(node_offsets[index]) + 0x14 - delta), index);
    for (size_t index = 0; index < graph.nodes.size(); ++index) {
        const size_t off = node_offsets[index];
        const uint32_t parent = u32(record, off + 0x24);
        if (parent && parent != 1) {
            const auto found = by_address.find(parent);
            if (found == by_address.end())
                return fail(error, "model node has an unknown parent");
            graph.nodes[index].parent = static_cast<int>(found->second);
        }
        if (!MDL_RelocPrimitives(record, delta, index,
                                 u32(record, off + 0xb8), vertex_bases,
                                 graph, error)) return false;
    }
    return true;
}

bool RES_Relocate(const std::vector<uint8_t>& record,
                  ModelGraph& graph, std::string& error) {
    std::vector<uint32_t> offsets;
    if (!RES_RelocOffsetTable(record, offsets, error)) return false;
    return MDL_RelocNodeTree(record, offsets, graph, error);
}

} // namespace od::port
