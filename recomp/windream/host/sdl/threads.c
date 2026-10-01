/*
 * WINDREAM recompilation - guest handles, threads, synchronisation, TLS (SDL3).
 *
 * Guest handles are small numbers into one table; each entry owns a host
 * object (file, find list, thread, event, mapping, std stream). A guest thread
 * runs on a real host thread with its own register file (RECOMP_TLS), stack and
 * TIB in the arena, and enters the lifted thread procedure through the dispatch
 * table (after pod-recomp's thread_shim.c).
 *
 * Win32 events and thread handles are waitable objects; SDL has neither, so
 * both are a flag under a mutex and condition. A thread's is manual-reset and
 * set when its procedure returns. WINDREAM imports no ResumeThread, so a
 * thread created suspended never runs, here as there.
 */
#include <setjmp.h>
#ifdef _WIN32
#include <process.h>
#define wd_getpid _getpid
#else
#include <unistd.h>
#define wd_getpid getpid
#endif
#define RECOMP_GENERATED_CODE
#include "host.h"

/* ---- handle table ---- */
#define H_BASE 0x00001000u
#define H_MAX  1024
enum { HK_THREAD = 4, HK_EVENT = 5 };
static struct { int kind; void* host; } g_h[H_MAX];
static SDL_SpinLock g_h_lock;

typedef struct {
    SDL_Mutex* lock;
    SDL_Condition* changed;
    int signaled, manual;
    uint32_t tid;          /* threads: the id CreateThread reports */
    SDL_AtomicInt refs;    /* the handle, a running thread, each waiter */
} Waitable;

static Waitable* waitable_new(int manual, int signaled) {
    Waitable* w = (Waitable*)SDL_calloc(1, sizeof *w);
    w->lock = SDL_CreateMutex();
    w->changed = SDL_CreateCondition();
    w->manual = manual; w->signaled = signaled;
    SDL_SetAtomicInt(&w->refs, 1);
    return w;
}
static void waitable_unref(Waitable* w) {
    if (!SDL_AtomicDecRef(&w->refs)) return;
    SDL_DestroyCondition(w->changed);
    SDL_DestroyMutex(w->lock);
    SDL_free(w);
}
static void waitable_signal(Waitable* w) {
    SDL_LockMutex(w->lock);
    w->signaled = 1;
    if (w->manual) SDL_BroadcastCondition(w->changed); else SDL_SignalCondition(w->changed);
    SDL_UnlockMutex(w->lock);
}
static uint32_t waitable_wait(Waitable* w, uint32_t ms) {
    SDL_LockMutex(w->lock);
    if (ms == W32_INFINITE) {
        while (!w->signaled) SDL_WaitCondition(w->changed, w->lock);
    } else {
        Uint64 end = SDL_GetTicksNS() + SDL_MS_TO_NS((Uint64)ms);
        while (!w->signaled) {
            Uint64 now = SDL_GetTicksNS();
            if (now >= end) break;
            Uint64 left = (end - now + 999999u) / 1000000u;   /* the timeout is a signed 32-bit count */
            SDL_WaitConditionTimeout(w->changed, w->lock, (Sint32)(left > 0x7FFFFFFFu ? 0x7FFFFFFFu : left));
        }
    }
    uint32_t r = w->signaled ? W32_WAIT_OBJECT_0 : W32_WAIT_TIMEOUT;
    if (w->signaled && !w->manual) w->signaled = 0;   /* auto-reset: one waiter takes it */
    SDL_UnlockMutex(w->lock);
    return r;
}

