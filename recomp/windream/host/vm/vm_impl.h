/*
 * WINDREAM recompilation - one virtual-memory implementation behind vm_front.c.
 *
 * host/vm holds the guest's memory manager: what VirtualAlloc hands out, what
 * VirtualFree takes back, what VirtualQuery and the host's own checks are
 * told about an address. There are three implementations of the same entry
 * points, picked by WD_VM at configure time (CMakeLists.txt):
 *
 *   vm_win32.c   the original: Windows' page state is the truth, read back
 *                with VirtualQuery; Windows only;
 *   vm_ledger.c  the portable one: keeps a bitmap of committed pages itself and
 *                asks the operating system for three things only (vm_os.h), so
 *                its answers are the same on every host;
 *   vm_shadow.c  both at once: win32 owns the memory, ledger is asked every
 *                question too, and each disagreement is reported. This is how
 *                the ledger is proven against Windows on a real play session.
 *
 * Each implementation defines its entry points through VM_FN(name). Alone in
 * a build it takes the default prefix, vm_impl_, which is what vm_front.c
 * calls. In the shadow build vm_shadow.c has vm_impl_ and the two it compares
 * are compiled with VM_PREFIX=vm_win32_ and VM_PREFIX=vm_ledger_.
 */
#ifndef WD_VM_IMPL_H
#define WD_VM_IMPL_H

#include "imports.h"
#include "guest_win32.h"

#ifndef VM_PREFIX
#define VM_PREFIX vm_impl_
#endif
#define VM_CAT_(a, b) a##b
#define VM_CAT(a, b) VM_CAT_(a, b)
#define VM_FN(name) VM_CAT(VM_PREFIX, name)

/* The entry points of one implementation with prefix P; the contracts are the
 * vm_* ones in imports.h. */
#define VM_DECLARE(P)                                                                  \
    void*       VM_CAT(P, reserve)(size_t bytes);                                      \
    void        VM_CAT(P, commit)(uint32_t va, uint32_t bytes);                        \
    uint32_t    VM_CAT(P, alloc)(uint32_t addr, uint32_t size, uint32_t type,          \
                                 uint32_t prot);                                       \
    int         VM_CAT(P, free)(uint32_t addr, uint32_t size, uint32_t type);          \
    uint32_t    VM_CAT(P, query)(uint32_t addr, uint32_t mbi_va);                      \
    const char* VM_CAT(P, describe)(uint32_t va);                                      \
    uint32_t    VM_CAT(P, state)(uint32_t va, uint32_t* run_base, uint32_t* run_bytes); \
    void        VM_CAT(P, dump)(FILE* out);

VM_DECLARE(VM_PREFIX)

/* GPU surface bookkeeping belongs to the implementation that owns the memory.
 * The secondary of a shadow build (VM_SECONDARY) leaves it to the primary. */
#ifdef VM_SECONDARY
#define VM_INVALIDATE(base, bytes) ((void)0)
#else
#define VM_INVALIDATE(base, bytes) ((void)wd_surface_invalidate_range((base), (bytes)))
#endif

/* Reservations are handed out 64 KB-aligned from the bottom half of the guest
 * heap region (the top half is shim_alloc's, growing down); address space is
 * not reused after MEM_RELEASE. Both allocators share these so that they hand
 * out the same addresses for the same sequence of calls. */
#define VM_MAX 4096
#define VM_ALLOC_LIMIT (WD_HEAP_BASE + WD_HEAP_SIZE / 2)
#define VM_PAGE 0x1000u
#define VM_GRANULE 0x10000u

#endif /* WD_VM_IMPL_H */
