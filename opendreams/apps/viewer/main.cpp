#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include "audio/audio_preview.h"
#include "inspect/catalog.h"
#include "inspect/still_preview.h"
#include "inspect/viewer_prefs.h"
#include "port/scene.h"
#include "render/model_preview.h"
#include "render/still_preview.h"
#include "render/video_preview.h"
#include "shell.h"
#include <imgui.h>
#include <util/sokol_imgui.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

od::Shell shell;
struct ViewerUi {
    od::inspect::Catalog catalog;
    std::array<std::array<char, 1024>, 2> paths{};
    std::array<std::string, 2> errors, dialog_paths;
    std::mutex dialog_mutex;
    char query[256]{};
    int group = 0, disc_filter = 0, status_filter = 0;
    std::string kind_filter;
    std::vector<std::string> kinds;
    std::array<size_t, 2> kind_counts{{SIZE_MAX, SIZE_MAX}};
    bool include_extras = false;
    bool focus_workspace = false;
    size_t selected_slot = SIZE_MAX, selected_row = SIZE_MAX, reveal_slot = SIZE_MAX;
    uint64_t selected_mount = 0;
    int sort_column = 0;
    bool sort_ascending = true;
    std::unique_ptr<od::port::PreviewLevelContext> preview_level;
    od::ModelPreview model_preview;
    od::VideoPreview video_preview;
    od::inspect::StillImage still_image;
    od::StillPreview still_preview;
    od::AudioPreview audio_preview;
    od::inspect::StillImage portrait_image;
    od::StillPreview portrait_preview;
    size_t audio_row = SIZE_MAX;
    size_t still_row = SIZE_MAX;
    uint32_t palette_row = 0;
    bool transparent_zero = false;
    std::string preview_error;
    od::inspect::ViewerPrefs preferences;
    std::string preferences_file, preferences_error;
} ui;

#ifndef __EMSCRIPTEN__
struct DialogRequest { ViewerUi* state; size_t slot; };
void SDLCALL selected_cue(void* user, const char* const* files, int) {
    std::unique_ptr<DialogRequest> request(static_cast<DialogRequest*>(user));
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(request->state->dialog_mutex);
    request->state->dialog_paths[request->slot] = files[0];
}
#endif

void load_selected_preview(ViewerUi& state);

void select(ViewerUi& state, size_t slot, size_t row) {
    const auto* source = state.catalog.source(slot);
    if (!source || row >= source->rows.size()) return;
    state.selected_slot = slot;
    state.selected_row = row;
    state.selected_mount = source->image->mount_id();
    state.focus_workspace = true;
    load_selected_preview(state);
}

const od::inspect::Row* selection(const ViewerUi& state) {
    const auto* source = state.catalog.source(state.selected_slot);
    return source && source->image->mount_id() == state.selected_mount
        ? state.catalog.row(state.selected_slot, state.selected_row) : nullptr;
}

void clear_preview(ViewerUi& state) {
    state.model_preview.clear_model();
    state.video_preview.close();
    state.still_preview.clear();
    state.still_image = {};
    state.still_row = SIZE_MAX;
    state.audio_preview.stop();
    state.portrait_preview.clear();
    state.portrait_image = {};
    state.audio_row = SIZE_MAX;
    state.preview_level.reset();
    state.preview_error.clear();
}

void load_selected_preview(ViewerUi& state) {
    clear_preview(state);
    const auto* row = selection(state);
    const auto* source = state.catalog.source(state.selected_slot);
    if (!row || !source) return;
    if (row->kind == "HNM6/HNS6 movie" || row->kind == "UBB2/UBS2 movie" ||
        row->kind == "HNM4 animated texture") {
        state.video_preview.open(source->image,row->path,state.preview_error);
        return;
    }
    const bool audio_parent = row->kind == "Sound bank" ||
        row->kind == "Dialogue bank";
    const bool audio_leaf = row->kind == "Sound effect" ||
        row->kind == "Dialogue entry" || row->kind == "Audio track";
    if (audio_parent || audio_leaf) {
        const od::inspect::Row* chosen = row;
        if (audio_parent) {
            for (size_t child : source->children[row->id]) {
                const auto& candidate=source->rows[child];
                if (candidate.status==od::inspect::Status::available &&
                    (candidate.kind=="Sound effect" || candidate.kind=="Dialogue entry")) {
                    chosen=&candidate;
                    break;
                }
            }
            if (chosen==row) {
                state.preview_error="selected sound bank has no playable entries";
                return;
            }
        }
        state.audio_row=chosen->id;
        const size_t colon=chosen->key.find(':');
        if (colon==std::string::npos) {
            state.preview_error="selected audio row has no source index";
            return;
        }
        char* end=nullptr;
        const unsigned long index=std::strtoul(chosen->key.c_str()+colon+1,&end,10);
        if (!end || *end || index>UINT_MAX) {
            state.preview_error="selected audio row index is invalid";
            return;
        }
        if (chosen->kind=="Sound effect")
            state.audio_preview.open_sound(source->image,chosen->path,
                                            static_cast<size_t>(index),state.preview_error);
        else if (chosen->kind=="Dialogue entry") {
            if (state.audio_preview.open_dialogue(source->image,chosen->path,
                          static_cast<size_t>(index),state.preview_error) &&
                state.audio_preview.has_portrait()) {
                std::string portrait_error;
                if (od::inspect::portrait_still_image(state.audio_preview.portrait(),
                       state.portrait_image,portrait_error)) {
                    if (!state.portrait_preview.load(state.portrait_image,0,true,
                                                     portrait_error))
                        state.preview_error=portrait_error;
                } else state.preview_error=portrait_error;
            }
        } else state.audio_preview.open_track(source->image,
                    static_cast<unsigned>(index),state.preview_error);
        return;
    }
    const bool image_parent = row->kind == "Font" || row->kind == "Sprite set" ||
        row->kind == "Icon bank" || row->kind == "VGA sprite sheet";
    const bool image_leaf = row->kind == "Sprite slot" || row->kind == "Font glyph" ||
        row->kind == "VGA sprite" || row->kind == "Material texture" ||
        row->kind == "Scene texture" || row->kind == "Texture bank" ||
        row->kind == "Palette tag" || row->kind == "Texture tile tag";
    if (image_parent || image_leaf) {
        const od::inspect::Row* display = row;
        if (image_parent) {
            size_t first = SIZE_MAX;
            for (size_t child : source->children[row->id]) {
                const auto& candidate = source->rows[child];
                if (candidate.status != od::inspect::Status::available) continue;
                if (first == SIZE_MAX) first = child;
                if (row->kind == "Font" && candidate.key == "slot:65") {
                    first = child;
                    break;
                }
            }
            if (first == SIZE_MAX) {
                state.preview_error = "selected image set has no renderable slot";
                return;
            }
            display = &source->rows[first];
        }
        state.still_row = display->id;
        if (!od::inspect::load_still_image(*source,*display,state.still_image,
                                           state.preview_error)) return;
        state.palette_row = state.still_image.default_row;
        state.transparent_zero = state.still_image.transparent_zero;
        state.still_preview.load(state.still_image,state.palette_row,
                                 state.transparent_zero,state.preview_error);
        return;
    }
    auto preview = std::make_unique<od::port::PreviewLevelContext>(source->image);
    if (row->kind == "Model name" || row->kind == "Model archive") {
        const std::string_view name = row->kind == "Model name"
            ? std::string_view(row->name) : std::string_view{};
        if (!preview->select_model(row->path, name, state.preview_error)) return;
    } else if (row->kind == "Project object" && row->parent != SIZE_MAX &&
               row->parent < source->rows.size() &&
               row->name.compare(0, 5, "OBJET") == 0) {
        char* end = nullptr;
        const unsigned long slot = std::strtoul(row->name.c_str() + 5, &end, 10);
        if (!end || *end || slot == 0 || slot >= 16) return;
        const auto& project = source->rows[row->parent];
        if (!preview->select_project(project.name, static_cast<size_t>(slot),
                                     state.preview_error)) return;
    } else return;
    if (!od::port::SCENE_LoadLevel(*preview, state.preview_error) ||
        !state.model_preview.load(preview->selected_actor(), state.preview_error))
        return;
    state.preview_level = std::move(preview);
}

