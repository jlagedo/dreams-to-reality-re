// TEST ONLY: see testscript.h and testing.h. Compiled only with the CMake option
// DREAMS_LAUNCHER_TESTING; a release build has none of it. The script is not
// reachable unless DREAMS_LAUNCHER_SCRIPT is set.
#include "testscript.h"

#include "testing.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "settings.h"

namespace testing {

// release.py looks for this text in the executable and refuses it. It has to be
// reachable from env() or the linker would drop it from a static library's object.
extern const char kBuildMarker[] = "launcher-testing";

const char* env(Var v) {
    static const char* const names[] = {
        "DREAMS_LAUNCHER_HOME", "DREAMS_LAUNCHER_VERBOSE", "DREAMS_LAUNCHER_SHOT",
        "DREAMS_LAUNCHER_SHOT_TAB", "DREAMS_LAUNCHER_SCRIPT",
    };
    static volatile const char* sink;
    sink = kBuildMarker;
    (void)sink;
    const char* value = SDL_getenv(names[(int)v]);
    return value && *value ? value : nullptr;
}

}  // namespace testing

namespace {

constexpr Uint32 kMarker = 0x7E57D0C5u;  // in every pushed event's "reserved" field; real events carry 0

struct StepSpec {
    const char* name;
    int min_args, max_args;  // arguments after the name
};
const StepSpec kSteps[] = {
    {"wait", 1, 1},   {"settle", 1, 1},       {"key", 1, 32},     {"keydown", 1, 1},     {"keyup", 1, 1},
    {"text", 1, 1},   {"move", 1, 2},         {"click", 1, 3},    {"tab", 1, 1},         {"drop", 1, 3},
    {"dialog", 2, 2}, {"dialog-thread", 2, 3}, {"dialog-error", 1, 1}, {"shot", 1, 1},   {"expect", 2, 3},
    {"print", 1, 1},  {"close", 0, 0},  {"end", 0, 0},          {"sdlquit", 0, 0},  {"pad", 1, 2},
};

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// One line into words: whitespace separates, double quotes group (and may be empty), nothing is escaped
// (a path keeps its backslashes).
bool tokenize(const std::string& line, std::vector<std::string>* out, std::string* err) {
    size_t i = 0, n = line.size();
    while (i < n) {
        while (i < n && std::isspace((unsigned char)line[i])) i++;
        if (i >= n) break;
        std::string tok;
        if (line[i] == '"') {
            size_t end = line.find('"', i + 1);
            if (end == std::string::npos) { *err = "unterminated quote"; return false; }
            tok = line.substr(i + 1, end - i - 1);
            i = end + 1;
        } else {
            while (i < n && !std::isspace((unsigned char)line[i])) tok += line[i++];
        }
        out->push_back(tok);
    }
    return true;
}

SDL_Keymod mod_of(int sc) {
    switch (sc) {
    case SDL_SCANCODE_LCTRL: return SDL_KMOD_LCTRL;
    case SDL_SCANCODE_RCTRL: return SDL_KMOD_RCTRL;
    case SDL_SCANCODE_LSHIFT: return SDL_KMOD_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return SDL_KMOD_RSHIFT;
    case SDL_SCANCODE_LALT: return SDL_KMOD_LALT;
    case SDL_SCANCODE_RALT: return SDL_KMOD_RALT;
    default: return SDL_KMOD_NONE;
    }
}

// A key name of the launcher's own table (UP, W, F5, KP3, LCTRL, ...), the generic
// CTRL / SHIFT / ALT as the left key, ENTER and ESCAPE; else whatever SDL calls it.
int scancode_from_name(const std::string& name) {
    std::string u = upper(name);
    if (u == "CTRL") u = "LCTRL";
    else if (u == "SHIFT") u = "LSHIFT";
    else if (u == "ALT") u = "LALT";
    else if (u == "ENTER") u = "RETURN";
    else if (u == "ESCAPE") u = "ESC";
    for (int sc = 1; sc < SDL_SCANCODE_COUNT; sc++)
        if (key_name_from_scancode(sc) == u) return sc;
    SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
    return sc == SDL_SCANCODE_UNKNOWN ? 0 : (int)sc;
}

int gamepad_button_from_name(const std::string& name) {
    static const struct { const char* n; SDL_GamepadButton b; } table[] = {
        {"a", SDL_GAMEPAD_BUTTON_SOUTH},         {"b", SDL_GAMEPAD_BUTTON_EAST},
        {"x", SDL_GAMEPAD_BUTTON_WEST},          {"y", SDL_GAMEPAD_BUTTON_NORTH},
        {"back", SDL_GAMEPAD_BUTTON_BACK},       {"start", SDL_GAMEPAD_BUTTON_START},
        {"ls", SDL_GAMEPAD_BUTTON_LEFT_STICK},   {"rs", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
        {"lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {"up", SDL_GAMEPAD_BUTTON_DPAD_UP},      {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},  {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    };
    std::string l = name;
    for (char& c : l) c = (char)std::tolower((unsigned char)c);
    for (const auto& t : table)
        if (l == t.n) return (int)t.b;
    return -1;
}

struct PickJob {
    TestScript::PickFn fn;
    int row;
    std::string path;
    int delay_ms;
};

}  // namespace

// ---- loading ----

std::unique_ptr<TestScript> TestScript::load(const std::string& path, std::string* error) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        *error = "script: cannot read " + path + ": " + SDL_GetError();
        return nullptr;
    }
    std::string text((const char*)data, size);
    SDL_free(data);

    std::unique_ptr<TestScript> s(new TestScript());
    int line_no = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        line_no++;
        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        Step step;
        step.line = line_no;
        std::string why;
        if (!tokenize(line, &step.args, &why)) {
            *error = "script:" + std::to_string(line_no) + ": " + why;
            return nullptr;
        }
        const StepSpec* spec = nullptr;
        for (const auto& k : kSteps)
            if (step.args[0] == k.name) spec = &k;
        if (!spec) {
            *error = "script:" + std::to_string(line_no) + ": unknown step \"" + step.args[0] + "\"";
            return nullptr;
        }
        int given = (int)step.args.size() - 1;
        if (given < spec->min_args || given > spec->max_args) {
            *error = "script:" + std::to_string(line_no) + ": \"" + step.args[0] + "\" takes " +
                     std::to_string(spec->min_args) + (spec->max_args != spec->min_args ? ".." + std::to_string(spec->max_args) : "") +
                     " argument(s), got " + std::to_string(given);
            return nullptr;
        }
        s->steps_.push_back(std::move(step));
    }
    // A virtual gamepad only works when SDL accepts joystick input for a window that is not focused.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    return s;
}

