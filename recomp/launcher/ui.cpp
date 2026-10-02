// The launcher window: Dear ImGui on SDL3's own 2D renderer.
//
// Isolated on purpose: its own SDL window, SDL_Renderer and ImGui context, all
// destroyed before run_window returns. It shares nothing with the game's
// renderers, so it opens when the GPU renderer cannot.
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
#include "imgui.h"
#include "model.h"
#include "testing.h"

namespace {

constexpr int kBaseWidth = 760, kBaseHeight = 620;  // logical pixels, multiplied by the display scale

const ImVec4 kGreen(0.45f, 0.85f, 0.45f, 1), kYellow(0.95f, 0.80f, 0.30f, 1), kRed(1.0f, 0.45f, 0.40f, 1),
    kGrey(0.60f, 0.60f, 0.60f, 1);

// What SDL_ShowOpenFileDialog reports. The callback may run on another thread,
// or later from the event loop, and the window may be gone by then, so it only
// appends here; the frame loop reads. The queue outlives the window (the
// callback holds a share of it).
struct PickQueue {
    struct Item {
        int row;
        std::string path;  // "" when cancelled or failed
    };
    std::mutex lock;
    std::vector<Item> items;
};
struct PickContext {
    std::shared_ptr<PickQueue> queue;
    int row;
};

void SDLCALL on_picked(void* userdata, const char* const* list, int /*filter*/) {
    std::unique_ptr<PickContext> ctx((PickContext*)userdata);
    std::lock_guard<std::mutex> g(ctx->queue->lock);
    ctx->queue->items.push_back({ctx->row, list && list[0] ? list[0] : ""});
}

struct Ui {
    Model& m;
    SDL_Window* window = nullptr;
    float scale = 1;

    std::shared_ptr<PickQueue> queue = std::make_shared<PickQueue>();
    bool picking = false;           // a file dialog is open
    int capture = -1;               // keyboard table row waiting for a key press
    bool capture_visible = false;   // the keyboard tab was drawn this frame
    std::string notice;             // one line under the discs
    std::string save_error;
    float row_top[2] = {0, 0}, row_bottom[2] = {0, 0};  // window y of the disc rows, for drops
    bool first_frame = true;
    const char* want_tab = nullptr;  // test hook: testing::Var::ShotTab
    bool play = false, quit = false;
    TestScript* script = nullptr;    // test only: testing::Var::Script; null in every real run
    std::string last_dialog_start;   // test only: the folder the last Browse would have opened
    int last_dialog_row = -1;
    std::string tab_shown;

    explicit Ui(Model& model) : m(model) {}

    // Test only: tell the script where the widget just submitted is.
    void track(const std::string& id) {
        if (!testing::kEnabled || !script) return;
        ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        script->widget(id, a.x, a.y, b.x, b.y, ImGui::IsItemFocused(), ImGui::IsItemVisible());
    }

    void discs_changed(const std::string& note) {
        notice = note;
        // A path the user chose is theirs to keep: it is saved at once, so a
        // later start finds it even if Play is never pressed. A command-line
        // value is left out by save().
        std::string err;
        if (!m.save(&err)) save_error = err;
        else save_error.clear();
    }

    void set_disc(int row, const std::string& path) {
        std::string note;
        assign(m.discs, row, path, &note);
        discs_changed(note);
    }

    void browse(int row) {
        static const SDL_DialogFileFilter filters[] = {
            {"Disc images (*.cue, *.iso)", "cue;iso"},
            {"All files", "*"},
        };
        std::string start;  // the folder of the current choice, so the other disc is found beside it
        const std::string& cur = m.discs.path[row].empty() ? m.discs.path[1 - row] : m.discs.path[row];
        size_t cut = cur.find_last_of("/\\");
        if (cut != std::string::npos) start = cur.substr(0, cut + 1);
        picking = true;
        if (testing::kEnabled && script) {  // test only: the OS dialog cannot be driven; the script calls the callback itself
            last_dialog_row = row;
            last_dialog_start = start;
            return;
        }
        auto* ctx = new PickContext{queue, row};
        SDL_ShowOpenFileDialog(on_picked, ctx, window, filters, 2, start.empty() ? nullptr : start.c_str(), false);
    }

