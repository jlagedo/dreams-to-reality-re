/*
 * WINDREAM recompilation - virtual memory with the ledger as the truth.
 *
 * The portable implementation. It keeps the truth about the guest's pages
 * itself: the reservation table (the same one vm_win32.c keeps) and one bit
 * per 4 KB page of the arena, set while the page is committed. VirtualQuery,
 * vm_state and the crash reports are answered from these, never from the
 * host, so the answers are the same on every host. The operating system is
 * asked for three things only (vm_os.h): reserve the arena, commit a range,
 * decommit a range. Its page protection is a safety net that makes a stray
 * guest pointer fault, not the source of any answer.
 *
 * The answers follow what vm_win32.c gets from Windows for the same calls
 * (vm_shadow.c checks this call by call). Windows' VirtualQuery reports the
 * page holding the address as the region base and the run of pages in the
 * same state from there on, so vm_state does the same. Every page of the
 * arena that is not committed is reserved: the arena is one host
 * reservation, which is what Windows reports for it.
 */
#define RECOMP_GENERATED_CODE
#include "vm_impl.h"
#include "vm_os.h"
#include <SDL3/SDL_atomic.h>

static struct { uint32_t base, size, protect, freed; } g_vm[VM_MAX];  /* freed: size before MEM_RELEASE */
static int g_vm_n;
static uint32_t g_vm_next = WD_HEAP_BASE;

/* One bit per page of the arena, set while the page is committed. */
#define VM_PAGES (WD_ARENA_SIZE / VM_PAGE)
#define VM_WORDS ((VM_PAGES + 31) / 32)
static uint32_t g_committed[VM_WORDS];

/* Guards the table and every write to the bitmap. Queries read the bitmap
 * without it: one aligned 32-bit load per word, so they see each word either
 * before or after a change, and the answer is a snapshot that another thread
 * may change right after (the vm_state contract says so). Never held while
 * calling VM_INVALIDATE, which takes the renderer's own lock. */
static SDL_SpinLock g_lock;

static int page_committed(uint32_t page) {
    return (g_committed[page >> 5] >> (page & 31)) & 1u;
}

/* The first page at or after `page` whose bit differs from `set` (0 or 1),
 * or VM_PAGES. Whole words in the same state are skipped. */
static uint32_t run_end(uint32_t page, uint32_t set) {
    uint32_t flip = set ? 0xFFFFFFFFu : 0u;
    uint32_t w = page >> 5;
    uint32_t diff = (g_committed[w] ^ flip) & (0xFFFFFFFFu << (page & 31));
    for (;;) {
        if (diff) {
            uint32_t bit = 0;
            while (!(diff & 1u)) { diff >>= 1; bit++; }
            uint32_t end = (w << 5) + bit;
            return end < VM_PAGES ? end : VM_PAGES;
        }
        if (++w >= VM_WORDS) return VM_PAGES;
        diff = g_committed[w] ^ flip;
    }
}

/* Commit the pages of [lo, hi), guest VAs rounded out to whole pages. Pages
 * committed for the first time read as zero and committed pages keep their
 * contents, as on Windows. A range that runs past the arena commits nothing:
 * on Windows it crosses the end of the arena's reservation, and VirtualAlloc
 * refuses all of it. Caller holds g_lock. Returns 0 if nothing was committed
 * (no page changes state). */
static int commit_pages(uint32_t lo, uint64_t hi) {
    lo &= ~(VM_PAGE - 1);
    hi = (hi + VM_PAGE - 1) & ~(uint64_t)(VM_PAGE - 1);
    if (hi <= lo) return 1;
    int r = hi > WD_ARENA_SIZE ? VM_OS_FAILED : vm_os_commit(PTR(lo), (size_t)(hi - lo));
    if (r == VM_OS_FAILED) {
        fprintf(stderr, "[vm] commit %08X+%X failed\n", lo, (uint32_t)(hi - lo));
        return 0;
    }
    for (uint32_t page = lo / VM_PAGE; page < (uint32_t)(hi / VM_PAGE); page++) {
        uint32_t bit = 1u << (page & 31);
        if (g_committed[page >> 5] & bit) continue;
        if (r == VM_OS_DIRTY) memset(PTR(page * VM_PAGE), 0, VM_PAGE);
        g_committed[page >> 5] |= bit;
    }
    return 1;
}

/* Decommit the pages of [lo, hi), 4 KB-aligned guest VAs. Pages that are not
 * committed may be in the range, as Windows allows; a range running past the
 * arena is refused whole, as for a commit (the callers pass ranges inside one
 * reservation). Caller holds g_lock. Returns 0 if nothing was decommitted. */