TestScript::~TestScript() {
    for (SDL_Thread* t : threads_) SDL_WaitThread(t, nullptr);
    if (gamepad_) SDL_CloseGamepad(gamepad_);
    if (joystick_) SDL_CloseJoystick(joystick_);
    if (joystick_id_) SDL_DetachVirtualJoystick(joystick_id_);
}

void TestScript::attach(SDL_Window* window, bool gamepad_subsystem, PickFn pick, PadFn padfn) {
    window_ = window;
    gamepad_subsystem_ = gamepad_subsystem;
    pick_ = std::move(pick);
    padfn_ = std::move(padfn);
}

// ---- what the window asks ----

bool TestScript::is_synthetic(const SDL_Event& e) { return e.common.reserved == kMarker; }

bool TestScript::is_real_input(const SDL_Event& e) {
    if (is_synthetic(e)) return false;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_COMPLETE:
        return true;
    default:
        return false;
    }
}

bool TestScript::pointer(float* x, float* y) const {
    if (!have_pointer_) return false;
    *x = mx_;
    *y = my_;
    return true;
}

void TestScript::begin_frame() {
    widgets_.clear();
    facts_["focus"] = "none";  // widget() sets it again for the widget that has the keyboard focus
}

void TestScript::widget(const std::string& id, float x0, float y0, float x1, float y1, bool focused, bool visible) {
    widgets_[id] = Widget{x0, y0, x1, y1, focused, visible};
    if (focused) facts_["focus"] = id;
}

void TestScript::fact(const std::string& name, const std::string& value) { facts_[name] = value; }

bool TestScript::take_shot(std::string* path) {
    if (shot_.empty()) return false;
    *path = shot_;
    shot_.clear();
    return true;
}

// ---- running ----

bool TestScript::fail(const std::string& msg) {
    if (error_.empty()) {
        error_ = "script:" + std::to_string(line_) + ": " + msg;
        std::fprintf(stderr, "%s\n", error_.c_str());
    }
    return false;
}

