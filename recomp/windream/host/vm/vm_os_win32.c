/*
 * WINDREAM recompilation - vm_os.h on Windows: VirtualAlloc and VirtualFree.
 *
 * Windows commits pages zeroed the first time and keeps the contents of pages
 * already committed, which is what the ledger expects of a commit.
 */
#ifndef WIN32_LEAN_AND_MEAN   /* CMakeLists.txt defines it for the whole target */
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "vm_os.h"

void* vm_os_reserve(size_t bytes) { return VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_READWRITE); }

int vm_os_commit(void* host, size_t bytes) {
    return VirtualAlloc(host, bytes, MEM_COMMIT, PAGE_READWRITE) ? VM_OS_ZEROED : VM_OS_FAILED;
}

int vm_os_decommit(void* host, size_t bytes) { return VirtualFree(host, bytes, MEM_DECOMMIT) != 0; }
