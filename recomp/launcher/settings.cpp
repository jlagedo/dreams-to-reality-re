#include "settings.h"

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

const char* const kPadButtonNames[kPadButtons] = {"a", "b", "x", "y", "lb", "rb", "back", "start", "ls", "rs", "lt", "rt"};
const char* const kPadButtonLabels[kPadButtons] = {
    "A", "B", "X", "Y", "Left bumper", "Right bumper", "Back", "Start", "Left stick click", "Right stick click",
    "Left trigger", "Right trigger",
};

// The controls the game's README lists (docs/research/running.md has the
// layout), in the order a player would look for them. Labels are short and only
// describe the retail meaning.
const std::vector<GameKey>& game_keys() {
    static const std::vector<GameKey> keys = {
        {"UP", "Run"},           {"DOWN", "Backwards"},   {"LEFT", "Turn left"},    {"RIGHT", "Turn right"},
        {"CTRL", "Jump / kick"}, {"ALT", "Walk / punch"}, {"SPACE", "Combat mode"},
        {"1", "Magic slot 1"},   {"2", "Magic slot 2"},   {"3", "Magic slot 3"},
        {"ESC", "Interface"},
        {"F1", "Resolution"},    {"F2", "Resolution"},    {"F3", "Resolution"},
        {"F4", "Resolution"},    {"F5", "Resolution"},    {"F6", "Resolution"},
        {"F10", "Help (hold)"},  {"J", "Joypad"},         {"K", "Keyboard"},
        {"5", "Camera (with Alt)"}, {"6", "Camera (with Alt)"}, {"7", "Camera (with Alt)"},
        {"8", "Camera (with Alt)"}, {"9", "Camera (with Alt)"}, {"0", "Camera (with Alt)"},
    };
    return keys;
}

namespace {

std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = (char)std::toupper((unsigned char)c);
    return r;
}

// The fixed names of input_map.h's table (without the aliases).
const char* const kNamed[] = {
    "UP", "DOWN", "LEFT", "RIGHT", "RETURN", "ESC", "SPACE", "TAB", "BACKSPACE",
    "CTRL", "SHIFT", "ALT", "LCTRL", "RCTRL", "LSHIFT", "RSHIFT", "LALT", "RALT",
    "INSERT", "DELETE", "HOME", "END", "PAGEUP", "PAGEDOWN",
    "MINUS", "EQUALS", "COMMA", "PERIOD", "SLASH", "SEMICOLON", "QUOTE", "GRAVE",
    "LBRACKET", "RBRACKET", "BACKSLASH",
};

bool is_function_key(const std::string& u) {
    if (u.size() < 2 || u.size() > 3 || u[0] != 'F') return false;
    int v = 0;
    for (size_t i = 1; i < u.size(); i++) {
        if (!std::isdigit((unsigned char)u[i])) return false;
        v = v * 10 + (u[i] - '0');
    }
    return v >= 1 && v <= 24;
}

}  // namespace

std::string canonical_key(std::string_view name) {
    std::string u = upper(name);
    if (u == "ENTER") return "RETURN";
    if (u == "ESCAPE") return "ESC";
    if (u == "BACK") return "BACKSPACE";
    if (u.size() == 1 && std::isalnum((unsigned char)u[0])) return u;
    if (is_function_key(u)) return u;
    if (u.size() == 3 && u[0] == 'K' && u[1] == 'P' && std::isdigit((unsigned char)u[2])) return u;
    for (const char* n : kNamed)
        if (u == n) return u;
    return "";
}

const std::vector<std::string>& key_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v = {"UP", "DOWN", "LEFT", "RIGHT", "CTRL", "ALT", "SHIFT", "SPACE", "RETURN", "ESC", "TAB", "BACKSPACE"};
        for (char c = '0'; c <= '9'; c++) v.emplace_back(1, c);
        for (char c = 'A'; c <= 'Z'; c++) v.emplace_back(1, c);
        for (int i = 1; i <= 12; i++) v.push_back("F" + std::to_string(i));
        for (int i = 0; i <= 9; i++) v.push_back("KP" + std::to_string(i));
        for (const char* n : kNamed)
            if (std::find(v.begin(), v.end(), n) == v.end()) v.emplace_back(n);
        return v;
    }();
    return names;
}