void TestScript::window_closed() {
    // Only steps never started count: what a last step still had queued (a button's release) is moot.
    if (failed() || pc_ >= steps_.size()) return;
    line_ = steps_[pc_].line;
    fail("the window closed before this step ran");
}

void TestScript::end_frame() {
    if (failed()) return;
    if (wait_ > 0) { wait_--; return; }
    if (!micro_.empty()) {
        Micro m = std::move(micro_.front());
        micro_.pop_front();
        m.run();
        wait_ = m.frames;
        return;
    }
    if (pc_ >= steps_.size()) {
        if (++idle_ > 40) {
            line_ = steps_.empty() ? 0 : steps_.back().line;
            fail("script ended, window still open");
        }
        return;
    }
    const Step& s = steps_[pc_++];
    line_ = s.line;
    run(s);
    if (wait_ == 0) wait_ = micro_.empty() ? settle_ : 1;
}

int TestScript::number(const std::string& s, const char* what, int lo, int hi, bool* ok) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end || v < lo || v > hi) {
        fail(std::string(what) + " must be a number from " + std::to_string(lo) + " to " + std::to_string(hi) + ", not \"" + s + "\"");
        *ok = false;
        return lo;
    }
    return (int)v;
}

const char* TestScript::store(const std::string& s) {
    keep_.push_back(s);
    return keep_.back().c_str();
}

void TestScript::push(SDL_Event& e) {
    e.common.reserved = kMarker;
    e.common.timestamp = 0;  // SDL fills it in
    if (!SDL_PushEvent(&e)) fail(std::string("SDL_PushEvent failed: ") + SDL_GetError());
}

void TestScript::key_event(int sc, bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    SDL_Keymod m = mod_of(sc);
    if (down) mods_ = (SDL_Keymod)(mods_ | m);
    else mods_ = (SDL_Keymod)(mods_ & ~m);
    e.key.windowID = SDL_GetWindowID(window_);
    e.key.scancode = (SDL_Scancode)sc;
    e.key.key = SDL_GetKeyFromScancode((SDL_Scancode)sc, mods_, false);
    e.key.mod = mods_;
    e.key.down = down;
    e.key.repeat = false;
    push(e);
}

void TestScript::mouse_move(float x, float y) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = SDL_GetWindowID(window_);
    e.motion.x = x;
    e.motion.y = y;
    e.motion.xrel = have_pointer_ ? x - mx_ : 0;
    e.motion.yrel = have_pointer_ ? y - my_ : 0;
    mx_ = x;
    my_ = y;
    have_pointer_ = true;
    push(e);
}

void TestScript::mouse_button(bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = SDL_GetWindowID(window_);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.down = down;
    e.button.clicks = 1;
    e.button.x = mx_;
    e.button.y = my_;
    push(e);
}

bool TestScript::find_widget(const std::string& id, Widget* out) {
    auto it = widgets_.find(id);
    if (it == widgets_.end()) return fail("no widget \"" + id + "\" on screen");
    if (!it->second.visible) return fail("widget \"" + id + "\" is scrolled out of view or clipped");
    *out = it->second;
    return true;
}

bool TestScript::click(const Step& s) {
    Widget w;
    if (!find_widget(s.args[1], &w)) return false;
    float fx = 0.5f, fy = 0.5f;
    if (s.args.size() > 2) fx = (float)std::atof(s.args[2].c_str());
    if (s.args.size() > 3) fy = (float)std::atof(s.args[3].c_str());
    float cx = w.x0 + fx * (w.x1 - w.x0), cy = w.y0 + fy * (w.y1 - w.y0);
    // Park the pointer elsewhere first, then arrive: ImGui only lets a widget be hovered
    // after the pointer moved (keyboard navigation turns hover off), and a button needs the
    // hover of an earlier frame than its press.
    mouse_move(cx - 6, cy);
    micro_.push_back({[this, cx, cy] { mouse_move(cx, cy); }, 2});
    micro_.push_back({[this] { mouse_button(true); }, 2});
    micro_.push_back({[this] { mouse_button(false); }, settle_});
    wait_ = 1;
    return true;
}

