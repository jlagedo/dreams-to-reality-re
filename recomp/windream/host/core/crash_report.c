/*
 * crash_report.c - crash diagnostics for recompiled 32-bit programs.
 * See crash_report.h.
 *
 * This file is the report itself, the same on every host: it reads only the
 * simulated machine. How a fault reaches it (and the host's own stack) is one
 * file per OS: crash_win32.c, crash_posix.c, crash_none.c. Each implements
 * recomp_install_crash_handler() and calls down into here.
 *
 * WINDREAM additions:
 *   - Ghidra names for guest VAs, from re/symbols/<exe>.tsv (found by walking up
 *     from the host exe, or WD_SYMS=<tsv>);
 *   - what each register points at, and return addresses on the guest stack;
 *   - the guest instruction behind a line of generated C, from its
 *     `/ * 0x...: insn * /` comment, for a host stack walk that has line numbers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL_filesystem.h>

#include "recomp_types.h"
#include "crash_report.h"
#include "crash_internal.h"
#include "imports.h"
#include "guest_win32.h"

static recomp_region_fn g_region = NULL;
static recomp_extra_fn  g_extra  = NULL;

void recomp_set_region_describer(recomp_region_fn fn) { g_region = fn; }
void recomp_set_extra_reporter(recomp_extra_fn fn) { g_extra = fn; }

static const char* region_of(uint32_t va) {
    return g_region ? g_region(va) : "";
}

/* ---- Ghidra names --------------------------------------------------------- */

typedef struct { uint32_t va; char name[48]; } Sym;
static Sym* g_syms;
static int g_nsyms = -1;

static int sym_cmp(const void* a, const void* b) {
    uint32_t x = ((const Sym*)a)->va, y = ((const Sym*)b)->va;
    return x < y ? -1 : x > y;
}

static FILE* open_symbols(void) {
    const char* env = getenv("WD_SYMS");
    if (env && *env) return fopen(env, "r");
    if (!g_wd_exe) return NULL;
    const char* base = strrchr(g_wd_exe, '\\');
    const char* b2 = strrchr(g_wd_exe, '/');
    if (b2 > base) base = b2;
    base = base ? base + 1 : g_wd_exe;
    char lower[64];
    size_t n = 0;
    for (; base[n] && n < sizeof lower - 1; n++)
        lower[n] = (char)((base[n] >= 'A' && base[n] <= 'Z') ? base[n] + 32 : base[n]);
    lower[n] = 0;
    /* The host exe's directory, with a trailing separator: the first cut below
     * removes only that. SDL owns the string. */
    const char* exe_dir = SDL_GetBasePath();
    if (!exe_dir) return NULL;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", exe_dir);
    for (int up = 0; up < 8; up++) {
        char* s = strrchr(dir, '\\');
        char* s2 = strrchr(dir, '/');
        if (s2 > s) s = s2;
        if (!s) break;
        *s = 0;
        char p[sizeof dir + 80];
        snprintf(p, sizeof p, "%s/re/symbols/%s.tsv", dir, lower);
        FILE* f = fopen(p, "r");
        if (f) return f;
    }
    return NULL;
}

static void load_symbols(void) {
    if (g_nsyms >= 0) return;
    g_nsyms = 0;
    FILE* f = open_symbols();
    if (!f) return;
    int cap = 4096;
    g_syms = (Sym*)malloc(cap * sizeof(Sym));
    uint32_t image = WD_IMAGE_BASE;
    char line[1024];
    while (g_syms && fgets(line, sizeof line, f)) {
        unsigned rva;
        char name[48];
        if (!strncmp(line, "#imagebase", 10)) { sscanf(line + 10, "%x", &image); continue; }
        if (strncmp(line, "FUNC\t", 5) || sscanf(line + 5, "%x\t%47[^\t\n]", &rva, name) != 2) continue;
        if (g_nsyms == cap) {
            Sym* grown = (Sym*)realloc(g_syms, (cap *= 2) * sizeof(Sym));
            if (!grown) break;
            g_syms = grown;
        }
        g_syms[g_nsyms].va = image + rva;
        strcpy(g_syms[g_nsyms].name, name);
        g_nsyms++;
    }
    fclose(f);
    qsort(g_syms, g_nsyms, sizeof(Sym), sym_cmp);
}

/* The first executable section of the mapped image: only code gets names. */
static void code_range(uint32_t* lo, uint32_t* hi) {
    static uint32_t l, h;
    if (!l) {
        uint32_t pe = WD_IMAGE_BASE + MEM32(WD_IMAGE_BASE + 0x3C);
        uint32_t n = MEM16(pe + 6), sec = pe + 0x18 + MEM16(pe + 0x14);
        for (uint32_t i = 0; i < n; i++, sec += 40)
            if (MEM32(sec + 36) & 0x20000020u) {   /* CNT_CODE | MEM_EXECUTE */
                l = WD_IMAGE_BASE + MEM32(sec + 12);
                h = l + (MEM32(sec + 8) ? MEM32(sec + 8) : MEM32(sec + 16));   /* Watcom: VirtualSize 0 */
                break;
            }
        if (!l) l = h = 1;
    }
    *lo = l; *hi = h;
}