static int decommit_pages(uint32_t lo, uint64_t hi) {
    if (hi <= lo) return 1;
    if (hi > WD_ARENA_SIZE) return 0;
    if (!vm_os_decommit(PTR(lo), (size_t)(hi - lo))) return 0;
    for (uint32_t page = lo / VM_PAGE; page < (uint32_t)(hi / VM_PAGE); page++)
        g_committed[page >> 5] &= ~(1u << (page & 31));
    return 1;
}

void* VM_FN(reserve)(size_t bytes) {
    SDL_LockSpinlock(&g_lock);
    memset(g_committed, 0, sizeof g_committed);
    SDL_UnlockSpinlock(&g_lock);
    return vm_os_reserve(bytes);
}

void VM_FN(commit)(uint32_t va, uint32_t bytes) {
    SDL_LockSpinlock(&g_lock);
    commit_pages(va, (uint64_t)va + (bytes ? bytes : 1));
    SDL_UnlockSpinlock(&g_lock);
}

static int vm_find(uint32_t va) {
    for (int i = 0; i < g_vm_n; i++)
        if (g_vm[i].size && va >= g_vm[i].base && va - g_vm[i].base < g_vm[i].size) return i;
    return -1;
}

/* Line for line vm_win32.c's, so both hand out the same addresses. As there,
 * a failed commit still returns the address. */
