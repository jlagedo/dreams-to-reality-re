/*
 * WINDREAM recompilation - runtime core.
 *
 * The whole guest address space is one host arena: guest VA v lives at host
 * g_mem_base + v. The arena is reserved up front and committed where the guest
 * has memory (image, stacks, TIBs, VirtualAlloc'd heap), so a wild guest pointer
 * faults instead of reading zeros. The original WINDREAM.EXE is mapped at its
 * real VAs from the file at startup; the lifted C is the code.
 */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_atomic.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>
#include "imports.h"
#include "crash_report.h"
#include "recomp_trace.h"
/* WD_WITH_LAUNCHER (CMakeLists.txt) links recomp/launcher and lets main run it;
 * a build that compiles this file on its own (the verify hosts) has neither.
 * SDL_main.h makes argv UTF-8 on Windows and supplies WinMain for the release
 * build, which has no console (WD_GUI_LOG, recomp/windream/release.py). */
#ifdef WD_WITH_LAUNCHER
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_main.h>
#include "launcher.h"
#endif
#ifdef __EMSCRIPTEN__
void wd_web_args_to_env(int argc, char** argv);   /* web/web_glue.c */
#endif
#ifdef WD_GUI_LOG
#include <direct.h>
#include <io.h>
#include <wchar.h>
#endif

/* ---- register file (per host thread) ---- */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS double   g_st[8];
RECOMP_TLS int      g_fp_top;
RECOMP_TLS uint16_t g_fpu_cw = 0x027F;   /* Win32 process default: 53-bit, round-nearest */
RECOMP_TLS uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;
RECOMP_TLS uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
RECOMP_TLS uint32_t g_fs_base, g_gs_base;
RECOMP_TLS uint64_t g_mm[8];
RECOMP_TLS uint32_t g_cur_func;
RECOMP_TLS uint32_t g_tail_armed, g_tail_pending;   /* RECOMP_ITAIL_FRAGMENT */

ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE];
uint32_t g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;


static uint32_t g_image_span;
void wd_scene_probe_init(void);
void wd_render_install(void);
void wd_render_close(void);
const char* g_wd_exe;
uint32_t wd_image_span(void) { return g_image_span; }

/* ---- small guest-memory helpers ---- */
static SDL_SpinLock g_alloc_lock;
static uint32_t g_shim_next;   /* shim allocations: top of the heap region, growing down */

uint32_t shim_alloc(uint32_t n, uint32_t align) {
    SDL_LockSpinlock(&g_alloc_lock);
    if (align < 16) align = 16;
    uint32_t va = (g_shim_next - n) & ~(align - 1u);
    g_shim_next = va;
    SDL_UnlockSpinlock(&g_alloc_lock);
    vm_commit(va, n);
    memset(wd_host_range(va, n, 1), 0, n);
    return va;
}
uint32_t shim_strdup(const char* s) {
    uint32_t n = (uint32_t)strlen(s) + 1, va = shim_alloc(n, 16);
    memcpy(wd_host_range(va, n, 1), s, n);
    return va;
}
void guest_strcpy_out(uint32_t va, uint32_t cap, const char* s) {
    if (!va || !cap) return;
    uint32_t n = (uint32_t)strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(wd_host_range(va, n, 1), s, n);
    WD_HOST_WRITE8(va + n) = 0;
}
int guest_str(uint32_t va, char* out, int cap) {
    int i = 0;
    if (!va) { out[0] = 0; return 0; }
    for (; i < cap - 1; i++) { char c = (char)WD_HOST_READ8(va + i); out[i] = c; if (!c) break; }
    out[cap - 1] = 0;
    return i;
}

/* ---- dispatch ---- */
recomp_func_t recomp_lookup(uint32_t va) {
    int lo = 0, hi = (int)recomp_dispatch_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t m = recomp_dispatch_table[mid].address;
        if (m == va) return recomp_dispatch_table[mid].func;
        if (m < va) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }
recomp_func_t recomp_lookup_import(uint32_t va) {
    if (va >= WD_COM_BASE && va < WD_COM_BASE + 0x1000u) return wd_com_lookup(va);
    for (uint32_t i = 0; i < wd_import_bridge_count; i++)
        if (wd_import_bridges[i].address == va) return wd_import_bridges[i].func;
    return NULL;
}

