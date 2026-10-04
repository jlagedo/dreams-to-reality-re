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

#include "disc.h"
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

// ---- the developer folder ----

const char* const kDeveloperMarker = ".developer-folder";

std::string Model::developer_dir() const { return with_separator(data_dir()) + "developer"; }

bool Model::developer_ready() const {
    SDL_PathInfo info;
    std::string marker = with_separator(developer_dir()) + kDeveloperMarker;
    return SDL_GetPathInfo(marker.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool Model::developer_started() const {
    SDL_PathInfo info;
    return SDL_GetPathInfo(developer_dir().c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

namespace {
struct CopyContext {
    const std::function<bool(const CopyProgress&)>* progress;
    CopyProgress state;
};
int SDLCALL copy_step(void* ctx, const DiscCopyProgress* p) {
    CopyContext* c = (CopyContext*)ctx;
    c->state.bytes_done = p->bytes_done;
    c->state.bytes_total = p->bytes_total;
    c->state.files_done = p->files_done;
    c->state.files_total = p->files_total;
    c->state.files_skipped = p->files_skipped;
    c->state.path = p->path ? p->path : "";
    return *c->progress && !(*c->progress)(c->state);
}
}  // namespace

int make_developer_folder(const Model& m, const std::function<bool(const CopyProgress&)>& progress, std::string* err) {
    char msg[512] = "";
    Disc* d1 = disc_open(m.discs.path[0].c_str(), msg, sizeof msg);
    Disc* d2 = d1 ? disc_open(m.discs.path[1].c_str(), msg, sizeof msg) : nullptr;
    int rc = -1;
    if (d1 && d2) {
        CopyContext ctx{&progress, {}};
        rc = disc_copy_merged(d1, d2, m.developer_dir().c_str(), copy_step, &ctx, msg, sizeof msg);
        if (rc == 1) msg[0] = 0;
    }
    disc_close(d2);
    disc_close(d1);
    if (rc == 0) {
        std::string marker = with_separator(m.developer_dir()) + kDeveloperMarker;
        static const char text[] = "complete: both discs copied (spec 008 phase M)\n";
        if (!SDL_SaveFile(marker.c_str(), text, sizeof text - 1)) {
            std::snprintf(msg, sizeof msg, "cannot write %s: %s", marker.c_str(), SDL_GetError());
            rc = -1;
        }
    }
    if (rc < 0 && err) *err = msg[0] ? msg : "the developer folder could not be made";
    return rc;
}

bool reset_edits(const Model& m, std::string* err) {
    char msg[512] = "";
    Disc* d1 = disc_open(m.discs.path[0].c_str(), msg, sizeof msg);
    DiscEntry e;
    bool ok = false;
    if (d1 && disc_find(d1, "DREAMS.DAT", &e) && !e.is_dir) {
        std::string data(e.size, '\0');
        DiscFile* f = disc_file_open(d1, &e);
        std::string target = with_separator(m.developer_dir()) + "DREAMS.DAT";
        if (f && disc_file_read(f, 0, data.data(), data.size()) == (int64_t)data.size())
            ok = SDL_SaveFile(target.c_str(), data.data(), data.size());
        if (!ok) std::snprintf(msg, sizeof msg, "cannot copy disc 1's DREAMS.DAT to %s", target.c_str());
        disc_file_close(f);
    } else if (d1) {
        std::snprintf(msg, sizeof msg, "disc 1 has no DREAMS.DAT");
    }
    disc_close(d1);
    if (!ok && err) *err = msg;
    return ok;
}

std::string Model::play_blocker() const {
    std::string why;
    switch (port.mode) {
    case LaunchMode::Play:
        if (discs.check[0].status == DiscStatus::NotSet && discs.check[1].status == DiscStatus::NotSet)
            return "Choose both disc images to play.";
        for (int i = 0; i < 2; i++)
            if (!discs.check[i].ok()) return "Disc " + std::to_string(i + 1) + ": " + discs.check[i].message;
        break;
    case LaunchMode::Develop:
        // The images are needed once, for the copy; after it Develop plays from the folder.
        if (!developer_ready())
            for (int i = 0; i < 2; i++)
                if (!discs.check[i].ok())
                    return "Develop copies both discs into the developer folder first. Disc " + std::to_string(i + 1) +
                           ": " + discs.check[i].message;
        break;
    case LaunchMode::Edited:
        if (!developer_ready()) return "Play edits plays the developer folder: start Develop once to make it.";
        break;
    }
    return keyboard_conflict(port);
}

VarList Model::vars() const {
    VarList v;
    bool tree = port.mode != LaunchMode::Play;
    // Play edits plays the discs' CD audio when both images are there, and silently without them.
    bool discs_too = port.mode == LaunchMode::Play || (port.mode == LaunchMode::Edited && discs.both_ok());
    if (discs_too) {
        v.emplace_back("WD_DISC1", discs.path[0]);
        v.emplace_back("WD_DISC2", discs.path[1]);
    }
    v.emplace_back("WD_DATA_DIR", data_dir());
    if (tree) {
        v.emplace_back("WD_MODE", port.mode == LaunchMode::Develop ? "dev" : "edited");
        v.emplace_back("WD_TREE", developer_dir());
    }
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
        for (int i = 0; i < 2 && why.empty() && m.port.mode == LaunchMode::Play; i++)
            if (!m.discs.check[i].ok())
                why = "disc " + std::to_string(i + 1) + ": " + m.discs.check[i].message +
                      (m.discs.path[i].empty() ? "" : " (" + m.discs.path[i] + ")");
        if (why.empty()) why = m.play_blocker();
        if (!why.empty()) {
            set_error(out, why);
            return -1;
        }
        if (m.needs_copy()) {  // Develop's first start: the developer folder, before the game
            unsigned long long next = 0;
            std::string err;
            auto report = [&next](const CopyProgress& p) {
                if (p.bytes_done >= next) {
                    std::fprintf(stderr, "launcher: developer folder %llu of %llu MB\n", p.bytes_done >> 20, p.bytes_total >> 20);
                    next = p.bytes_done + (64ull << 20);
                }
                return true;
            };
            if (make_developer_folder(m, report, &err) != 0) {
                set_error(out, "cannot make the developer folder: " + err);
                return -1;
            }
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
    if (m.needs_copy()) {  // the window makes the folder before it returns Play; never start without it
        set_error(out, "the developer folder is not complete");
        return -1;
    }
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