uint32_t handle_new(void* host, int kind) {
    uint32_t gh = 0;
    SDL_LockSpinlock(&g_h_lock);
    for (int i = 1; i < H_MAX; i++)
        if (!g_h[i].kind) { g_h[i].kind = kind; g_h[i].host = host; gh = H_BASE + (uint32_t)i * 4; break; }
    SDL_UnlockSpinlock(&g_h_lock);
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
    return i >= 0 && (!kind || g_h[i].kind == kind) ? g_h[i].host : NULL;
}
int handle_kind(uint32_t gh) {
    int i = h_index(gh);
    return i >= 0 ? g_h[i].kind : 0;
}
void* handle_acquire(uint32_t gh, int kind) {
    void* host = NULL;
    SDL_LockSpinlock(&g_h_lock);
    int i = h_index(gh);
    if (i >= 0 && g_h[i].kind == kind && (kind == HK_FILE || kind == HK_FIND)) {
        host = g_h[i].host;
        SDL_AtomicIncRef(&((WdObject*)host)->refs);
    }
    SDL_UnlockSpinlock(&g_h_lock);
    return host;
}
void handle_close(uint32_t gh) {
    SDL_LockSpinlock(&g_h_lock);
    int i = h_index(gh);
    /* A std handle keeps its slot: GetStdHandle hands the same number out
     * again, and it must not come to name a file opened in between. */
    if (i >= 0 && g_h[i].kind == HK_STD) i = -1;
    int kind = i >= 0 ? g_h[i].kind : 0;
    void* host = i >= 0 ? g_h[i].host : NULL;
    if (i >= 0) { g_h[i].kind = 0; g_h[i].host = NULL; }
    SDL_UnlockSpinlock(&g_h_lock);
    if (!host) return;
    if (kind == HK_FILE || kind == HK_FIND) files_release(kind, host);
    else if (kind == HK_MAP) mapping_release(host);
    else if (kind == HK_THREAD || kind == HK_EVENT) waitable_unref((Waitable*)host);
}
/* A waitable behind a handle, with a reference of its own: another thread may
 * close the handle while this one waits. */
static Waitable* waitable_get(uint32_t gh, int kind) {
    Waitable* w = NULL;
    SDL_LockSpinlock(&g_h_lock);
    int i = h_index(gh);
    if (i >= 0 && (g_h[i].kind == HK_EVENT || g_h[i].kind == HK_THREAD) && (!kind || g_h[i].kind == kind)) {
        w = (Waitable*)g_h[i].host;
        SDL_AtomicIncRef(&w->refs);
    }
    SDL_UnlockSpinlock(&g_h_lock);
    return w;
}

void imp_CloseHandle(void) {
    uint32_t gh = ARG(0);
    int ok = h_index(gh) >= 0;
    handle_close(gh);
    if (!ok) g_last_error = W32_ERROR_INVALID_HANDLE;
    RET(ok); STDRET(1);
}

/* ---- threads ---- */
#define T_STACK 0x00100000u

typedef struct { Waitable* done; uint32_t proc, param, stack, tib; SDL_Semaphore* started; } ThreadStart;
static RECOMP_TLS jmp_buf* t_exit;   /* ExitThread: back out to thread_main */

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
    vm_commit(WD_TIB_BASE, 0x1000);
    vm_commit(WD_STACK_BASE, WD_STACK_SIZE);
    init_thread_state(WD_TIB_BASE, WD_STACK_BASE, WD_STACK_SIZE);
}

static int SDLCALL thread_main(void* arg) {
    ThreadStart t = *(ThreadStart*)arg;
    jmp_buf leave;
    t.done->tid = (uint32_t)SDL_GetCurrentThreadID();
    SDL_SignalSemaphore(t.started);                /* arg belongs to CreateThread again */
    init_thread_state(t.tib, t.stack, T_STACK);
    g_last_error = 0;
    t_exit = &leave;
    if (!setjmp(leave)) {
        PUSH32(g_esp, t.param);                    /* stdcall ThreadProc(lpParameter) */
        PUSH32(g_esp, RECOMP_RETADDR);
        recomp_func_t fn = recomp_lookup(t.proc);
        fprintf(stderr, "[thread] tid %u runs 0x%08X(0x%08X)\n", t.done->tid, t.proc, t.param);
        if (fn) fn(); else fprintf(stderr, "[thread] proc 0x%08X not lifted\n", t.proc);
    }
    t_exit = NULL;
    int code = (int)g_eax;
    waitable_signal(t.done);
    waitable_unref(t.done);
    return code;
}

