// Port settings: the model behind the window, its dreams.ini mapping, and the
// WD_* variables the host reads.
//
// Only the port's own additions live here (renderer, display, device
// remapping, output mute); nothing from the game's in-game options. The value
// formats are the ones documented in recomp/README.md.
#ifndef LAUNCHER_SETTINGS_H
#define LAUNCHER_SETTINGS_H

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ini.h"

enum class PadMode { Game, Keys, Off };  // WD_PAD winmm | keys | off
enum class PadDir { Stick, Dpad, Both };

constexpr int kPadButtons = 12;  // a b x y lb rb back start ls rs lt rt, in WD_PADMAP's order
constexpr int kPadJoyButtons = 10;  // lt and rt are an axis in joystick mode, not buttons
extern const char* const kPadButtonNames[kPadButtons];   // as WD_PADMAP spells them
extern const char* const kPadButtonLabels[kPadButtons];  // as the window shows them

// A key the game reads, with what it does in retail (a label only).
struct GameKey {
    const char* name;   // WD_KEYMAP name
    const char* label;
};
const std::vector<GameKey>& game_keys();

// Key names WD_KEYMAP and WD_PADMAP accept. canonical_key returns the
// upper-case spelling of a name (aliases resolved), or "" when unknown.
// The host's table is recomp/windream/host/sdl/input_map.h (wd_vk_from_name);
// this one duplicates it on purpose so the launcher does not include it.
std::string canonical_key(std::string_view name);
const std::vector<std::string>& key_names();  // the names offered in combos
std::string key_name_from_scancode(int sdl_scancode);  // "" for a key the host has no name for

struct KeyPair {
    std::string physical;  // the key the player presses
    std::string game;      // the key the game sees
};

struct PortSettings {
    // The renderer when dreams.ini does not name one, and what the host does with no
    // WD_RENDERER (wd_render_requested, render_live.cpp): the GPU renderer on Windows,
    // software elsewhere.
#ifdef _WIN32
    static constexpr bool kGpuDefault = true;
#else
    static constexpr bool kGpuDefault = false;
#endif
    bool gpu = kGpuDefault;
    bool fullscreen = false;
    int scale = 2;
    std::string filter = "pixelart";  // pixelart | nearest | linear
    int fps = 25;                     // 0 = uncapped; the fixed step replaces it when smooth
    bool mute = false;
    // Smooth motion, not the original timing: WD_FIXED_STEP (30 steps a second,
    // frame delta 1.0) and, with the GPU renderer, WD_INTERPOLATE (a frame at
    // every display refresh). smooth_camera is WD_SMOOTH_CAMERA, in ms, 0 = off.
    bool smooth = false;
    int smooth_camera = 0;

    PadMode pad = PadMode::Game;
    bool dir_set = false;  // false: the host's default for the mode
    PadDir dir = PadDir::Stick;
    int dz_inner = 10, dz_outer = 95;
    int joy[kPadButtons];          // joystick button 1..32 per pad button (joystick mode)
    std::string key[kPadButtons];  // key name per pad button (keys mode), "" = none

    std::vector<KeyPair> keymap;   // WD_KEYMAP pairs

    PortSettings();
    PadDir effective_dir() const;
    static PadDir default_dir(PadMode mode) { return mode == PadMode::Keys ? PadDir::Both : PadDir::Stick; }

    void reset_pad_buttons();
    void reset_keymap() { keymap.clear(); }
    void wasd_preset();
    // Make `physical` stand for the game key `game` (one physical key per game
    // key); physical == game, or empty, is the default and removes the pair.
    void bind_key(const std::string& game, const std::string& physical);

    void load(const Ini& ini);
    void store(Ini& ini) const;
};

int default_joy_button(int pad_button);              // 1..10, 0 for lt and rt
const char* default_pad_key(int pad_button);         // "" = none

// What each row of the keyboard table holds and whether it clashes.
struct KeyRowState {
    std::string physical;  // the key that produces this game key
    bool remapped;
    bool conflict;         // another game key claims the same physical key
};
std::vector<KeyRowState> keyboard_rows(const PortSettings& s);
// First problem with the key table, "" when there is none (the same list the
// window highlights; Play is blocked on it).
std::string keyboard_conflict(const PortSettings& s);

using VarList = std::vector<std::pair<std::string, std::string>>;
// The port's WD_* pairs (not the paths), leaving out whatever is at the host's default.
void emit_port_vars(const PortSettings& s, VarList& out);

#endif
