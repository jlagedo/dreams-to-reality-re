#include "inspect/catalog.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

size_t count(const od::inspect::Source& source, std::string_view kind) {
    size_t result = 0;
    for (const auto& row : source.rows) if (row.kind == kind) ++result;
    return result;
}

size_t bf_members(const od::inspect::Source& source) {
    size_t result = 0;
    for (const auto& row : source.rows)
        if (row.parent != SIZE_MAX && source.rows[row.parent].kind == "BF archive" &&
            row.key.compare(0, 7, "member:") == 0) ++result;
    return result;
}
}

int main() {
    const char* cue1 = std::getenv("DREAMS_CUE1");
    const char* cue2 = std::getenv("DREAMS_CUE2");
    if (!cue1 || !cue2) {
        std::cout << "DREAMS_CUE1/2 are not configured\n";
        return 77;
    }
    od::inspect::Catalog catalog;
    std::string error;
    bool okay = true;
    okay &= expect(catalog.replace(0, cue2, error), "Disc 2 mount failed");
    if (!okay) { std::cerr << error << '\n'; return 1; }
    okay &= expect(catalog.replace(1, cue1, error), "Disc 1 mount failed");
    if (!okay) { std::cerr << error << '\n'; return 1; }
    okay &= expect(catalog.source(0)->image->identity() == od::disc::Identity::disc2,
                   "Disc 2 identity changed with selection order");
    okay &= expect(catalog.source(1)->image->identity() == od::disc::Identity::disc1,
                   "Disc 1 identity changed with selection order");
    for (size_t i = 0; i < 2000 && (!catalog.source(0)->complete ||
                                     !catalog.source(1)->complete); ++i) catalog.tick(4);
    const auto* first = catalog.source(0);
    const auto* second = catalog.source(1);
    okay &= expect(first->complete && second->complete, "Catalog did not complete");
    okay &= expect(first->total_files == 409 && second->total_files == 1219,
                   "Physical file inventory differs from extracted reference");
    okay &= expect(count(*first, "Project") == 150 && count(*second, "Project") == 150,
                   "Project index did not expose 150 records per disc");
    okay &= expect(bf_members(*first) == 6 && bf_members(*second) == 5,
                   "BF member inventory differs from reference");
    okay &= expect(count(*first, "Audio track") == 13 && count(*second, "Audio track") == 11,
                   "Audio track inventory differs from CUE");
    size_t invalid_physical = 0;
    for (const auto* source : {first, second}) {
        for (const auto& row : source->rows) {
            if (!row.physical || row.status != od::inspect::Status::invalid) continue;
            ++invalid_physical;
            std::cerr << "Index failure: " << row.path << ": " << row.detail << '\n';
        }
    }
    okay &= expect(invalid_physical == 0, "Retail metadata index rejected physical assets");
    okay &= expect(count(*first, "Model archive") + count(*second, "Model archive") == 191,
                   "DAN archive index count differs from corpus");
    okay &= expect(count(*first, "Scene") + count(*second, "Scene") == 98,
                   "DSN scene index count differs from corpus");
    okay &= expect(count(*first, "VGA sprite") == 232 &&
                   count(*second, "VGA sprite") == 232,
                   "Viewer-derived OBJET sheet index differs from independent parser");
    okay &= expect(count(*first, "Geometry tag") + count(*second, "Geometry tag") == 98 &&
                   count(*first, "Texture tile tag") +
                       count(*second, "Texture tile tag") == 98 * 64,
                   "Viewer-derived DSN tag inventory differs from reference");
    const auto cai = catalog.resolve("CAI.DAN");
    okay &= expect(cai.size() == 2, "CAI.DAN should retain both physical sources");
    const auto projects = catalog.search("Project71", od::inspect::Group::projects,
                                          0, 0, false, false);
    size_t project_copies = 0;
    for (const auto& item : projects)
        if (catalog.row(item.slot, item.row)->kind == "Project") ++project_copies;
    okay &= expect(project_copies == 2, "Project71 should retain both record copies");
    bool placed_cai = false;
    for (const auto& item : projects) {
        const auto* project = catalog.row(item.slot, item.row);
        if (item.slot != 0 || project->kind != "Project") continue;
        for (size_t child : catalog.source(item.slot)->children[item.row]) {
            const auto* object = catalog.row(item.slot, child);
            if (object->kind == "Project object" &&
                object->detail == "Target: CAI.DAN" &&
                object->status == od::inspect::Status::ambiguous)
                placed_cai = true;
        }
    }
    okay &= expect(placed_cai, "Disc 2 Project71 did not retain ambiguous CAI.DAN placement");
    const auto disc2_results = catalog.search("CAI.DAN", od::inspect::Group::count,
                                               2, 0, true, false);
    okay &= expect(!disc2_results.empty(), "Global search lost CAI.DAN references");
    for (const auto& item : disc2_results)
        okay &= expect(catalog.source(item.slot)->image->identity() == od::disc::Identity::disc2,
                       "Disc filter leaked a different source");
    const auto all_scenes = catalog.search("", od::inspect::Group::scenes,
                                           0, 0, false, false);
    okay &= expect(all_scenes.size() >= 98, "Scene catalog lost physical DSN files");
    const uint64_t previous_mount = first->image->mount_id();
    okay &= expect(!catalog.replace(0, "missing-image.cue", error),
                   "Invalid replacement unexpectedly succeeded");
    okay &= expect(catalog.source(0)->image->mount_id() == previous_mount,
                   "Failed replacement invalidated the usable source");
    catalog.unmount(0);
    okay &= expect(catalog.source(0) == nullptr && catalog.source(1) != nullptr,
                   "Unmount invalidated the other source");
    return okay ? 0 : 1;
}
