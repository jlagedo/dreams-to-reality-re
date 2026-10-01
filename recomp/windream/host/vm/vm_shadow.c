/*
 * WINDREAM recompilation - virtual memory, two implementations side by side.
 *
 * This is how the ledger (vm_ledger.c) is proven against Windows (vm_win32.c)
 * on a real play session. Every question the host or the guest asks is asked
 * of both: win32 is the primary and owns the memory (its writes to guest memory
 * stand, its answers are returned), the ledger is asked the same thing with the
 * same arguments right after, and each disagreement is printed with the call
 * that caused it and both answers. After every call that changes page state the
 * two page maps are also walked side by side over the whole arena.
 *
 * WD_VM_SHADOW=abort stops at the first disagreement with a crash report
 * (recomp_report_state), so the guest's state at that moment can be read.
 * Otherwise the run goes on and a count is printed at exit.
 */
#include "vm_impl.h"
#include "crash_report.h"
#include <SDL3/SDL_atomic.h>
#include <stdarg.h>

VM_DECLARE(vm_win32_)
VM_DECLARE(vm_ledger_)

/* One lock around the primary and secondary call of every entry point, the
 * read-only ones included. Both implementations are deterministic bump
 * allocators, so the ledger must see the calls in exactly the order win32 saw
 * them: without the lock two guest threads could interleave as A-primary,
 * B-primary, A-secondary, B-secondary and the two would hand out each other's
 * addresses, which is a reported mismatch that is not a bug. Neither
 * implementation calls back into vm_*, so holding it across both cannot
 * deadlock. (The crash report can: its region describer calls vm_describe.
 * So the abort path reports only after the lock is released, and describe
 * does not wait for the lock forever; see vm_impl_describe.) */
static SDL_SpinLock g_lock;

#define SHADOW_DETAIL_MAX 200u   /* mismatches printed in full; the rest are counted */
#define SHADOW_WALK_MAX 100000   /* runs per audit; guards against a zero-length run bug */

static unsigned g_checks, g_mismatches;
static int g_inited, g_abort_mode, g_abort_pending, g_aborting;

static void shadow_summary(void) {
    if (g_mismatches)
        fprintf(stderr, "[vm-shadow] %u mismatches in %u checks\n", g_mismatches, g_checks);
    else
        fprintf(stderr, "[vm-shadow] no mismatches in %u checks\n", g_checks);
}

/* Called with the lock held. The environment is read once, on first use. */
static void shadow_init(void) {
    const char* mode;
    if (g_inited) return;
    g_inited = 1;
    mode = getenv("WD_VM_SHADOW");
    g_abort_mode = mode && strcmp(mode, "abort") == 0;
    atexit(shadow_summary);
}

static void shadow_lock(void) {
    SDL_LockSpinlock(&g_lock);
    shadow_init();
}

/* Releases the lock, then stops the process if a mismatch asked for it. The
 * report runs outside the lock because it may ask vm_describe about addresses. */
static void shadow_unlock(void) {
    int stop = g_abort_pending && !g_aborting;
    if (stop) g_aborting = 1;
    SDL_UnlockSpinlock(&g_lock);
    if (stop) {
        recomp_report_state("vm shadow mismatch");
        shadow_summary();
        fflush(stderr);
        abort();
    }
}

/* One comparison made. */
static void shadow_check(void) { g_checks++; }

/* One comparison failed: print "[vm-shadow] MISMATCH <what>" for the first
 * SHADOW_DETAIL_MAX of them. Called with the lock held. */
static void shadow_mismatch(const char* fmt, ...) {
    va_list ap;
    g_mismatches++;
    if (g_mismatches <= SHADOW_DETAIL_MAX) {
        fprintf(stderr, "[vm-shadow] MISMATCH ");
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fputc('\n', stderr);
        if (g_mismatches == SHADOW_DETAIL_MAX)
            fprintf(stderr, "[vm-shadow] %u mismatches printed; further ones are only counted\n",
                    SHADOW_DETAIL_MAX);
    }
    if (g_abort_mode) g_abort_pending = 1;
}

