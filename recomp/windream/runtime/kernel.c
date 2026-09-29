/*
 * WINDREAM recompilation - KERNEL32 process, CRT and console bridges.
 *
 * What the Watcom CRT asks for on the way to WinMain: version, command line,
 * environment, module handle and name, code pages, std handles, console mode,
 * the unhandled-exception filter. The guest sees itself as C:\WINDREAM.EXE.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

#define GUEST_EXE "C:\\WINDREAM.EXE"

void imp_GetVersion(void) { RET(0x05650004u); STDRET(0); }          /* Windows NT 4.0 build 1381 */
void imp_GetACP(void)     { RET(1252); STDRET(0); }
void imp_GetOEMCP(void)   { RET(437);  STDRET(0); }
void imp_GetCPInfo(void) {  /* (codepage, CPINFO*) 20 bytes */
    uint32_t p = ARG(1);
    memset(PTR(p), 0, 20);
    MEM32(p) = 1; MEM8(p + 4) = '?';
    RET(1); STDRET(2);
}

void imp_GetCommandLineA(void) {
    static uint32_t va;
    if (!va) va = shim_strdup(GUEST_EXE);
    RET(va); STDRET(0);
}
void imp_GetCommandLineW(void) {
    static uint32_t va;
    if (!va) {
        const char* s = GUEST_EXE;
        uint32_t n = (uint32_t)strlen(s) + 1;
        va = shim_alloc(n * 2, 16);
        for (uint32_t i = 0; i < n; i++) MEM16(va + i * 2) = (uint16_t)(uint8_t)s[i];
    }
    RET(va); STDRET(0);
}
void imp_GetEnvironmentStrings(void) {
    static uint32_t va;
    if (!va) {
        /* WD=1, plus host variables named T_* (difftest hooks such as T_ONLY);
         * the rest of the host environment stays out of the guest's. */
        char buf[4096];
        size_t n = 0;
        memcpy(buf, "WD=1", 5);
        n = 5;
        char* env = GetEnvironmentStringsA();
        for (char* e = env; e && *e; e += strlen(e) + 1)
            if (!strncmp(e, "T_", 2) && n + strlen(e) + 2 < sizeof buf) {
                memcpy(buf + n, e, strlen(e) + 1);
                n += strlen(e) + 1;
            }
        if (env) FreeEnvironmentStringsA(env);
        buf[n++] = 0;
        va = shim_alloc((uint32_t)n, 16);
        if (n > 6) fprintf(stderr, "[kernel] guest environment: WD=1 %s ...\n", buf + 5);
        memcpy(PTR(va), buf, n);
    }
    RET(va); STDRET(0);
}
void imp_SetEnvironmentVariableA(void) { RET(1); STDRET(2); }
void imp_SetEnvironmentVariableW(void) { RET(1); STDRET(2); }

void imp_GetModuleHandleA(void) {
    char name[64];
    guest_str(ARG(0), name, sizeof name);
    if (ARG(0)) fprintf(stderr, "[kernel] GetModuleHandleA(\"%s\") -> 0\n", name);
    RET(ARG(0) ? 0 : WD_IMAGE_BASE); STDRET(1);
}
void imp_GetModuleFileNameA(void) {  /* (module, buf, size) */
    guest_strcpy_out(ARG(1), ARG(2), GUEST_EXE);
    RET(strlen(GUEST_EXE)); STDRET(3);
}
void imp_GetModuleFileNameW(void) {
    const char* s = GUEST_EXE;
    uint32_t buf = ARG(1), cap = ARG(2), n = (uint32_t)strlen(s);
    if (n >= cap) n = cap ? cap - 1 : 0;
    for (uint32_t i = 0; i < n; i++) MEM16(buf + i * 2) = (uint16_t)(uint8_t)s[i];
    if (cap) MEM16(buf + n * 2) = 0;
    RET(n); STDRET(3);
}
void imp_LoadLibraryA(void) {
    char name[128];
    guest_str(ARG(0), name, sizeof name);
    fprintf(stderr, "[kernel] LoadLibraryA(\"%s\") -> 0 (not provided)\n", name);
    RET(0); STDRET(1);
}
void imp_GetProcAddress(void) {
    char name[128];
    if (ARG(1) > 0xFFFF) guest_str(ARG(1), name, sizeof name); else sprintf(name, "#%u", ARG(1));
    fprintf(stderr, "[kernel] GetProcAddress(0x%08X, \"%s\") -> 0\n", ARG(0), name);
    RET(0); STDRET(2);
}

