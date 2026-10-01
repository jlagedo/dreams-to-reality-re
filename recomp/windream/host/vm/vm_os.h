/*
 * WINDREAM recompilation - the operating system under vm_ledger.c.
 *
 * Three operations on host memory. Everything else about the guest's memory
 * (which pages are committed, which reservation an address belongs to, what
 * VirtualQuery answers) is the ledger's own bookkeeping, so those answers do
 * not depend on the host. What the operating system adds is a safety net: a
 * guest pointer into memory the ledger never committed faults instead of
 * reading zeros, on hosts that can protect pages.
 *
 * One file per system:
 *   vm_os_win32.c  VirtualAlloc/VirtualFree;
 *   vm_os_posix.c  mmap/mprotect (Linux, macOS, BSD; host pages may be 16 KB);
 *   vm_os_wasm.c   one buffer in linear memory, no protection;
 *   vm_os_null.c   nothing: the secondary of a shadow build, whose primary owns
 *                  the memory.
 *
 * The ledger passes host addresses (PTR(va)) and 4 KB-aligned ranges inside
 * the arena it reserved.
 */
#ifndef WD_VM_OS_H
#define WD_VM_OS_H

#include <stddef.h>

enum { VM_OS_FAILED = 0, VM_OS_ZEROED = 1, VM_OS_DIRTY = 2 };

/* Address space for the arena, inaccessible until committed. NULL on failure. */
void* vm_os_reserve(size_t bytes);

/* Make [host, host + bytes) readable and writable. Pages committed before keep
 * their contents. Returns VM_OS_ZEROED when pages committed for the first time
 * read as zero (every real OS), VM_OS_DIRTY when the ledger must zero them
 * itself (a plain buffer), VM_OS_FAILED on failure. */
int vm_os_commit(void* host, size_t bytes);

/* Make [host, host + bytes) inaccessible and give its pages back; a later
 * commit of the same range reads as zero. Returns 0 on failure, else nonzero.
 * A host that cannot protect pages returns nonzero and does nothing. */
int vm_os_decommit(void* host, size_t bytes);

#endif /* WD_VM_OS_H */