/* "Name+0x12" for a code VA inside a named function, "" otherwise. Two
 * rotating buffers so one printf can name two addresses. */
static const char* guest_name(uint32_t va) {
    static char buf[2][80];
    static int k;
    /* an IAT slot: the bridge's host name (imp_CreateFileA), from the table
     * gen_imports.py writes beside the bridges */
    for (uint32_t i = 0; i < wd_import_bridge_count; i++)
        if (wd_import_bridges[i].address == va) {
            char* b = buf[k ^= 1];
            snprintf(b, sizeof buf[0], "[IAT %s]", wd_import_bridge_names[i]);
            return b;
        }
    if (va >= WD_COM_BASE && va < WD_COM_BASE + 0x100000) return "[COM method]";
    uint32_t clo, chi;
    code_range(&clo, &chi);
    if (va < clo || va >= chi) return "";
    load_symbols();
    int lo = 0, hi = g_nsyms - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_syms[mid].va <= va) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    /* Symbols are function starts only; beyond 64 KB it is not that function. */
    if (best < 0 || va - g_syms[best].va > 0x10000) return "";
    char* b = buf[k ^= 1];
    if (va == g_syms[best].va) snprintf(b, sizeof buf[0], "%s", g_syms[best].name);
    else snprintf(b, sizeof buf[0], "%s+0x%X", g_syms[best].name, va - g_syms[best].va);
    return b;
}

const char* crash_guest_name(uint32_t va) { return guest_name(va); }

static int guest_readable(uint32_t va, uint32_t n) {
    uint32_t base, bytes;
    if (vm_state(va, &base, &bytes) != W32_MEM_COMMIT) return 0;
    return (uint64_t)va + n <= (uint64_t)base + bytes;
}

static int in_image(uint32_t va) { return va >= WD_IMAGE_BASE && va < WD_IMAGE_BASE + wd_image_span(); }

/* ---- register file -------------------------------------------------------- */

static void dump_register(const char* nm, uint32_t v) {
    fprintf(stderr, "  %s=%08X", nm, v);
    const char* r = region_of(v);
    if (in_image(v) && *guest_name(v)) fprintf(stderr, "  %s", guest_name(v));
    else if (*r) fprintf(stderr, "  %s", r);
    if (v >= 0x10000 && guest_readable(v, 16)) {
        fprintf(stderr, "  ->");
        for (int i = 0; i < 16; i += 4) fprintf(stderr, " %08X", MEM32(v + i));
    }
    fprintf(stderr, "\n");
}

static void dump_registers(void) {
    dump_register("eax", g_eax); dump_register("ecx", g_ecx);
    dump_register("edx", g_edx); dump_register("ebx", g_ebx);
    dump_register("esp", g_esp); dump_register("ebp", g_ebp);
    dump_register("esi", g_esi); dump_register("edi", g_edi);
}

/* ---- guest stack ---------------------------------------------------------- */

/* A dword on the guest stack is a return address if the bytes before it are a
 * call: E8 rel32, or FF /2 in its register / memory forms. */
static int after_call(uint32_t ra, uint32_t* target) {
    *target = 0;
    if (!in_image(ra) || ra < WD_IMAGE_BASE + 0x1000) return 0;
    const uint8_t* p = (const uint8_t*)PTR(ra);
    if (p[-5] == 0xE8) { *target = ra + (uint32_t)(int32_t)MEM32(ra - 4); return in_image(*target); }
    for (int len = 2; len <= 7; len++)
        if (p[-len] == 0xFF && ((p[-len + 1] >> 3) & 7) == 2) return 1;
    return 0;
}

static void dump_guest_stack(void) {
    fprintf(stderr, "  guest stack return addresses (from esp=%08X):\n", g_esp);
    int shown = 0;
    for (uint32_t a = g_esp & ~3u; shown < 24 && a < WD_STACK_BASE + WD_STACK_SIZE && a < g_esp + 0x4000; a += 4) {
        if (!guest_readable(a, 4)) break;
        uint32_t v = MEM32(a), t;
        if (!after_call(v, &t)) continue;
        fprintf(stderr, "    [esp+%04X] %08X %-36s", a - g_esp, v, guest_name(v));
        if (t) fprintf(stderr, " call %08X %s", t, guest_name(t));
        fprintf(stderr, "\n");
        shown++;
    }
    if (!shown) fprintf(stderr, "    (none)\n");
}

/* ---- indirect-call history ----------------------------------------------- */

