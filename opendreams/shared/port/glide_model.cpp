#include "port/glide_model.h"

#include "port/math.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace od::port {
namespace {

bool fail(std::string& error, const char* message) {
    error = message;
    return false;
}

struct WorldNode {
    Mat3 rotation{};
    Vec3 position{};
};

bool world_node(const ModelGraph& graph, size_t index,
                std::vector<WorldNode>& world, std::vector<uint8_t>& state,
                unsigned depth, std::string& error) {
    if (index >= graph.nodes.size()) return fail(error, "model node parent is invalid");
    if (state[index] == 2) return true;
    if (state[index] == 1 || depth > 128)
        return fail(error, "model node hierarchy is cyclic or too deep");
    state[index] = 1;
    const auto& node = graph.nodes[index];
    WorldNode current{node.local_rot,node.local_xyz};
    if (node.parent >= 0) {
        const size_t parent = static_cast<size_t>(node.parent);
        if (!world_node(graph,parent,world,state,depth+1,error)) return false;
        MATH_MulMat3(world[parent].rotation,node.local_rot,current.rotation);
        Vec3 translated{};
        MATH_MulMat3Vec3(world[parent].rotation,node.local_xyz,translated);
        for (size_t axis=0; axis<3; ++axis)
            current.position[axis] = static_cast<int32_t>(
                static_cast<uint32_t>(translated[axis]) +
                static_cast<uint32_t>(world[parent].position[axis]));
    }
    world[index] = current;
    state[index] = 2;
    return true;
}

bool face_mode(int32_t type, GlideFaceMode& mode) {
    switch (type) {
    case 2: case 3: mode=GlideFaceMode::clamp; return true;
    case 9: mode=GlideFaceMode::wrap; return true;
    case -6: case -5: mode=GlideFaceMode::chroma; return true;
    case -7: case -4: case -3: mode=GlideFaceMode::translucent; return true;
    default: return false;
    }
}

struct PreparedFace {
    std::array<GlideModelVertex,3> vertices{};
    size_t material = 0;
    uint32_t row = 0;
    GlideFaceMode mode = GlideFaceMode::clamp;
};

void append_face(GlideModelDraw& draw, const PreparedFace& face) {
    if (draw.batches.empty() ||
        draw.batches.back().material != face.material ||
        draw.batches.back().palette_row != face.row ||
        draw.batches.back().mode != face.mode) {
        GlideModelBatch batch;
        batch.first = static_cast<uint32_t>(draw.vertices.size());
        batch.material = face.material;
        batch.palette_row = face.row;
        batch.mode = face.mode;
        draw.batches.push_back(batch);
    }
    draw.vertices.insert(draw.vertices.end(),face.vertices.begin(),face.vertices.end());
    draw.batches.back().count += 3;
}

} // namespace

bool GLIDE_ModelJoints(const ModelGraph& graph, std::vector<ModelJoint>& joints,
                       std::string& error) {
    error.clear();
    joints.clear();
    if (graph.nodes.empty()) return fail(error,"model has no joint nodes");
    std::vector<WorldNode> world(graph.nodes.size());
    std::vector<uint8_t> state(graph.nodes.size());
    for (size_t slot=0; slot<graph.nodes.size(); ++slot)
        if (!world_node(graph,slot,world,state,0,error)) return false;
    std::vector<uint8_t> relevant(graph.nodes.size());
    for (const auto& face : graph.faces) {
        if (face.owner_node>=graph.nodes.size())
            return fail(error,"joint overlay has an invalid face owner");
        relevant[face.owner_node]=1;
        for (const auto& corner : face.corners) {
            if (corner.node>=graph.nodes.size())
                return fail(error,"joint overlay has an invalid face corner");
            relevant[corner.node]=1;
        }
    }
    for (size_t slot=0; slot<graph.nodes.size(); ++slot) {
        if (!relevant[slot]) continue;
        for (int parent=graph.nodes[slot].parent; parent>=0;
             parent=graph.nodes[static_cast<size_t>(parent)].parent)
            relevant[static_cast<size_t>(parent)]=1;
    }
    joints.reserve(graph.nodes.size());
    for (size_t slot=0; slot<graph.nodes.size(); ++slot)
        joints.push_back({slot,graph.nodes[slot].parent,graph.nodes[slot].name,
                          world[slot].position,relevant[slot]!=0});
    return true;
}

