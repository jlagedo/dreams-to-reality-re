/*
 * WINDREAM recompilation - host virtual memory, and VirtualAlloc/VirtualFree/
 * VirtualQuery.
 *
 * The one part of the KERNEL32 side that still stands on Win32: reserving the
 * arena's address space, committing and decommitting pages in it and asking
 * which pages are committed. SDL has no virtual memory interface.
 *
 * The Watcom CRT gets its heap from VirtualAlloc (WINDREAM imports no HeapAlloc
 * or GlobalAlloc). Reservations are handed out 64 KB-aligned from the bottom of
 * the guest heap region and committed in the host arena, so guest VAs stay
 * below 4 GB on any host. Address space is not reused after MEM_RELEASE.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define RECOMP_GENERATED_CODE
#include "imports.h"

#define VM_MAX 4096
static struct { uint32_t base, size, protect, freed; } g_vm[VM_MAX];  /* freed: size before MEM_RELEASE */
static int g_vm_n;
static uint32_t g_vm_next = WD_HEAP_BASE;
static SRWLOCK g_vm_lock = SRWLOCK_INIT;

void* vm_reserve(size_t bytes) { return VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_READWRITE); }
void vm_commit(uint32_t va, uint32_t bytes) {
    VirtualAlloc(PTR(va), bytes ? bytes : 1, MEM_COMMIT, PAGE_READWRITE);
}

static int vm_find(uint32_t va) {
    for (int i = 0; i < g_vm_n; i++)
        if (g_vm[i].size && va >= g_vm[i].base && va - g_vm[i].base < g_vm[i].size) return i;
    return -1;
}

uint32_t vm_alloc(uint32_t addr, uint32_t size, uint32_t type, uint32_t prot) {
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
    if (g_vm_n >= VM_MAX || g_vm_next + size > WD_HEAP_BASE + WD_HEAP_SIZE / 2) {
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

int vm_free(uint32_t addr, uint32_t size, uint32_t type) {
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
                wd_surface_invalidate_range(g_vm[i].base, g_vm[i].size);
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
                if (ok) wd_surface_invalidate_range(lo, bytes);
            }
        }
    }
    ReleaseSRWLockExclusive(&g_vm_lock);
    if (!ok) g_last_error = error;
    return ok;
}

/* For crash reports: which reservation holds va, and whether it is live. */
const char* vm_describe(uint32_t va) {
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

/* MEMORY_BASIC_INFORMATION, 32-bit layout (28 bytes). */
uint32_t vm_query(uint32_t addr, uint32_t mbi) {
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
    WD_HOST_WRITE32(mbi + 0) = base;
    WD_HOST_WRITE32(mbi + 4) = abase;
    WD_HOST_WRITE32(mbi + 8) = PAGE_READWRITE;
    WD_HOST_WRITE32(mbi + 12) = (uint32_t)h.RegionSize;
    WD_HOST_WRITE32(mbi + 16) = h.State;
    WD_HOST_WRITE32(mbi + 20) = h.State == MEM_COMMIT ? PAGE_READWRITE : 0;
    WD_HOST_WRITE32(mbi + 24) = h.State == MEM_FREE ? 0 : (abase == WD_IMAGE_BASE ? MEM_IMAGE : MEM_PRIVATE);
    return 28;
}

void imp_VirtualAlloc(void) { RET(vm_alloc(ARG(0), ARG(1), ARG(2), ARG(3))); STDRET(4); }
void imp_VirtualFree(void)  { RET(vm_free(ARG(0), ARG(1), ARG(2))); STDRET(3); }
void imp_VirtualQuery(void) { RET(vm_query(ARG(0), ARG(1))); STDRET(3); }