#ifndef __EMSCRIPTEN__
void mount(ViewerUi& state, size_t slot) {
    if (!state.paths[slot][0]) return;
    std::string error;
    if (!state.catalog.replace(slot, std::filesystem::u8path(state.paths[slot].data()), error)) {
        state.errors[slot] = std::move(error);
        return;
    }
    state.errors[slot].clear();
    const auto* mounted = state.catalog.source(slot);
    const std::string canonical = mounted->image->cue_path().u8string();
    const bool changed = state.preferences.cue_paths[slot] != canonical;
    if (changed) {
        state.preferences.cue_paths[slot] = canonical;
    }
    if (!state.preferences_file.empty() && (changed || !state.preferences_error.empty()))
        od::inspect::save_viewer_prefs(state.preferences_file, state.preferences,
                                        state.preferences_error);
    if (state.selected_slot == slot) {
        clear_preview(state);
        state.selected_slot = SIZE_MAX;
    }
}
#endif

void source_card(ViewerUi& state, size_t slot) {
    ImGui::PushID(static_cast<int>(slot));
    const auto* source = state.catalog.source(slot);
    ImGui::Text("Source %c  |  %s", slot ? 'B' : 'A',
                source ? od::inspect::identity_name(source->image->identity()) : "Not mounted");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##cue", "Path to a .cue file", state.paths[slot].data(),
                             state.paths[slot].size());
#ifndef __EMSCRIPTEN__
    if (ImGui::Button("Choose .cue")) {
        static const SDL_DialogFileFilter filters[] = {{"CUE sheets", "cue"}};
        SDL_ShowOpenFileDialog(selected_cue, new DialogRequest{&state, slot},
                               nullptr, filters, 1, nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button(source ? "Replace" : "Mount")) {
        mount(state, slot);
        source = state.catalog.source(slot);
    }
    if (source) {
        ImGui::SameLine();
        if (ImGui::Button("Unmount")) {
            state.catalog.unmount(slot);
            source = nullptr;
            state.preferences.cue_paths[slot].clear();
            if (!state.preferences_file.empty())
                od::inspect::save_viewer_prefs(state.preferences_file,
                                               state.preferences,
                                               state.preferences_error);
            if (state.selected_slot == slot) {
                clear_preview(state);
                state.selected_slot = SIZE_MAX;
            }
        }
    }
#endif
    if (source) {
        ImGui::Text("%zu files, %zu/%zu formats indexed%s", source->total_files,
            source->indexed_files, source->indexable_files,
            source->complete ? " (complete)" : " (partial search)");
        if (source->image->identity() == od::disc::Identity::unknown ||
            source->image->identity() == od::disc::Identity::ambiguous)
            ImGui::TextDisabled("Identity unresolved; raw source remains browseable.");
        const auto* other = state.catalog.source(1 - slot);
        if (other && other->image->identity() == source->image->identity() &&
            (source->image->identity() == od::disc::Identity::disc1 ||
             source->image->identity() == od::disc::Identity::disc2))
            ImGui::TextDisabled("Duplicate game-disc identity; both sources remain separate.");
        for (const auto& track : source->image->tracks())
            if (track.status != od::disc::TrackStatus::available)
                ImGui::TextDisabled("Track %u: %s", track.number, track.note.c_str());
    }
    if (!state.errors[slot].empty())
        ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "%s", state.errors[slot].c_str());
    ImGui::PopID();
}

void source_strip(ViewerUi& state) {
    const int mounted = static_cast<int>(state.catalog.source(0) != nullptr) +
                        static_cast<int>(state.catalog.source(1) != nullptr);
    ImGui::SetNextItemOpen(mounted == 0, ImGuiCond_Once);
    const std::string heading = "Game discs (" + std::to_string(mounted) + "/2 mounted)##sources";
    if (!ImGui::CollapsingHeader(heading.c_str())) return;
    const bool side_by_side = ImGui::GetContentRegionAvail().x >= 800.0f;
    if (ImGui::BeginTable(side_by_side ? "source cards wide" : "source cards stacked",
                          side_by_side ? 2 : 1,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        for (size_t slot = 0; slot < 2; ++slot) {
            ImGui::TableNextColumn();
            source_card(state, slot);
        }
        ImGui::EndTable();
    }
    if (!state.preferences_error.empty())
        ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "Settings: %s",
                           state.preferences_error.c_str());
    if (mounted == 0) {
#ifdef __EMSCRIPTEN__
        ImGui::TextUnformatted("Disc-image opening is currently desktop-only.");
#else
        ImGui::TextDisabled("Choose one or two original game-disc .cue files to begin browsing.");
#endif
    }
}