std::string key_name_from_scancode(int sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return std::string(1, (char)('A' + (sc - SDL_SCANCODE_A)));
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return std::string(1, (char)('1' + (sc - SDL_SCANCODE_1)));
    if (sc == SDL_SCANCODE_0) return "0";
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return "F" + std::to_string(13 + sc - SDL_SCANCODE_F13);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return "KP" + std::to_string(1 + sc - SDL_SCANCODE_KP_1);
    if (sc == SDL_SCANCODE_KP_0) return "KP0";
    switch (sc) {
    case SDL_SCANCODE_UP: return "UP";
    case SDL_SCANCODE_DOWN: return "DOWN";
    case SDL_SCANCODE_LEFT: return "LEFT";
    case SDL_SCANCODE_RIGHT: return "RIGHT";
    case SDL_SCANCODE_RETURN: return "RETURN";
    case SDL_SCANCODE_ESCAPE: return "ESC";
    case SDL_SCANCODE_SPACE: return "SPACE";
    case SDL_SCANCODE_TAB: return "TAB";
    case SDL_SCANCODE_BACKSPACE: return "BACKSPACE";
    case SDL_SCANCODE_LCTRL: return "LCTRL";
    case SDL_SCANCODE_RCTRL: return "RCTRL";
    case SDL_SCANCODE_LSHIFT: return "LSHIFT";
    case SDL_SCANCODE_RSHIFT: return "RSHIFT";
    case SDL_SCANCODE_LALT: return "LALT";
    case SDL_SCANCODE_RALT: return "RALT";
    case SDL_SCANCODE_INSERT: return "INSERT";
    case SDL_SCANCODE_DELETE: return "DELETE";
    case SDL_SCANCODE_HOME: return "HOME";
    case SDL_SCANCODE_END: return "END";
    case SDL_SCANCODE_PAGEUP: return "PAGEUP";
    case SDL_SCANCODE_PAGEDOWN: return "PAGEDOWN";
    case SDL_SCANCODE_MINUS: return "MINUS";
    case SDL_SCANCODE_EQUALS: return "EQUALS";
    case SDL_SCANCODE_COMMA: return "COMMA";
    case SDL_SCANCODE_PERIOD: return "PERIOD";
    case SDL_SCANCODE_SLASH: return "SLASH";
    case SDL_SCANCODE_SEMICOLON: return "SEMICOLON";
    case SDL_SCANCODE_APOSTROPHE: return "QUOTE";
    case SDL_SCANCODE_GRAVE: return "GRAVE";
    case SDL_SCANCODE_LEFTBRACKET: return "LBRACKET";
    case SDL_SCANCODE_RIGHTBRACKET: return "RBRACKET";
    case SDL_SCANCODE_BACKSLASH: return "BACKSLASH";
    default: return "";
    }
}

int default_joy_button(int b) { return b < kPadJoyButtons ? b + 1 : 0; }

// The layout `--pad keys` ships with (recomp/README.md, "Playing").
const char* default_pad_key(int b) {
    static const char* const keys[kPadButtons] = {"CTRL", "DOWN", "ALT", "SPACE", "1", "2", "RETURN", "ESC", "", "", "3", ""};
    return keys[b];
}

PortSettings::PortSettings() { reset_pad_buttons(); }

PadDir PortSettings::effective_dir() const { return dir_set ? dir : default_dir(pad); }

void PortSettings::reset_pad_buttons() {
    for (int i = 0; i < kPadButtons; i++) {
        joy[i] = default_joy_button(i);
        key[i] = default_pad_key(i);
    }
}

void PortSettings::bind_key(const std::string& game, const std::string& physical) {
    keymap.erase(std::remove_if(keymap.begin(), keymap.end(), [&](const KeyPair& p) { return p.game == game; }), keymap.end());
    if (!physical.empty() && physical != game) keymap.push_back({physical, game});
}

void PortSettings::wasd_preset() {
    bind_key("UP", "W");
    bind_key("LEFT", "A");
    bind_key("DOWN", "S");
    bind_key("RIGHT", "D");
}

// ---- dreams.ini ----

namespace {

bool parse_bool(const std::string& v, bool fallback) {
    if (v == "1" || iequals(v, "true") || iequals(v, "yes") || iequals(v, "on")) return true;
    if (v == "0" || iequals(v, "false") || iequals(v, "no") || iequals(v, "off")) return false;
    return fallback;
}

int parse_int(const std::string& v, int fallback, int lo, int hi) {
    char* end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    if (v.empty() || *end) return fallback;
    return (int)std::clamp(n, (long)lo, (long)hi);
}

// "button7" -> 7, 0 when it is not that.
int parse_joy_target(const std::string& v) {
    if (v.size() < 7 || !iequals(std::string_view(v).substr(0, 6), "button")) return 0;
    int n = parse_int(v.substr(6), 0, 0, 32);
    return n;
}

}  // namespace

