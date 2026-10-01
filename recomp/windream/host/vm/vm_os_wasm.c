/*
 * WINDREAM recompilation - vm_os.h on WebAssembly: one buffer in linear memory.
 *
 * Linear memory can be neither protected nor shrunk, so commit and decommit
 * change nothing: a stray guest pointer reads whatever is there instead of
 * faulting, and the memory in use follows the game's high-water mark. Commit
 * answers VM_OS_DIRTY, so the ledger zeroes each page it commits for the first
 * time (a page decommitted and committed again counts as first-time too). A
 * later step may put the arena at linear address 0 and build with
 * RECOMP_FLAT_MEMORY. This file has not been compiled yet.
 */
#include <stdlib.h>
#include "vm_os.h"

void* vm_os_reserve(size_t bytes) { return calloc(bytes, 1); }

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