bool TestScript::key_step(const Step& s, bool down, bool up) {
    for (size_t i = 1; i < s.args.size(); i++) {
        // "CTRL+A": modifiers held around the last key.
        std::vector<int> codes;
        const std::string& spec = s.args[i];
        size_t from = 0;
        while (true) {
            size_t plus = spec.find('+', from);
            std::string part = spec.substr(from, plus == std::string::npos ? std::string::npos : plus - from);
            if (part.empty() && plus == std::string::npos && from > 0) part = "+";  // "SHIFT++"
            int sc = scancode_from_name(part);
            if (!sc) return fail("unknown key \"" + part + "\"");
            codes.push_back(sc);
            if (plus == std::string::npos) break;
            from = plus + 1;
        }
        auto act = [this, codes, down, up] {
            if (down) for (int sc : codes) key_event(sc, true);
            if (up) for (size_t k = codes.size(); k-- > 0;) key_event(codes[k], false);
        };
        if (i == 1) act();
        else micro_.push_back({act, 2});
        if (i == 1 && s.args.size() > 2) wait_ = 2;
    }
    if (!micro_.empty()) micro_.back().frames = settle_;
    return true;
}

bool TestScript::drop(const Step& s) {
    float x = 2, y = 2;
    if (s.args.size() == 3 && s.args[2][0] == '@') {
        Widget w;
        if (!find_widget(s.args[2].substr(1), &w)) return false;
        x = (w.x0 + w.x1) / 2;
        y = (w.y0 + w.y1) / 2;
    } else if (s.args.size() == 3) {
        return fail("drop takes PATH, PATH X Y or PATH @WIDGET");
    }
    if (s.args.size() == 4) {
        x = (float)std::atof(s.args[2].c_str());
        y = (float)std::atof(s.args[3].c_str());
    }
    SDL_WindowID win = SDL_GetWindowID(window_);
    const char* path = store(s.args[1]);
    // What SDL sends for one dropped file: begin, position, file, complete.
    for (SDL_EventType t : {SDL_EVENT_DROP_BEGIN, SDL_EVENT_DROP_POSITION, SDL_EVENT_DROP_FILE, SDL_EVENT_DROP_COMPLETE}) {
        SDL_Event e;
        SDL_zero(e);
        e.type = t;
        e.drop.windowID = win;
        e.drop.x = x;
        e.drop.y = y;
        e.drop.data = t == SDL_EVENT_DROP_FILE ? path : nullptr;
        push(e);
    }
    return true;
}

int TestScript::pick_thread(void* data) {
    std::unique_ptr<PickJob> job((PickJob*)data);
    if (job->delay_ms > 0) SDL_Delay((Uint32)job->delay_ms);
    const char* list[2] = {job->path.empty() ? nullptr : job->path.c_str(), nullptr};
    job->fn(job->row, list);
    return 0;
}

bool TestScript::dialog(const Step& s) {
    bool ok = true;
    const std::string& n = s.args[0];
    int row = number(s.args[1], "the disc row", 1, 2, &ok) - 1;
    if (!ok) return false;
    if (n == "dialog-error") {  // SDL reports a dialog that failed with a NULL list
        pick_(row, nullptr);
        return true;
    }
    const std::string& path = s.args[2];
    if (n == "dialog") {
        const char* list[2] = {path.empty() ? nullptr : path.c_str(), nullptr};  // "" is a cancelled dialog
        pick_(row, list);
        return true;
    }
    int delay = 0;
    if (s.args.size() > 3) delay = number(s.args[3], "the delay in milliseconds", 0, 60000, &ok);
    if (!ok) return false;
    PickJob* job = new PickJob{pick_, row, path, delay};
    SDL_Thread* t = SDL_CreateThread(pick_thread, "launcher-test-pick", job);
    if (!t) {
        delete job;
        return fail(std::string("cannot start a thread: ") + SDL_GetError());
    }
    threads_.push_back(t);
    return true;
}

bool TestScript::expect(const Step& s) {
    const std::string& name = s.args[1];
    std::string op = "=", want = s.args[2];
    if (s.args.size() == 4) {
        op = s.args[2];
        want = s.args[3];
    }
    auto it = facts_.find(name);
    if (it == facts_.end()) return fail("expect: unknown fact \"" + name + "\"");
    const std::string& got = it->second;
    bool ok;
    if (op == "=" || op == "==") ok = got == want;
    else if (op == "!=") ok = got != want;
    else if (op == "~") ok = got.find(want) != std::string::npos;
    else if (op == "!~") ok = got.find(want) == std::string::npos;
    else return fail("expect: unknown operator \"" + op + "\" (use = != ~ !~)");
    if (!ok) return fail("expect " + name + " " + op + " \"" + want + "\": got \"" + got + "\"");
    return true;
}

