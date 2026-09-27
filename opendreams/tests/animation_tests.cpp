#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/animation.h"
#include "port/dan.h"
#include "port/scene.h"

#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
bool check_pose(const char* cue, const char* stem, bool spline) {
    od::disc::Error source_error;
    auto opened=od::disc::Image::open(std::filesystem::u8path(cue),source_error);
    if (!opened) { std::cerr << source_error.message << '\n'; return false; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    const std::string path=std::string("DATA/3DC/")+stem+".DAN";
    od::port::PreviewLevelContext context(image);
    std::string error;
    if (!context.select_model(path,"",error) ||
        !od::port::SCENE_LoadLevel(context,error)) {
        std::cerr << stem << " model: " << error << '\n'; return false;
    }
    od::port::VfsContext vfs(image);
    od::port::DanArchive archive(vfs);
    od::port::DanError dan_error;
    std::vector<uint8_t> bytes;
    if (!od::port::DAN_OpenArchive(archive,path,dan_error) ||
        !od::port::DAN_ReadAnimChunks(archive,dan_error) ||
        !od::port::DAN_Load3DA(archive,0,bytes,dan_error)) {
        std::cerr << stem << " clip: " << dan_error.message << '\n'; return false;
    }
    od::port::AnimationClip clip;
    if (!od::port::ANIM_DecodeClip(bytes,archive.clips()[0].text,clip,error)) {
        std::cerr << stem << " decode: " << error << '\n'; return false;
    }
    const auto& bind=context.selected_actor().model;
    od::port::ModelGraph pose=bind;
    if (clip.tracks.size()!=bind.nodes.size()) return false;
    if (spline) {
        if (bind.nodes[0].name!="bassin" || clip.duration!=200 ||
            clip.resource_type!=6 ||
            !od::port::ANIM_ApplyModelSpline(clip,21,bind,pose,false,error) ||
            pose.nodes[0].local_xyz!=od::port::Vec3{0,4,0} ||
            pose.nodes[0].local_rot!=od::port::Mat3{
                31129,7899,-6508,-7891,31793,848,6518,761,32105} ||
            pose.nodes[2].local_xyz!=od::port::Vec3{0,-7,-39}) {
            std::cerr << "XH_ spline pose differs from Python key oracle: "
                      << error << '\n'; return false;
        }
        if (!od::port::ANIM_ApplyModelSpline(clip,10,bind,pose,false,error) ||
            std::abs(pose.nodes[0].local_xyz[1]-5)>1 ||
            std::abs(pose.nodes[0].local_rot[0]-31323)>20 ||
            std::abs(pose.nodes[0].local_rot[1]-7601)>20 ||
            std::abs(pose.nodes[0].local_rot[4]-31867)>20) {
            std::cerr << "XH_ spline midpoint differs from Python curve oracle\n";
            return false;
        }
    } else {
        if (clip.duration!=140 || clip.resource_type!=4 ||
            !od::port::ANIM_ApplyModelLinear(clip,60,bind,pose,false,error) ||
            pose.nodes[0].local_xyz!=od::port::Vec3{0,-367,0} ||
            pose.nodes[0].local_rot!=od::port::Mat3{
                32271,-5689,0,5688,32271,0,0,0,32768}) {
            std::cerr << "BA0 linear pose differs from Python key oracle: "
                      << error << '\n'; return false;
        }
    }
    return true;
}

bool check_cue(const char* cue, size_t& archives, size_t& clips,
               size_t& tracks, size_t& rotations, size_t& translations,
               size_t& matched, size_t& unbound) {
    od::disc::Error source_error;
    auto opened=od::disc::Image::open(std::filesystem::u8path(cue),source_error);
    if (!opened) { std::cerr << source_error.message << '\n'; return false; }
    std::shared_ptr<const od::disc::Image> image(std::move(opened));
    od::port::VfsContext vfs(image);
    for (const auto& file : image->entries()) {
        if (file.kind!=od::disc::EntryKind::file || file.path.size()<4 ||
            file.path.compare(file.path.size()-4,4,".DAN")!=0) continue;
        od::port::DanArchive archive(vfs);
        od::port::DanError source;
        if (!od::port::DAN_OpenArchive(archive,file.path,source) ||
            !od::port::DAN_ReadAnimChunks(archive,source)) {
            std::cerr << file.path << ": " << source.message << '\n'; return false;
        }
        std::vector<uint8_t> model_bytes;
        const std::string model_name=file.path.substr(0,file.path.size()-4)+".3DC";
        od::port::ModelGraph graph;
        std::string model_error;
        if (!od::port::DAN_Read3DC(archive,model_name,model_bytes,source) ||
            !od::port::RES_Relocate(model_bytes,graph,model_error)) {
            std::cerr << file.path << " model graph: " <<
                (model_error.empty() ? source.message : model_error) << '\n';
            return false;
        }
        od::port::ModelGraph pose=graph;
        ++archives;
        for (size_t index=0; index<archive.clips().size(); ++index) {
            std::vector<uint8_t> bytes;
            od::port::AnimationClip clip;
            std::string error;
            if (!od::port::DAN_Load3DA(archive,index,bytes,source) ||
                !od::port::ANIM_DecodeClip(bytes,archive.clips()[index].text,
                                            clip,error)) {
                std::cerr << file.path << " clip " << index << ": " <<
                    (error.empty() ? source.message : error) << '\n';
                return false;
            }
            ++clips;
            tracks+=clip.tracks.size();
            for (const auto& track : clip.tracks) {
                rotations+=track.rotations.size();
                translations+=track.translations.size();
            }
            if (clip.tracks.size()!=graph.nodes.size()) {
                std::cerr << file.path << " clip " << index << " has "
                          << clip.tracks.size() << " tracks for "
                          << graph.nodes.size() << " node slots\n";
                ++unbound;
                continue;
            }
            ++matched;
            for (const float frame : {0.0f,clip.duration/2.0f,
                                      static_cast<float>(clip.duration)}) {
                const bool applied=clip.resource_type==4 ?
                    od::port::ANIM_ApplyModelLinear(clip,frame,graph,pose,false,error) :
                    od::port::ANIM_ApplyModelSpline(clip,frame,graph,pose,false,error);
                if (!applied) {
                    std::cerr << file.path << " clip " << index
                              << " frame " << frame << ": " << error << '\n';
                    return false;
                }
            }
        }
    }
    return true;
}
} // namespace