void PortSettings::load(const Ini& ini) {
    *this = PortSettings();
    std::string v = ini.get_or("port", "renderer", "");
    // A saved choice is kept either way; only a missing (or unknown) value takes the default.
    if (iequals(v, "gpu") || iequals(v, "direct")) gpu = true;
    else if (iequals(v, "software")) gpu = false;
    fullscreen = parse_bool(ini.get_or("port", "fullscreen", ""), false);
    scale = parse_int(ini.get_or("port", "scale", ""), 2, 1, 6);
    v = ini.get_or("port", "filter", "");
    for (const char* f : {"pixelart", "nearest", "linear"})
        if (iequals(v, f)) filter = f;
    fps = parse_int(ini.get_or("port", "fps", ""), 25, 0, 240);
    mute = parse_bool(ini.get_or("port", "mute", ""), false);
    smooth = parse_bool(ini.get_or("port", "smooth", ""), true);
    smooth_camera = parse_int(ini.get_or("port", "smooth_camera", ""), kSmoothCameraDefault, 0, 200);
    v = ini.get_or("port", "mode", "");
    mode = iequals(v, "dev") ? LaunchMode::Develop : iequals(v, "edited") ? LaunchMode::Edited : LaunchMode::Play;

    v = ini.get_or("gamepad", "mode", "");
    pad = iequals(v, "keys") ? PadMode::Keys : iequals(v, "off") ? PadMode::Off : PadMode::Game;
    v = ini.get_or("gamepad", "direction", "");
    for (auto [name, d] : {std::pair{"stick", PadDir::Stick}, {"dpad", PadDir::Dpad}, {"both", PadDir::Both}})
        if (iequals(v, name)) { dir = d; dir_set = true; }
    v = ini.get_or("gamepad", "deadzone", "");
    size_t comma = v.find(',');
    if (comma != std::string::npos) {
        int in = parse_int(v.substr(0, comma), -1, 0, 100), out = parse_int(v.substr(comma + 1), -1, 0, 100);
        if (in >= 0 && out >= 0 && in < out) { dz_inner = in; dz_outer = out; }  // the host refuses anything else
    }
    if (const IniSection* g = ini.section("gamepad")) {
        for (const auto& kv : g->items) {
            std::string name = kv.first;
            bool keys_entry = false;
            if (name.size() > 5 && iequals(std::string_view(name).substr(0, 5), "keys_")) { keys_entry = true; name = name.substr(5); }
            int b = -1;
            for (int i = 0; i < kPadButtons; i++)
                if (iequals(name, kPadButtonNames[i])) b = i;
            if (b < 0) continue;
            int n = keys_entry ? 0 : parse_joy_target(kv.second);
            if (n >= 1 && b < kPadJoyButtons) { joy[b] = n; continue; }
            std::string k = canonical_key(kv.second);  // "a = CTRL" is a keys-mode entry too
            if (!k.empty() && (keys_entry || n == 0)) key[b] = k;
        }
    }

    if (const IniSection* k = ini.section("keyboard")) {
        for (const auto& kv : k->items) {
            std::string from = canonical_key(kv.first), to = canonical_key(kv.second);
            if (from.empty() || to.empty() || from == to) continue;
            bool dup = false;
            for (const auto& p : keymap) dup |= p.physical == from;  // the host refuses a physical key twice
            if (!dup) keymap.push_back({from, to});
        }
    }
}

void PortSettings::store(Ini& ini) const {
    ini.set("port", "renderer", gpu ? "gpu" : "software");
    ini.set("port", "fullscreen", fullscreen ? "1" : "0");
    ini.set("port", "scale", std::to_string(scale));
    ini.set("port", "filter", filter);
    ini.set("port", "fps", std::to_string(fps));
    ini.set("port", "mute", mute ? "1" : "0");
    ini.set("port", "smooth", smooth ? "1" : "0");
    ini.set("port", "smooth_camera", std::to_string(smooth_camera));
    ini.set("port", "mode", mode == LaunchMode::Develop ? "dev" : mode == LaunchMode::Edited ? "edited" : "retail");

    ini.set("gamepad", "mode", pad == PadMode::Keys ? "keys" : pad == PadMode::Off ? "off" : "game");
    if (dir_set) ini.set("gamepad", "direction", dir == PadDir::Dpad ? "dpad" : dir == PadDir::Both ? "both" : "stick");
    else ini.erase("gamepad", "direction");
    ini.set("gamepad", "deadzone", std::to_string(dz_inner) + "," + std::to_string(dz_outer));
    // Only what differs from the defaults, so a later change of default reaches everyone who never touched it.
    for (int i = 0; i < kPadButtons; i++) {
        std::string joy_key = kPadButtonNames[i], key_key = std::string("keys_") + kPadButtonNames[i];
        if (i < kPadJoyButtons && joy[i] != default_joy_button(i)) ini.set("gamepad", joy_key, "button" + std::to_string(joy[i]));
        else ini.erase("gamepad", joy_key);
        if (key[i] != default_pad_key(i)) ini.set("gamepad", key_key, key[i]);
        else ini.erase("gamepad", key_key);
    }

    ini.clear_section("keyboard");
    for (const auto& p : keymap) ini.set("keyboard", p.physical, p.game);
}