/* ---- synthetic COM methods ----
 * A fake COM object lives in guest memory as { vtbl_va, ... }; its vtable holds
 * made-up method VAs from WD_COM_BASE up. `call [vtbl+off]` in lifted code is a
 * RECOMP_ICALL of that VA, which recomp_lookup_import resolves here. */
static recomp_func_t g_com_fn[1024];
static uint32_t g_com_n;
uint32_t wd_com_vtable(const recomp_func_t* fns, int n) {
    SDL_LockSpinlock(&g_alloc_lock);
    uint32_t base = g_com_n;
    for (int i = 0; i < n; i++) g_com_fn[g_com_n++] = fns[i];
    SDL_UnlockSpinlock(&g_alloc_lock);
    uint32_t vt = shim_alloc((uint32_t)n * 4u, 16);
    for (int i = 0; i < n; i++) WD_HOST_WRITE32(vt + 4u * (uint32_t)i) = WD_COM_BASE + base + (uint32_t)i;
    return vt;
}
recomp_func_t wd_com_lookup(uint32_t va) {
    uint32_t i = va - WD_COM_BASE;
    return i < g_com_n ? g_com_fn[i] : NULL;
}

/* ---- callbacks: host code calling a lifted stdcall function ----
 * Pushes the arguments and the dummy return address on the calling thread's
 * guest stack and runs the lifted body. Only valid on a thread with guest
 * state, i.e. from inside an import bridge. The caller's registers are put back
 * (the callee would preserve ebx/esi/edi/ebp; ecx/edx are restored too). */
uint32_t guest_call(uint32_t va, int argc, const uint32_t* args) {
    recomp_func_t fn = recomp_lookup(va);
    if (!fn) { fprintf(stderr, "[callback] no lifted function at 0x%08X\n", va); return 0; }
    uint32_t esp = g_esp, ebx = g_ebx, ecx = g_ecx, edx = g_edx, esi = g_esi, edi = g_edi, ebp = g_ebp;
    wd_host_range(g_esp - ((uint32_t)argc + 1) * 4, ((size_t)argc + 1) * 4, 1);
    for (int i = argc - 1; i >= 0; i--) PUSH32(g_esp, args[i]);
    PUSH32(g_esp, RECOMP_RETADDR);
    fn();
    uint32_t r = g_eax;
    g_esp = esp; g_ebx = ebx; g_ecx = ecx; g_edx = edx; g_esi = esi; g_edi = edi; g_ebp = ebp;
    return r;
}

/* The same for a Watcom register call (EAX, EDX, EBX, ECX): every guest
 * register is put back, EAX included. */
uint32_t guest_call_regs(uint32_t va, uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t ecx) {
    recomp_func_t fn = recomp_lookup(va);
    if (!fn) { fprintf(stderr, "[callback] no lifted function at 0x%08X\n", va); return 0; }
    uint32_t a = g_eax, esp = g_esp, ebx0 = g_ebx, ecx0 = g_ecx, edx0 = g_edx, esi = g_esi, edi = g_edi,
             ebp = g_ebp;
    g_eax = eax; g_edx = edx; g_ebx = ebx; g_ecx = ecx;
    wd_host_range(g_esp - 4, 4, 1);
    PUSH32(g_esp, RECOMP_RETADDR);
    fn();
    uint32_t r = g_eax;
    g_eax = a; g_esp = esp; g_ebx = ebx0; g_ecx = ecx0; g_edx = edx0; g_esi = esi; g_edi = edi; g_ebp = ebp;
    return r;
}

/* ---- image loader ---- */
#pragma pack(push, 1)
typedef struct { char name[8]; uint32_t vsize, vaddr, rsize, roff, a, b; uint16_t c, d; uint32_t chr; } SecHdr;
#pragma pack(pop)

#define DISC_EXE "GDIDREAM.EXE"

/* The guest EXE's bytes: a host file, or with no path GDIDREAM.EXE of disc 1.
 * The path is UTF-8, as SDL takes it and as files.c reads it: main's argv is
 * UTF-8 on Windows too (SDL_main.h), where fopen would want the ANSI code page. */
