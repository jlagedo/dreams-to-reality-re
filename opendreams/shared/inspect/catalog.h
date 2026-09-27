#pragma once

// ODViewer inspection data. These search, grouping and reference snapshots are
// new navigation code, not reconstructed retail game entry points.
#include "disc/image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace od::inspect {

enum class Group {
    projects, scenes, models, animations, textures, video, audio,
    dialogue, interface_assets, game_data, extras, source, count
};
enum class Status { indexing, available, missing, ambiguous, invalid, unindexed };

const char* group_name(Group group);
const char* status_name(Status status);
const char* identity_name(disc::Identity identity);

struct Row {
    size_t id = 0;
    size_t parent = SIZE_MAX;
    Group group = Group::extras;
    Status status = Status::indexing;
    bool physical = false;
    bool extra = false;
    std::string name;
    std::string kind;
    std::string path; // Owning physical ISO path, never a synthetic filename.
    std::string key;  // Internal index/name for a logical child.
    std::string detail;
    std::string provenance; // Retail symbol or explicitly viewer-derived field.
    uint64_t size = 0;
    uint64_t offset = 0;
    bool has_extent = false;
};

struct Source {
    std::shared_ptr<const disc::Image> image;
    std::vector<Row> rows;
    std::vector<std::vector<size_t>> children; // Indexed by Row::id.
    size_t indexed_files = 0;
    size_t total_files = 0;
    size_t indexable_files = 0;
    bool complete = false;
};

struct Candidate {
    size_t slot = 0;
    size_t row = 0;
};

class Catalog {
public:
    Catalog();
    ~Catalog();
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;
    // Replacement commits only after the new image has opened successfully.
    bool replace(size_t slot, const std::filesystem::path& cue, std::string& error);
    void unmount(size_t slot);
    const Source* source(size_t slot) const;
    const Row* row(size_t slot, size_t index) const;
    void tick(size_t files_per_source = 2);
    std::vector<Candidate> resolve(std::string_view target) const;
    std::vector<Candidate> search(std::string_view text, Group group,
                                  int disc_filter, int status_filter,
                                  bool include_extras, bool source_view) const;

private:
    struct State;
    std::array<std::unique_ptr<State>, 2> states_;
    void refresh_references();
};

} // namespace od::inspect
