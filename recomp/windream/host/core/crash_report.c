/*
 * crash_report.c - crash diagnostics for recompiled 32-bit programs.
 * See crash_report.h.
 *
 * WINDREAM additions:
 *   - host stack walk (dbghelp): the chain of lifted functions, each with the
 *     guest instruction it was executing, read from the generated C line's
 *     `/ * 0x...: insn * /` comment (needs the build's PDB; line tables suffice);
 *   - Ghidra names for guest VAs, from re/symbols/<exe>.tsv (found by walking up
 *     from the host exe, or WD_SYMS=<tsv>);
 *   - what each register points at, and return addresses on the guest stack;
 *   - a minidump next to the logs: crash-<pid>.dmp, by default with all memory
 *     including the guest arena (WD_DUMP=mini for the small dump, =0 off).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "recomp_types.h"
#include "crash_report.h"
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
    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof dir);
    for (int up = 0; up < 8; up++) {
        char* s = strrchr(dir, '\\');
        if (!s) break;
        *s = 0;
        char p[MAX_PATH + 80];
        snprintf(p, sizeof p, "%s\\re\\symbols\\%s.tsv", dir, lower);
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

/* "Name+0x12" for a code VA inside a named function, "" otherwise. Two
 * rotating buffers so one printf can name two addresses. */
static void sym_init(void);
static int g_sym_ok;

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