void imp_CreateThread(void) {  /* (attr, stackSize, start, param, flags, pTid) */
    ThreadStart t = { waitable_new(1, 0), ARG(2), ARG(3), 0, 0, NULL };
    uint32_t gh = handle_new(t.done, HK_THREAD);
    uint32_t tid = 0;
    if (!gh) {
        waitable_unref(t.done);
        g_last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
    } else if (ARG(4) & W32_CREATE_SUSPENDED) {
        fprintf(stderr, "[thread] 0x%08X created suspended: it never runs (no ResumeThread import)\n", t.proc);
    } else {
        t.stack = shim_alloc(T_STACK, 0x1000);
        t.tib = shim_alloc(0x1000, 0x1000);
        t.started = SDL_CreateSemaphore(0);
        SDL_AtomicIncRef(&t.done->refs);           /* the thread's own */
        SDL_Thread* thread = SDL_CreateThread(thread_main, "guest", &t);
        if (thread) {
            SDL_WaitSemaphore(t.started);          /* it has copied t and stored its id */
            SDL_DetachThread(thread);
            tid = t.done->tid;
        } else {
            fprintf(stderr, "[thread] cannot start 0x%08X: %s\n", t.proc, SDL_GetError());
            waitable_unref(t.done);
            handle_close(gh);
            gh = 0;
            g_last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        }
        SDL_DestroySemaphore(t.started);
    }
    if (ARG(5)) WD_HOST_WRITE32(ARG(5)) = tid;
    RET(gh); STDRET(6);
}
void imp_ExitThread(void) {
    g_eax = ARG(0);
    if (t_exit) longjmp(*t_exit, 1);
    /* The main thread: Win32 keeps the process until the last thread ends;
     * here leaving the main thread ends it. */
    fprintf(stderr, "[thread] ExitThread(%u) on the main thread: exiting\n", g_eax);
    fflush(NULL);
    _Exit((int)g_eax);
}
void imp_GetCurrentThreadId(void) { RET((uint32_t)SDL_GetCurrentThreadID()); STDRET(0); }
void imp_GetCurrentThread(void) { RET(W32_CURRENT_THREAD); STDRET(0); }
void imp_GetCurrentProcessId(void) { RET((uint32_t)wd_getpid()); STDRET(0); }
void imp_Sleep(void) { SDL_Delay(ARG(0)); STDRET(1); }   /* 1 ms precision (SDL sets the timer resolution) */

/* ---- events and waits ---- */
void imp_CreateEventA(void) {  /* (attr, manualReset, initialState, name): the name is not used */
    Waitable* w = waitable_new(ARG(1) != 0, ARG(2) != 0);
    uint32_t gh = handle_new(w, HK_EVENT);
    if (!gh) { waitable_unref(w); g_last_error = W32_ERROR_NOT_ENOUGH_MEMORY; }
    RET(gh); STDRET(4);
}
void imp_SetEvent(void) {
    Waitable* w = waitable_get(ARG(0), HK_EVENT);
    if (w) { waitable_signal(w); waitable_unref(w); }
    else g_last_error = W32_ERROR_INVALID_HANDLE;
    RET(w != NULL); STDRET(1);
}
void imp_WaitForSingleObject(void) {
    uint32_t gh = ARG(0), ms = ARG(1), r;
    Waitable* w = waitable_get(gh, 0);
    if (w) {
        r = waitable_wait(w, ms);
        waitable_unref(w);
    } else if ((gh == W32_CURRENT_THREAD || gh == W32_CURRENT_PROCESS) && ms != W32_INFINITE) {
        SDL_Delay(ms);                             /* neither ends while it waits on itself */
        r = W32_WAIT_TIMEOUT;
    } else {
        g_last_error = W32_ERROR_INVALID_HANDLE;
        r = W32_WAIT_FAILED;
    }
    RET(r); STDRET(2);
}