    void drain_picks() {
        std::vector<PickQueue::Item> got;
        {
            std::lock_guard<std::mutex> g(queue->lock);
            got.swap(queue->items);
        }
        for (auto& it : got) {
            picking = false;
            if (!it.path.empty()) set_disc(it.row, it.path);
        }
    }

    void on_event(const SDL_Event& e) {
        if (testing::kEnabled && script && TestScript::is_real_input(e)) return;  // a scripted run takes no input but its own
        switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit = true;
            return;
        case SDL_EVENT_DROP_FILE:
            if (e.drop.data) {
                // The row the file lands on; an image that is the other disc moves itself.
                int row = e.drop.y >= row_top[1] && row_top[1] > row_top[0] ? 1 : 0;
                set_disc(row, e.drop.data);
            }
            return;
        case SDL_EVENT_KEY_DOWN:
            if (capture >= 0 && !e.key.repeat) {
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                    capture = -1;
                } else {
                    std::string name = key_name_from_scancode((int)e.key.scancode);
                    if (!name.empty()) {
                        m.port.bind_key(game_keys()[(size_t)capture].name, name);
                        capture = -1;
                    }
                }
                return;  // not for ImGui: a captured key must not also navigate
            }
            break;
        default:
            break;
        }
        // X11 reports Shift+Tab as SDLK_LEFT_TAB (XK_ISO_Left_Tab); ImGui's SDL3 backend knows only
        // SDLK_TAB, so without this the keyboard could not navigate backwards on Linux.
        SDL_Event forwarded = e;
        if ((e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) && e.key.key == SDLK_LEFT_TAB)
            forwarded.key.key = SDLK_TAB;
        ImGui_ImplSDL3_ProcessEvent(&forwarded);
    }

    // ---- widgets ----

    static std::string ellipsize(const std::string& s, float width) {
        if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
        auto boundary = [&](size_t i) {  // do not cut inside a UTF-8 sequence
            while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) i++;
            return i;
        };
        for (size_t keep = s.size(); keep > 4; keep -= std::max<size_t>(1, keep / 16)) {
            size_t head = boundary(keep / 2);
            size_t tail = boundary(s.size() - (keep - keep / 2));
            std::string t = s.substr(0, head) + "..." + s.substr(tail);
            if (ImGui::CalcTextSize(t.c_str()).x <= width) return t;
        }
        return "...";
    }

    static void label(const char* text, float width) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::SameLine(width);
    }

    // track_id and item_ids name the combo and its entries for the test script.
    bool combo(const char* id, const char* track_id, int* index, const char* const* items, const char* const* item_ids, int count) {
        bool changed = false;
        bool open = ImGui::BeginCombo(id, items[*index]);
        track(track_id);
        if (open) {
            for (int i = 0; i < count; i++) {
                if (ImGui::Selectable(items[i], i == *index)) { *index = i; changed = true; }
                if (testing::kEnabled && script) track(std::string(track_id) + "." + item_ids[i]);
                if (i == *index) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // ---- areas ----

    void draw_disc_row(int i) {
        const DiscCheck& c = m.discs.check[i];
        float em = ImGui::GetFontSize();
        row_top[i] = ImGui::GetCursorScreenPos().y - ImGui::GetMainViewport()->Pos.y;
        ImGui::PushID(i);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Disc %d", i + 1);
        ImGui::SameLine(em * 4.5f);
        float browse_w = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2;
        float path_w = ImGui::GetContentRegionAvail().x - browse_w - ImGui::GetStyle().ItemSpacing.x;
        ImGui::AlignTextToFramePadding();
        const std::string& p = m.discs.path[i];
        if (p.empty()) {
            ImGui::TextColored(kGrey, "(drop an image here or use Browse)");
        } else {
            ImGui::TextUnformatted(ellipsize(p, path_w).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.c_str());
        }
        ImGui::SameLine(em * 4.5f + path_w + ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginDisabled(picking);
        if (ImGui::Button("Browse...")) browse(i);
        if (testing::kEnabled && script) track("browse" + std::to_string(i + 1));
        ImGui::EndDisabled();

        const ImVec4& col = c.ok() ? (c.status == DiscStatus::FoundNoMusic ? kYellow : kGreen)
                                   : (c.status == DiscStatus::NotSet ? kGrey : kRed);
        // Wrapped, not cut off at the window's edge: the reason is the point of the line.
        ImGui::Indent(em * 4.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextWrapped("%s", c.message.c_str());
        ImGui::PopStyleColor();
        ImGui::Unindent(em * 4.5f);
        ImGui::PopID();
        row_bottom[i] = ImGui::GetCursorScreenPos().y - ImGui::GetMainViewport()->Pos.y;
        if (testing::kEnabled && script)
            script->widget("disc" + std::to_string(i + 1) + ".row", 0, row_top[i], ImGui::GetIO().DisplaySize.x, row_bottom[i], false, true);
    }

    void draw_display_tab() {
        float lw = ImGui::GetFontSize() * 11, w = ImGui::GetFontSize() * 18;
        PortSettings& s = m.port;

        label("Renderer", lw);
        int r = s.gpu ? 1 : 0;
        static const char* const renderers[] = {"Original (software)", "New (GPU)"};
        static const char* const renderer_ids[] = {"software", "gpu"};
        ImGui::SetNextItemWidth(w);
        if (combo("##renderer", "renderer", &r, renderers, renderer_ids, 2)) s.gpu = r == 1;

        label("Fullscreen window", lw);
        ImGui::Checkbox("##fullscreen", &s.fullscreen);
        track("fullscreen");

        // AlwaysClamp: a value typed into a slider (Ctrl+click) is held to the slider's range too.
        label("Window scale", lw);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("##scale", &s.scale, 1, 6, "%dx of 640x480", ImGuiSliderFlags_AlwaysClamp);
        track("scale");

        label("Scaling filter", lw);
        static const char* const filter_names[] = {"Pixel art (sharp, even pixels)", "Nearest", "Linear"};
        static const char* const filter_ids[] = {"pixelart", "nearest", "linear"};
        int f = 0;
        for (int i = 0; i < 3; i++)
            if (s.filter == filter_ids[i]) f = i;
        ImGui::SetNextItemWidth(w);
        if (combo("##filter", "filter", &f, filter_names, filter_ids, 3)) s.filter = filter_ids[f];

        label("Frame cap", lw);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("##fps", &s.fps, 0, 60, s.fps == 0 ? "uncapped" : "%d frames per second", ImGuiSliderFlags_AlwaysClamp);
        track("fps");
        if (s.fps > 30) {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, "above 30 the original physics breaks");
        }

        label("Output mute", lw);
        ImGui::Checkbox("##mute", &s.mute);
        track("mute");
    }

    void draw_keyboard_tab() {
        PortSettings& s = m.port;
        capture_visible = true;
        ImGui::TextWrapped("Key to key: choose which physical key stands for each key the game reads. "
                           "The game itself still reads the keys on the left.");
        if (ImGui::Button("WASD preset")) s.wasd_preset();
        track("key.wasd");
        ImGui::SameLine();
        if (ImGui::Button("Reset to defaults")) { s.reset_keymap(); capture = -1; }
        track("key.reset");
        std::string clash = keyboard_conflict(s);
        if (!clash.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kRed, "%s", clash.c_str());
        }

        std::vector<KeyRowState> rows = keyboard_rows(s);
        const auto& keys = game_keys();
        float em = ImGui::GetFontSize();
        if (ImGui::BeginTable("keys", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
                              ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Game key", ImGuiTableColumnFlags_WidthFixed, em * 6);
            ImGui::TableSetupColumn("Retail meaning", ImGuiTableColumnFlags_WidthFixed, em * 12);
            ImGui::TableSetupColumn("Physical key", ImGuiTableColumnFlags_WidthFixed, em * 8);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (size_t i = 0; i < keys.size(); i++) {
                const KeyRowState& r = rows[i];
                ImGui::PushID((int)i);
                ImGui::TableNextRow();
                if (r.conflict) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 30, 30, 170));
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(keys[i].name);
                ImGui::TableNextColumn();
                ImGui::TextColored(kGrey, "%s", keys[i].label);
                ImGui::TableNextColumn();
                if (r.remapped) ImGui::TextColored(r.conflict ? kRed : kGreen, "%s", r.physical.c_str());
                else if (r.conflict) ImGui::TextColored(kRed, "%s (default)", r.physical.c_str());
                else ImGui::TextColored(kGrey, "%s (default)", r.physical.c_str());
                ImGui::TableNextColumn();
                bool waiting = capture == (int)i;
                if (ImGui::Button(waiting ? "Press a key (Esc cancels)" : "Press a key...")) capture = waiting ? -1 : (int)i;
                if (testing::kEnabled && script) track(std::string("key.") + keys[i].name + ".capture");
                if (r.remapped) {
                    ImGui::SameLine();
                    if (ImGui::Button("Reset")) s.bind_key(keys[i].name, "");
                    if (testing::kEnabled && script) track(std::string("key.") + keys[i].name + ".reset");
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void draw_gamepad_tab() {
        PortSettings& s = m.port;
        float lw = ImGui::GetFontSize() * 11, w = ImGui::GetFontSize() * 18;

        label("Mode", lw);
        static const char* const modes[] = {"Game joystick", "Keys", "Off"};
        static const char* const mode_ids[] = {"game", "keys", "off"};
        int mode = (int)s.pad;
        ImGui::SetNextItemWidth(w);
        if (combo("##mode", "pad.mode", &mode, modes, mode_ids, 3)) s.pad = (PadMode)mode;
        if (s.pad == PadMode::Game)
            ImGui::TextColored(kGrey, "The pad is the joystick the game reads (press J in game).");
        else if (s.pad == PadMode::Keys)
            ImGui::TextColored(kGrey, "The pad presses keys.");
        else
            ImGui::TextColored(kGrey, "Gamepads are ignored.");
        if (s.pad == PadMode::Off) return;

        label("Direction", lw);
        static const char* const dirs[] = {"Left stick", "D-pad", "Both"};
        static const char* const dir_ids[] = {"stick", "dpad", "both"};
        int d = (int)s.effective_dir();
        ImGui::SetNextItemWidth(w);
        if (combo("##dir", "pad.dir", &d, dirs, dir_ids, 3)) { s.dir = (PadDir)d; s.dir_set = true; }

        label("Deadzone inner", lw);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("##dzin", &s.dz_inner, 0, s.dz_outer - 1, "%d%%", ImGuiSliderFlags_AlwaysClamp);
        track("pad.dzin");
        label("Deadzone outer", lw);
        ImGui::SetNextItemWidth(w);
        ImGui::SliderInt("##dzout", &s.dz_outer, s.dz_inner + 1, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
        track("pad.dzout");

        ImGui::Spacing();
        ImGui::TextUnformatted(s.pad == PadMode::Game ? "Pad button = joystick button the game sees" : "Pad button = key it presses");
        ImGui::SameLine();
        if (ImGui::Button("Reset buttons")) s.reset_pad_buttons();
        track("pad.reset");

        int rows = s.pad == PadMode::Game ? kPadJoyButtons : kPadButtons;
        if (ImGui::BeginTable("pad", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH,
                              ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupColumn("##b", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 11);
            ImGui::TableSetupColumn("##t", ImGuiTableColumnFlags_WidthStretch);
            for (int b = 0; b < rows; b++) {
                ImGui::PushID(b);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(kPadButtonLabels[b]);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
                if (s.pad == PadMode::Game) {
                    std::string cur = "button" + std::to_string(s.joy[b]);
                    bool open = ImGui::BeginCombo("##t", cur.c_str());
                    std::string tid = std::string("pad.btn.") + kPadButtonNames[b];
                    if (testing::kEnabled && script) track(tid);
                    if (open) {
                        for (int n = 1; n <= 32; n++) {
                            std::string item = "button" + std::to_string(n);
                            if (ImGui::Selectable(item.c_str(), n == s.joy[b])) s.joy[b] = n;
                            if (testing::kEnabled && script) track(tid + "." + item);
                            if (n == s.joy[b]) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    bool optional = default_pad_key(b)[0] == 0;  // the host cannot say "no key" for a button that has one
                    const char* cur = s.key[b].empty() ? "(none)" : s.key[b].c_str();
                    bool open = ImGui::BeginCombo("##t", cur);
                    std::string tid = std::string("pad.btn.") + kPadButtonNames[b];
                    if (testing::kEnabled && script) track(tid);
                    if (open) {
                        if (optional && ImGui::Selectable("(none)", s.key[b].empty())) s.key[b].clear();
                        for (const auto& name : key_names()) {
                            if (ImGui::Selectable(name.c_str(), name == s.key[b])) s.key[b] = name;
                            if (testing::kEnabled && script) track(tid + "." + name);
                            if (name == s.key[b]) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    static std::string file_url(std::string path) {
        std::string out = "file://";
        std::replace(path.begin(), path.end(), '\\', '/');
        if (path.empty() || path[0] != '/') out += '/';  // C:/... needs three slashes
        for (unsigned char c : path) {
            if (std::isalnum(c) || std::string("/:-_.~").find((char)c) != std::string::npos) {
                out += (char)c;
            } else {
                char hex[4];
                SDL_snprintf(hex, sizeof hex, "%%%02X", c);
                out += hex;
            }
        }
        return out;
    }

    void draw() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        capture_visible = false;

        ImGui::SeparatorText("Discs");
        for (int i = 0; i < 2; i++) draw_disc_row(i);
        if (!notice.empty()) ImGui::TextColored(kYellow, "%s", notice.c_str());
        if (!save_error.empty()) ImGui::TextColored(kRed, "Could not save settings: %s", save_error.c_str());

        ImGui::SeparatorText("Port settings");
        ImGui::TextColored(kGrey, "The game's own options (shadows, fight mode, volume, cinemascope) are in its in-game menu.");

        float line = ImGui::GetFrameHeightWithSpacing();
        float lines = m.loc.fallback ? 5 : 4;  // data, settings (and the fallback note), buttons
        float bottom = line * lines + ImGui::GetStyle().ItemSpacing.y * 3 + ImGui::GetStyle().WindowPadding.y;
        if (ImGui::BeginTabBar("##tabs")) {
            struct Tab { const char* title; const char* id; void (Ui::*draw)(); } tabs[] = {
                {"Display and sound", "display", &Ui::draw_display_tab},
                {"Keyboard", "keyboard", &Ui::draw_keyboard_tab},
                {"Gamepad", "gamepad", &Ui::draw_gamepad_tab},
            };
            for (auto& t : tabs) {
                ImGuiTabItemFlags flags = want_tab && SDL_strcmp(want_tab, t.id) == 0 && first_frame ? ImGuiTabItemFlags_SetSelected : 0;
                bool selected = ImGui::BeginTabItem(t.title, nullptr, flags);
                if (testing::kEnabled && script) track(std::string("tab.") + t.id);
                if (selected) {
                    tab_shown = t.id;
                    ImGui::BeginChild("##tab", ImVec2(0, ImGui::GetContentRegionAvail().y - bottom),
                                      ImGuiChildFlags_NavFlattened);  // Tab and the arrows cross into the tab's controls
                    (this->*t.draw)();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        if (!capture_visible) capture = -1;  // a key press must not land in a tab that is not showing

        ImGui::SeparatorText("Data");
        std::string data = m.data_dir();
        float open_w = ImGui::CalcTextSize("Open folder").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ellipsize(data, ImGui::GetContentRegionAvail().x - open_w - ImGui::GetStyle().ItemSpacing.x * 2).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", data.c_str());
        ImGui::SameLine(ImGui::GetContentRegionMax().x - open_w);
        if (ImGui::Button("Open folder")) {
            SDL_CreateDirectory(data.c_str());
            SDL_OpenURL(file_url(data).c_str());
        }
        track("open_folder");
        std::string ini_line = "Settings: " + m.loc.ini_path();
        ImGui::TextColored(kGrey, "%s", ellipsize(ini_line, ImGui::GetContentRegionAvail().x).c_str());
        if (m.loc.fallback)
            ImGui::TextColored(kYellow, "The program's folder is not writable: settings and data are in the per-user folder.");

        std::string blocker = m.play_blocker();
        ImGui::BeginDisabled(!blocker.empty());
        if (ImGui::Button("Play", ImVec2(ImGui::GetFontSize() * 7, 0))) play = true;
        track("play");
        if (first_frame && blocker.empty()) ImGui::SetItemDefaultFocus();  // not on a button that does nothing
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(ImGui::GetFontSize() * 7, 0))) quit = true;
        track("quit");
        if (!blocker.empty()) {
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(kRed, "%s", ellipsize(blocker, ImGui::GetContentRegionAvail().x).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", blocker.c_str());
        }

        ImGui::End();
        if (testing::kEnabled && script) publish_facts(blocker);
    }

    // ---- test only: what the script may ask about ----

    static const char* status_name(DiscStatus s) {
        switch (s) {
        case DiscStatus::NotSet: return "NotSet";
        case DiscStatus::NotFound: return "NotFound";
        case DiscStatus::NotDisc: return "NotDisc";
        case DiscStatus::WrongDisc: return "WrongDisc";
        case DiscStatus::WrongEdition: return "WrongEdition";
        case DiscStatus::Found: return "Found";
        case DiscStatus::FoundNoMusic: return "FoundNoMusic";
        }
        return "?";
    }

    void publish_facts(const std::string& blocker) {
        TestScript& t = *script;
        for (int i = 0; i < 2; i++) {
            std::string n = "disc" + std::to_string(i + 1);
            t.fact(n, m.discs.check[i].message);
            t.fact(n + ".status", status_name(m.discs.check[i].status));
            t.fact(n + ".path", m.discs.path[i]);
        }
        t.fact("notice", notice);
        t.fact("save_error", save_error);
        t.fact("play", blocker.empty() ? "enabled" : "disabled");
        t.fact("blocker", blocker);
        std::string clash = keyboard_conflict(m.port);
        t.fact("conflict", clash.empty() ? "no" : "yes");
        t.fact("conflict.text", clash);

        // The WD_* pairs the port settings would emit now ("" for a name left out), as var.NAME.
        static const char* const names[] = {"WD_RENDERER", "WD_FULLSCREEN", "WD_SCALE", "WD_FILTER", "WD_FPS", "WD_MUTE",
                                            "WD_PAD", "WD_DEADZONE", "WD_PAD_DIRECTION", "WD_KEYMAP", "WD_PADMAP"};
        for (const char* n : names) t.fact(std::string("var.") + n, "");
        VarList vars;
        emit_port_vars(m.port, vars);
        std::string keymap;
        for (const auto& kv : vars) {
            t.fact("var." + kv.first, kv.second);
            if (kv.first == "WD_KEYMAP") keymap = kv.second;
        }
        t.fact("keymap", keymap);

        // The keyboard table: the physical key of each game key, and which rows are flagged.
        std::vector<KeyRowState> rows = keyboard_rows(m.port);
        std::string flagged;
        for (size_t i = 0; i < rows.size(); i++) {
            t.fact(std::string("key.") + game_keys()[i].name, rows[i].physical);
            if (rows[i].conflict) flagged += (flagged.empty() ? "" : ",") + std::string(game_keys()[i].name);
        }
        t.fact("conflict.rows", flagged);
        t.fact("capture", capture < 0 ? "none" : game_keys()[(size_t)capture].name);

        t.fact("picking", picking ? "yes" : "no");
        t.fact("dialog.row", last_dialog_row < 0 ? "none" : std::to_string(last_dialog_row + 1));
        t.fact("dialog.start", last_dialog_start);
        t.fact("tab", tab_shown);
        ImGuiIO& io = ImGui::GetIO();
        t.fact("nav.keyboard", (io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) ? "yes" : "no");
        t.fact("nav.gamepad", (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) ? "yes" : "no");
        t.fact("gamepad.seen", (io.BackendFlags & ImGuiBackendFlags_HasGamepad) ? "yes" : "no");
    }
};

SDL_Renderer* make_renderer(SDL_Window* w) {
    SDL_Renderer* r = SDL_CreateRenderer(w, nullptr);
    if (!r) r = SDL_CreateRenderer(w, SDL_SOFTWARE_RENDERER);  // the launcher must open on a machine with no working GPU path
    return r;
}

}  // namespace

namespace {

// Saves what has been drawn to the renderer (call after the frame's draw data, before present).
void save_bmp(SDL_Renderer* renderer, const char* path) {
    if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
        SDL_SaveBMP(s, path);
        SDL_DestroySurface(s);
    }
}

}  // namespace

bool run_window(Model& m, bool* quit, std::string* err) {
    *quit = false;
    // Test only (testing.h): a script drives the window (testscript.h). Read first, so a bad
    // script is refused before any window opens.
    std::unique_ptr<TestScript> script;
    if (const char* path = testing::env(testing::Var::Script)) {
        script = TestScript::load(path, err);
        if (!script) {
            std::fprintf(stderr, "%s\n", err->c_str());
            return false;
        }
    }
    SDL_Window* window = SDL_CreateWindow("Dreams to Reality", kBaseWidth, kBaseHeight,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!window) {
        *err = std::string("cannot open the launcher window: ") + SDL_GetError();
        return false;
    }
    SDL_Renderer* renderer = make_renderer(window);
    if (!renderer) {
        *err = std::string("cannot create the launcher renderer: ") + SDL_GetError();
        SDL_DestroyWindow(window);
        return false;
    }
    float scale = SDL_GetWindowDisplayScale(window);
    if (!(scale > 0)) scale = 1;
    SDL_SetWindowSize(window, (int)(kBaseWidth * scale), (int)(kBaseHeight * scale));
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SetRenderVSync(renderer, script ? 0 : 1);  // a script has no reason to wait for the display
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // the launcher writes dreams.ini and nothing else
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0;
    style.ScaleAllSizes(scale);
    io.Fonts->AddFontDefaultVector();  // built in: nothing from the game is available before a disc is known
    style.FontSizeBase = 15.0f;
    style.FontScaleDpi = scale;
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    Ui ui(m);
    ui.window = window;
    ui.scale = scale;
    ui.want_tab = testing::env(testing::Var::ShotTab);  // test only
    const char* shot = testing::env(testing::Var::Shot);  // test only: save the window to a BMP and quit
    int frames = 0;
    if (testing::kEnabled && script) {
        ui.script = script.get();
        // The callback SDL_ShowOpenFileDialog would call; it holds a share of the queue and nothing else.
        // A scripted run reads no real gamepad either: ImGui gets the script's virtual one, when it has one.
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
        script->attach(
            window, (SDL_WasInit(SDL_INIT_GAMEPAD) & SDL_INIT_GAMEPAD) != 0,
            [q = ui.queue](int row, const char* const* list) { on_picked(new PickContext{q, row}, list, 0); },
            [](SDL_Gamepad* pad) {
                ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, pad ? &pad : nullptr, pad ? 1 : 0);
            });
    }

    while (!ui.quit && !ui.play) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) ui.on_event(e);
        ui.drain_picks();

        Uint64 t0 = SDL_GetTicks();
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (testing::kEnabled && script) {
            // The SDL backend follows the real pointer when it is not over the window; the script's wins.
            float px, py;
            if (script->pointer(&px, &py)) io.AddMousePosEvent(px, py);
            script->begin_frame();
        }
        ImGui::NewFrame();
        ui.draw();
        ui.first_frame = false;
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 20, 20, 24, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        if (shot && *shot && ++frames == 6) {  // the font atlas and the tab selection need a few frames
            save_bmp(renderer, shot);
            ui.quit = true;
        }
        if (testing::kEnabled && script) {
            script->end_frame();  // runs the next step; its events reach the next frame
            std::string path;
            if (script->take_shot(&path)) save_bmp(renderer, path.c_str());
            if (script->failed()) ui.quit = true;
        }
        SDL_RenderPresent(renderer);
        // Vsync paces the loop; this covers a minimised window or a driver that ignores it.
        if (SDL_GetTicks() - t0 < 4) SDL_Delay((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) ? 50 : script ? 2 : 8);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    if (testing::kEnabled && script) {
        script->window_closed();  // steps left over are an error
        ui.script = nullptr;
        if (script->failed()) {
            *err = script->error();
            *quit = false;
            return false;
        }
    }
    *quit = ui.quit && !ui.play;
    return ui.play;
}
