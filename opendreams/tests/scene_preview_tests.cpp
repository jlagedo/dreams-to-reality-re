#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/ddat.h"
#include "port/glide_model.h"
#include "port/scene.h"

#include <cstdlib>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>

int main() {
    const char* cues[]{std::getenv("DREAMS_CUE1"),std::getenv("DREAMS_CUE2")};
    if (!cues[0] || !*cues[0] || !cues[1] || !*cues[1]) {
        std::cout << "SKIP: set DREAMS_CUE1 and DREAMS_CUE2 for scene corpus\n";
        return 77;
    }
    std::array<std::shared_ptr<const od::disc::Image>,2> images;
    for (size_t index=0; index<2; ++index) {
        od::disc::Error source_error;
        auto opened=od::disc::Image::open(std::filesystem::u8path(cues[index]),source_error);
        if (!opened) { std::cerr << source_error.message << '\n'; return 1; }
        images[index]=std::shared_ptr<const od::disc::Image>(std::move(opened));
    }
    std::set<std::string> visited;
    size_t faces=0,materials=0,objects=0;
    size_t diagnostic_scenes=0, diagnostic_names=0;
    for (const auto& image : images) {
        for (const auto& file : image->entries()) {
            if (file.kind!=od::disc::EntryKind::file || file.path.size()<4 ||
                file.path.compare(file.path.size()-4,4,".DSN")!=0 ||
                !visited.insert(file.path).second) continue;
            od::port::PreviewLevelContext scene(image);
            std::string error;
            if (!scene.select_scene(file.path,error) ||
                !od::port::SCENE_LoadLevel(scene,error)) {
                std::cerr << file.path << ": " << error << '\n'; return 2;
            }
            const auto& graph=scene.render_graph();
            if (graph.faces.empty() || graph.materials.empty()) {
                std::cerr << file.path << ": empty render graph/materials\n";
                return 3;
            }
            for (const auto& face : graph.faces)
                if (face.material_index>=graph.materials.size()) {
                    std::cerr << file.path << ": unbound material\n";
                    return 4;
                }
            od::port::GlideModelDraw draw;
            if (!od::port::GLIDE_DrawObjectFaces(graph,draw,error) ||
                draw.vertices.size()!=graph.faces.size()*3u) {
                std::cerr << file.path << ": " << error << '\n'; return 5;
            }
            faces+=graph.faces.size();
            materials+=graph.materials.size();
            if (!scene.missing_scene_materials().empty()) {
                ++diagnostic_scenes;
            }
            diagnostic_names+=scene.missing_scene_materials().size();
            ++objects;
        }
    }
    if (objects!=95 || faces!=157433 || diagnostic_scenes!=14 ||
        diagnostic_names!=54) {
        std::cerr << "scene corpus differs from Python tag-1 oracle: "
                  << objects << " scenes, " << faces << " faces, "
                  << diagnostic_scenes << " scenes with " << diagnostic_names
                  << " missing texture names\n";
        return 6;
    }
    od::port::VfsContext vfs(images[1]);
    od::port::DdatBank bank;
    od::port::DdatError bank_error;
    if (!od::port::DDAT_Load(vfs,bank,bank_error)) {
        std::cerr << bank_error.message << '\n'; return 7;
    }
    size_t projects=0,placed=0,unavailable=0,secondary_actors=0;
    for (size_t index=0; index<bank.record_count; ++index) {
        const std::string name(bank.record_name(index));
        const uint8_t* record=nullptr;
        if (!od::port::DDAT_LoadRecord(bank,name,record,bank_error) || !record) {
            std::cerr << name << ": " << bank_error.message << '\n'; return 8;
        }
        const uint8_t* scene_name=record+0x600+0xc;
        size_t length=0;
        while (length<16 && scene_name[length]) ++length;
        const std::string path="DATA/3DC/"+
            std::string(reinterpret_cast<const char*>(scene_name),length);
        std::shared_ptr<const od::disc::Image> source;
        for (const auto& image : images) {
            od::disc::FileId file;
            od::disc::Error source_error;
            if (image->find(path,file,source_error)) { source=image; break; }
        }
        if (!source) { std::cerr << name << ": scene " << path << " missing\n"; return 9; }
        const auto secondary=source==images[0] ? images[1] : images[0];
        od::port::PreviewLevelContext project(source,secondary);
        std::string error;
        if (!project.select_project_scene(name,error) ||
            !od::port::SCENE_LoadLevel(project,error) ||
            project.render_graph().faces.size()<project.level_graph().faces.size()) {
            std::cerr << name << ": " << error << '\n'; return 10;
        }
        placed+=project.placed_actors().size();
        unavailable+=project.object_issues().size();
        for (const auto& actor : project.placed_actors())
            secondary_actors+=actor.from_secondary_source ? 1u : 0u;
        ++projects;
    }
    if (projects!=150 || placed!=531 || unavailable!=29 ||
        secondary_actors!=4) {
        std::cerr << "project placement census differs: " << projects
                  << " projects, " << placed << " actors, " << unavailable
                  << " unavailable, " << secondary_actors << " from second disc\n";
        return 11;
    }
    std::cout << objects << " original DSN scenes, " << faces
              << " faces, " << materials-diagnostic_names << " source texture pages, "
              << diagnostic_names << " diagnostic materials, "
              << projects << " projects, " << placed << " placed actors, "
              << unavailable << " unavailable actors, " << secondary_actors
              << " from the secondary source\n";
    return 0;
}