uint32_t VM_FN(alloc)(uint32_t addr, uint32_t size, uint32_t type, uint32_t prot) {
    uint32_t r = 0;
    SDL_LockSpinlock(&g_lock);
    if (!size) goto done;
    if (addr) {
        int i = vm_find(addr);
        if (i >= 0) {                       /* commit inside an existing reservation */
            uint32_t lo = addr & ~0xFFFu;
            uint32_t hi = (addr + size + 0xFFFu) & ~0xFFFu;
            if (hi > g_vm[i].base + g_vm[i].size) hi = g_vm[i].base + g_vm[i].size;
            /* hi < lo when addr + size wrapped: VirtualAlloc fails there and
             * commit_pages commits nothing. */
            if (type & W32_MEM_COMMIT) commit_pages(lo, hi);
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
    if (type & W32_MEM_COMMIT) commit_pages(r, (uint64_t)r + size);
done:
    SDL_UnlockSpinlock(&g_lock);
    return r;
}

int VM_FN(free)(uint32_t addr, uint32_t size, uint32_t type) {
    int ok = 0;
    uint32_t inv_base = 0, inv_bytes = 0;
    SDL_LockSpinlock(&g_lock);
    int i = vm_find(addr);
    if (i >= 0) {
        if (type & W32_MEM_RELEASE) {
            /* Guest release retains the enclosing host arena reservation. */
            ok = decommit_pages(g_vm[i].base, (uint64_t)g_vm[i].base + g_vm[i].size);
            if (ok) {
                inv_base = g_vm[i].base;
                inv_bytes = g_vm[i].size;
                g_vm[i].freed = g_vm[i].size;
                g_vm[i].size = 0;
            }
        } else if (type & W32_MEM_DECOMMIT) {
            uint32_t lo = addr & ~0xFFFu;
            uint64_t hi = ((uint64_t)addr + size + 0xFFFu) & ~0xFFFull;
            if (!size && addr == g_vm[i].base) hi = (uint64_t)addr + g_vm[i].size;
            /* Do not turn zero-size interior requests into an arena-wide free,
             * or let an overflowing/cross-allocation range decommit neighbors. */
            if ((size || addr == g_vm[i].base) && hi > lo &&
                hi <= (uint64_t)g_vm[i].base + g_vm[i].size) {
                ok = decommit_pages(lo, hi);
                if (ok) {
                    inv_base = lo;
                    inv_bytes = (uint32_t)(hi - lo);
                }
            }
        }
    }
    SDL_UnlockSpinlock(&g_lock);
    (void)inv_base;   /* unread where VM_INVALIDATE does nothing (VM_SECONDARY) */
    if (inv_bytes) VM_INVALIDATE(inv_base, inv_bytes);
    /* What Windows' VirtualFree leaves in GetLastError for each of these. */
    if (!ok) g_last_error = W32_ERROR_INVALID_ADDRESS;
    return ok;
}

/* For crash reports: which reservation holds va, and whether it is live. No
 * lock: a crash handler must not wait on a thread that may never let go. */
const char* VM_FN(describe)(uint32_t va) {
    static char buf[128];
    for (int i = 0; i < g_vm_n; i++) {
        uint32_t sz = g_vm[i].size ? g_vm[i].size : g_vm[i].freed;
        if (va >= g_vm[i].base && va - g_vm[i].base < sz) {
            int committed = va < WD_ARENA_SIZE && page_committed(va / VM_PAGE);
            sprintf(buf, "(heap: region #%d 0x%08X+0x%X, %s, page %s)", i, g_vm[i].base, sz,
                    g_vm[i].size ? "live" : "RELEASED", committed ? "committed" : "not committed");
            return buf;
        }
    }
    return "(heap: no region)";
}

/* The page holding va and the run of pages in the same state from there to
 * the first page in another state or the end of the arena, as Windows'
 * VirtualQuery reports it. Past the arena everything is free, to the end of
 * the 32-bit space. */
uint32_t VM_FN(state)(uint32_t va, uint32_t* run_base, uint32_t* run_bytes) {
    uint32_t base = va & ~(VM_PAGE - 1);
    uint32_t state = W32_MEM_FREE, bytes = 0u - base;
    if (va < WD_ARENA_SIZE) {
        uint32_t page = va / VM_PAGE;
        uint32_t set = (uint32_t)page_committed(page);
        bytes = (run_end(page, set) - page) * VM_PAGE;
        state = set ? W32_MEM_COMMIT : W32_MEM_RESERVE;
    }
    if (run_base) *run_base = base;
    if (run_bytes) *run_bytes = bytes;
    return state;
}

/* MEMORY_BASIC_INFORMATION, 32-bit layout (28 bytes), filled as vm_win32.c
 * fills it from Windows' answer. A VA past the arena is MEM_FREE, not
 * whatever the host maps there. */
uint32_t VM_FN(query)(uint32_t addr, uint32_t mbi) {
    if (addr >= WD_ARENA_SIZE) {
        uint32_t page = addr & ~(VM_PAGE - 1);
        WD_HOST_WRITE32(mbi + W32_MBI_BASE) = page;
        WD_HOST_WRITE32(mbi + W32_MBI_ALLOCBASE) = 0;
        WD_HOST_WRITE32(mbi + W32_MBI_ALLOCPROTECT) = 0;
        WD_HOST_WRITE32(mbi + W32_MBI_REGIONSIZE) = 0u - page;
        WD_HOST_WRITE32(mbi + W32_MBI_STATE) = W32_MEM_FREE;
        WD_HOST_WRITE32(mbi + W32_MBI_PROTECT) = W32_PAGE_NOACCESS;
        WD_HOST_WRITE32(mbi + W32_MBI_TYPE) = 0;
        return W32_MBI_SIZE;
    }
    uint32_t base, bytes;
    uint32_t state = VM_FN(state)(addr, &base, &bytes);
    uint32_t abase = base;
    SDL_LockSpinlock(&g_lock);
    int i = vm_find(addr);
    if (i >= 0) abase = g_vm[i].base;
    SDL_UnlockSpinlock(&g_lock);
    if (addr >= WD_IMAGE_BASE && addr < WD_IMAGE_BASE + wd_image_span()) abase = WD_IMAGE_BASE;
    if (addr >= WD_STACK_BASE && addr < WD_STACK_BASE + WD_STACK_SIZE) abase = WD_STACK_BASE;
    WD_HOST_WRITE32(mbi + W32_MBI_BASE) = base;
    WD_HOST_WRITE32(mbi + W32_MBI_ALLOCBASE) = abase;
    WD_HOST_WRITE32(mbi + W32_MBI_ALLOCPROTECT) = W32_PAGE_READWRITE;
    WD_HOST_WRITE32(mbi + W32_MBI_REGIONSIZE) = bytes;
    WD_HOST_WRITE32(mbi + W32_MBI_STATE) = state;
    WD_HOST_WRITE32(mbi + W32_MBI_PROTECT) = state == W32_MEM_COMMIT ? W32_PAGE_READWRITE : 0;
    WD_HOST_WRITE32(mbi + W32_MBI_TYPE) = abase == WD_IMAGE_BASE ? W32_MEM_IMAGE : W32_MEM_PRIVATE;
    return W32_MBI_SIZE;
}

/* The same text as vm_win32.c's, so the logs of two builds can be diffed. */
void VM_FN(dump)(FILE* out) {
    SDL_LockSpinlock(&g_lock);
    fprintf(out, "regions %d next %08X\n", g_vm_n, g_vm_next);
    for (int i = 0; i < g_vm_n; i++)
        fprintf(out, "  #%d %08X+%X prot=%X %s\n", i, g_vm[i].base,
                g_vm[i].size ? g_vm[i].size : g_vm[i].freed, g_vm[i].protect,
                g_vm[i].size ? "live" : "released");
    SDL_UnlockSpinlock(&g_lock);
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
