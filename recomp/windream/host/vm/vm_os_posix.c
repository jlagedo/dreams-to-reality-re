/*
 * WINDREAM recompilation - vm_os.h on POSIX systems: mmap and mprotect.
 *
 * The arena is reserved as inaccessible anonymous memory. The ledger works in
 * 4 KB pages, but the host's pages may be larger (16 KB on Apple Silicon), so
 * a commit opens every host page the range touches, and a decommit gives back
 * only the host pages that lie wholly inside the range. A host page shared
 * with another guest page stays mapped and keeps its contents, so a guest page
 * decommitted inside it would not read as zero when committed again. On such
 * hosts commit answers VM_OS_DIRTY and the ledger zeroes every guest page it
 * commits for the first time itself; with 4 KB host pages the kernel's zeroes
 * are enough. This file has not been compiled yet.
 */
#include <sys/mman.h>
#include <unistd.h>
#include <stdint.h>
#include "vm_os.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifdef MAP_NORESERVE
#define VM_OS_NORESERVE MAP_NORESERVE
#else
#define VM_OS_NORESERVE 0
#endif

#define VM_OS_GUEST_PAGE 4096u   /* the ledger's page (VM_PAGE) */

static uintptr_t host_page(void) {
    static uintptr_t size;
    if (!size) {
        long n = sysconf(_SC_PAGESIZE);
        size = n > 0 ? (uintptr_t)n : VM_OS_GUEST_PAGE;
    }
    return size;
}

void* vm_os_reserve(size_t bytes) {
    void* p = mmap(NULL, bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | VM_OS_NORESERVE, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

int vm_os_commit(void* host, size_t bytes) {
    uintptr_t page = host_page();
    uintptr_t lo = (uintptr_t)host & ~(page - 1);
    uintptr_t hi = ((uintptr_t)host + bytes + page - 1) & ~(page - 1);
    int fresh = page == VM_OS_GUEST_PAGE ? VM_OS_ZEROED : VM_OS_DIRTY;
    if (hi <= lo) return fresh;
    return mprotect((void*)lo, hi - lo, PROT_READ | PROT_WRITE) == 0 ? fresh : VM_OS_FAILED;
}

/* Mapping fresh anonymous memory over the pages drops their contents and makes
 * them read as zero when committed again, on every POSIX system.
 * madvise(MADV_DONTNEED) would only promise the zeros on Linux; elsewhere the
 * kernel may keep the old contents. */
int vm_os_decommit(void* host, size_t bytes) {
    uintptr_t page = host_page();
    uintptr_t lo = ((uintptr_t)host + page - 1) & ~(page - 1);
    uintptr_t hi = ((uintptr_t)host + bytes) & ~(page - 1);
    if (hi <= lo) return 1;
    void* p = mmap((void*)lo, hi - lo, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | VM_OS_NORESERVE,
                   -1, 0);
    return p != MAP_FAILED;
}
