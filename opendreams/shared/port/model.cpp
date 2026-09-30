#include "port/model.h"

#include <algorithm>
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
        const bool flat = flat_block_type(type);
        if (count > 20000 || (count && stride != (flat ? 56u : 68u)))
            return fail(error, "model face block has an unsupported count or stride");
        size_t face_offset = 0;
        if (count && !address_offset(first_face, delta, record,
                                     flat ? 0x34 : 0x40, face_offset))
            return fail(error, "model face list is outside its record");
        for (uint32_t index = 0; index < count; ++index) {
            const size_t at = face_offset + static_cast<size_t>(index) * stride;
            const bool grayscale = type >= 0x16 && type <= 0x18;
            if (!range(record, at, grayscale ? 0x44 : flat ? 0x34 : 0x40))
                return fail(error, "model face ends outside its record");
            ModelFace face;
            face.owner_node = owner;
            face.source_block = block;
            face.flags = u32(record,at);
            face.type = type;
            face.material_name = material;
            size_t normal_offset = 0;
            if (!address_offset(u32(record,at+0x2c),delta,record,16,normal_offset))
                return fail(error,"model face normal is outside its record");
            for (size_t axis=0; axis<3; ++axis)
                face.normal[axis]=i32(record,normal_offset+axis*4u);
            face.plane_distance=i32(record,at+0x30);
            if (!flat && range(record, at, 0x41))
                face.shade = record[at + 0x40];
            if (!flat && range(record, at, 0x44)) {
                std::copy_n(record.data() + at + 0x41, 3, face.corner_shades.begin());
            }
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
                if (flat) continue; // 56-byte records carry no UV pointers.
                size_t uv_offset = 0;
                if (!address_offset(u32(record, at + uv_fields[corner]), delta,
                                    record, 8, uv_offset))
                    return fail(error, "model face has an invalid UV reference");
                face.corners[corner].u = i32(record, uv_offset);
                face.corners[corner].v = i32(record, uv_offset + 4);
            }
            (flat ? graph.flat_faces : graph.faces).push_back(std::move(face));
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
        node.name = fixed_text(record,off+0x14,12);
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
        // Offset 1 is a valid source pointer to directory node 0, not a null
        // marker. Only zero means no parent; XH_ thighs and torso use 1.
        if (parent) {
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
    if (!RES_RelocOffsetTable(record, offsets, error) ||
        !MDL_RelocNodeTree(record, offsets, graph, error)) return false;
    // MDL_LoadMaterials (0x456038) reads the u32 record count and the 44-byte
    // records that follow the node offset table (payload[0] = node count).
    const size_t at = 0x18 + offsets.size() * 4u;
    if (!range(record, at, 4))
        return fail(error, "model record has no material directory count");
    const uint32_t count = u32(record, at);
    if (count > 256 || !range(record, at + 4, static_cast<size_t>(count) * 44u))
        return fail(error, "model material directory is invalid");
    graph.directory.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        const size_t entry = at + 4 + static_cast<size_t>(index) * 44u;
        MaterialRecord material;
        std::copy_n(record.begin() + static_cast<std::ptrdiff_t>(entry), 44,
                    material.raw.begin());
        material.name = fixed_text(record, entry, 16);
        material.file = fixed_text(record, entry + 16, 16);
        material.colour = u32(record, entry + 32);
        graph.directory.push_back(std::move(material));
    }
    return true;
}

} // namespace od::port