static uint8_t* read_image(const char* path, size_t* size) {
    if (!path) {
        uint8_t* buf = (uint8_t*)files_disc_read(1, DISC_EXE, size);
        if (!buf) fprintf(stderr, "FATAL: cannot read %s from disc 1\n", DISC_EXE);
        return buf;
    }
    SDL_IOStream* f = SDL_IOFromFile(path, "rb");
    if (!f) { fprintf(stderr, "FATAL: cannot open %s\n", path); return NULL; }
    Sint64 sz = SDL_GetIOSize(f);
    uint8_t* buf = sz >= 0 ? (uint8_t*)malloc(sz ? (size_t)sz : 1) : NULL;
    if (!buf || SDL_ReadIO(f, buf, (size_t)sz) != (size_t)sz) { free(buf); buf = NULL; }
    SDL_CloseIO(f);
    *size = (size_t)sz;
    return buf;
}

static int load_image(const char* path) {
    size_t size = 0;
    uint8_t* buf = read_image(path, &size);
    if (!buf) return 0;
    long sz = (long)size;
    if (sz < 0x40 || *(uint32_t*)(buf + 0x3C) > (uint32_t)sz - 0x40) {
        fprintf(stderr, "FATAL: %s is not a Windows executable\n", path ? path : DISC_EXE);
        free(buf);
        return 0;
    }
    uint8_t* nt = buf + *(uint32_t*)(buf + 0x3C);
    uint16_t nsec = *(uint16_t*)(nt + 6), optsz = *(uint16_t*)(nt + 20);
    uint32_t hdrsz = *(uint32_t*)(nt + 24 + 60);
    SecHdr* sec = (SecHdr*)(nt + 24 + optsz);
    uint32_t end = 0;
    for (int i = 0; i < nsec; i++) {
        uint32_t e = sec[i].vaddr + (sec[i].vsize ? sec[i].vsize : sec[i].rsize);
        if (e > end) end = e;
    }
    g_image_span = (end + 0xFFFu) & ~0xFFFu;
    vm_commit(WD_IMAGE_BASE, g_image_span);
    memcpy(PTR(WD_IMAGE_BASE), buf, hdrsz < (uint32_t)sz ? hdrsz : (uint32_t)sz);
    for (int i = 0; i < nsec; i++) {
        /* Watcom .bss: PointerToRawData 0 with the memory size in SizeOfRawData. */
        if (!sec[i].roff || !sec[i].rsize) continue;
        uint32_t n = sec[i].vsize && sec[i].vsize < sec[i].rsize ? sec[i].vsize : sec[i].rsize;
        if (sec[i].roff + n > (uint32_t)sz) n = (uint32_t)sz - sec[i].roff;
        memcpy(PTR(WD_IMAGE_BASE + sec[i].vaddr), buf + sec[i].roff, n);
    }
    free(buf);
    fprintf(stderr, "[*] image: %u sections, span 0x%X at guest 0x%08X\n", nsec, g_image_span, WD_IMAGE_BASE);
    return 1;
}

static const char* region(uint32_t va) {
    if (va >= WD_IMAGE_BASE && va < WD_IMAGE_BASE + g_image_span) return "(image)";
    if (va >= WD_STACK_BASE && va < WD_STACK_BASE + WD_STACK_SIZE) return "(main stack)";
    if (va >= WD_HEAP_BASE && va < WD_HEAP_BASE + WD_HEAP_SIZE) return vm_describe(va);
    if (va == RECOMP_RETADDR) return "(dummy return address)";
    return "";
}