/* ---- critical sections: guest CRITICAL_SECTION address -> host mutex ----
 * An SDL mutex is recursive, as a critical section is. */
#define CS_MAX 512
static struct { uint32_t va; SDL_Mutex* lock; } g_cs[CS_MAX];
static int g_cs_n;
static SDL_SpinLock g_cs_lock;
static SDL_Mutex* cs_find(uint32_t va) {
    for (int i = 0; i < g_cs_n; i++) if (g_cs[i].va == va) return g_cs[i].lock;
    return NULL;
}
static SDL_Mutex* cs_for(uint32_t va) {
    SDL_LockSpinlock(&g_cs_lock);
    SDL_Mutex* r = cs_find(va);
    SDL_UnlockSpinlock(&g_cs_lock);
    if (r) return r;
    SDL_Mutex* made = SDL_CreateMutex();           /* outside the spinlock */
    SDL_LockSpinlock(&g_cs_lock);
    r = cs_find(va);
    if (!r && g_cs_n < CS_MAX) { g_cs[g_cs_n].va = va; g_cs[g_cs_n++].lock = r = made; made = NULL; }
    SDL_UnlockSpinlock(&g_cs_lock);
    if (made) SDL_DestroyMutex(made);
    if (!r) { fprintf(stderr, "[threads] critical section table full\n"); abort(); }
    return r;
}
void imp_InitializeCriticalSection(void) { cs_for(ARG(0)); STDRET(1); }
void imp_DeleteCriticalSection(void) { STDRET(1); }
void imp_EnterCriticalSection(void) { SDL_LockMutex(cs_for(ARG(0))); STDRET(1); }
void imp_LeaveCriticalSection(void) { SDL_UnlockMutex(cs_for(ARG(0))); STDRET(1); }

/* ---- TLS: guest slots, each thread's values in host thread-local storage ----
 * A slot's generation changes when it is allocated, so a value another thread
 * left in a freed slot reads as zero in the next owner's. */
#define TLS_MAX 64
static struct { uint32_t used, generation; } g_tls[TLS_MAX];
static SDL_SpinLock g_tls_lock;
static RECOMP_TLS struct { uint32_t generation, value; } t_tls[TLS_MAX];

static int tls_live(uint32_t i) { return i < TLS_MAX && g_tls[i].used; }
void imp_TlsAlloc(void) {
    uint32_t r = W32_TLS_OUT_OF_INDEXES;
    SDL_LockSpinlock(&g_tls_lock);
    for (uint32_t i = 0; i < TLS_MAX; i++)
        if (!g_tls[i].used) { g_tls[i].used = 1; g_tls[i].generation++; r = i; break; }
    SDL_UnlockSpinlock(&g_tls_lock);
    if (r == W32_TLS_OUT_OF_INDEXES) g_last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
    RET(r); STDRET(0);
}
void imp_TlsFree(void) {
    uint32_t i = ARG(0);
    SDL_LockSpinlock(&g_tls_lock);
    int ok = tls_live(i);
    if (ok) g_tls[i].used = 0;
    SDL_UnlockSpinlock(&g_tls_lock);
    if (!ok) g_last_error = W32_ERROR_INVALID_PARAMETER;
    RET(ok); STDRET(1);
}
void imp_TlsGetValue(void) {
    uint32_t i = ARG(0), r = 0;
    if (tls_live(i)) {
        if (t_tls[i].generation == g_tls[i].generation) r = t_tls[i].value;
        g_last_error = 0;                          /* a stored zero is not a failure */
    } else {
        g_last_error = W32_ERROR_INVALID_PARAMETER;
    }
    RET(r); STDRET(1);
}
void imp_TlsSetValue(void) {
    uint32_t i = ARG(0);
    int ok = tls_live(i);
    if (ok) { t_tls[i].generation = g_tls[i].generation; t_tls[i].value = ARG(1); }
    else g_last_error = W32_ERROR_INVALID_PARAMETER;
    RET(ok); STDRET(2);
}