static const char* guest_name(uint32_t va) {
    static char buf[2][80];
    static int k;
    /* an IAT slot: the bridge's host name (imp_CreateFileA) */
    for (uint32_t i = 0; i < wd_import_bridge_count; i++)
        if (wd_import_bridges[i].address == va) {
            union { SYMBOL_INFO s; char b[sizeof(SYMBOL_INFO) + 64]; } u;
            memset(&u, 0, sizeof u);
            u.s.SizeOfStruct = sizeof(SYMBOL_INFO);
            u.s.MaxNameLen = 63;
            sym_init();
            char* b = buf[k ^= 1];
            snprintf(b, sizeof buf[0], "[IAT %s]", g_sym_ok &&
                     SymFromAddr(GetCurrentProcess(), (DWORD64)(uintptr_t)wd_import_bridges[i].func, NULL, &u.s)
                     ? u.s.Name : "?");
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

/* ---- host stack: lifted functions and the guest instruction in each ------- */

static void sym_init(void) {
    static int done;
    if (done) return;
    done = 1;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
    g_sym_ok = SymInitialize(GetCurrentProcess(), NULL, TRUE);
}

/* The generated line `...; / * 0x0045E46A: mov ebp, dword ptr [ecx + 4] * /`:
 * copy the guest address and instruction text into out. */
static int guest_insn_at(const char* file, DWORD line, char* out, size_t n) {
    FILE* f = fopen(file, "r");
    if (!f) return 0;
    static char buf[4096];
    int ok = 0;
    /* A statement spans lines (sbb blocks); the comment can be up to a few
     * lines below where the line table points. */
    for (DWORD i = 1; fgets(buf, sizeof buf, f); i++) {
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

static void dump_host_stack(const CONTEXT* ctx) {
    sym_init();
    if (!g_sym_ok) { fprintf(stderr, "  (dbghelp unavailable: no host stack)\n"); return; }
    HANDLE proc = GetCurrentProcess(), thr = GetCurrentThread();
    CONTEXT c = *ctx;
    STACKFRAME64 sf;
    memset(&sf, 0, sizeof sf);
    sf.AddrPC.Offset = c.Rip; sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = c.Rbp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = c.Rsp; sf.AddrStack.Mode = AddrModeFlat;
    fprintf(stderr, "  host stack (lifted functions; innermost first):\n");
    char last[256] = "";
    int rep = 0, frames = 0;
    for (int depth = 0; depth < 400 && frames < 40; depth++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thr, &sf, &c, NULL,
                         SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        DWORD64 pc = sf.AddrPC.Offset;
        if (!pc) break;
        /* return addresses point after the call: look up the call itself */
        DWORD64 q = depth ? pc - 1 : pc;
        union { SYMBOL_INFO s; char b[sizeof(SYMBOL_INFO) + 256]; } u;
        memset(&u, 0, sizeof u);
        u.s.SizeOfStruct = sizeof(SYMBOL_INFO);
        u.s.MaxNameLen = 255;
        DWORD64 off = 0;
        const char* fn = SymFromAddr(proc, q, &off, &u.s) ? u.s.Name : "?";
        char line[512];
        IMAGEHLP_LINE64 li;
        memset(&li, 0, sizeof li);
        li.SizeOfStruct = sizeof li;
        DWORD disp = 0;
        char insn[200] = "";
        if (SymGetLineFromAddr64(proc, q, &disp, &li) && !strncmp(fn, "sub_", 4))
            guest_insn_at(li.FileName, li.LineNumber, insn, sizeof insn);
        unsigned long va = 0;
        if (!strncmp(fn, "sub_", 4)) va = strtoul(fn + 4, NULL, 16);
        if (va) snprintf(line, sizeof line, "%s %s  at %s", fn, guest_name((uint32_t)va), insn);
        else snprintf(line, sizeof line, "%s+0x%llx", fn, (unsigned long long)off);
        if (!strcmp(line, last)) { rep++; continue; }
        if (rep) fprintf(stderr, "      (same frame x%d more)\n", rep);
        rep = 0;
        fprintf(stderr, "    %s\n", line);
        snprintf(last, sizeof last, "%s", line);
        frames++;
        if (!strcmp(fn, "main")) break;
    }
    if (rep) fprintf(stderr, "      (same frame x%d more)\n", rep);
}

/* ---- minidump ------------------------------------------------------------- */

typedef struct { uint32_t lo[16], hi[16]; int n, i; } Ranges;

static BOOL CALLBACK dump_cb(PVOID param, const PMINIDUMP_CALLBACK_INPUT in, PMINIDUMP_CALLBACK_OUTPUT out) {
    Ranges* r = (Ranges*)param;
    if (in->CallbackType != MemoryCallback) return TRUE;
    while (r->i < r->n) {
        int k = r->i++;
        uint32_t lo = r->lo[k] & ~0xFFFu, hi = r->hi[k];
        /* only committed pages, or the whole dump fails */
        uint32_t base, bytes;
        if (vm_state(lo, &base, &bytes) != W32_MEM_COMMIT) continue;
        uintptr_t end = (uintptr_t)PTR(base) + bytes;
        uintptr_t want = (uintptr_t)PTR(hi);
        out->MemoryBase = (ULONG64)(uintptr_t)PTR(lo);
        out->MemorySize = (ULONG)((want < end ? want : end) - (uintptr_t)PTR(lo));
        return TRUE;
    }
    return FALSE;
}

static void add_range(Ranges* r, uint32_t lo, uint32_t hi) {
    if (r->n < 16 && hi > lo) { r->lo[r->n] = lo; r->hi[r->n] = hi; r->n++; }
}

static void write_minidump(EXCEPTION_POINTERS* ep) {
    const char* mode = getenv("WD_DUMP");
    if (mode && !strcmp(mode, "0")) return;
    int full = !(mode && !strcmp(mode, "mini"));
    char path[64];
    snprintf(path, sizeof path, "crash-%lu.dmp", GetCurrentProcessId());
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION ei = { GetCurrentThreadId(), ep, FALSE };
    /* Guest memory the host stack does not reference: the guest stack, the
     * image's data, and a window around every register. */
    Ranges r;
    memset(&r, 0, sizeof r);
    add_range(&r, g_esp - 0x1000, g_esp + 0x10000);
    add_range(&r, WD_IMAGE_BASE, WD_IMAGE_BASE + wd_image_span());
    uint32_t regs[] = { g_eax, g_ecx, g_edx, g_ebx, g_ebp, g_esi, g_edi };
    for (int i = 0; i < 7; i++)
        if (regs[i] >= 0x10000) add_range(&r, regs[i] - 0x800, regs[i] + 0x800);
    MINIDUMP_CALLBACK_INFORMATION cb = { dump_cb, &r };
    MINIDUMP_TYPE type = (MINIDUMP_TYPE)(full ? MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo
                                              : MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory |
                                                MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f, type, &ei, NULL, full ? NULL : &cb);
    CloseHandle(f);
    fprintf(stderr, "  minidump: %s (%s)%s\n", path, full ? "full memory" : "mini",
            ok ? "" : " FAILED");
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

static LONG WINAPI veh_handler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    const char* name;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      name = "access violation"; break;
    case EXCEPTION_STACK_OVERFLOW:        name = "stack overflow"; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    name = "integer divide by zero"; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:   name = "illegal instruction"; break;
    case EXCEPTION_IN_PAGE_ERROR:         name = "in-page error"; break;
    default:
        /* Not ours -- and in particular not a C++/SEH exception being used for
         * control flow, which we must not narrate. */
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static volatile LONG once;
    if (InterlockedExchange(&once, 1)) return EXCEPTION_CONTINUE_SEARCH;   /* a fault while reporting */

    fprintf(stderr, "\n=== recomp: CRASH (%s) ===\n", name);
    fprintf(stderr, "  host address: %p (exe+0x%llx)\n",
            (void*)ep->ExceptionRecord->ExceptionAddress,
            (unsigned long long)((uintptr_t)ep->ExceptionRecord->ExceptionAddress - (uintptr_t)GetModuleHandleA(NULL)));
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        uintptr_t bad = (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1];
        const char* op = ep->ExceptionRecord->ExceptionInformation[0]
                       ? "write" : "read";
        /* WINDREAM: guest memory is an arena at g_mem_base; name the guest VA. */
        uintptr_t gva = bad - (uintptr_t)g_mem_base;
        fprintf(stderr, "  bad %s of %p (guest VA %08X) %s\n", op, (void*)bad,
                (uint32_t)gva, gva <= 0xFFFFFFFFu ? region_of((uint32_t)gva) : "outside arena");
    }
    fflush(stderr);
    if (code != EXCEPTION_STACK_OVERFLOW) dump_host_stack(ep->ContextRecord);
    recomp_report_state("state at fault");
    write_minidump(ep);
    fflush(stderr);

    /* Report, then let it die: swallowing the fault would turn a crash into an
     * infinite loop of the same crash. */
    return EXCEPTION_CONTINUE_SEARCH;
}

void recomp_install_crash_handler(void) {
    static int installed = 0;
    if (installed) return;
    AddVectoredExceptionHandler(1, veh_handler);
    installed = 1;
}
