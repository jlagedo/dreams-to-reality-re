#include "launcher.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "model.h"
#include "testing.h"

namespace {

bool is_sep(char c) { return c == '/' || c == '\\'; }

std::string with_separator(std::string p) {
    if (!p.empty() && !is_sep(p.back())) p += (p.find('\\') != std::string::npos) ? '\\' : '/';
    return p;
}

bool is_absolute(const std::string& p) {
    if (p.empty()) return false;
    if (is_sep(p[0])) return true;
    return p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':';
}

// An absolute path: relative ones are taken from `base` (ends with a separator).
std::string resolve(const std::string& p, const std::string& base) {
    if (p.empty() || is_absolute(p)) return p;
    return base + p;
}

std::string current_directory() {
    char* cwd = SDL_GetCurrentDirectory();
    std::string s = cwd ? cwd : "";
    SDL_free(cwd);
    return with_separator(s);
}

// Can files be created in dir? Tried for real: a read-only flag says little on
// Windows (Program Files, a read-only unzip folder).
bool writable(const std::string& dir) {
    std::string probe = dir + ".dreams_write_test";
    SDL_IOStream* io = SDL_IOFromFile(probe.c_str(), "wb");
    if (!io) return false;
    SDL_CloseIO(io);
    SDL_RemovePath(probe.c_str());
    return true;
}

char* dup_string(const std::string& s) {
    char* p = (char*)std::malloc(s.size() + 1);
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

void set_error(LauncherResult* out, const std::string& msg) {
    std::strncpy(out->error, msg.c_str(), sizeof out->error - 1);
    out->error[sizeof out->error - 1] = 0;
}

void fill_vars(LauncherResult* out, const VarList& vars) {
    out->vars = (LauncherVar*)std::calloc(vars.size() ? vars.size() : 1, sizeof(LauncherVar));
    if (!out->vars) return;
    for (const auto& kv : vars) {
        out->vars[out->count].name = dup_string(kv.first);
        out->vars[out->count].value = dup_string(kv.second);
        out->count++;
    }
}

struct Args {
    bool play = false;
    std::string disc1, disc2, data;
    std::string error;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--play") { a.play = true; continue; }
        for (auto [name, dest] : {std::pair<const char*, std::string*>{"--disc1", &a.disc1}, {"--disc2", &a.disc2}, {"--data", &a.data}}) {
            std::string n = name;
            if (arg == n) {
                if (i + 1 >= argc) { a.error = n + " needs a value"; return a; }
                *dest = argv[++i];
            } else if (arg.compare(0, n.size() + 1, n + "=") == 0) {
                *dest = arg.substr(n.size() + 1);
            }
        }
    }
    return a;
}

}  // namespace

// ---- Location and Model ----

Location resolve_location() {
    Location loc;
    const char* home = testing::env(testing::Var::Home);  // test only
    std::string base;
    if (home && *home) {
        base = with_separator(home);
        SDL_CreateDirectory(base.c_str());
    } else if (const char* exe = SDL_GetBasePath()) {
        base = exe;
    }
    if (!base.empty() && writable(base)) {
        loc.dir = base;
        return loc;
    }
    char* pref = SDL_GetPrefPath("", "DreamsToReality");
    loc.dir = with_separator(pref ? pref : "");
    loc.fallback = true;
    SDL_free(pref);
    return loc;
}

std::string Model::data_dir() const {
    if (!data_cli.empty()) return data_cli;
    if (!data_ini.empty()) return resolve(data_ini, loc.dir);
    return loc.default_data();
}

void Model::load(const std::string& cli_disc1, const std::string& cli_disc2, const std::string& cli_data) {
    ini = Ini();
    size_t size = 0;
    if (void* text = SDL_LoadFile(loc.ini_path().c_str(), &size)) {
        ini = Ini::parse(std::string_view((const char*)text, size));
        SDL_free(text);
    }
    port.load(ini);
    disc_ini[0] = ini.get_or("discs", "disc1", "");
    disc_ini[1] = ini.get_or("discs", "disc2", "");
    data_ini = ini.get_or("data", "dir", "");

    std::string cwd = current_directory();
    disc_cli[0] = resolve(cli_disc1, cwd);
    disc_cli[1] = resolve(cli_disc2, cwd);
    data_cli = resolve(cli_data, cwd);
    for (int i = 0; i < 2; i++) discs.path[i] = disc_cli[i].empty() ? resolve(disc_ini[i], loc.dir) : disc_cli[i];
    refresh(discs, true);
}