static int setup(const char* exe) {
    void* arena = vm_reserve(WD_ARENA_SIZE);
    if (!arena) { fprintf(stderr, "FATAL: cannot reserve %u MB arena\n", WD_ARENA_SIZE >> 20); return 0; }
    g_mem_base = (ptrdiff_t)(uintptr_t)arena;
    g_shim_next = WD_HEAP_BASE + WD_HEAP_SIZE;
    fprintf(stderr, "[*] arena: %u MB reserved at host %p\n", WD_ARENA_SIZE >> 20, arena);
    if (!load_image(exe)) return 0;

    /* IAT: each slot holds its own VA, so `call [slot]` and the `jmp [slot]`
     * thunks both dispatch to recomp_lookup_import(slot) -> the bridge. */
    for (uint32_t i = 0; i < wd_import_bridge_count; i++)
        WD_HOST_WRITE32(wd_import_bridges[i].address) = wd_import_bridges[i].address;

    wd_thread_init_main();
    fprintf(stderr, "[*] %u lifted functions, %u import bridges, esp=%08X\n",
            recomp_dispatch_count, wd_import_bridge_count, g_esp);
    return 1;
}

/* WD_POKE="va=value,...": dword writes into the loaded image before the entry
 * point runs, e.g. the debug flags nothing in the retail code sets (run.py
 * --overlays). Values are strtoul base 0 (0x.. hex, else decimal). */
static int apply_pokes(void) {
    const char* s = getenv("WD_POKE");
    while (s && *s) {
        char* end;
        uint32_t va = (uint32_t)strtoul(s, &end, 0);
        if (*end != '=') break;
        uint32_t v = (uint32_t)strtoul(end + 1, &end, 0);
        if (va < WD_IMAGE_BASE || va + 4u > WD_IMAGE_BASE + g_image_span) {
            fprintf(stderr, "FATAL: WD_POKE 0x%08X is outside the image\n", va);
            return 0;
        }
        WD_HOST_WRITE32(va) = v;
        fprintf(stderr, "[*] poke [0x%08X] = 0x%X\n", va, v);
        if (*end != ',') { s = end; break; }
        s = end + 1;
    }
    if (s && *s) { fprintf(stderr, "FATAL: WD_POKE: expected va=value at \"%s\"\n", s); return 0; }
    return 1;
}

/* Where the guest EXE comes from: a host path as the first argument (run.py,
 * the verify hosts), else GDIDREAM.EXE of disc 1 when WD_DISC1 and WD_DISC2
 * name the discs. Returns 0 to go on (*exe NULL: the disc's; *options: the
 * index of the first option), else the exit code. */
static const char* exe_argument(int argc, char** argv) {
    if (argc < 2 || !strncmp(argv[1], "--", 2)) return NULL;
#ifdef __EMSCRIPTEN__
    if (strchr(argv[1], '=')) return NULL;   /* a WD_NAME=VALUE option of the page (web_glue.c) */
#endif
    return argv[1];
}
#ifdef __EMSCRIPTEN__
/* The browser build (CONTRACT.md): the page puts the game's files in the
 * Emscripten FS under the install root (WD_INSTALL_ROOT, default /dreams), the
 * program is the EXE there (WD_WEB_EXE_NAME, set by CMake: GDIDREAM.EXE for the
 * retail build), saves go to the same tree (the write root is the install root
 * itself, so DATA/GAME is where the page may mount IDBFS), and there is no
 * launcher and no disc mode. */
#ifndef WD_WEB_EXE_NAME
#define WD_WEB_EXE_NAME "GDIDREAM.EXE"
#endif
static char g_web_exe[512];
static const char* web_exe(void) {
    const char* root = getenv("WD_INSTALL_ROOT");
    snprintf(g_web_exe, sizeof g_web_exe, "%s/%s", root && *root ? root : "/dreams", WD_WEB_EXE_NAME);
    if (!getenv("WD_WRITE_ROOT") && !getenv("WD_DATA_DIR")) setenv("WD_WRITE_ROOT", root && *root ? root : "/dreams", 1);
    return g_web_exe;
}
#endif
static int choose_exe(int argc, char** argv, const char** exe, int* options) {
    *exe = exe_argument(argc, argv);
    *options = *exe ? 2 : 1;
#ifdef __EMSCRIPTEN__
    if (!*exe) { *exe = web_exe(); return 0; }
#endif
    int discs = files_open_discs();
    if (discs < 0) return 1;
    /* Tree mode (WD_TREE, spec 008 phase M): the program is the tree's own
     * GDIDREAM.EXE, whether or not discs are open for their audio. */
    static char tree_exe[1100];
    const char* tree = files_tree();
    if (!*exe && tree) {
        snprintf(tree_exe, sizeof tree_exe, "%s/%s", tree, DISC_EXE);
        *exe = tree_exe;
        return 0;
    }
    if (*exe || discs) return 0;
    fprintf(stderr, "usage: %s [<GDIDREAM.EXE>] [--run] [trace options]\n"
                    "  without <GDIDREAM.EXE>, WD_DISC1 and WD_DISC2 name the discs (.cue, .iso or\n"
                    "  directory) and the program is read from disc 1\n", argv[0]);
    recomp_trace_help();
    return 2;
}

