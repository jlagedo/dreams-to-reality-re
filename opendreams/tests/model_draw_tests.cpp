#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/dan.h"
#include "port/glide_model.h"
#include "port/math.h"
#include "port/resource.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool ends_dan(const std::string& path) {
    return path.size() >= 4 && path.compare(path.size()-4,4,".DAN") == 0;
}

bool known_unavailable_material(const std::string& path, const std::string& error) {
    if (error.find("texture name is absent from the open DAN archive") ==
        std::string::npos) return false;
    static constexpr std::array<std::string_view,11> names{{
        "AF0.DAN","AH0.DAN","CLEARAI.DAN","DBN.DAN","L14.DAN",
        "MAR.DAN","O01FLUTE.DAN","PARCHEM.DAN","SUR.DAN",
        "SURFPLAN.DAN","WW0.DAN"}};
    const auto slash=path.find_last_of('/');
    const std::string_view basename(path.c_str()+(slash==std::string::npos ? 0 : slash+1));
    for (const auto name : names) if (basename==name) return true;
    return false;
}

bool synthetic() {
    using namespace od::port;
    Mat3 identity{32768,0,0,0,32768,0,0,0,32768};
    Mat3 yaw{0,0,32768,0,32768,0,-32768,0,0};
    Mat3 product{};
    MATH_MulMat3(identity,yaw,product);
    if (product != yaw) return false;
    Vec3 vector{2,0,0}, transformed{};
    MATH_MulMat3Vec3(yaw,vector,transformed);
    if (transformed != Vec3{0,0,-2}) return false;

    ModelGraph graph;
    ModelNode root;
    root.local_rot=identity;
    root.local_xyz={10,20,30};
    root.shade=15;
    root.vertices={{0,0,0},{2,0,0},{0,2,0}};
    graph.nodes.push_back(root);
    ModelNode child;
    child.parent=0;
    child.local_rot=yaw;
    child.local_xyz={5,0,0};
    child.name="hip";
    child.shade=15;
    child.vertices={{2,0,0},{0,2,0},{0,0,2}};
    graph.nodes.push_back(child);
    ModelMaterial material;
    material.name="TEST";
    material.bank.resize(0x18014u);
    material.bank[0x14u+15u*0x400u+4u+2u]=0xff;
    material.bank[0x14u+15u*0x400u+4u+3u]=0xff;
    material.bank[0x8014u+2u]=1;
    graph.materials.push_back(material);
    ModelFace face;
    face.owner_node=0;
    face.type=2;
    face.material_index=0;
    face.corners={ModelCorner{1,0,-65536,0},ModelCorner{1,1,0,0},
                  ModelCorner{1,2,0,65536}};
    graph.faces.push_back(face);
    face.type=9;
    graph.faces.push_back(face);
    face.type=-5;
    graph.faces.push_back(face);
    GlideModelDraw draw;
    std::string error;
    if (!GLIDE_DrawObjectFaces(graph,draw,error)) {
        std::cerr << error << '\n'; return false;
    }
    std::vector<ModelJoint> joints;
    if (!GLIDE_ModelJoints(graph,joints,error) || joints.size()!=2 ||
        joints[0].world_xyz!=Vec3{10,20,30} ||
        joints[1].world_xyz!=Vec3{15,20,30} ||
        joints[1].parent!=0 || joints[1].name!="hip" ||
        !joints[0].render_relevant || !joints[1].render_relevant) return false;
    if (draw.vertices.size()!=9 || draw.batches.size()!=3 ||
        draw.batches[0].mode!=GlideFaceMode::clamp ||
        draw.batches[1].mode!=GlideFaceMode::wrap ||
        draw.batches[2].mode!=GlideFaceMode::chroma ||
        draw.vertices[0].position[0]!=15 ||
        draw.vertices[0].position[1]!=-20 ||
        draw.vertices[0].position[2]!=28 ||
        draw.vertices[0].uv[0]!=-1.0f/256.0f) return false;
    std::array<uint8_t,1024> palette{};
    if (!GLIDE_ConvertPalette(graph.materials[0].bank,15,palette,error) ||
        palette[4]!=248 || palette[5]!=252 || palette[6]!=248 ||
        palette[7]!=0) return false;
    std::vector<uint8_t> lod;
    if (!model_texture_lod(graph.materials[0].bank,15,lod,error) ||
        lod.size()!=128u*128u*4u || lod[3]!=0 ||
        lod[4]!=248 || lod[5]!=252 || lod[6]!=248 || lod[7]!=255)
        return false;
    graph.faces[0].corners[0].vertex=100;
    return !GLIDE_DrawObjectFaces(graph,draw,error) && !error.empty();
}

