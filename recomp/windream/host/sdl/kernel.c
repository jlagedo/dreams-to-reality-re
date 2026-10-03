/*
 * WINDREAM recompilation - KERNEL32 process, CRT and console bridges (SDL3).
 *
 * What the Watcom CRT asks for on the way to WinMain: version, command line,
 * environment, module handle and name, code pages, std handles, console mode,
 * the unhandled-exception filter. The guest sees itself as C:\WINDREAM.EXE.
 *
 * The last-error value is the guest's own, one per thread: the bridges set it
 * where Win32 would, and nothing the host calls can change it.
 */
#define RECOMP_GENERATED_CODE
#include "host.h"

#define GUEST_EXE "C:\\WINDREAM.EXE"

RECOMP_TLS uint32_t g_last_error;

void imp_GetVersion(void) { RET(0x05650004u); STDRET(0); }          /* Windows NT 4.0 build 1381 */
void imp_GetACP(void)     { RET(1252); STDRET(0); }
void imp_GetOEMCP(void)   { RET(437);  STDRET(0); }
void imp_GetCPInfo(void) {  /* (codepage, CPINFO*) 20 bytes */
    uint32_t p = ARG(1);
    memset(wd_host_range(p, 20, 1), 0, 20);
    WD_HOST_WRITE32(p) = 1; WD_HOST_WRITE8(p + 4) = '?';
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
        for (uint32_t i = 0; i < n; i++) WD_HOST_WRITE16(va + i * 2) = (uint16_t)(uint8_t)s[i];
    }
    RET(va); STDRET(0);
}
static int SDLCALL env_order(const void* a, const void* b) {
    return SDL_strcasecmp(*(const char* const*)a, *(const char* const*)b);
}
void imp_GetEnvironmentStrings(void) {
    static uint32_t va;
    if (!va) {
        /* WD=1, plus host variables named T_* (difftest hooks such as T_ONLY),
         * in name order as Windows keeps its block; the rest of the host
         * environment stays out of the guest's. */
        char buf[4096];
        size_t n = 0, count = 0;
        memcpy(buf, "WD=1", 5);
        n = 5;
        char** env = SDL_GetEnvironmentVariables(SDL_GetEnvironment());
        for (char** e = env; e && *e; e++) count++;
        if (count) SDL_qsort(env, count, sizeof *env, env_order);
        for (char** e = env; e && *e; e++)
            if (!strncmp(*e, "T_", 2) && n + strlen(*e) + 2 < sizeof buf) {
                memcpy(buf + n, *e, strlen(*e) + 1);
                n += strlen(*e) + 1;
            }
        SDL_free(env);
        buf[n++] = 0;
        va = shim_alloc((uint32_t)n, 16);
        if (n > 6) fprintf(stderr, "[kernel] guest environment: WD=1 %s ...\n", buf + 5);
        memcpy(wd_host_range(va, n, 1), buf, n);
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
    for (uint32_t i = 0; i < n; i++) WD_HOST_WRITE16(buf + i * 2) = (uint16_t)(uint8_t)s[i];
    if (cap) WD_HOST_WRITE16(buf + n * 2) = 0;
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

void imp_GetLastError(void) { RET(g_last_error); STDRET(0); }
void imp_SetLastError(void) { g_last_error = ARG(0); STDRET(1); }
void imp_SetUnhandledExceptionFilter(void) { RET(0); STDRET(1); }
void imp_UnhandledExceptionFilter(void) { RET(0); STDRET(1); }   /* EXCEPTION_CONTINUE_SEARCH */
void imp_ExitProcess(void) {
    fprintf(stderr, "[kernel] ExitProcess(%u) from sub_%08X\n", ARG(0), g_cur_func);
    /* As ExitProcess does: no atexit handlers, the other threads just stop. */
    wd_web_status("exit", "the game was closed");
    fflush(NULL);
    _Exit((int)ARG(0));
}
void imp_QueryPerformanceCounter(void) {
    uint64_t t = SDL_GetPerformanceCounter();
    WD_HOST_WRITE32(ARG(0)) = (uint32_t)t; WD_HOST_WRITE32(ARG(0) + 4) = (uint32_t)(t >> 32);
    RET(1); STDRET(1);
}

/* ---- std handles and the console: the game is a GUI program ----
 * The handle's object is 1 for input, 2 for output, 3 for error (files.c writes
 * the last two to the host's stdout and stderr). */
void imp_GetStdHandle(void) {
    static uint32_t std[3];
    uint32_t which = ARG(0), k = which == W32_STD_INPUT_HANDLE ? 0 : which == W32_STD_OUTPUT_HANDLE ? 1 : 2;
    if (!std[k]) std[k] = handle_new((void*)(uintptr_t)(k + 1), HK_STD);
    RET(std[k]); STDRET(1);
}
void imp_SetStdHandle(void) { RET(1); STDRET(2); }
void imp_GetConsoleMode(void) { RET(0); STDRET(2); }
void imp_SetConsoleMode(void) { RET(0); STDRET(2); }
void imp_SetConsoleCtrlHandler(void) { RET(1); STDRET(2); }
void imp_PeekConsoleInputA(void) { if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = 0; RET(0); STDRET(4); }
void imp_ReadConsoleInputA(void) { if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = 0; RET(0); STDRET(4); }
void imp_WriteConsoleA(void) {  /* (h, buf, n, *written, reserved) */
    fwrite(wd_host_range(ARG(1), ARG(2), 0), 1, ARG(2), stderr);
    if (ARG(3)) WD_HOST_WRITE32(ARG(3)) = ARG(2);
    RET(1); STDRET(5);
}

/* ---- strings ----
 * The two code pages the guest is told it has (GetACP 1252, GetOEMCP 437),
 * whatever the host's are. Both are single-byte and ASCII below 0x80; the
 * tables are Windows' own mappings, including the five bytes 1252 leaves
 * undefined (they map to the C1 control of the same value). A character
 * outside the page becomes the default character: Windows' best-fit
 * substitutions (an unaccented letter for an accented one the page lacks) are
 * not reproduced. */
static const uint16_t g_cp1252[32] = {   /* 0x80..0x9F; 0xA0..0xFF are Latin-1 */
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};
static const uint16_t g_cp437[128] = {   /* 0x80..0xFF */
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};
/* The page a Win32 code page argument names here, or 0. */
static int code_page(uint32_t cp) {
    if (cp == W32_CP_ACP || cp == W32_CP_THREAD_ACP || cp == 1252) return 1252;
    if (cp == W32_CP_OEMCP || cp == 437) return 437;
    return 0;
}
static uint16_t to_wide(int page, uint8_t c) {
    if (c < 0x80) return c;
    if (page == 437) return g_cp437[c - 0x80];
    return c < 0xA0 ? g_cp1252[c - 0x80] : c;
}
static int to_byte(int page, uint16_t w, uint8_t* out) {
    if (w < 0x80) { *out = (uint8_t)w; return 1; }
    if (page == 1252 && w >= 0xA0 && w <= 0xFF) { *out = (uint8_t)w; return 1; }
    const uint16_t* table = page == 437 ? g_cp437 : g_cp1252;
    for (int i = 0, n = page == 437 ? 128 : 32; i < n; i++)
        if (table[i] == w) { *out = (uint8_t)(0x80 + i); return 1; }
    return 0;
}
/* Elements in a source of `count` (negative: up to and including its NUL),
 * `width` bytes each. NUL termination is an actual read boundary, not a guessed range. */
static uint32_t source_length(uint32_t address, int count, unsigned width) {
    if (count >= 0) {
        if (count) wd_host_range(address, (size_t)count * width, 0);
        return (uint32_t)count;
    }
    uint32_t n = 1;
    for (uint32_t cursor = address;; cursor += width, n++) {
        if (!(width == 1 ? WD_HOST_READ8(cursor) : WD_HOST_READ16(cursor))) return n;
        if (cursor > UINT32_MAX - width) abort();
    }
}
void imp_MultiByteToWideChar(void) {  /* (cp, flags, src, srcLen, dst, dstLen) */
    int page = code_page(ARG(0)), capacity = (int)ARG(5);
    uint32_t src = ARG(2), dst = ARG(4), n = 0;
    /* Invalid in Win32: an empty source, a destination size with no
     * destination, converting in place; and here a page that is not the guest's. */
    if (!page || !ARG(3) || capacity < 0 || (capacity > 0 && (!dst || src == dst))) {
        g_last_error = W32_ERROR_INVALID_PARAMETER;
    } else {
        n = source_length(src, (int)ARG(3), 1);
        if (capacity && n > (uint32_t)capacity) {
            g_last_error = W32_ERROR_INSUFFICIENT_BUFFER; n = 0;   /* nothing is written */
        } else if (capacity) {
            uint16_t* out = (uint16_t*)wd_host_range(dst, (size_t)n * 2, 1);
            for (uint32_t i = 0; i < n; i++) out[i] = to_wide(page, WD_HOST_READ8(src + i));
        }
    }
    RET(n); STDRET(6);
}
void imp_WideCharToMultiByte(void) {  /* (cp, flags, src, srcLen, dst, dstLen, defChar, usedDef) */
    int page = code_page(ARG(0)), capacity = (int)ARG(5);
    uint32_t src = ARG(2), dst = ARG(4), n = 0;
    if (!page || !ARG(3) || capacity < 0 || (capacity > 0 && (!dst || src == dst))) {
        g_last_error = W32_ERROR_INVALID_PARAMETER;
    } else {
        n = source_length(src, (int)ARG(3), 2);
        if (capacity && n > (uint32_t)capacity) {
            g_last_error = W32_ERROR_INSUFFICIENT_BUFFER; n = 0;
        } else {
            uint8_t replacement = ARG(6) ? WD_HOST_READ8(ARG(6)) : '?';
            uint8_t* out = capacity ? (uint8_t*)wd_host_range(dst, n, 1) : NULL;
            uint32_t used = 0;
            for (uint32_t i = 0; i < n; i++) {
                uint8_t c;
                if (!to_byte(page, WD_HOST_READ16(src + i * 2), &c)) { c = replacement; used = 1; }
                if (out) out[i] = c;
            }
            if (ARG(7)) WD_HOST_WRITE32(ARG(7)) = used;
        }
    }
    RET(n); STDRET(8);
}

/* ---- file mappings ----
 * SYS_CreateInstanceMapping (0x445fd2): a 32-byte pagefile mapping named
 * "Dreams to Reality", the single-instance marker; nothing ever maps a view
 * (WINDREAM imports no MapViewOfFile). Here a mapping is a name counted
 * inside this process, so a second CreateFileMappingA of a live name reports
 * ERROR_ALREADY_EXISTS as Win32 does. Two running copies of the game do not
 * see each other's: SDL has no object shared between processes. */
typedef struct Mapping { struct Mapping* next; int refs; char name[W32_MAX_PATH]; } Mapping;
static Mapping* g_mappings;
static SDL_SpinLock g_mapping_lock;

void mapping_release(void* host) {
    Mapping* m = (Mapping*)host;
    SDL_LockSpinlock(&g_mapping_lock);
    int last = --m->refs == 0;
    for (Mapping** link = &g_mappings; last && *link; link = &(*link)->next)
        if (*link == m) { *link = m->next; break; }
    SDL_UnlockSpinlock(&g_mapping_lock);
    if (last) SDL_free(m);
}
void imp_CreateFileMappingA(void) {  /* (file, sa, protect, sizeHigh, sizeLow, name) */
    char name[W32_MAX_PATH];
    guest_str(ARG(5), name, sizeof name);
    Mapping* fresh = (Mapping*)SDL_calloc(1, sizeof *fresh);
    Mapping* m = NULL;
    SDL_LockSpinlock(&g_mapping_lock);
    for (m = name[0] ? g_mappings : NULL; m; m = m->next)   /* an unnamed mapping is always new */
        if (!strcmp(m->name, name)) break;
    uint32_t error = m ? W32_ERROR_ALREADY_EXISTS : 0;
    if (!m) { m = fresh; fresh = NULL; strcpy(m->name, name); m->next = g_mappings; g_mappings = m; }
    m->refs++;
    SDL_UnlockSpinlock(&g_mapping_lock);
    SDL_free(fresh);
    uint32_t gh = handle_new(m, HK_MAP);
    if (!gh) { mapping_release(m); error = W32_ERROR_NOT_ENOUGH_MEMORY; }
    g_last_error = error;
    RET(gh); STDRET(6);
}
