/*
 * WINDREAM recompilation - guest handles, threads, synchronisation, TLS.
 *
 * Guest handles are small numbers into one table; each entry owns a host
 * HANDLE (file, find handle, thread, event, std handle). A guest thread runs on
 * a real host thread with its own register file (RECOMP_TLS), stack and TIB in
 * the arena, and enters the lifted thread procedure through the dispatch table
 * (after pod-recomp's thread_shim.c).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <SDL3/SDL_timer.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

/* ---- handle table ---- */
#define H_BASE 0x00001000u
#define H_MAX  1024
enum { HK_THREAD = 4, HK_EVENT = 5 };
static struct { int kind; HANDLE h; uint32_t proc, param, stack, tib; } g_h[H_MAX];
static SRWLOCK g_h_lock = SRWLOCK_INIT;

uint32_t handle_new(void* host, int kind) {
    uint32_t gh = 0;
    AcquireSRWLockExclusive(&g_h_lock);
    for (int i = 1; i < H_MAX; i++)
        if (!g_h[i].kind) { g_h[i].kind = kind; g_h[i].h = host; gh = H_BASE + (uint32_t)i * 4; break; }
    ReleaseSRWLockExclusive(&g_h_lock);
    if (!gh) fprintf(stderr, "[handles] table full\n");
    return gh;
}
static int h_index(uint32_t gh) {
    if (gh < H_BASE || (gh - H_BASE) % 4) return -1;
    uint32_t i = (gh - H_BASE) / 4;
    return i < H_MAX && g_h[i].kind ? (int)i : -1;
}
void* handle_get(uint32_t gh, int kind) {
    int i = h_index(gh);
    return i >= 0 && (!kind || g_h[i].kind == kind) ? g_h[i].h : NULL;
}
void handle_close(uint32_t gh) {
    int i = h_index(gh);
    if (i < 0) return;
    if (g_h[i].kind == HK_FIND) FindClose(g_h[i].h);
    else if (g_h[i].kind != HK_STD && g_h[i].h) CloseHandle(g_h[i].h);
    g_h[i].kind = 0; g_h[i].h = NULL;
}
static HANDLE any_handle(uint32_t gh) {
    if (gh == 0xFFFFFFFEu) return GetCurrentThread();
    if (gh == 0xFFFFFFFFu) return GetCurrentProcess();
    return (HANDLE)handle_get(gh, 0);
}

void imp_CloseHandle(void) { uint32_t gh = ARG(0); int ok = h_index(gh) >= 0; handle_close(gh); RET(ok); STDRET(1); }

/* ---- threads ---- */
#define T_STACK 0x00100000u

static void init_thread_state(uint32_t tib, uint32_t stack, uint32_t size) {
    g_fpu_cw = 0x027F; g_fp_top = 0;
    g_fs_base = tib;
    WD_HOST_WRITE32(tib + 0x00) = 0xFFFFFFFFu;     /* SEH chain end    */
    WD_HOST_WRITE32(tib + 0x04) = stack + size;    /* stack base (top) */
    WD_HOST_WRITE32(tib + 0x08) = stack;           /* stack limit      */
    WD_HOST_WRITE32(tib + 0x18) = tib;             /* self             */
    g_esp = stack + size - 64;
}