/* Most recent last: the tail is where you start reading. Repeats of one entry
 * or of a pair (the stack check's __STK -> __threadid_ call) collapse. */
static void dump_icall_trace(void) {
    uint32_t va[ICALL_TRACE_SIZE], from[ICALL_TRACE_SIZE], rep[ICALL_TRACE_SIZE];
    int n = 0;
    for (int i = 0; i < ICALL_TRACE_SIZE; i++) {   /* pass 1: runs of one entry */
        uint32_t idx = (g_icall_trace_idx - ICALL_TRACE_SIZE + i) & (ICALL_TRACE_SIZE - 1);
        uint32_t v = g_icall_trace[idx], f = g_icall_from[idx];
        if (!v) continue;
        if (n && va[n - 1] == v && from[n - 1] == f) { rep[n - 1]++; continue; }
        va[n] = v; from[n] = f; rep[n] = 0; n++;
    }
    int m = 0;                                      /* pass 2: runs of a pair */
    for (int i = 0; i < n; i++) {
        if (m >= 2 && va[i] == va[m - 2] && from[i] == from[m - 2] && i + 1 < n &&
            va[i + 1] == va[m - 1] && from[i + 1] == from[m - 1]) {
            rep[m - 2] += rep[i] + 1; rep[m - 1] += rep[i + 1] + 1;
            i++;
            continue;
        }
        va[m] = va[i]; from[m] = from[i]; rep[m] = rep[i]; m++;
    }
    fprintf(stderr, "  recent indirect calls (oldest first):\n");
    for (int i = m > 24 ? m - 24 : 0; i < m; i++) {
        fprintf(stderr, "    %08X %-32s <- %08X %s", va[i], guest_name(va[i]), from[i], guest_name(from[i]));
        if (rep[i]) fprintf(stderr, "  (x%u)", rep[i] + 1);
        fprintf(stderr, "\n");
    }
    if (!m) fprintf(stderr, "    (none)\n");
    fprintf(stderr, "  total indirect calls: %u\n", g_icall_count);
}

/* ---- the guest instruction behind a generated line ------------------------ */

/* The generated line `...; / * 0x0045E46A: mov ebp, dword ptr [ecx + 4] * /`:
 * copy the guest address and instruction text into out. */
int crash_guest_insn_at(const char* file, unsigned line, char* out, size_t n) {
    FILE* f = fopen(file, "r");
    if (!f) return 0;
    static char buf[4096];
    int ok = 0;
    /* A statement spans lines (sbb blocks); the comment can be up to a few
     * lines below where the line table points. */
    for (unsigned i = 1; fgets(buf, sizeof buf, f); i++) {
        if (i < line) continue;
        char* c = strstr(buf, "/* 0x");
        if (c) {
            char* e = strstr(c, " */");
            if (e) *e = 0;
            snprintf(out, n, "%s", c + 3);
            ok = 1;
            break;
        }
        if (i > line + 4) break;
    }
    fclose(f);
    return ok;
}

/* ---- report --------------------------------------------------------------- */

void recomp_report_state(const char* why) {
    fprintf(stderr, "\n=== recomp: %s ===\n", why ? why : "state");
    /* The single most useful line: which lifted function was executing. Every
     * lifted body sets this on entry, with or without RECOMP_TRACE. */
    fprintf(stderr, "  current lifted function: sub_%08X %s\n", g_cur_func, guest_name(g_cur_func));
    dump_registers();
    dump_guest_stack();
    dump_icall_trace();
    recomp_dump_trace(why);   /* no-op unless built with RECOMP_TRACE */
    if (g_extra) g_extra();
    fflush(stderr);
}

void crash_report_fault(const char* kind, const void* host_pc, const void* host_base,
                        int has_addr, uintptr_t bad_addr, int is_write) {
    fprintf(stderr, "\n=== recomp: CRASH (%s) ===\n", kind);
    if (host_base)
        fprintf(stderr, "  host address: %p (exe+0x%llx)\n", (void*)host_pc,
                (unsigned long long)((uintptr_t)host_pc - (uintptr_t)host_base));
    else if (host_pc)
        fprintf(stderr, "  host address: %p\n", (void*)host_pc);
    else
        fprintf(stderr, "  host address: unknown\n");
    if (has_addr) {
        const char* op = is_write < 0 ? "access" : is_write ? "write" : "read";
        /* WINDREAM: guest memory is an arena at g_mem_base; name the guest VA. */
        uintptr_t gva = bad_addr - (uintptr_t)g_mem_base;
        fprintf(stderr, "  bad %s of %p (guest VA %08X) %s\n", op, (void*)bad_addr,
                (uint32_t)gva, gva <= 0xFFFFFFFFu ? region_of((uint32_t)gva) : "outside arena");
    }
    fflush(stderr);
}