bool corpus(const char* cue, size_t& models, size_t& faces,
            bool& saw_wrap, bool& saw_chroma, bool& saw_multi,
            std::vector<std::string>& unavailable) {
    od::disc::Error source_error;
    auto opened=od::disc::Image::open(std::filesystem::u8path(cue),source_error);
    if (!opened) { std::cerr << source_error.message << '\n'; return false; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    for (const auto& entry : image->entries()) {
        if (entry.kind != od::disc::EntryKind::file || !ends_dan(entry.path))
            continue;
        od::port::DanArchive archive(vfs);
        od::port::DanError dan_error;
        if (!od::port::DAN_OpenArchive(archive,entry.path,dan_error)) {
            std::cerr << entry.path << ": " << dan_error.message << '\n';
            return false;
        }
        const auto slash=entry.path.find_last_of('/');
        const auto dot=entry.path.find_last_of('.');
        const auto name=entry.path.substr(slash==std::string::npos ? 0 : slash+1,
                                          dot-(slash==std::string::npos ? 0 : slash+1));
        od::port::ModelGraph graph;
        std::string error;
        if (!od::port::RES_Load(archive,name+".3DC",graph,error)) {
            if (!known_unavailable_material(entry.path,error)) {
                std::cerr << entry.path << ": unexpected model load failure: "
                          << error << '\n';
                return false;
            }
            unavailable.push_back(entry.path+": "+error);
            continue;
        }
        od::port::GlideModelDraw draw;
        if (!od::port::GLIDE_DrawObjectFaces(graph,draw,error) ||
            draw.vertices.size()!=graph.faces.size()*3u || draw.batches.empty()) {
            std::cerr << entry.path << ": " << error << '\n'; return false;
        }
        size_t submitted=0;
        for (const auto& batch : draw.batches) {
            if (batch.first!=submitted || batch.count==0 || batch.count%3 ||
                batch.material>=graph.materials.size()) {
                std::cerr << entry.path << ": bad GPU batch\n"; return false;
            }
            submitted+=batch.count;
            saw_wrap |= batch.mode==od::port::GlideFaceMode::wrap;
            saw_chroma |= batch.mode==od::port::GlideFaceMode::chroma;
            std::vector<uint8_t> pixels;
            if (!od::port::model_texture_lod(graph.materials[batch.material].bank,
                                             batch.palette_row,pixels,error)) {
                std::cerr << entry.path << ": " << error << '\n'; return false;
            }
        }
        if (submitted!=draw.vertices.size()) return false;
        saw_multi |= graph.materials.size()>1;
        ++models;
        faces+=graph.faces.size();
    }
    return true;
}

} // namespace

int main() {
    if (!synthetic()) { std::cerr << "synthetic model draw contract failed\n"; return 1; }
    size_t models=0,faces=0;
    bool wrap=false,chroma=false,multi=false;
    std::vector<std::string> unavailable;
    for (const char* variable : {"DREAMS_CUE1","DREAMS_CUE2"}) {
        const char* cue=std::getenv(variable);
        if (cue && *cue && !corpus(cue,models,faces,wrap,chroma,multi,
                                    unavailable)) return 2;
    }
    for (const auto& item : unavailable) std::cerr << item << '\n';
    std::cout << "model draw contract: " << models << " DAN files, " << faces
              << " faces; wrap=" << wrap << " chroma=" << chroma
              << " multiple materials=" << multi
              << " unavailable=" << unavailable.size() << '\n';
    if (std::getenv("DREAMS_CUE1") && std::getenv("DREAMS_CUE2") &&
        (models != 178 || faces != 46091 || unavailable.size() != 13 ||
         !chroma || !multi)) {
        std::cerr << "disc corpus did not exercise supported model material modes\n";
        return 3;
    }
    return 0;
}