void wd_thread_init_main(void) {
    VirtualAlloc(PTR(WD_TIB_BASE), 0x1000, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(PTR(WD_STACK_BASE), WD_STACK_SIZE, MEM_COMMIT, PAGE_READWRITE);
    init_thread_state(WD_TIB_BASE, WD_STACK_BASE, WD_STACK_SIZE);
}

static unsigned __stdcall thread_main(void* arg) {
    int i = (int)(intptr_t)arg;
    init_thread_state(g_h[i].tib, g_h[i].stack, T_STACK);
    PUSH32(g_esp, g_h[i].param);          /* stdcall ThreadProc(lpParameter) */
    PUSH32(g_esp, RECOMP_RETADDR);
    recomp_func_t fn = recomp_lookup(g_h[i].proc);
    fprintf(stderr, "[thread] tid %lu runs 0x%08X(0x%08X)\n", GetCurrentThreadId(), g_h[i].proc, g_h[i].param);
    if (fn) fn(); else fprintf(stderr, "[thread] proc 0x%08X not lifted\n", g_h[i].proc);
    return g_eax;
}

void imp_CreateThread(void) {  /* (attr, stackSize, start, param, flags, pTid) */
    uint32_t gh = handle_new(NULL, HK_THREAD);
    int i = h_index(gh);
    g_h[i].proc = ARG(2); g_h[i].param = ARG(3);
    g_h[i].stack = shim_alloc(T_STACK, 0x1000);
    g_h[i].tib = shim_alloc(0x1000, 0x1000);
    unsigned tid = 0;
    g_h[i].h = (HANDLE)_beginthreadex(NULL, 0, thread_main, (void*)(intptr_t)i,
                                      (ARG(4) & CREATE_SUSPENDED) ? CREATE_SUSPENDED : 0, &tid);
    if (ARG(5)) WD_HOST_WRITE32(ARG(5)) = tid;
    RET(gh); STDRET(6);
}
void imp_ExitThread(void) { _endthreadex(ARG(0)); }
void imp_GetCurrentThreadId(void) { RET(GetCurrentThreadId()); STDRET(0); }
void imp_GetCurrentThread(void) { RET(0xFFFFFFFEu); STDRET(0); }
void imp_GetCurrentProcessId(void) { RET(GetCurrentProcessId()); STDRET(0); }
void imp_Sleep(void) { SDL_Delay(ARG(0)); STDRET(1); }   /* 1 ms precision (SDL sets the timer resolution) */

/* ---- events and waits ---- */
void imp_CreateEventA(void) {  /* (attr, manualReset, initialState, name) */
    RET(handle_new(CreateEventA(NULL, (BOOL)ARG(1), (BOOL)ARG(2), NULL), HK_EVENT)); STDRET(4);
}
void imp_SetEvent(void) { HANDLE h = any_handle(ARG(0)); RET(h ? SetEvent(h) : 0); STDRET(1); }
void imp_WaitForSingleObject(void) {
    HANDLE h = any_handle(ARG(0));
    RET(h ? WaitForSingleObject(h, ARG(1)) : WAIT_FAILED); STDRET(2);
}

/* ---- critical sections: guest CRITICAL_SECTION address -> host object ---- */
#define CS_MAX 512
static struct { uint32_t va; CRITICAL_SECTION cs; } g_cs[CS_MAX];
static int g_cs_n;
static SRWLOCK g_cs_lock = SRWLOCK_INIT;
static CRITICAL_SECTION* cs_for(uint32_t va) {
    CRITICAL_SECTION* r = NULL;
    AcquireSRWLockExclusive(&g_cs_lock);
    for (int i = 0; i < g_cs_n; i++) if (g_cs[i].va == va) { r = &g_cs[i].cs; break; }
    if (!r && g_cs_n < CS_MAX) { g_cs[g_cs_n].va = va; InitializeCriticalSection(&g_cs[g_cs_n].cs); r = &g_cs[g_cs_n++].cs; }
    ReleaseSRWLockExclusive(&g_cs_lock);
    return r;
}
void imp_InitializeCriticalSection(void) { cs_for(ARG(0)); STDRET(1); }
void imp_DeleteCriticalSection(void) { STDRET(1); }
void imp_EnterCriticalSection(void) { EnterCriticalSection(cs_for(ARG(0))); STDRET(1); }
void imp_LeaveCriticalSection(void) { LeaveCriticalSection(cs_for(ARG(0))); STDRET(1); }

/* ---- TLS: guest slot -> host TLS index holding the guest value ---- */
void imp_TlsAlloc(void) { RET(TlsAlloc()); STDRET(0); }
void imp_TlsFree(void) { RET(TlsFree(ARG(0))); STDRET(1); }
void imp_TlsGetValue(void) { RET((uint32_t)(uintptr_t)TlsGetValue(ARG(0))); STDRET(1); }
void imp_TlsSetValue(void) { RET(TlsSetValue(ARG(0), (void*)(uintptr_t)ARG(1))); STDRET(2); }