void source_branch(ViewerUi& state, size_t slot, size_t id, int depth) {
    const auto* source = state.catalog.source(slot);
    if (!source || depth > 64) return;
    const auto& row = source->rows[id];
    const bool active = state.selected_slot == slot && state.selected_row == id &&
        state.selected_mount == source->image->mount_id();
    ImGui::PushID(static_cast<int>(id));
    if (!source->children[id].empty()) {
        if (state.reveal_slot == slot && selection(state)) {
            size_t ancestor = state.selected_row;
            while (ancestor != SIZE_MAX && ancestor < source->rows.size()) {
                if (ancestor == id) { ImGui::SetNextItemOpen(true); break; }
                ancestor = source->rows[ancestor].parent;
            }
        }
        const bool open = ImGui::TreeNodeEx(row.name.c_str(),
            active ? ImGuiTreeNodeFlags_Selected : 0);
        if (ImGui::IsItemClicked()) select(state, slot, id);
        if (open) {
            for (size_t child : source->children[id])
                source_branch(state, slot, child, depth + 1);
            ImGui::TreePop();
        }
    } else if (ImGui::Selectable(row.name.c_str(), active)) select(state, slot, id);
    ImGui::PopID();
}

void source_tree(ViewerUi& state, size_t slot) {
    const auto* source = state.catalog.source(slot);
    if (!source) return;
    for (size_t i = 0; i < source->image->entries().size(); ++i) {
        if (source->rows[i].parent == SIZE_MAX)
            source_branch(state, slot, i, 0);
    }
}

