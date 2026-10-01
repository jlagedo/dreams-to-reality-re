/*
 * WINDREAM recompilation - virtual memory with Windows' page state as the truth.
 *
 * The original implementation (host/win32/vm.c), kept as the reference the
 * ledger is compared with (vm_shadow.c). It reserves the arena's address space
 * with VirtualAlloc, commits and decommits pages in it, and answers the guest's
 * VirtualQuery by asking VirtualQuery about the host pages. Windows only.
 *
 * The Watcom CRT gets its heap from VirtualAlloc (WINDREAM imports no HeapAlloc
 * or GlobalAlloc). Reservations are handed out 64 KB-aligned from the bottom of
 * the guest heap region and committed in the host arena, so guest VAs stay
 * below 4 GB on any host. Address space is not reused after MEM_RELEASE.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define RECOMP_GENERATED_CODE
#include "vm_impl.h"

static struct { uint32_t base, size, protect, freed; } g_vm[VM_MAX];  /* freed: size before MEM_RELEASE */
static int g_vm_n;
static uint32_t g_vm_next = WD_HEAP_BASE;
static SRWLOCK g_vm_lock = SRWLOCK_INIT;

void* VM_FN(reserve)(size_t bytes) { return VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_READWRITE); }
void VM_FN(commit)(uint32_t va, uint32_t bytes) {
    VirtualAlloc(PTR(va), bytes ? bytes : 1, MEM_COMMIT, PAGE_READWRITE);
}

static int vm_find(uint32_t va) {
    for (int i = 0; i < g_vm_n; i++)
        if (g_vm[i].size && va >= g_vm[i].base && va - g_vm[i].base < g_vm[i].size) return i;
    return -1;
}

uint32_t VM_FN(alloc)(uint32_t addr, uint32_t size, uint32_t type, uint32_t prot) {
    uint32_t r = 0;
    AcquireSRWLockExclusive(&g_vm_lock);
    if (!size) goto done;
    if (addr) {
        int i = vm_find(addr);
        if (i >= 0) {                       /* commit inside an existing reservation */
            uint32_t lo = addr & ~0xFFFu;
            uint32_t hi = (addr + size + 0xFFFu) & ~0xFFFu;
            if (hi > g_vm[i].base + g_vm[i].size) hi = g_vm[i].base + g_vm[i].size;
            if (type & MEM_COMMIT) VirtualAlloc(PTR(lo), hi - lo, MEM_COMMIT, PAGE_READWRITE);
            r = lo;
            goto done;
        }
        fprintf(stderr, "[vm] VirtualAlloc at fixed 0x%08X size 0x%X type 0x%X: not supported\n",
                addr, size, type);
        goto done;
    }
    size = (size + 0xFFFFu) & ~0xFFFFu;
    if (g_vm_n >= VM_MAX || g_vm_next + size > VM_ALLOC_LIMIT) {
        fprintf(stderr, "[vm] out of guest address space (0x%X requested)\n", size);
        goto done;
    }
    r = g_vm_next;
    g_vm_next += size;
    g_vm[g_vm_n].base = r; g_vm[g_vm_n].size = size; g_vm[g_vm_n].protect = prot; g_vm_n++;
    if (type & MEM_COMMIT) VirtualAlloc(PTR(r), size, MEM_COMMIT, PAGE_READWRITE);
done:
    ReleaseSRWLockExclusive(&g_vm_lock);
    return r;
}