int main() {
    const std::vector<uint8_t> short_clip(10,0);
    od::port::AnimationClip malformed;
    std::string error;
    if (od::port::ANIM_DecodeClip(short_clip,"bad",malformed,error) || error.empty()) {
        std::cerr << "truncated animation was accepted\n"; return 1;
    }
    if (std::abs(od::port::ANIM_ApplyEase(0.1f,0.25f,0.25f)-
                 0.026666667f)>0.00001f ||
        std::abs(od::port::ANIM_ApplyEase(0.9f,0.25f,0.25f)-
                 0.973333333f)>0.00001f) {
        std::cerr << "spline ease path differs from retail formula\n";
        return 1;
    }
    od::port::Mat3 matrix{};
    od::port::MATH_QuatToMatrix({0,0,0,32768},matrix);
    if (matrix!=od::port::Mat3{32768,0,0,0,32768,0,0,0,32768}) {
        std::cerr << "identity quaternion produced a nonidentity matrix\n";
        return 2;
    }
    size_t archives=0,clips=0,tracks=0,rotations=0,translations=0;
    size_t matched=0,unbound=0;
    for (const char* variable : {"DREAMS_CUE1","DREAMS_CUE2"}) {
        const char* cue=std::getenv(variable);
        if (cue && *cue && !check_cue(cue,archives,clips,tracks,
                                      rotations,translations,matched,unbound)) return 3;
    }
    const char* cue1=std::getenv("DREAMS_CUE1");
    const char* cue2=std::getenv("DREAMS_CUE2");
    if (cue1 && *cue1 && !check_pose(cue1,"XH_",true)) return 4;
    if (cue2 && *cue2 && !check_pose(cue2,"BA0",false)) return 5;
    if (cue1 && *cue1 && cue2 && *cue2 &&
        (archives!=191 || clips!=1048 || tracks!=21050 ||
         rotations!=197102 || translations!=110418 ||
         matched!=1037 || unbound!=11)) {
        std::cerr << "animation corpus counts differ from Python decoder\n";
        return 6;
    }
    std::cout << archives << " physical DAN archives, " << clips << " clips, "
              << tracks << " tracks, " << rotations << " rotation keys, "
              << translations << " translation keys; " << matched
              << " bound clips, " << unbound << " track-count mismatches\n";
    return 0;
}