static const char* state_name(uint32_t s) {
    return s == W32_MEM_COMMIT ? "commit" : s == W32_MEM_RESERVE ? "reserve" : s == W32_MEM_FREE ? "free" : "?";
}

/* Walks both page maps over the arena, run by run along win32's runs, and
 * reports every run on which the ledger answers differently. `after` names the
 * call that just changed the state. Called with the lock held. */
static void shadow_audit(const char* after) {
    uint64_t va = 0;
    int steps;
    for (steps = 0; va < WD_ARENA_SIZE && steps < SHADOW_WALK_MAX; steps++) {
        uint32_t b1 = 0, n1 = 0, b2 = 0, n2 = 0;
        uint32_t s1 = vm_win32_state((uint32_t)va, &b1, &n1);
        uint32_t s2 = vm_ledger_state((uint32_t)va, &b2, &n2);
        shadow_check();
        if (s1 != s2 || b1 != b2 || n1 != n2)
            shadow_mismatch("state %08X after %s: win32 %s %08X+0x%X, ledger %s %08X+0x%X",
                            (uint32_t)va, after, state_name(s1), b1, n1, state_name(s2), b2, n2);
        if (n1 == 0) break;
        va = (uint64_t)b1 + n1;
    }
    if (steps == SHADOW_WALK_MAX)
        shadow_mismatch("audit after %s: walk stopped after %d runs at %08X", after, SHADOW_WALK_MAX,
                        (uint32_t)va);
}

void* vm_impl_reserve(size_t bytes) {
    void *p, *q;
    shadow_lock();
    p = vm_win32_reserve(bytes);
    /* The ledger's operating-system layer is vm_os_null.c in this build, so
     * its pointer means nothing; only a failure is a disagreement. */
    q = vm_ledger_reserve(bytes);
    shadow_check();
    if ((p == NULL) != (q == NULL))
        shadow_mismatch("reserve 0x%zX: win32 %s, ledger %s", bytes, p ? "ok" : "NULL", q ? "ok" : "NULL");
    shadow_unlock();
    return p;
}

void vm_impl_commit(uint32_t va, uint32_t bytes) {
    char after[64];
    shadow_lock();
    vm_win32_commit(va, bytes);
    vm_ledger_commit(va, bytes);
    snprintf(after, sizeof after, "commit %08X 0x%X", va, bytes);
    shadow_audit(after);
    shadow_unlock();
}

uint32_t vm_impl_alloc(uint32_t addr, uint32_t size, uint32_t type, uint32_t prot) {
    uint32_t r1, r2;
    char after[96];
    shadow_lock();
    r1 = vm_win32_alloc(addr, size, type, prot);
    r2 = vm_ledger_alloc(addr, size, type, prot);
    shadow_check();
    if (r1 != r2)
        shadow_mismatch("alloc %08X 0x%X type=0x%X prot=0x%X: win32 %08X, ledger %08X", addr, size, type,
                        prot, r1, r2);
    snprintf(after, sizeof after, "alloc %08X 0x%X type=0x%X", addr, size, type);
    shadow_audit(after);
    shadow_unlock();
    return r1;
}

/* Each implementation sets g_last_error only on failure, so each starts from
 * the caller's value; the guest is left with win32's. */
int vm_impl_free(uint32_t addr, uint32_t size, uint32_t type) {
    int ok1, ok2;
    uint32_t before, e1, e2;
    char after[96];
    shadow_lock();
    before = g_last_error;
    ok1 = vm_win32_free(addr, size, type);
    e1 = g_last_error;
    g_last_error = before;
    ok2 = vm_ledger_free(addr, size, type);
    e2 = g_last_error;
    shadow_check();
    if (ok1 != ok2 || (!ok1 && e1 != e2))
        shadow_mismatch("free %08X 0x%X type=0x%X: win32 %d err=%u, ledger %d err=%u", addr, size, type, ok1,
                        ok1 ? 0u : e1, ok2, ok2 ? 0u : e2);
    g_last_error = e1;
    snprintf(after, sizeof after, "free %08X 0x%X type=0x%X", addr, size, type);
    shadow_audit(after);
    shadow_unlock();
    return ok1;
}