bool Model::save(std::string* err) {
    for (int i = 0; i < 2; i++) {
        const std::string& p = discs.path[i];
        std::string words;
        if (p.empty()) {
            words = "";
        } else if (p == disc_cli[0] || p == disc_cli[1]) {
            words = disc_ini[i];  // a command-line value is for this run only
        } else {
            words = p;
            for (int j = 0; j < 2; j++)
                if (p == resolve(disc_ini[j], loc.dir)) { words = disc_ini[j]; break; }  // keep a relative path relative
        }
        ini.set("discs", i == 0 ? "disc1" : "disc2", words);
    }
    // Remember what the file now says, for the next save in this session.
    disc_ini[0] = ini.get_or("discs", "disc1", "");
    disc_ini[1] = ini.get_or("discs", "disc2", "");
    ini.set("data", "dir", data_ini);
    port.store(ini);

    std::string text =
        "; Dreams to Reality port settings, written by the launcher.\n"
        "; Port settings only: the game's own options stay in the game.\n"
        "; Gamepad buttons: \"a = button3\" is a joystick button (mode game), \"keys_a = CTRL\" a key (mode keys).\n"
        "; [keyboard]: physical key = the key the game sees.\n\n" +
        ini.dump();
    std::string path = loc.ini_path(), tmp = path + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), text.data(), text.size()) || !SDL_RenamePath(tmp.c_str(), path.c_str())) {
        if (err) *err = std::string("cannot write ") + path + ": " + SDL_GetError();
        SDL_RemovePath(tmp.c_str());
        return false;
    }
    return true;
}

std::string Model::play_blocker() const {
    if (discs.check[0].status == DiscStatus::NotSet && discs.check[1].status == DiscStatus::NotSet)
        return "Choose both disc images to play.";
    for (int i = 0; i < 2; i++)
        if (!discs.check[i].ok()) return "Disc " + std::to_string(i + 1) + ": " + discs.check[i].message;
    return keyboard_conflict(port);
}

VarList Model::vars() const {
    VarList v;
    v.emplace_back("WD_DISC1", discs.path[0]);
    v.emplace_back("WD_DISC2", discs.path[1]);
    v.emplace_back("WD_DATA_DIR", data_dir());
    emit_port_vars(port, v);
    return v;
}

// ---- the public interface ----

int launcher_run(int argc, char** argv, LauncherResult* out) {
    std::memset(out, 0, sizeof *out);
    Args args = parse_args(argc, argv);
    if (!args.error.empty()) {
        set_error(out, args.error);
        return -1;
    }

    Model m;
    m.loc = resolve_location();
    m.load(args.disc1, args.disc2, args.data);

    if (args.play) {
        if (testing::env(testing::Var::Verbose))  // test only: the status lines
            for (int i = 0; i < 2; i++) std::fprintf(stderr, "disc %d: %s\n", i + 1, m.discs.check[i].message.c_str());
        // Name the bad image's path as well: a script has nothing else to go by.
        std::string why;
        for (int i = 0; i < 2 && why.empty(); i++)
            if (!m.discs.check[i].ok())
                why = "disc " + std::to_string(i + 1) + ": " + m.discs.check[i].message +
                      (m.discs.path[i].empty() ? "" : " (" + m.discs.path[i] + ")");
        if (why.empty()) why = keyboard_conflict(m.port);
        if (!why.empty()) {
            set_error(out, why);
            return -1;
        }
        m.save(nullptr);  // as Play does; a read-only location cannot stop a run
        fill_vars(out, m.vars());
        out->play = 1;
        return 1;
    }

    Uint32 inited = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
    if (!SDL_InitSubSystem(inited)) {  // no gamepad subsystem is not a reason to give up
        inited = SDL_INIT_VIDEO;
        if (!SDL_InitSubSystem(inited)) {
            set_error(out, std::string("cannot start SDL video: ") + SDL_GetError());
            return -1;
        }
    }
    bool quit = false;
    std::string err;
    bool play = run_window(m, &quit, &err);
    SDL_QuitSubSystem(inited);  // never SDL_Quit: the host keeps using SDL

    if (!err.empty()) {
        set_error(out, err);
        return -1;
    }
    if (!play) return 0;
    std::string save_err;
    m.save(&save_err);
    fill_vars(out, m.vars());
    out->play = 1;
    return 1;
}

void launcher_free(LauncherResult* out) {
    if (!out) return;
    for (int i = 0; i < out->count; i++) {
        std::free((void*)out->vars[i].name);
        std::free((void*)out->vars[i].value);
    }
    std::free(out->vars);
    out->vars = nullptr;
    out->count = 0;
    out->play = 0;
}