// ---- the key table ----

std::vector<KeyRowState> keyboard_rows(const PortSettings& s) {
    const auto& keys = game_keys();
    std::vector<bool> used(s.keymap.size(), false);
    std::vector<KeyRowState> rows;
    std::map<std::string, int> claims;
    for (const auto& k : keys) {
        KeyRowState r{k.name, false, false};
        for (size_t i = 0; i < s.keymap.size(); i++)
            if (!used[i] && s.keymap[i].game == k.name) { used[i] = true; r.physical = s.keymap[i].physical; r.remapped = true; break; }
        claims[r.physical]++;
        rows.push_back(r);
    }
    // Pairs for keys the table has no row for still hold their physical key.
    for (size_t i = 0; i < s.keymap.size(); i++)
        if (!used[i]) claims[s.keymap[i].physical]++;
    for (auto& r : rows) r.conflict = claims[r.physical] > 1;
    return rows;
}

std::string keyboard_conflict(const PortSettings& s) {
    std::map<std::string, std::string> first;  // physical key -> the game key it serves
    std::vector<bool> used(s.keymap.size(), false);
    auto claim = [&](const std::string& physical, const std::string& game) -> std::string {
        auto [it, fresh] = first.emplace(physical, game);
        if (fresh) return "";
        return "The key " + physical + " is used for both " + it->second + " and " + game + ".";
    };
    for (const auto& k : game_keys()) {
        std::string physical = k.name;
        for (size_t i = 0; i < s.keymap.size(); i++)
            if (!used[i] && s.keymap[i].game == k.name) { used[i] = true; physical = s.keymap[i].physical; break; }
        std::string c = claim(physical, k.name);
        if (!c.empty()) return c;
    }
    for (size_t i = 0; i < s.keymap.size(); i++)
        if (!used[i]) {
            std::string c = claim(s.keymap[i].physical, s.keymap[i].game);
            if (!c.empty()) return c;
        }
    return "";
}

// ---- the host's variables ----

void emit_port_vars(const PortSettings& s, VarList& out) {
    const bool gpu = s.gpu && s.mode != LaunchMode::Develop;
    if (gpu != PortSettings::kGpuDefault) out.emplace_back("WD_RENDERER", gpu ? "direct" : "software");
    if (s.fullscreen) out.emplace_back("WD_FULLSCREEN", "1");
    if (s.scale != 2) out.emplace_back("WD_SCALE", std::to_string(s.scale));
    if (s.filter != "pixelart") out.emplace_back("WD_FILTER", s.filter);
    if (s.fps != 25) out.emplace_back("WD_FPS", std::to_string(s.fps));  // "0" is uncapped, so it is not empty
    if (s.mute) out.emplace_back("WD_MUTE", "1");
    if (!s.smooth) out.emplace_back("WD_FIXED_STEP", "0");  // retail timing; no interpolation either
    else if (gpu && PortSettings::kInterpolates && s.smooth_camera != PortSettings::kSmoothCameraDefault)
        out.emplace_back("WD_SMOOTH_CAMERA", std::to_string(s.smooth_camera));  // "0" is off, so it is not empty
    if (s.pad != PadMode::Game) out.emplace_back("WD_PAD", s.pad == PadMode::Keys ? "keys" : "off");
    if (s.dz_inner != 10 || s.dz_outer != 95)
        out.emplace_back("WD_DEADZONE", std::to_string(s.dz_inner) + "," + std::to_string(s.dz_outer));
    if (s.pad != PadMode::Off && s.dir_set && s.dir != PortSettings::default_dir(s.pad))
        out.emplace_back("WD_PAD_DIRECTION", s.dir == PadDir::Dpad ? "dpad" : s.dir == PadDir::Both ? "both" : "stick");
    if (!s.keymap.empty()) {
        std::string v;
        for (const auto& p : s.keymap) v += (v.empty() ? "" : ",") + p.physical + "=" + p.game;
        out.emplace_back("WD_KEYMAP", v);
    }
    if (s.pad != PadMode::Off) {
        std::string v;
        for (int i = 0; i < kPadButtons; i++) {
            std::string target;
            if (s.pad == PadMode::Game) {
                if (i < kPadJoyButtons && s.joy[i] != default_joy_button(i)) target = "button" + std::to_string(s.joy[i]);
            } else if (s.key[i] != default_pad_key(i) && !s.key[i].empty()) {
                target = s.key[i];
            }
            if (!target.empty()) v += (v.empty() ? "" : ",") + std::string(kPadButtonNames[i]) + "=" + target;
        }
        if (!v.empty()) out.emplace_back("WD_PADMAP", v);
    }
}