bool TestScript::pad(const Step& s) {
    const std::string& what = s.args[1];
    if (what == "attach") {
        if (!gamepad_subsystem_) return fail("pad: SDL's gamepad subsystem did not start");
        if (joystick_) return fail("pad: already attached");
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Launcher test pad";
        joystick_id_ = SDL_AttachVirtualJoystick(&desc);
        if (!joystick_id_) return fail(std::string("pad: cannot attach a virtual joystick: ") + SDL_GetError());
        joystick_ = SDL_OpenJoystick(joystick_id_);
        if (!joystick_) return fail(std::string("pad: cannot open the virtual joystick: ") + SDL_GetError());
        gamepad_ = SDL_OpenGamepad(joystick_id_);
        if (!gamepad_) return fail(std::string("pad: SDL does not take the virtual joystick for a gamepad: ") + SDL_GetError());
        if (padfn_) padfn_(gamepad_);
        return true;
    }
    if (what == "detach") {
        if (!joystick_) return fail("pad: nothing attached");
        if (padfn_) padfn_(nullptr);
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
        SDL_CloseJoystick(joystick_);
        SDL_DetachVirtualJoystick(joystick_id_);
        joystick_ = nullptr;
        joystick_id_ = 0;
        return true;
    }
    if (what != "down" && what != "up" && what != "press") return fail("pad: unknown action \"" + what + "\"");
    if (s.args.size() < 3) return fail("pad " + what + " needs a button name");
    if (!joystick_) return fail("pad: attach a pad first");
    int b = gamepad_button_from_name(s.args[2]);
    if (b < 0) return fail("pad: unknown button \"" + s.args[2] + "\"");
    SDL_Joystick* js = joystick_;
    if (what == "down") SDL_SetJoystickVirtualButton(js, b, true);
    if (what == "up") SDL_SetJoystickVirtualButton(js, b, false);
    if (what == "press") {
        SDL_SetJoystickVirtualButton(js, b, true);
        micro_.push_back({[js, b] { SDL_SetJoystickVirtualButton(js, b, false); }, settle_});
        wait_ = 3;  // ImGui reads the pad once per frame: hold it for a few
    }
    return true;
}

void TestScript::run(const Step& s) {
    const std::string& n = s.args[0];
    bool ok = true;
    if (n == "wait") {
        wait_ = number(s.args[1], "wait", 1, 100000, &ok);
    } else if (n == "settle") {
        settle_ = number(s.args[1], "settle", 1, 1000, &ok);
    } else if (n == "key") {
        key_step(s, true, true);
    } else if (n == "keydown") {
        key_step(s, true, false);
    } else if (n == "keyup") {
        key_step(s, false, true);
    } else if (n == "text") {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.windowID = SDL_GetWindowID(window_);
        e.text.text = store(s.args[1]);
        push(e);
    } else if (n == "move") {
        if (s.args.size() == 3) {
            mouse_move((float)std::atof(s.args[1].c_str()), (float)std::atof(s.args[2].c_str()));
        } else {
            Widget w;
            if (find_widget(s.args[1], &w)) mouse_move((w.x0 + w.x1) / 2, (w.y0 + w.y1) / 2);
        }
    } else if (n == "click") {
        click(s);
    } else if (n == "tab") {
        Step c = s;
        c.args = {"click", "tab." + s.args[1]};
        click(c);
    } else if (n == "drop") {
        drop(s);
    } else if (n == "dialog" || n == "dialog-thread" || n == "dialog-error") {
        dialog(s);
    } else if (n == "shot") {
        shot_ = s.args[1];
    } else if (n == "expect") {
        expect(s);
    } else if (n == "print") {  // for writing a script: a fact on stderr
        auto it = facts_.find(s.args[1]);
        std::fprintf(stderr, "script:%d: %s = \"%s\"\n", s.line, s.args[1].c_str(), it == facts_.end() ? "(unknown)" : it->second.c_str());
    } else if (n == "close" || n == "end") {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        e.window.windowID = SDL_GetWindowID(window_);
        push(e);
    } else if (n == "sdlquit") {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_EVENT_QUIT;
        push(e);
    } else if (n == "pad") {
        pad(s);
    }
    (void)ok;
}