void left_pane(ViewerUi& state) {
    ImGui::SeparatorText("Browse assets");
    ImGui::TextUnformatted("Search");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search names, keys, paths", state.query,
                             sizeof(state.query));
    static constexpr const char* discs[] = {"Both discs", "Disc 1", "Disc 2"};
    ImGui::TextUnformatted("Disc");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##disc", &state.disc_filter, discs, 3);
    static constexpr const char* statuses[] = {"All statuses", "Indexing", "Available",
        "Missing", "Ambiguous", "Invalid", "Unindexed"};
    ImGui::TextUnformatted("Status");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##status", &state.status_filter, statuses, 7);
    std::array<size_t, 2> counts{};
    for (size_t slot = 0; slot < 2; ++slot)
        counts[slot] = state.catalog.source(slot) ? state.catalog.source(slot)->rows.size() : 0;
    if (counts != state.kind_counts) {
        state.kinds.clear();
        for (size_t slot = 0; slot < 2; ++slot)
            if (const auto* source = state.catalog.source(slot))
                for (const auto& row : source->rows) state.kinds.push_back(row.kind);
        std::sort(state.kinds.begin(), state.kinds.end());
        state.kinds.erase(std::unique(state.kinds.begin(), state.kinds.end()), state.kinds.end());
        state.kind_counts = counts;
    }
    ImGui::TextUnformatted("Kind");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##kind", state.kind_filter.empty() ? "All kinds" :
                                              state.kind_filter.c_str())) {
        if (ImGui::Selectable("All kinds", state.kind_filter.empty())) state.kind_filter.clear();
        for (const auto& kind : state.kinds)
            if (ImGui::Selectable(kind.c_str(), kind == state.kind_filter)) state.kind_filter = kind;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Include extras", &state.include_extras);
    ImGui::SeparatorText("Collections");
    for (int i = 0; i <= static_cast<int>(od::inspect::Group::extras); ++i) {
        if (i == static_cast<int>(od::inspect::Group::extras) && !state.include_extras) continue;
        if (ImGui::Selectable(od::inspect::group_name(static_cast<od::inspect::Group>(i)),
                              state.group == i)) state.group = i;
    }
    ImGui::SeparatorText("Source discs");
    const int source_group = static_cast<int>(od::inspect::Group::source);
    if (ImGui::Selectable("All source files", state.group == source_group))
        state.group = source_group;
    for (size_t slot = 0; slot < 2; ++slot) {
        const auto* source = state.catalog.source(slot);
        if (!source) continue;
        ImGui::PushID(static_cast<int>(slot));
        if (state.reveal_slot == slot) ImGui::SetNextItemOpen(true);
        if (ImGui::TreeNode(od::inspect::identity_name(source->image->identity()))) {
            source_tree(state, slot);
            for (const auto& row : source->rows) {
                if (row.kind != "Audio track") continue;
                ImGui::PushID(static_cast<int>(row.id));
                if (ImGui::Selectable(row.name.c_str(),
                    state.selected_slot == slot && state.selected_row == row.id))
                    select(state, slot, row.id);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void results_pane(ViewerUi& state) {
    const auto section = static_cast<od::inspect::Group>(state.group);
    const bool global_search = state.query[0] != '\0';
    const auto group = global_search ? od::inspect::Group::count : section;
    const bool source_view = !global_search && section == od::inspect::Group::source;
    auto found = state.catalog.search(state.query, group, state.disc_filter,
        state.status_filter, state.include_extras || source_view, source_view);
    if (!state.kind_filter.empty())
        found.erase(std::remove_if(found.begin(), found.end(), [&](const auto& item) {
            return state.catalog.row(item.slot, item.row)->kind != state.kind_filter;
        }), found.end());
    std::sort(found.begin(), found.end(), [&](const auto& a, const auto& b) {
        const auto* left = state.catalog.row(a.slot, a.row);
        const auto* right = state.catalog.row(b.slot, b.row);
        int comparison = 0;
        switch (state.sort_column) {
        case 0: comparison = left->name.compare(right->name); break;
        case 1: comparison = left->kind.compare(right->kind); break;
        case 2: comparison = static_cast<int>(a.slot) - static_cast<int>(b.slot); break;
        case 3: comparison = left->path.compare(right->path); break;
        case 4: comparison = static_cast<int>(left->status) - static_cast<int>(right->status); break;
        }
        if (!comparison) comparison = static_cast<int>(a.row) - static_cast<int>(b.row);
        return state.sort_ascending ? comparison < 0 : comparison > 0;
    });
    std::vector<std::string> names;
    names.reserve(found.size());
    for (const auto& item : found) names.push_back(state.catalog.row(item.slot, item.row)->name);
    std::sort(names.begin(), names.end());
    const size_t logical_count = static_cast<size_t>(std::unique(names.begin(), names.end()) - names.begin());
    ImGui::Text("%s  |  %zu sources, %zu names", global_search ? "Search results" :
                od::inspect::group_name(section),
                found.size(), logical_count);
    if (ImGui::BeginTable("assets", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort |
                                ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 180.0f);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Parent / path", ImGuiTableColumnFlags_WidthFixed, 300.0f);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty && specs->SpecsCount) {
            state.sort_column = specs->Specs[0].ColumnIndex;
            state.sort_ascending = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            specs->SpecsDirty = false;
        }
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(found.size()));
        while (clipper.Step()) for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n) {
            const auto item = found[static_cast<size_t>(n)];
            const auto* row = state.catalog.row(item.slot, item.row);
            const auto* source = state.catalog.source(item.slot);
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::PushID(n);
            const bool active = state.selected_slot == item.slot && state.selected_row == item.row &&
                state.selected_mount == source->image->mount_id();
            if (ImGui::Selectable(row->name.c_str(), active, ImGuiSelectableFlags_SpanAllColumns))
                select(state, item.slot, item.row);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(row->kind.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(od::inspect::identity_name(source->image->identity()));
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(row->path.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(od::inspect::status_name(row->status));
        }
        ImGui::EndTable();
    }
}

void details_pane(ViewerUi& state) {
    ImGui::SeparatorText("Details");
    const auto* row = selection(state);
    if (!row) { ImGui::TextDisabled("Select an asset or source file."); return; }
    const auto* source = state.catalog.source(state.selected_slot);
    ImGui::TextWrapped("%s", row->name.c_str());
    ImGui::TextDisabled("%s  |  %s", row->kind.c_str(), od::inspect::status_name(row->status));
    ImGui::SeparatorText("Source");
    ImGui::Text("Source: %s", od::inspect::identity_name(source->image->identity()));
    ImGui::TextDisabled("CUE file");
    ImGui::TextWrapped("%s", source->image->cue_path().u8string().c_str());
    ImGui::TextDisabled("Physical path");
    ImGui::TextWrapped("%s", row->path.c_str());
    ImGui::SeparatorText("Asset data");
    if (!row->key.empty()) {
        ImGui::TextDisabled("Internal key");
        ImGui::TextWrapped("%s", row->key.c_str());
    }
    ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(row->size));
    if (row->has_extent)
        ImGui::Text("%s: %llu", row->physical ? "ISO logical offset" : "Offset in physical file",
                    static_cast<unsigned long long>(row->offset));
    if (!row->detail.empty()) ImGui::TextWrapped("%s", row->detail.c_str());
    ImGui::TextDisabled("Derived from");
    ImGui::TextWrapped("%s", row->provenance.c_str());
    if (row->parent != SIZE_MAX && ImGui::Button("Open parent"))
        select(state, state.selected_slot, row->parent);
    if (!row->physical) {
        if (row->parent != SIZE_MAX) ImGui::SameLine();
        if (ImGui::Button("Reveal in source disc")) {
            size_t parent = row->parent;
            while (parent != SIZE_MAX && !source->rows[parent].physical)
                parent = source->rows[parent].parent;
            if (parent != SIZE_MAX) {
                select(state, state.selected_slot, parent);
                state.group = static_cast<int>(od::inspect::Group::source);
                state.reveal_slot = state.selected_slot;
            }
        }
    }
    if (row->detail.compare(0, 8, "Target: ") == 0) {
        const auto target = row->detail.substr(8);
        const auto candidates = state.catalog.resolve(target);
        ImGui::SeparatorText("Reference candidates");
        if (candidates.empty()) ImGui::TextDisabled("Missing: %s", target.c_str());
        else {
            if (candidates.size() > 1)
                ImGui::TextDisabled("%zu candidates; choose a source", candidates.size());
            for (const auto& candidate : candidates) {
                const auto* option = state.catalog.row(candidate.slot, candidate.row);
                const auto* owner = state.catalog.source(candidate.slot);
                ImGui::PushID(static_cast<int>(candidate.slot * 100000 + candidate.row));
                if (ImGui::Button("Open")) select(state, candidate.slot, candidate.row);
                ImGui::SameLine();
                ImGui::TextWrapped("%s: %s", od::inspect::identity_name(owner->image->identity()),
                                    option->path.c_str());
                ImGui::PopID();
            }
        }
    }
    bool any = false;
    const size_t parent_id = state.selected_row;
    for (size_t child_id : source->children[parent_id]) {
        const auto& child = source->rows[child_id];
        if (!any) { ImGui::SeparatorText("Contents"); any = true; }
        ImGui::PushID(static_cast<int>(child.id));
        if (ImGui::Selectable(child.name.c_str())) select(state, state.selected_slot, child.id);
        ImGui::PopID();
    }
}

void audio_preview_pane(ViewerUi& state, const od::inspect::Source& source) {
    if (state.audio_row>=source.rows.size()) return;
    const auto& shown=source.rows[state.audio_row];
    ImGui::Text("%s  |  %s",shown.name.c_str(),
                od::inspect::identity_name(source.image->identity()));
    if (shown.parent!=SIZE_MAX && shown.parent<source.children.size()) {
        std::vector<size_t> siblings;
        for (size_t child : source.children[shown.parent]) {
            const auto& candidate=source.rows[child];
            if (candidate.kind==shown.kind &&
                candidate.status==od::inspect::Status::available)
                siblings.push_back(child);
        }
        if (siblings.size()>1) {
            const auto at=std::find(siblings.begin(),siblings.end(),state.audio_row);
            const size_t position=static_cast<size_t>(at-siblings.begin());
            if (position<siblings.size() && ImGui::Button("Previous")) {
                select(state,state.selected_slot,siblings[(position+siblings.size()-1)%siblings.size()]);
                return;
            }
            ImGui::SameLine();
            if (position<siblings.size() && ImGui::Button("Next")) {
                select(state,state.selected_slot,siblings[(position+1)%siblings.size()]);
                return;
            }
            ImGui::SameLine();
            ImGui::Text("%zu / %zu",position+1,siblings.size());
        }
    }
    if (!state.audio_preview.active()) {
        if (!state.preview_error.empty()) ImGui::TextWrapped("%s",state.preview_error.c_str());
        if (ImGui::Button("Open audio")) load_selected_preview(state);
        return;
    }
    std::string playback_error;
    if (!state.audio_preview.tick(playback_error)) {
        state.preview_error=playback_error;
        ImGui::TextWrapped("%s",state.preview_error.c_str());
        return;
    }
    if (ImGui::Button(state.audio_preview.ended() ? "Replay" :
                       state.audio_preview.paused() ? "Play" : "Pause")) {
        if (state.audio_preview.ended()) {
            if (!state.audio_preview.restart(playback_error))
                state.preview_error=playback_error;
        } else state.audio_preview.set_paused(!state.audio_preview.paused());
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart") && !state.audio_preview.restart(playback_error))
        state.preview_error=playback_error;
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        state.audio_preview.stop();
        state.portrait_preview.clear();
        state.portrait_image={};
        return;
    }
    const double duration=state.audio_preview.duration_seconds();
    const double position=state.audio_preview.position_seconds();
    const float fraction=duration>0 ? static_cast<float>(std::clamp(position/duration,0.0,1.0)) : 0.0f;
    ImGui::ProgressBar(fraction,ImVec2(-1,0));
    ImGui::Text("%.1f / %.1f s  |  %u Hz, %u ch, %u-bit",position,duration,
                state.audio_preview.rate(),state.audio_preview.channels(),
                state.audio_preview.bits());
    if (state.audio_preview.repaired_header())
        ImGui::TextDisabled("Retail clip has an invalid float tag; playing its PCM samples.");
    if (!state.preview_error.empty())
        ImGui::TextWrapped("%s",state.preview_error.c_str());
    const auto& waveform=state.audio_preview.waveform();
    if (!waveform.empty())
        ImGui::PlotLines("##waveform",waveform.data(),static_cast<int>(waveform.size()),
                         0,nullptr,0.0f,1.0f,ImVec2(-1,55));
    if (state.audio_preview.kind()!=od::AudioPreview::Kind::dialogue) return;
    if (state.portrait_preview.has_image()) {
        const float side=std::min(128.0f,std::max(64.0f,ImGui::GetContentRegionAvail().y*0.55f));
        ImGui::Image(simgui_imtextureid(state.portrait_preview.image_view()),
                     ImVec2(side,side));
        ImGui::SameLine();
    } else if (!state.audio_preview.portrait_note().empty()) {
        ImGui::TextDisabled("Portrait: %s",state.audio_preview.portrait_note().c_str());
    }
    const auto& lines=state.audio_preview.captions();
    const size_t current=state.audio_preview.current_caption_index();
    ImGui::BeginGroup();
    ImGui::Text("Dialogue: %zu lines",lines.size());
    if (current!=SIZE_MAX && current<lines.size())
        ImGui::TextWrapped("%s",lines[current].text.c_str());
    else ImGui::TextDisabled("Waiting for the first caption");
    ImGui::EndGroup();
    if (ImGui::TreeNode("Script and timing")) {
        for (size_t i=0; i<lines.size(); ++i) {
            if (i==current) ImGui::TextColored(ImVec4(1,0.86f,0.45f,1),
                "%u: %s",lines[i].start_tick,lines[i].text.c_str());
            else ImGui::Text("%u: %s",lines[i].start_tick,lines[i].text.c_str());
        }
        ImGui::TreePop();
    }
}

void still_preview_pane(ViewerUi& state, const od::inspect::Source& source) {
    if (state.still_row >= source.rows.size()) return;
    const auto& shown = source.rows[state.still_row];
    ImGui::Text("%s  |  %u x %u", shown.name.c_str(),
                state.still_image.width,state.still_image.height);
    if (shown.parent != SIZE_MAX && shown.parent < source.children.size()) {
        std::vector<size_t> siblings;
        for (size_t child : source.children[shown.parent]) {
            const auto& candidate = source.rows[child];
            if (candidate.kind == shown.kind &&
                candidate.status == od::inspect::Status::available)
                siblings.push_back(child);
        }
        if (siblings.size() > 1) {
            const auto at = std::find(siblings.begin(),siblings.end(),state.still_row);
            const size_t position = static_cast<size_t>(at-siblings.begin());
            if (ImGui::Button("Previous") && position < siblings.size()) {
                select(state,state.selected_slot,siblings[(position+siblings.size()-1)%siblings.size()]);
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Next") && position < siblings.size()) {
                select(state,state.selected_slot,siblings[(position+1)%siblings.size()]);
                return;
            }
            ImGui::SameLine();
            ImGui::Text("%zu / %zu",position+1,siblings.size());
        }
    }
    bool changed = false;
    if (state.still_image.palette_rows() > 1) {
        int row = static_cast<int>(state.palette_row);
        if (ImGui::SliderInt("Palette row",&row,0,
                              static_cast<int>(state.still_image.palette_rows()-1))) {
            state.palette_row = static_cast<uint32_t>(row);
            changed = true;
        }
    }
    if (ImGui::Checkbox("Index 0 transparent",&state.transparent_zero)) changed = true;
    if (changed && !state.still_preview.load(state.still_image,state.palette_row,
                                               state.transparent_zero,state.preview_error)) {
        ImGui::TextWrapped("%s",state.preview_error.c_str());
        return;
    }
    ImGui::TextWrapped("%s",state.still_image.note.c_str());
    if (!state.still_preview.has_image()) {
        ImGui::TextWrapped("%s",state.preview_error.c_str());
        return;
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 50 || available.y < 30) return;
    const float palette_side = std::min(150.0f,std::max(64.0f,available.x*0.25f));
    const float picture_space = std::max(1.0f,available.x-palette_side-12.0f);
    const float scale = std::min({8.0f,picture_space/state.still_image.width,
                                  available.y/state.still_image.height});
    if (scale <= 0) return;
    const ImVec2 picture(state.still_image.width*scale,state.still_image.height*scale);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin,ImVec2(origin.x+picture.x,origin.y+picture.y),true);
    for (int y=0; y<static_cast<int>(picture.y); y+=16)
        for (int x=0; x<static_cast<int>(picture.x); x+=16)
            draw->AddRectFilled(ImVec2(origin.x+x,origin.y+y),
                ImVec2(origin.x+x+16,origin.y+y+16),
                ((x+y)/16)&1 ? IM_COL32(54,60,68,255) : IM_COL32(31,36,43,255));
    draw->PopClipRect();
    ImGui::Image(simgui_imtextureid(state.still_preview.image_view()),picture);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Source palette");
    ImGui::Image(simgui_imtextureid(state.still_preview.palette_view()),
                 ImVec2(palette_side,palette_side));
    ImGui::Text("Row %u",state.palette_row);
    ImGui::EndGroup();
}

void preview_pane(ViewerUi& state) {
    ImGui::SeparatorText("Preview");
    const auto* row = selection(state);
    if (state.audio_row!=SIZE_MAX) {
        const auto* source=state.catalog.source(state.selected_slot);
        if (source) audio_preview_pane(state,*source);
        return;
    }
    if (state.still_preview.has_image()) {
        const auto* source = state.catalog.source(state.selected_slot);
        if (source) still_preview_pane(state,*source);
        return;
    }
    if (state.video_preview.has_video()) {
        std::string playback_error;
        if (!state.video_preview.tick(playback_error)) state.preview_error = playback_error;
        if (!state.video_preview.has_video()) {
            ImGui::TextWrapped("%s", state.preview_error.c_str());
            return;
        }
        if (ImGui::Button(state.video_preview.playing() ? "Pause" : "Play"))
            state.video_preview.set_playing(!state.video_preview.playing());
        ImGui::SameLine();
        if (ImGui::Button("Step")) {
            state.video_preview.set_playing(false);
            if (!state.video_preview.single_step(playback_error))
                state.preview_error = playback_error;
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart") && !state.video_preview.restart(playback_error))
            state.preview_error = playback_error;
        ImGui::SameLine();
        ImGui::Text("%u / %u", state.video_preview.decoded_frames(),
                    state.video_preview.total_frames());
        if (!state.preview_error.empty())
            ImGui::TextWrapped("%s", state.preview_error.c_str());
        if (state.video_preview.has_image()) {
            state.video_preview.upload();
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const float width = std::min(available.x, available.y * (4.0f/3.0f));
            const float height = width * (3.0f/4.0f);
            const float inset = std::max(0.0f,(available.x-width)*0.5f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()+inset);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Image(simgui_imtextureid(state.video_preview.texture_view()),
                         ImVec2(width,height));
            if (!state.video_preview.caption().empty()) {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const auto& caption = state.video_preview.caption();
                const float wrap = std::max(0.0f,width-24.0f);
                const ImVec2 text_size = ImGui::CalcTextSize(caption.c_str(),nullptr,false,wrap);
                const float top = std::max(origin.y,
                    origin.y+height-text_size.y-12.0f);
                draw->PushClipRect(origin,ImVec2(origin.x+width,origin.y+height),true);
                draw->AddRectFilled(ImVec2(origin.x,top-4),
                                    ImVec2(origin.x+width,origin.y+height),
                                    IM_COL32(0,0,0,210));
                draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),
                              ImVec2(origin.x+12.0f,top),IM_COL32(255,255,255,255),
                              caption.c_str(),nullptr,wrap);
                draw->PopClipRect();
            }
        }
        return;
    }
    std::string title = "Select a model or prop";
    std::string subtitle = "3D preview is not ready yet.";
    if (row) {
        const auto* source = state.catalog.source(state.selected_slot);
        std::string target = row->name;
        const bool object_reference = row->kind == "Project object" &&
            row->detail.compare(0, 8, "Target: ") == 0;
        if (object_reference) target = row->detail.substr(8);
        const bool model = (row->group == od::inspect::Group::models &&
                            row->kind != "Project object") ||
            (object_reference && (target.find(".DAN") != std::string::npos ||
                                  target.find(".3DC") != std::string::npos));
        if (model) {
            title = target;
            if (object_reference && row->status == od::inspect::Status::ambiguous)
                subtitle = "Choose a source copy in Details.";
            else if (object_reference && row->status == od::inspect::Status::missing)
                subtitle = "Referenced model is missing.";
            else if (object_reference && row->status == od::inspect::Status::indexing)
                subtitle = "Indexing the referenced model...";
            else subtitle = std::string("3D preview is not ready yet  |  ") +
                od::inspect::identity_name(source->image->identity());
        } else {
            title = row->name;
            subtitle = "No visual preview for this asset.";
        }
    }
    if (state.model_preview.has_model() && row && state.preview_level) {
        const auto* source = state.catalog.source(state.selected_slot);
        ImGui::Text("%s -> %s | %s", row->name.c_str(),
            state.preview_level->selected_actor().asset_name.c_str(),
            od::inspect::identity_name(source->image->identity()));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row->path.c_str());
        const auto& model=state.preview_level->selected_actor().model;
        ImGui::TextDisabled("Static model | %zu nodes | %zu faces | %zu materials",
                            model.nodes.size(),model.faces.size(),model.materials.size());
    }
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x <= 0 || size.y <= 0) return;
    if (state.model_preview.has_model()) {
        state.model_preview.draw();
        const float side = std::min(size.x, size.y);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (size.x - side) * 0.5f);
        ImGui::Image(simgui_imtextureid(state.model_preview.texture_view()),
                     ImVec2(side, side));
        return;
    }
    if (!state.preview_error.empty()) subtitle = state.preview_error;
    ImGui::InvisibleButton("preview surface", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end(start.x + size.x, start.y + size.y);
    draw->AddRectFilled(start, end, IM_COL32(19, 24, 31, 255));
    for (float x = start.x + 32; x < end.x; x += 32)
        draw->AddLine(ImVec2(x, start.y), ImVec2(x, end.y), IM_COL32(43, 50, 61, 95));
    for (float y = start.y + 32; y < end.y; y += 32)
        draw->AddLine(ImVec2(start.x, y), ImVec2(end.x, y), IM_COL32(43, 50, 61, 95));
    draw->AddRect(start, end, IM_COL32(75, 88, 106, 255));
    const ImVec2 title_size = ImGui::CalcTextSize(title.c_str());
    const ImVec2 subtitle_size = ImGui::CalcTextSize(subtitle.c_str());
    const float center_y = start.y + size.y * 0.5f;
    draw->AddText(ImVec2(start.x + (size.x - title_size.x) * 0.5f,
                         center_y - title_size.y - 4.0f),
                  IM_COL32(226, 233, 244, 255), title.c_str());
    draw->AddText(ImVec2(start.x + (size.x - subtitle_size.x) * 0.5f,
                         center_y + 4.0f),
                  IM_COL32(155, 170, 190, 255), subtitle.c_str());
}

