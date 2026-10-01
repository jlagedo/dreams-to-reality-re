/*
 * crash_win32.c - the crash handler on Windows: a vectored exception handler.
 * See crash_report.h; the report itself is crash_report.c.
 *
 * WINDREAM additions:
 *   - host stack walk (dbghelp): the chain of lifted functions, each with the
 *     guest instruction it was executing, read from the generated C line's
 *     `/ * 0x...: insn * /` comment (needs the build's PDB; line tables suffice);
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
#include "crash_internal.h"
#include "imports.h"
#include "guest_win32.h"

/* ---- host stack: lifted functions and the guest instruction in each ------- */

static int g_sym_ok;

static void sym_init(void) {
    static int done;
    if (done) return;
    done = 1;
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS);
    g_sym_ok = SymInitialize(GetCurrentProcess(), NULL, TRUE);
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
            crash_guest_insn_at(li.FileName, li.LineNumber, insn, sizeof insn);
        unsigned long va = 0;
        if (!strncmp(fn, "sub_", 4)) va = strtoul(fn + 4, NULL, 16);
        if (va) snprintf(line, sizeof line, "%s %s  at %s", fn, crash_guest_name((uint32_t)va), insn);
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

/* ---- handler -------------------------------------------------------------- */

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

    int has_addr = code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2;
    crash_report_fault(name, ep->ExceptionRecord->ExceptionAddress, GetModuleHandleA(NULL), has_addr,
                       has_addr ? (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1] : 0,
                       has_addr && ep->ExceptionRecord->ExceptionInformation[0]);
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
