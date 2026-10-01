// The launcher's state, shared by the window (ui.cpp) and the entry point
// (launcher.cpp). Not part of the public interface.
#ifndef LAUNCHER_MODEL_H
#define LAUNCHER_MODEL_H

#include <string>

#include "ini.h"
#include "settings.h"
#include "validate.h"

// Where dreams.ini and the data directory live.
struct Location {
    std::string dir;          // ends with a separator
    bool fallback = false;    // the per-user directory, because the executable's folder is not writable
    std::string ini_path() const { return dir + "dreams.ini"; }
    std::string default_data() const { return dir + "userdata"; }
};

// The folder beside the executable when it can be written to, else the per-user one.
Location resolve_location();

struct Model {
    Location loc;
    Ini ini;                  // as read, so keys the launcher does not know survive a save
    PortSettings port;
    DiscRows discs;           // what the window shows and the game gets (resolved, absolute)
    std::string disc_ini[2];  // dreams.ini's own words for the discs
    std::string disc_cli[2];  // --disc1 / --disc2 (never saved)
    std::string data_ini;     // [data] dir
    std::string data_cli;     // --data (never saved)

    // The data directory in force: --data, then [data] dir, then userdata beside dreams.ini.
    std::string data_dir() const;
    bool data_is_default() const { return data_cli.empty() && data_ini.empty(); }

    // Reads dreams.ini and applies the command-line values. Validates the discs.
    void load(const std::string& cli_disc1, const std::string& cli_disc2, const std::string& cli_data);
    // Writes dreams.ini; command-line values are left out. Returns false with a reason in err.
    bool save(std::string* err);

    // "" when Play is allowed, else why not.
    std::string play_blocker() const;
    // The WD_* pairs for a run.
    VarList vars() const;
};

// Shows the launcher window until the user plays or quits. Returns true for
// Play; a window that cannot open returns false with err set (and quit false).
bool run_window(Model& m, bool* quit, std::string* err);

#endif
