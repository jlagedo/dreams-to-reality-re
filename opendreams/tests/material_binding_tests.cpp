#define _CRT_SECURE_NO_WARNINGS
#include "disc/image.h"
#include "port/dan.h"
#include "port/ddat.h"
#include "port/resource.h"
#include "port/scene.h"
#include "port/vfs.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Retail material binding (MDL_LoadMaterials 0x456038, MDL_BindFaceMaterials
// 0x4554e0) over both discs: every physical DAN and DSN through its own
// material directory, then all 150 projects in the retail level load order
// (out/dev/research-materials.md, cross-checked by out/dev/mat_sim.py).

namespace {

using od::port::FaceBinding;
using od::port::MaterialCache;
using od::port::MaterialPage;
using od::port::ModelFace;
using od::port::ModelGraph;

bool ends_with(const std::string& text, const char* suffix) {
    const std::string tail(suffix);
    return text.size() >= tail.size() &&
           text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

std::string stem_of(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    const size_t dot = path.find_last_of('.');
    return path.substr(slash + 1, dot - slash - 1);
}

struct Misses {
    size_t textured = 0; // no drawable page (unbound or zero-size)
    size_t flat = 0;     // flat block without a colour binding
    std::map<std::string, size_t> names;
};

void count_misses(const MaterialCache& cache, const ModelGraph& graph,
                  const std::string& label, Misses& misses) {
    for (const ModelFace& face : graph.faces) {
        const bool drawable = face.binding == FaceBinding::page &&
            face.material_index < graph.materials.size() &&
            face.cache_slot < cache.slots.size() &&
            cache.slots[face.cache_slot].page == MaterialPage::loaded;
        if (drawable) continue;
        ++misses.textured;
        ++misses.names[label + ":" + face.material_name];
    }
    for (const ModelFace& face : graph.flat_faces)
        if (face.binding != FaceBinding::colour) {
            ++misses.flat;
            ++misses.names[label + ":" + face.material_name + " (flat)"];
        }
}

// A duplicate directory name collapses to its first record: one live entry,
// one reference per record, the first record's file.
bool duplicates_collapse(const MaterialCache& cache, const ModelGraph& graph,
                         const std::string& label) {
    std::map<std::string, std::vector<std::string>> files;
    for (const auto& record : graph.directory) files[record.name].push_back(record.file);
    bool ok = true;
    for (const auto& [name, list] : files) {
        if (list.size() < 2) continue;
        size_t live = 0;
        for (const auto& slot : cache.slots) {
            if (!slot.refcount || slot.record.name != name) continue;
            ++live;
            ok &= slot.refcount == list.size() && slot.record.file == list.front();
        }
        ok &= live == 1;
        std::cout << label << ": duplicate " << name << " -> " << list.front()
                  << " (" << list.size() << " records)\n";
    }
    return ok;
}

std::string slot_source(const MaterialCache& cache, const ModelFace& face) {
    return face.cache_slot < cache.slots.size() ? cache.slots[face.cache_slot].source
                                                : std::string("<unbound>");
}

} // namespace

int main() {
    const char* cues[]{std::getenv("DREAMS_CUE1"), std::getenv("DREAMS_CUE2")};
    if (!cues[0] || !*cues[0] || !cues[1] || !*cues[1]) {
        std::cout << "SKIP: set DREAMS_CUE1 and DREAMS_CUE2 for the material corpus\n";
        return 77;
    }
    std::array<std::shared_ptr<const od::disc::Image>, 2> images;
    for (size_t index = 0; index < 2; ++index) {
        od::disc::Error source_error;
        auto opened = od::disc::Image::open(std::filesystem::u8path(cues[index]), source_error);
        if (!opened) { std::cerr << source_error.message << '\n'; return 1; }
        images[index] = std::shared_ptr<const od::disc::Image>(std::move(opened));
    }

    // Per physical file, a fresh cache seeded only by that file.
    size_t dan_files = 0, dsn_files = 0, duplicate_files = 0;
    Misses file_misses;
    bool duplicates_ok = true;
    for (const auto& image : images) {
        od::port::VfsContext vfs(image);
        for (const auto& entry : image->entries()) {
            if (entry.kind != od::disc::EntryKind::file) continue;
            if (ends_with(entry.path, ".DAN")) {
                od::port::DanArchive archive(vfs);
                od::port::DanError dan_error;
                ModelGraph graph;
                MaterialCache cache;
                std::string error;
                if (!od::port::DAN_OpenArchive(archive, entry.path, dan_error) ||
                    !od::port::RES_Load(archive, stem_of(entry.path) + ".3DC", cache,
                                        graph, error)) {
                    std::cerr << entry.path << ": " << dan_error.message << error << '\n';
                    return 2;
                }
                count_misses(cache, graph, stem_of(entry.path) + ".DAN", file_misses);
                bool has_duplicate = false;
                for (size_t a = 0; a < graph.directory.size(); ++a)
                    for (size_t b = a + 1; b < graph.directory.size(); ++b)
                        has_duplicate |= graph.directory[a].name == graph.directory[b].name;
                if (has_duplicate) {
                    ++duplicate_files;
                    duplicates_ok &= duplicates_collapse(cache, graph, entry.path);
                }
                ++dan_files;
            } else if (ends_with(entry.path, ".DSN")) {
                od::port::PreviewLevelContext scene(image);
                std::string error;
                if (!scene.select_scene(entry.path, error) ||
                    !od::port::SCENE_LoadLevel(scene, error)) {
                    std::cerr << entry.path << ": " << error << '\n';
                    return 3;
                }
                count_misses(scene.material_cache(), scene.level_graph(),
                             stem_of(entry.path) + ".DSN", file_misses);
                ++dsn_files;
            }
        }
    }
    for (const auto& [name, faces] : file_misses.names)
        std::cout << "per-file miss " << name << ": " << faces << " faces\n";
    // The one per-file miss: E21_RIDE's map table lacks e21arrow (6 faces).
    const bool file_ok = dan_files == 191 && dsn_files == 98 && file_misses.flat == 0 &&
        file_misses.names.size() == 1 && file_misses.textured == 6 &&
        file_misses.names.count("E21_RIDE.DSN:E21ARROW") == 1 &&
        duplicate_files == 3 && duplicates_ok;
    std::cout << dan_files << " DAN and " << dsn_files << " DSN physical files; "
              << file_misses.textured << " textured and " << file_misses.flat
              << " flat faces unbound; " << duplicate_files
              << " DAN files with duplicate names\n";
    if (!file_ok) {
        std::cerr << "per-file material binding differs from retail research\n";
        return 4;
    }

    // Per project, the retail load order.
    od::port::VfsContext bank_vfs(images[1]);
    od::port::DdatBank bank;
    od::port::DdatError bank_error;
    if (!od::port::DDAT_Load(bank_vfs, bank, bank_error)) {
        std::cerr << bank_error.message << '\n';
        return 5;
    }
    size_t projects = 0, placed = 0, issues = 0, gaps = 0, zero_size = 0;
    size_t max_entries = 0, grille_faces = 0, unused_maps = 0;
    Misses project_misses;
    bool pins_ok = true, grille_ok = true;
    std::map<std::string, size_t> issue_reasons;
    for (size_t index = 0; index < bank.record_count; ++index) {
        const std::string name(bank.record_name(index));
        const uint8_t* record = nullptr;
        if (!od::port::DDAT_LoadRecord(bank, name, record, bank_error) || !record) {
            std::cerr << name << ": " << bank_error.message << '\n';
            return 6;
        }
        size_t length = 0;
        while (length < 16 && record[0x60c + length]) ++length;
        const std::string path = "DATA/3DC/" +
            std::string(reinterpret_cast<const char*>(record + 0x60c), length);
        std::shared_ptr<const od::disc::Image> source;
        for (const auto& image : images) {
            od::disc::FileId file;
            od::disc::Error source_error;
            if (image->find(path, file, source_error)) { source = image; break; }
        }
        if (!source) { std::cerr << name << ": " << path << " missing\n"; return 7; }
        const auto secondary = source == images[0] ? images[1] : images[0];
        od::port::PreviewLevelContext level(source, secondary);
        std::string error;
        if (!level.select_project_scene(name, error) ||
            !od::port::SCENE_LoadLevel(level, error)) {
            std::cerr << name << ": " << error << '\n';
            return 8;
        }
        const MaterialCache& cache = level.material_cache();
        count_misses(cache, level.level_graph(), name, project_misses);
        for (const auto& actor : level.placed_actors())
            count_misses(cache, actor.model, name + "/" + actor.asset_name, project_misses);
        for (const auto& slot : cache.slots)
            zero_size += slot.refcount && slot.page == MaterialPage::zero_size;
        max_entries = std::max(max_entries, cache.slots.size());
        placed += level.placed_actors().size();
        issues += level.object_issues().size();
        for (const auto& issue : level.object_issues())
            ++issue_reasons[issue.asset_name + ": " + issue.reason];
        gaps += level.load_order_gaps().size();
        for (const auto& gap : level.load_order_gaps())
            std::cout << name << " load-order gap: " << gap << '\n';
        unused_maps += level.unused_scene_maps().size();
        // Every GRILLE face takes OMBRE.3DC's entry, loaded first every level.
        const auto check_grille = [&](const ModelGraph& graph) {
            for (const auto& face : graph.faces)
                if (face.material_name == "GRILLE") {
                    ++grille_faces;
                    grille_ok &= slot_source(cache, face) == "OMBRE.3DC";
                }
        };
        check_grille(level.level_graph());
        for (const auto& actor : level.placed_actors()) check_grille(actor.model);
        const auto pin = [&](std::initializer_list<const char*> names, const char* expected) {
            for (const char* material : names) {
                size_t faces = 0;
                for (const auto& face : level.level_graph().faces) {
                    if (face.material_name != material) continue;
                    ++faces;
                    pins_ok &= slot_source(cache, face) == expected;
                }
                std::cout << name << ' ' << material << " -> " << expected << ": "
                          << faces << " scene faces\n";
                pins_ok &= faces != 0;
            }
        };
        if (name == "Project58") pin({"E21ARROW"}, "E22.DAN");
        if (name == "Project65")
            pin({"H02ROUT2.BMP", "H02ROUT3.BMP", "H02ROUT5.BMP"}, "L14.DAN");
        if (name == "Project34") pin({"F15SH01", "F15SH02", "F15SB02"}, "F91.DAN");
        ++projects;
    }
    for (const auto& [reason, count] : issue_reasons)
        std::cout << "unavailable x" << count << ": " << reason << '\n';
    for (const auto& [miss, faces] : project_misses.names)
        std::cout << "project miss " << miss << ": " << faces << " faces\n";
    std::cout << projects << " projects, " << placed << " placed actors, " << issues
              << " unavailable, " << gaps << " load-order gaps, "
              << project_misses.textured << " textured and " << project_misses.flat
              << " flat misses, " << zero_size << " zero-size loads, " << max_entries
              << " max cache entries, " << grille_faces << " GRILLE faces, "
              << unused_maps << " unused DSN maps\n";
    if (projects != 150 || project_misses.textured || project_misses.flat || zero_size ||
        gaps || !pins_ok || !grille_ok || grille_faces == 0 ||
        max_entries > MaterialCache::capacity) {
        std::cerr << "project material binding differs from the retail load order\n";
        return 9;
    }
    return 0;
}