void imp_GetLastError(void) { RET(GetLastError()); STDRET(0); }
void imp_SetLastError(void) { SetLastError(ARG(0)); STDRET(1); }
void imp_SetUnhandledExceptionFilter(void) { RET(0); STDRET(1); }
void imp_UnhandledExceptionFilter(void) { RET(0); STDRET(1); }   /* EXCEPTION_CONTINUE_SEARCH */
void imp_ExitProcess(void) {
    fprintf(stderr, "[kernel] ExitProcess(%u) from sub_%08X\n", ARG(0), g_cur_func);
    fflush(stderr);
    ExitProcess(ARG(0));
}
void imp_QueryPerformanceCounter(void) {
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    MEM32(ARG(0)) = t.LowPart; MEM32(ARG(0) + 4) = (uint32_t)t.HighPart;
    RET(1); STDRET(1);
}

/* ---- std handles and the console: the game is a GUI program ---- */
void imp_GetStdHandle(void) {
    static uint32_t std[3];
    uint32_t which = ARG(0), k = which == (uint32_t)STD_INPUT_HANDLE ? 0 : which == (uint32_t)STD_OUTPUT_HANDLE ? 1 : 2;
    if (!std[k]) std[k] = handle_new(GetStdHandle(which), HK_STD);
    RET(std[k]); STDRET(1);
}
void imp_SetStdHandle(void) { RET(1); STDRET(2); }
void imp_GetConsoleMode(void) { RET(0); STDRET(2); }
void imp_SetConsoleMode(void) { RET(0); STDRET(2); }
void imp_SetConsoleCtrlHandler(void) { RET(1); STDRET(2); }
void imp_PeekConsoleInputA(void) { if (ARG(3)) MEM32(ARG(3)) = 0; RET(0); STDRET(4); }
void imp_ReadConsoleInputA(void) { if (ARG(3)) MEM32(ARG(3)) = 0; RET(0); STDRET(4); }
void imp_WriteConsoleA(void) {  /* (h, buf, n, *written, reserved) */
    fwrite(PTR(ARG(1)), 1, ARG(2), stderr);
    if (ARG(3)) MEM32(ARG(3)) = ARG(2);
    RET(1); STDRET(5);
}

/* ---- strings ---- */
void imp_MultiByteToWideChar(void) {  /* (cp, flags, src, srcLen, dst, dstLen) */
    int r = MultiByteToWideChar(ARG(0), ARG(1), (LPCCH)PTR(ARG(2)), (int)ARG(3),
                                ARG(4) ? (LPWSTR)PTR(ARG(4)) : NULL, (int)ARG(5));
    RET(r); STDRET(6);
}
void imp_WideCharToMultiByte(void) {  /* (cp, flags, src, srcLen, dst, dstLen, defChar, usedDef) */
    int r = WideCharToMultiByte(ARG(0), ARG(1), (LPCWCH)PTR(ARG(2)), (int)ARG(3),
                                ARG(4) ? (LPSTR)PTR(ARG(4)) : NULL, (int)ARG(5),
                                ARG(6) ? (LPCCH)PTR(ARG(6)) : NULL,
                                ARG(7) ? (LPBOOL)PTR(ARG(7)) : NULL);
    RET(r); STDRET(8);
}

/* SYS_CreateInstanceMapping (0x445fd2): a 32-byte pagefile mapping named
 * "Dreams to Reality", the single-instance marker. A real named mapping keeps
 * that behaviour (and GetLastError) across two running copies. */
void imp_CreateFileMappingA(void) {  /* (file, sa, protect, sizeHigh, sizeLow, name) */
    char name[MAX_PATH];
    guest_str(ARG(5), name, sizeof name);
    HANDLE file = ARG(0) == 0xFFFFFFFFu ? INVALID_HANDLE_VALUE : (HANDLE)handle_get(ARG(0), HK_FILE);
    HANDLE h = CreateFileMappingA(file, NULL, ARG(2), ARG(3), ARG(4), ARG(5) ? name : NULL);
    DWORD err = GetLastError();
    uint32_t gh = h ? handle_new(h, HK_MAP) : 0;
    SetLastError(err);
    RET(gh); STDRET(6);
}
