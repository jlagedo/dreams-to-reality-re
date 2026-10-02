/*
 * WINDREAM recompilation - vm_os.h on WebAssembly: one buffer in linear memory.
 *
 * Linear memory can be neither protected nor shrunk, so commit and decommit
 * change nothing: a stray guest pointer reads whatever is there instead of
 * faulting, and the memory in use follows the game's high-water mark. Commit
 * answers VM_OS_DIRTY, so the ledger zeroes each page it commits for the first
 * time (a page decommitted and committed again counts as first-time too). A
 * later step may put the arena at linear address 0 and build with
 * RECOMP_FLAT_MEMORY.
 */
#include <stdlib.h>
#include "vm_os.h"

/* malloc, not calloc: the ledger zeroes every page it commits (VM_OS_DIRTY), and
 * nothing else reads the arena, so the untouched pages stay uncommitted by the
 * browser. */
void* vm_os_reserve(size_t bytes) { return malloc(bytes); }

int vm_os_commit(void* host, size_t bytes) {
    (void)host;
    (void)bytes;
    return VM_OS_DIRTY;
}

int vm_os_decommit(void* host, size_t bytes) {
    (void)host;
    (void)bytes;
    return 1;
}
