/*
 * WINDREAM recompilation - vm_os.h for the secondary of a shadow build.
 *
 * In the shadow build (vm_shadow.c) the primary implementation owns the
 * arena's memory and the ledger only keeps its books beside it. The ledger's
 * calls here change nothing. Commit answers VM_OS_ZEROED because the primary
 * has already committed the same pages, zeroed when new: the ledger must not
 * zero them again over data the primary holds.
 */
#include "vm_os.h"

/* Any non-NULL pointer; the arena itself is the primary's. */
void* vm_os_reserve(size_t bytes) {
    static unsigned char token;
    (void)bytes;
    return &token;
}

int vm_os_commit(void* host, size_t bytes) {
    (void)host;
    (void)bytes;
    return VM_OS_ZEROED;
}

int vm_os_decommit(void* host, size_t bytes) {
    (void)host;
    (void)bytes;
    return 1;
}