/* ---- the release build's log (WD_GUI_LOG: Windows GUI subsystem) ----
 * Without a console stderr and stdout lead nowhere, so both go to log.txt in
 * the user data directory, unbuffered (the crash report and a killed process
 * lose nothing). The directory is known once WD_DATA_DIR is set: from the
 * launcher, or from the environment when the launcher is skipped; until then,
 * and with no WD_DATA_DIR at all, output is dropped. gui_enter_data_dir makes
 * it the current directory, where crash_win32.c writes crash-<pid>.dmp and
 * user.c the snapshots; it runs after files_init, which resolves a relative
 * WD_DATA_DIR against the directory the program was started in. */
#ifdef WD_GUI_LOG
static wchar_t* wide(const char* utf8) {
    return (wchar_t*)SDL_iconv_string("UTF-16LE", "UTF-8", utf8, SDL_strlen(utf8) + 1);
}
static void gui_log_open(void) {
    static int opened;
    const char* dir = SDL_getenv("WD_DATA_DIR");
    if (opened || !dir || !*dir) return;
    opened = 1;
    SDL_CreateDirectory(dir);   /* with its parents */
    char path[1100];
    snprintf(path, sizeof path, "%s/log.txt", dir);
    wchar_t* w = wide(path);
    if (w && _wfreopen(w, L"w", stderr)) {
        setvbuf(stderr, NULL, _IONBF, 0);
        if (freopen("NUL", "w", stdout) && _dup2(_fileno(stderr), _fileno(stdout)) == 0) setvbuf(stdout, NULL, _IONBF, 0);
    }
    SDL_free(w);
}
static void gui_enter_data_dir(void) {
    const char* dir = SDL_getenv("WD_DATA_DIR");
    wchar_t* w = dir && *dir ? wide(dir) : NULL;
    if (w && _wchdir(w)) fprintf(stderr, "[*] cannot enter the data directory %s\n", dir);
    SDL_free(w);
}
#else
#define gui_log_open() ((void)0)
#define gui_enter_data_dir() ((void)0)
#endif

/* The build's name (WD_VERSION, set by release.py), the log's first line, so
 * a user's log.txt says which release ran. Development builds have none. */
static void log_version(void) {
#ifdef WD_VERSION
    static int done;
    if (!done++) fprintf(stderr, "[*] Dreams to Reality port %s\n", WD_VERSION);
#endif
}

/* ---- launcher ----
 * The launcher does not know the host and the host does not know the launcher.
 * This is the only code that knows both: it runs the launcher when the
 * program is started with neither an EXE path, WD_DISC1 nor WD_TREE (so
 * run.py never sees it), and puts the WD_* pairs it returns into the process environment,
 * where the host reads every option. A variable that is already set (and not
 * empty) is kept: the real environment wins over dreams.ini.
 *
 * The host reads the environment through SDL_getenv (files.c: the paths,
 * UTF-8) and through C getenv (host_env and others), and on Windows the two do
 * not share a copy: SDL_setenv_unsafe updates SDL's table and the Win32
 * environment (SetEnvironmentVariableA), which the C runtime's getenv, a
 * snapshot taken at start, never looks at again. So the C runtime's copy is
 * set as well, with the same UTF-8 bytes, and each pair is read back through
 * both: a "WARNING: ... not visible" line means a launcher setting is ignored. */