int VM_FN(free)(uint32_t addr, uint32_t size, uint32_t type) {
    int ok = 0;
    DWORD error = ERROR_INVALID_ADDRESS;
    AcquireSRWLockExclusive(&g_vm_lock);
    int i = vm_find(addr);
    if (i >= 0) {
        if (type & MEM_RELEASE) {
            /* Guest release retains the enclosing host arena reservation. */
            ok = VirtualFree(PTR(g_vm[i].base), g_vm[i].size, MEM_DECOMMIT) != 0;
            error = GetLastError();
            if (ok) {
                VM_INVALIDATE(g_vm[i].base, g_vm[i].size);
                g_vm[i].freed = g_vm[i].size;
                g_vm[i].size = 0;
            }
        } else if (type & MEM_DECOMMIT) {
            uint32_t lo = addr & ~0xFFFu;
            uint64_t hi = ((uint64_t)addr + size + 0xFFFu) & ~0xFFFull;
            if (!size && addr == g_vm[i].base) hi = (uint64_t)addr + g_vm[i].size;
            /* Do not turn zero-size interior requests into an arena-wide free,
             * or let an overflowing/cross-allocation range decommit neighbors. */
            if ((size || addr == g_vm[i].base) && hi > lo &&
                hi <= (uint64_t)g_vm[i].base + g_vm[i].size) {
                uint32_t bytes = (uint32_t)(hi - lo);
                ok = VirtualFree(PTR(lo), bytes, MEM_DECOMMIT) != 0;
                error = GetLastError();
                if (ok) VM_INVALIDATE(lo, bytes);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_vm_lock);
    if (!ok) g_last_error = error;
    return ok;
}

/* For crash reports: which reservation holds va, and whether it is live. */
const char* VM_FN(describe)(uint32_t va) {
    static char buf[128];
    for (int i = 0; i < g_vm_n; i++) {
        uint32_t sz = g_vm[i].size ? g_vm[i].size : g_vm[i].freed;
        if (va >= g_vm[i].base && va - g_vm[i].base < sz) {
            MEMORY_BASIC_INFORMATION h;
            int committed = VirtualQuery(PTR(va), &h, sizeof h) && h.State == MEM_COMMIT;
            sprintf(buf, "(heap: region #%d 0x%08X+0x%X, %s, page %s)", i, g_vm[i].base, sz,
                    g_vm[i].size ? "live" : "RELEASED", committed ? "committed" : "not committed");
            return buf;
        }
    }
    return "(heap: no region)";
}

/* MEMORY_BASIC_INFORMATION, 32-bit layout (28 bytes). The host answers for the
 * arena's pages; a VA past the arena gets whatever the host maps there, which
 * the ledger reports as MEM_FREE instead (a shadow build shows the difference). */
uint32_t VM_FN(query)(uint32_t addr, uint32_t mbi) {
    MEMORY_BASIC_INFORMATION h;
    if (!VirtualQuery(PTR(addr), &h, sizeof h)) return 0;
    uint32_t base = (uint32_t)((uintptr_t)h.BaseAddress - (uintptr_t)g_mem_base);
    uint32_t abase = base;
    AcquireSRWLockShared(&g_vm_lock);
    int i = vm_find(addr);
    if (i >= 0) abase = g_vm[i].base;
    ReleaseSRWLockShared(&g_vm_lock);
    if (addr >= WD_IMAGE_BASE && addr < WD_IMAGE_BASE + wd_image_span()) abase = WD_IMAGE_BASE;
    if (addr >= WD_STACK_BASE && addr < WD_STACK_BASE + WD_STACK_SIZE) abase = WD_STACK_BASE;
    WD_HOST_WRITE32(mbi + W32_MBI_BASE) = base;
    WD_HOST_WRITE32(mbi + W32_MBI_ALLOCBASE) = abase;
    WD_HOST_WRITE32(mbi + W32_MBI_ALLOCPROTECT) = PAGE_READWRITE;
    WD_HOST_WRITE32(mbi + W32_MBI_REGIONSIZE) = (uint32_t)h.RegionSize;
    WD_HOST_WRITE32(mbi + W32_MBI_STATE) = h.State;
    WD_HOST_WRITE32(mbi + W32_MBI_PROTECT) = h.State == MEM_COMMIT ? PAGE_READWRITE : 0;
    WD_HOST_WRITE32(mbi + W32_MBI_TYPE) = h.State == MEM_FREE ? 0 : (abase == WD_IMAGE_BASE ? MEM_IMAGE : MEM_PRIVATE);
    return W32_MBI_SIZE;
}

/* The run of host pages in one state around va, clipped to the arena. */
uint32_t VM_FN(state)(uint32_t va, uint32_t* run_base, uint32_t* run_bytes) {
    uint32_t page = va & ~(VM_PAGE - 1);
    uint32_t state = W32_MEM_FREE, base = page, bytes = 0u - page;   /* to the end of the 32-bit space */
    MEMORY_BASIC_INFORMATION h;
    if (va < WD_ARENA_SIZE && VirtualQuery(PTR(va), &h, sizeof h)) {
        uintptr_t lo = (uintptr_t)h.BaseAddress, arena = (uintptr_t)g_mem_base;
        base = lo > arena ? (uint32_t)(lo - arena) : 0;
        uint64_t end = (uint64_t)base + h.RegionSize;
        if (end > WD_ARENA_SIZE) end = WD_ARENA_SIZE;
        bytes = (uint32_t)(end - base);
        state = h.State;
    }
    if (run_base) *run_base = base;
    if (run_bytes) *run_bytes = bytes;
    return state;
}

void VM_FN(dump)(FILE* out) {
    AcquireSRWLockShared(&g_vm_lock);
    fprintf(out, "regions %d next %08X\n", g_vm_n, g_vm_next);
    for (int i = 0; i < g_vm_n; i++)
        fprintf(out, "  #%d %08X+%X prot=%X %s\n", i, g_vm[i].base,
                g_vm[i].size ? g_vm[i].size : g_vm[i].freed, g_vm[i].protect,
                g_vm[i].size ? "live" : "released");
    ReleaseSRWLockShared(&g_vm_lock);
    uint64_t va = 0;
    while (va < WD_ARENA_SIZE) {
        uint32_t base, bytes;
        uint32_t state = VM_FN(state)((uint32_t)va, &base, &bytes);
        if (!bytes) break;
        fprintf(out, "  %08X+%X %s\n", base, bytes,
                state == W32_MEM_COMMIT ? "committed" : state == W32_MEM_RESERVE ? "reserved" : "free");
        va = (uint64_t)base + bytes;
    }
}