void workspace_pane(ViewerUi& state) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float upper_height = std::clamp(available.y * 0.55f, 190.0f,
                                          std::max(190.0f, available.y - 190.0f));
    ImGui::BeginChild("inspection", ImVec2(0, upper_height), ImGuiChildFlags_None);
    if (available.x >= 900.0f) {
        const float preview_width = (ImGui::GetContentRegionAvail().x -
                                     ImGui::GetStyle().ItemSpacing.x) * 0.62f;
        ImGui::BeginChild("preview", ImVec2(preview_width, 0), ImGuiChildFlags_Borders);
        preview_pane(state);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
        details_pane(state);
        ImGui::EndChild();
    } else if (ImGui::BeginTabBar("inspection tabs")) {
        if (ImGui::BeginTabItem("Preview")) {
            ImGui::BeginChild("preview", ImVec2(0, 0), ImGuiChildFlags_Borders);
            preview_pane(state);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Details")) {
            ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
            details_pane(state);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::BeginChild("results", ImVec2(0, 0), ImGuiChildFlags_Borders);
    results_pane(state);
    ImGui::EndChild();
}

void draw_ui(void* user) {
    auto& state = *static_cast<ViewerUi*>(user);
    {
        std::lock_guard<std::mutex> lock(state.dialog_mutex);
        for (size_t slot = 0; slot < 2; ++slot) if (!state.dialog_paths[slot].empty()) {
            SDL_strlcpy(state.paths[slot].data(), state.dialog_paths[slot].c_str(),
                        state.paths[slot].size());
            state.dialog_paths[slot].clear();
        }
    }
    state.catalog.tick(2);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("ODViewer - game assets", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::TextUnformatted("ODViewer");
    ImGui::SameLine();
    ImGui::TextDisabled("/  Browse and preview original game assets");
    source_strip(state);
    ImGui::Spacing();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x < 950.0f) {
        if (ImGui::BeginTabBar("main tabs")) {
            if (ImGui::BeginTabItem("Browse")) {
                ImGui::BeginChild("sections", ImVec2(0, 0), ImGuiChildFlags_Borders);
                left_pane(state);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Workspace", nullptr,
                                    state.focus_workspace ? ImGuiTabItemFlags_SetSelected : 0)) {
                workspace_pane(state);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        const float navigation_width = std::clamp(available.x * 0.22f, 260.0f, 320.0f);
        ImGui::BeginChild("sections", ImVec2(navigation_width, 0), ImGuiChildFlags_Borders);
        left_pane(state);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("workspace", ImVec2(0, 0), ImGuiChildFlags_None);
        workspace_pane(state);
        ImGui::EndChild();
    }
    state.focus_workspace = false;
    state.reveal_slot = SIZE_MAX;
    ImGui::End();
}

bool parse_args(int argc, char** argv, int& frames,
                std::array<const char*, 2>& cues, bool& preview_cai,
                std::string& preview_model, bool& preview_movie, std::string& preview_still,
                std::string& preview_audio) {
    for (int i = 1; i < argc;) {
        if (std::strcmp(argv[i], "--preview-cai") == 0) {
            preview_cai = true;
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--preview-movie") == 0) {
            preview_movie = true;
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--preview-model") == 0) {
            if (i+1>=argc) return false;
            preview_model=argv[i+1];
            i+=2;
            continue;
        }
        if (std::strcmp(argv[i], "--preview-still") == 0) {
            if (i+1>=argc) return false;
            preview_still=argv[i+1];
            i+=2;
            continue;
        }
        if (std::strcmp(argv[i], "--preview-audio") == 0) {
            if (i+1>=argc) return false;
            preview_audio=argv[i+1];
            i+=2;
            continue;
        }
        if (i + 1 >= argc) return false;
        if (std::strcmp(argv[i], "--cue1") == 0) cues[0] = argv[i + 1];
        else if (std::strcmp(argv[i], "--cue2") == 0) cues[1] = argv[i + 1];
        else if (std::strcmp(argv[i], "--frames") == 0) {
            errno = 0; char* end = nullptr;
            const long parsed = std::strtol(argv[i + 1], &end, 10);
            if (errno || !end || *end || parsed < 1 || parsed > INT_MAX) return false;
            frames = static_cast<int>(parsed);
        } else return false;
        i += 2;
    }
    return true;
}
} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    *appstate = &shell;
    int frames = 0;
    std::array<const char*, 2> cues{};
    bool preview_cai = false;
    std::string preview_model;
    bool preview_movie = false;
    std::string preview_still;
    std::string preview_audio;
    if (!parse_args(argc, argv, frames, cues, preview_cai, preview_model, preview_movie,
                    preview_still, preview_audio)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "usage: ODViewer [--frames N] [--cue1 path] [--cue2 path] [--preview-cai] [--preview-model DAN-stem] [--preview-movie] [--preview-still font|icon|vga|bank|material|scene] [--preview-audio effect|effect12|dialogue|track]");
        return SDL_APP_FAILURE;
    }
    if (!shell.init({"ODViewer", draw_ui, &ui, frames, 20.0f, 1.2f,
                     "ODViewer-window.rgba", 1440, 900})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", shell.error().c_str());
        return SDL_APP_FAILURE;
    }
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.CellPadding = ImVec2(10.0f, 7.0f);
    style.IndentSpacing = 22.0f;
    style.ScrollbarSize = 16.0f;
    std::string preview_error;
    if (!ui.model_preview.init(preview_error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", preview_error.c_str());
        shell.shutdown();
        return SDL_APP_FAILURE;
    }
    if (!ui.video_preview.init(preview_error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", preview_error.c_str());
        ui.model_preview.shutdown();
        shell.shutdown();
        return SDL_APP_FAILURE;
    }
#ifndef __EMSCRIPTEN__
    if (od::inspect::viewer_prefs_file(ui.preferences_file, ui.preferences_error))
        od::inspect::load_viewer_prefs(ui.preferences_file, ui.preferences,
                                       ui.preferences_error);
    for (size_t slot = 0; slot < cues.size(); ++slot) {
        const char* path = cues[slot] ? cues[slot] : ui.preferences.cue_paths[slot].c_str();
        if (!path || !*path) continue;
        SDL_strlcpy(ui.paths[slot].data(), path, ui.paths[slot].size());
        mount(ui, slot);
    }
#endif
    if (preview_cai) {
        ui.catalog.tick(10000);
        bool found = false;
        for (size_t slot = 0; slot < 2 && !found; ++slot) {
            const auto* source = ui.catalog.source(slot);
            if (!source || source->image->identity() != od::disc::Identity::disc2)
                continue;
            for (const auto& row : source->rows) {
                if (row.name != "CAISSE" || row.kind != "Model name" ||
                    row.path != "DATA/3DC/CAI.DAN") continue;
                select(ui, slot, row.id);
                found = true;
                break;
            }
        }
        if (!found || !ui.model_preview.has_model()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "CAI preview load failed: %s",
                         ui.preview_error.empty() ? "Disc 2 CAISSE in CAI.DAN is unavailable"
                                                  : ui.preview_error.c_str());
            ui.model_preview.shutdown();
            ui.video_preview.shutdown();
            ui.still_preview.clear();
            shell.shutdown();
            return SDL_APP_FAILURE;
        }
    }
    if (!preview_model.empty()) {
        ui.catalog.tick(10000);
        bool found=false;
        const std::string archive_name=preview_model+".DAN";
        for (size_t slot=0; slot<2 && !found; ++slot) {
            const auto* source=ui.catalog.source(slot);
            if (!source) continue;
            for (const auto& row : source->rows) {
                if (row.kind!="Model archive" || row.name!=archive_name) continue;
                select(ui,slot,row.id);
                found=true;
                break;
            }
        }
        if (!found || !ui.model_preview.has_model()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"model preview load failed: %s",
                ui.preview_error.empty() ? "requested DAN archive is unavailable"
                                         : ui.preview_error.c_str());
            ui.model_preview.shutdown();
            ui.video_preview.shutdown();
            ui.still_preview.clear();
            shell.shutdown();
            return SDL_APP_FAILURE;
        }
    }
    if (preview_movie) {
        ui.catalog.tick(10000);
        bool found = false;
        for (size_t slot = 0; slot < 2 && !found; ++slot) {
            const auto* source = ui.catalog.source(slot);
            if (!source || source->image->identity() != od::disc::Identity::disc2)
                continue;
            for (const auto& row : source->rows) {
                if (row.name != "GENERIC.HNM" || row.path != "DATA/HNM/GENERIC.HNM")
                    continue;
                select(ui, slot, row.id);
                found = true;
                break;
            }
        }
        if (!found || !ui.video_preview.has_image()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "movie preview load failed: %s",
                         ui.preview_error.empty() ? "Disc 2 GENERIC.HNM is unavailable"
                                                  : ui.preview_error.c_str());
            ui.video_preview.shutdown();
            ui.model_preview.shutdown();
            ui.still_preview.clear();
            shell.shutdown();
            return SDL_APP_FAILURE;
        }
    }
    if (!preview_still.empty()) {
        ui.catalog.tick(10000);
        struct Target { const char* mode; const char* kind; const char* path; const char* key; };
        constexpr Target targets[] = {
            {"font","Font glyph","DATA/FONT/HI640.SPR","slot:65"},
            {"icon","Sprite slot","DATA/ICONE/ICONES.BF","slot:0"},
            {"vga","VGA sprite","DATA/OBJET/ALPHABET.SPR","slot:2"},
            {"bank","Texture bank","DATA/3DC/ESSAI.3DM",""},
            {"material","Material texture","DATA/3DC/CAI.DAN","material:0"},
            {"scene","Scene texture","DATA/3DC/E29USINE.DSN","texture:0"},
        };
        const Target* target=nullptr;
        for (const auto& option : targets)
            if (preview_still==option.mode) target=&option;
        bool found=false;
        if (target) for (size_t slot=0; slot<2 && !found; ++slot) {
            const auto* source=ui.catalog.source(slot);
            if (!source || source->image->identity()!=od::disc::Identity::disc2) continue;
            for (const auto& row : source->rows) {
                if (row.kind!=target->kind || row.path!=target->path ||
                    (!std::string_view(target->key).empty() && row.key!=target->key)) continue;
                if (preview_still=="icon" && (row.parent==SIZE_MAX ||
                    source->rows[row.parent].name!="MAGIE.ALP")) continue;
                select(ui,slot,row.id);
                found=true;
                break;
            }
        }
        if (!found || !ui.still_preview.has_image()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"still preview load failed: %s",
                ui.preview_error.empty() ? "requested Disc 2 image row is unavailable"
                                         : ui.preview_error.c_str());
            ui.still_preview.clear();
            ui.video_preview.shutdown();
            ui.model_preview.shutdown();
            shell.shutdown();
            return SDL_APP_FAILURE;
        }
    }
    if (!preview_audio.empty()) {
        ui.catalog.tick(10000);
        struct Target { const char* mode; const char* kind; const char* path; const char* key; };
        constexpr Target targets[] = {
            {"effect","Sound effect","DATA/SOUND/FSB.DAT","clip:0"},
            {"effect12","Sound effect","DATA/SOUND/FSB.DAT","clip:12"},
            {"dialogue","Dialogue entry","DATA/3DC/DIALOG.DRD","entry:0"},
            {"track","Audio track","","track:2"},
        };
        const Target* target=nullptr;
        for (const auto& option : targets)
            if (preview_audio==option.mode) target=&option;
        bool found=false;
        if (target) for (size_t slot=0;slot<2 && !found;++slot) {
            const auto* source=ui.catalog.source(slot);
            if (!source || source->image->identity()!=od::disc::Identity::disc1) continue;
            for (const auto& row : source->rows) {
                if (row.kind!=target->kind || row.key!=target->key ||
                    (target->path[0] && row.path!=target->path)) continue;
                select(ui,slot,row.id);
                found=true;
                break;
            }
        }
        if (!found || !ui.audio_preview.active() ||
            (preview_audio=="dialogue" && !ui.portrait_preview.has_image())) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,"audio preview load failed: %s",
                ui.preview_error.empty() ? "requested Disc 1 audio row is unavailable"
                                         : ui.preview_error.c_str());
            ui.audio_preview.stop();
            ui.portrait_preview.clear();
            ui.still_preview.clear();
            ui.video_preview.shutdown();
            ui.model_preview.shutdown();
            shell.shutdown();
            return SDL_APP_FAILURE;
        }
    }
    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return static_cast<od::Shell*>(appstate)->event(*event);
}
SDL_AppResult SDL_AppIterate(void* appstate) {
    return static_cast<od::Shell*>(appstate)->iterate();
}
void SDL_AppQuit(void* appstate, SDL_AppResult) {
    if (appstate) {
        ui.model_preview.shutdown();
        ui.video_preview.shutdown();
        ui.still_preview.clear();
        ui.audio_preview.stop();
        ui.portrait_preview.clear();
        static_cast<od::Shell*>(appstate)->shutdown();
    }
}