#ifdef WD_WITH_LAUNCHER
static int launcher_wanted(int argc, char** argv) {
    const char* disc1 = SDL_getenv("WD_DISC1");
    return !exe_argument(argc, argv) && !(disc1 && *disc1) && !files_tree();
}
/* -1: play, the pairs are in the environment; else the exit code. */
static int launch(int argc, char** argv) {
    LauncherResult r;
    int rc = launcher_run(argc, argv, &r), code = rc < 0 ? 2 : rc == 0 ? 0 : -1;
    if (rc < 0) {
        fprintf(stderr, "launcher: %s\n", r.error[0] ? r.error : "failed");
#ifdef WD_GUI_LOG
        const char* headless = SDL_getenv("WD_HEADLESS");
        if (!(headless && *headless))
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Dreams to Reality", r.error[0] ? r.error : "failed", NULL);
#endif
    }
    uint8_t kept[64] = { 0 };
    for (int i = 0; rc > 0 && i < r.count; i++) {
        const char* set = SDL_getenv(r.vars[i].name);   /* also: SDL's table exists before the first change */
        if (set && *set) { if (i < 64) kept[i] = 1; continue; }
        SDL_setenv_unsafe(r.vars[i].name, r.vars[i].value, 1);
#ifdef _WIN32
        _putenv_s(r.vars[i].name, r.vars[i].value);
#endif
    }
    if (rc > 0) { gui_log_open(); log_version(); }
    for (int i = 0; rc > 0 && i < r.count; i++) {
        const char* name = r.vars[i].name;
        const char* value = r.vars[i].value;
        const char* c = getenv(name);
        const char* sdl = SDL_getenv(name);
        if (i < 64 && kept[i]) {
            fprintf(stderr, "[launcher] %s=%s (set in the environment; the launcher's \"%s\" is not used)\n", name, sdl, value);
            continue;
        }
        fprintf(stderr, "[launcher] %s=%s\n", name, value);
        if (!c || strcmp(c, value)) fprintf(stderr, "[launcher] WARNING: %s not visible to getenv (\"%s\")\n", name, c ? c : "unset");
        if (!sdl || strcmp(sdl, value)) fprintf(stderr, "[launcher] WARNING: %s not visible to SDL_getenv (\"%s\")\n", name, sdl ? sdl : "unset");
    }
    launcher_free(&r);
    return code;
}
#endif

int main(int argc, char** argv) {
    const char* exe;
    int options, run = 0;
#ifdef __EMSCRIPTEN__
    wd_web_args_to_env(argc, argv);
    wd_web_status("boot", "starting");
    run = 1;
#endif
#ifdef WD_WITH_LAUNCHER
    if (launcher_wanted(argc, argv)) {
        int code = launch(argc, argv);
        if (code >= 0) return code;
        run = 1;   /* Play: no --run needed */
    }
#endif
    gui_log_open();
    log_version();
    int rc = choose_exe(argc, argv, &exe, &options);
    if (rc) return rc;
    for (int i = options; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
    }
    recomp_install_crash_handler();
    recomp_set_region_describer(region);
    if (!setup(exe) || !apply_pokes()) {
        wd_web_status("fatal", "cannot load the game program (is /dreams filled?)");
        return 1;
    }
    /* The PE entry point (WINDREAM: 0x465538), from the mapped headers. */
    uint32_t entry = WD_IMAGE_BASE + WD_HOST_READ32(WD_IMAGE_BASE + WD_HOST_READ32(WD_IMAGE_BASE + 0x3C) + 0x28);
    recomp_func_t entry_fn = recomp_lookup(entry);
    if (!entry_fn) { fprintf(stderr, "FATAL: entry point 0x%08X is not a lifted function\n", entry); return 1; }
    if (!run) { fprintf(stderr, "[*] ready; pass --run to enter the program at 0x%08X\n", entry); return 0; }

    g_wd_exe = exe ? exe : DISC_EXE;
    files_init(exe);
    gui_enter_data_dir();
    host_init();
    wd_scene_probe_init();
    wd_render_install();
    host_mode_install();
    fprintf(stderr, "[*] entering the program at 0x%08X\n", entry);
    PUSH32(g_esp, RECOMP_RETADDR);
    entry_fn();
    fprintf(stderr, "[*] entry returned, eax=%08X\n", g_eax);
    recomp_trace_flush();
    wd_render_close();
    wd_web_status("exit", "the program ended");
    return (int)g_eax;
}
