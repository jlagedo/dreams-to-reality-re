/*
 * crash_internal.h - what crash_report.c shares with the per-OS crash files
 * (crash_win32.c, crash_posix.c). Not for the rest of the host: that uses
 * crash_report.h.
 */
#ifndef RECOMP_CRASH_INTERNAL_H
#define RECOMP_CRASH_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* The head of a fault report: the "=== recomp: CRASH (<kind>) ===" line, the
 * host address, and (has_addr) the address the fault touched with its guest VA
 * and region. host_base is the module the host address is in, printed as
 * "exe+0x..." when known; NULL leaves that out. host_pc NULL with no base
 * means the OS file could not read it.
 * is_write: 1 a write, 0 a read, negative when the OS does not say. */
void crash_report_fault(const char* kind, const void* host_pc, const void* host_base,
                        int has_addr, uintptr_t bad_addr, int is_write);

/* "Name+0x12" for a guest code VA, "[IAT imp_Name]" for an import slot, ""
 * when nothing is known. A static string, good until the call after next. */
const char* crash_guest_name(uint32_t va);

/* The guest address and instruction text of line `line` of generated file
 * `file`, from its comment. 0 when the file or the comment is not there. */
int crash_guest_insn_at(const char* file, unsigned line, char* out, size_t n);

#endif /* RECOMP_CRASH_INTERNAL_H */
