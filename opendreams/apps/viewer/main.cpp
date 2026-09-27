#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include "inspect/catalog.h"
#include "inspect/viewer_prefs.h"
#include "port/scene.h"
#include "render/model_preview.h"
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
    size_t selected_slot = SIZE_MAX, selected_row = SIZE_MAX, reveal_slot = SIZE_MAX;
    uint64_t selected_mount = 0;
    int sort_column = 0;
    bool sort_ascending = true;
    std::unique_ptr<od::port::PreviewLevelContext> preview_level;
    od::ModelPreview model_preview;
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
    load_selected_preview(state);
}

const od::inspect::Row* selection(const ViewerUi& state) {
    const auto* source = state.catalog.source(state.selected_slot);
    return source && source->image->mount_id() == state.selected_mount
        ? state.catalog.row(state.selected_slot, state.selected_row) : nullptr;
}

void clear_preview(ViewerUi& state) {
    state.model_preview.clear_model();
    state.preview_level.reset();
    state.preview_error.clear();
}

void load_selected_preview(ViewerUi& state) {
    clear_preview(state);
    const auto* row = selection(state);
    const auto* source = state.catalog.source(state.selected_slot);
    if (!row || !source) return;
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

void source_strip(ViewerUi& state) {
    for (size_t slot = 0; slot < 2; ++slot) {
        ImGui::PushID(static_cast<int>(slot));
        const auto* source = state.catalog.source(slot);
        ImGui::TextUnformatted(source ? od::inspect::identity_name(source->image->identity())
                                      : slot ? "Source B" : "Source A");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(300);
        ImGui::InputText("##cue", state.paths[slot].data(), state.paths[slot].size());
#ifndef __EMSCRIPTEN__
        ImGui::SameLine();
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
    if (!state.preferences_error.empty())
        ImGui::TextColored(ImVec4(1, 0.45f, 0.35f, 1), "Settings: %s",
                           state.preferences_error.c_str());
    if (!state.catalog.source(0) && !state.catalog.source(1)) {
#ifdef __EMSCRIPTEN__
        ImGui::TextUnformatted("Disc-image opening is currently desktop-only.");
#else
        ImGui::TextUnformatted("Open game discs: choose one or two .cue files.");
        if (ImGui::Button("Mount selected discs")) { mount(state, 0); mount(state, 1); }
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
    ImGui::InputTextWithHint("##search", "Search names, keys, paths", state.query,
                             sizeof(state.query));
    static constexpr const char* discs[] = {"Both discs", "Disc 1", "Disc 2"};
    ImGui::Combo("Disc", &state.disc_filter, discs, 3);
    static constexpr const char* statuses[] = {"All statuses", "Indexing", "Available",
        "Missing", "Ambiguous", "Invalid", "Unindexed"};
    ImGui::Combo("Status", &state.status_filter, statuses, 7);
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
    if (ImGui::BeginCombo("Kind", state.kind_filter.empty() ? "All kinds" : state.kind_filter.c_str())) {
        if (ImGui::Selectable("All kinds", state.kind_filter.empty())) state.kind_filter.clear();
        for (const auto& kind : state.kinds)
            if (ImGui::Selectable(kind.c_str(), kind == state.kind_filter)) state.kind_filter = kind;
        ImGui::EndCombo();
    }
    ImGui::Checkbox("Include extras", &state.include_extras);
    ImGui::SeparatorText("Game assets");
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
    ImGui::Text("%s: %zu sources, %zu names", global_search ? "Search results" :
                od::inspect::group_name(section),
                found.size(), logical_count);
    if (ImGui::BeginTable("assets", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Kind"); ImGui::TableSetupColumn("Source");
        ImGui::TableSetupColumn("Parent / path"); ImGui::TableSetupColumn("Status");
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
    const auto* row = selection(state);
    if (!row) { ImGui::TextDisabled("Select an asset or source file."); return; }
    const auto* source = state.catalog.source(state.selected_slot);
    ImGui::TextWrapped("%s", row->name.c_str()); ImGui::Separator();
    ImGui::Text("%s | %s", row->kind.c_str(), od::inspect::status_name(row->status));
    ImGui::Text("Source: %s", od::inspect::identity_name(source->image->identity()));
    ImGui::TextWrapped("CUE: %s", source->image->cue_path().u8string().c_str());
    ImGui::TextWrapped("Physical path: %s", row->path.c_str());
    if (!row->key.empty()) ImGui::TextWrapped("Internal key: %s", row->key.c_str());
    ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(row->size));
    if (row->has_extent)
        ImGui::Text("%s: %llu", row->physical ? "ISO logical offset" : "Offset in physical file",
                    static_cast<unsigned long long>(row->offset));
    if (!row->detail.empty()) ImGui::TextWrapped("%s", row->detail.c_str());
    ImGui::TextWrapped("Derived from: %s", row->provenance.c_str());
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

void preview_pane(ViewerUi& state) {
    ImGui::TextUnformatted("Preview");
    ImGui::Separator();
    const auto* row = selection(state);
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
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    source_strip(state); ImGui::Separator();
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::BeginChild("sections", ImVec2(width * 0.24f, 0), ImGuiChildFlags_Borders);
    left_pane(state); state.reveal_slot = SIZE_MAX; ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("workspace", ImVec2(width * 0.48f, 0), ImGuiChildFlags_None);
    const float preview_height = ImGui::GetContentRegionAvail().y * 0.55f;
    ImGui::BeginChild("preview", ImVec2(0, preview_height), ImGuiChildFlags_Borders);
    preview_pane(state); ImGui::EndChild();
    ImGui::BeginChild("results", ImVec2(0, 0), ImGuiChildFlags_Borders);
    results_pane(state); ImGui::EndChild();
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_Borders);
    details_pane(state); ImGui::EndChild(); ImGui::End();
}

bool parse_args(int argc, char** argv, int& frames,
                std::array<const char*, 2>& cues, bool& preview_cai) {
    for (int i = 1; i < argc;) {
        if (std::strcmp(argv[i], "--preview-cai") == 0) {
            preview_cai = true;
            ++i;
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
    if (!parse_args(argc, argv, frames, cues, preview_cai)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "usage: ODViewer [--frames N] [--cue1 path] [--cue2 path] [--preview-cai]");
        return SDL_APP_FAILURE;
    }
    if (!shell.init({"ODViewer", draw_ui, &ui, frames, 18.0f, 1.2f})) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", shell.error().c_str());
        return SDL_APP_FAILURE;
    }
    std::string preview_error;
    if (!ui.model_preview.init(preview_error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", preview_error.c_str());
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
        static_cast<od::Shell*>(appstate)->shutdown();
    }
}