bool GLIDE_DrawObjectFaces(const ModelGraph& graph, GlideModelDraw& draw,
                           std::string& error) {
    error.clear();
    draw = {};
    if (graph.nodes.empty() || graph.faces.empty())
        return fail(error,"model has no nodes or faces");
    if (graph.faces.size() > 1000000u)
        return fail(error,"model exceeds the GPU face limit");
    std::vector<WorldNode> world(graph.nodes.size());
    std::vector<uint8_t> state(graph.nodes.size());
    for (size_t index=0; index<graph.nodes.size(); ++index)
        if (!world_node(graph,index,world,state,0,error)) return false;

    draw.minimum.fill(std::numeric_limits<float>::max());
    draw.maximum.fill(std::numeric_limits<float>::lowest());
    std::vector<PreparedFace> deferred;
    size_t bound_material = SIZE_MAX;
    uint32_t bound_row = 0;
    for (const ModelFace& face : graph.faces) {
        // Types without a textured branch in the 3dfx hook submit nothing
        // (0x16..0x18 grayscale is not in the static corpus and not ported).
        GlideFaceMode mode{};
        if (!face_mode(face.type,mode)) continue;
        if (face.owner_node >= graph.nodes.size())
            return fail(error,"model face has an invalid owner node");
        // Unbound blocks keep retail's unrelocated +0x08 and zero-size loads
        // point at stale arena memory; neither has a page to draw.
        if (face.material_index >= graph.materials.size() ||
            graph.materials[face.material_index].bank.size() != 0x18014u) continue;
        PreparedFace prepared;
        prepared.material = face.material_index;
        prepared.mode = mode;
        const auto& owner = graph.nodes[face.owner_node];
        const uint32_t requested_row =
            graph.materials[face.material_index].static_palette_row15 ? 15u :
            mode == GlideFaceMode::translucent ? 15u :
            std::min(owner.light_count ? static_cast<uint32_t>(face.shade) :
                     owner.shade,31u);
        // The retail P8 palette cache is keyed by page pointer, not shade row.
        // Binding the same page again retains its previously downloaded row.
        if (bound_material != face.material_index) {
            bound_material = face.material_index;
            bound_row = requested_row;
        }
        prepared.row = bound_row;
        for (size_t corner=0; corner<3; ++corner) {
            const auto& ref = face.corners[corner];
            if (ref.node >= graph.nodes.size() ||
                ref.vertex >= graph.nodes[ref.node].vertices.size())
                return fail(error,"model face corner references an invalid vertex");
            Vec3 transformed{};
            MATH_MulMat3Vec3(world[ref.node].rotation,
                             graph.nodes[ref.node].vertices[ref.vertex],transformed);
            for (size_t axis=0; axis<3; ++axis) {
                const int32_t value = static_cast<int32_t>(
                    static_cast<uint32_t>(transformed[axis]) +
                    static_cast<uint32_t>(world[ref.node].position[axis]));
                const float point = static_cast<float>(axis == 1 ? -value : value);
                prepared.vertices[corner].position[axis] = point;
                draw.minimum[axis] = std::min(draw.minimum[axis],point);
                draw.maximum[axis] = std::max(draw.maximum[axis],point);
            }
            prepared.vertices[corner].uv[0] = static_cast<float>(ref.u) /
                (65536.0f*256.0f);
            prepared.vertices[corner].uv[1] = static_cast<float>(ref.v) /
                (65536.0f*256.0f);
        }
        if (mode == GlideFaceMode::translucent) deferred.push_back(prepared);
        else append_face(draw,prepared);
    }
    // Retail's deferred hook walks queued blocks after the main scene pass,
    // in insertion order, with depth testing and writes still enabled.
    for (const PreparedFace& face : deferred) append_face(draw,face);
    return true;
}

bool GLIDE_ConvertPalette(const std::vector<uint8_t>& bank, unsigned row,
                          std::array<uint8_t,1024>& rgba, std::string& error) {
    error.clear();
    if (bank.size() != 0x18014u || row >= 32)
        return fail(error,"model palette row is outside its texture bank");
    for (size_t index=0; index<256; ++index) {
        const size_t at=0x14u+static_cast<size_t>(row)*0x400u+index*4u+2u;
        const uint16_t color=static_cast<uint16_t>(bank[at]) |
            static_cast<uint16_t>(bank[at+1]<<8);
        rgba[index*4]=static_cast<uint8_t>(((color>>11)&31u)*8u);
        rgba[index*4+1]=static_cast<uint8_t>(((color>>5)&63u)*4u);
        rgba[index*4+2]=static_cast<uint8_t>((color&31u)*8u);
        rgba[index*4+3]=0;
    }
    return true;
}

bool texture_page(const std::vector<uint8_t>& bank, unsigned row, unsigned size,
                  std::vector<uint8_t>& rgba, std::string& error) {
    rgba.clear();
    std::array<uint8_t,1024> palette{};
    if (!GLIDE_ConvertPalette(bank,row,palette,error)) return false;
    const size_t step=256u/size;
    rgba.resize(static_cast<size_t>(size)*size*4u);
    for (size_t y=0; y<size; ++y)
        for (size_t x=0; x<size; ++x) {
            const uint8_t index=bank[0x8014u+(y*step)*256u+x*step];
            const size_t at=(y*size+x)*4u;
            std::copy_n(palette.data()+static_cast<size_t>(index)*4u,3,
                        rgba.data()+at);
            const size_t color=static_cast<size_t>(index)*4u;
            rgba[at+3]=(palette[color]==palette[0] &&
                        palette[color+1]==palette[1] &&
                        palette[color+2]==palette[2]) ? 0 : 255;
        }
    return true;
}

bool model_texture_lod(const std::vector<uint8_t>& bank, unsigned row,
                       std::vector<uint8_t>& rgba, std::string& error) {
    return texture_page(bank,row,128,rgba,error);
}

bool scene_texture_page(const std::vector<uint8_t>& bank, unsigned row,
                        std::vector<uint8_t>& rgba, std::string& error) {
    return texture_page(bank,row,256,rgba,error);
}

} // namespace od::port