/* The guest's MEMORY_BASIC_INFORMATION, field by field (guest_win32.h). */
static const uint32_t k_mbi_off[7] = {
    W32_MBI_BASE, W32_MBI_ALLOCBASE, W32_MBI_ALLOCPROTECT, W32_MBI_REGIONSIZE,
    W32_MBI_STATE, W32_MBI_PROTECT, W32_MBI_TYPE,
};
static const char* const k_mbi_name[7] = {
    "BaseAddress", "AllocationBase", "AllocationProtect", "RegionSize", "State", "Protect", "Type",
};

/* Both write the MBI at the same guest address: the ledger's answer is read
 * back and then overwritten with win32's, which is what the guest sees. When
 * win32 answers 0 it wrote nothing, so there is nothing to restore; a ledger
 * that answers anyway is reported (and its MBI stays in guest memory). */
uint32_t vm_impl_query(uint32_t addr, uint32_t mbi) {
    uint32_t r1, r2, a[7], b[7];
    int i;
    shadow_lock();
    r1 = vm_win32_query(addr, mbi);
    if (r1)
        for (i = 0; i < 7; i++) a[i] = WD_HOST_READ32(mbi + k_mbi_off[i]);
    r2 = vm_ledger_query(addr, mbi);
    shadow_check();
    if (r1 != r2) shadow_mismatch("query %08X: win32 returned %u, ledger %u", addr, r1, r2);
    if (r1) {
        for (i = 0; i < 7; i++) b[i] = WD_HOST_READ32(mbi + k_mbi_off[i]);
        for (i = 0; i < 7; i++) WD_HOST_WRITE32(mbi + k_mbi_off[i]) = a[i];
        if (r2)
            for (i = 0; i < 7; i++)
                if (a[i] != b[i])
                    shadow_mismatch("query %08X: %s win32 0x%08X, ledger 0x%08X", addr, k_mbi_name[i], a[i],
                                    b[i]);
    }
    shadow_unlock();
    return r1;
}

uint32_t vm_impl_state(uint32_t va, uint32_t* run_base, uint32_t* run_bytes) {
    uint32_t s1, s2, b1 = 0, n1 = 0, b2 = 0, n2 = 0;
    shadow_lock();
    s1 = vm_win32_state(va, &b1, &n1);
    s2 = vm_ledger_state(va, &b2, &n2);
    shadow_check();
    if (s1 != s2 || b1 != b2 || n1 != n2)
        shadow_mismatch("state %08X: win32 %s %08X+0x%X, ledger %s %08X+0x%X", va, state_name(s1), b1, n1,
                        state_name(s2), b2, n2);
    shadow_unlock();
    if (run_base) *run_base = b1;
    if (run_bytes) *run_bytes = n1;
    return s1;
}

/* For crash reports, so it is win32's alone. The crash handler can run on a
 * thread that faulted while holding the lock (writing a bad MBI pointer in
 * query, say), so this tries the lock a bounded number of times and then
 * answers without it: vm_win32_describe takes no lock of its own. */
const char* vm_impl_describe(uint32_t va) {
    const char* s;
    int locked = 0, tries;
    for (tries = 0; tries < 100000 && !locked; tries++) locked = SDL_TryLockSpinlock(&g_lock);
    s = vm_win32_describe(va);
    if (locked) SDL_UnlockSpinlock(&g_lock);
    return s;
}

void vm_impl_dump(FILE* out) {
    shadow_lock();
    fprintf(out, "--- win32\n");
    vm_win32_dump(out);
    fprintf(out, "--- ledger\n");
    vm_ledger_dump(out);
    fprintf(out, "--- shadow: %u mismatches in %u checks\n", g_mismatches, g_checks);
    shadow_unlock();
}
