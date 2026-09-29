/*
 * Bring-up diagnostics for a statically recompiled program.
 *
 * recomp_types.h defines RECOMP_ENTER(va) to call recomp_trace_enter when the
 * generated code is built with -DRECOMP_TRACE. That is the one hook every
 * lifted function passes through, and it is where a debugger would put a
 * breakpoint if there were a debugger. This is the reference implementation of
 * what to hang off it, so a new target does not rewrite it:
 *
 *   --calltrace FILE     every entry, thread-tagged, to a file
 *   --firsthit LO HI     the FIRST entry to each distinct function in a window
 *   --argtrace VA        one line per entry to VA with its first three args
 *   --watch VA           registers, stack arguments and the object under ecx
 *   --watchspan LO HI    move or narrow the [ecx+..] window --watch dumps
 *   --poison ADDR        report every change of one target dword
 *   --poisonval V        ...only when it becomes V
 *   --poke ADDR VAL VA   write one byte the first time VA is entered
 *
 * Wire it up with three lines in the host:
 *
 *   for (i = 1; i < argc; i++) {
 *       int n = recomp_trace_arg(argc, argv, i);
 *       if (n) { i += n - 1; continue; }
 *       ... the host's own options ...
 *   }
 *
 * and call recomp_trace_flush() from the fault handler.
 *
 * A target with a diagnostic of its own -- a watch that knows a class layout,
 * a poison that follows a pointer -- sets recomp_trace_extra instead of
 * forking this file.
 */
#ifndef RECOMP_TRACE_H
#define RECOMP_TRACE_H

#include <stdint.h>

/* Consume one option at argv[i]. Returns how many argv entries it took (1 for
 * a flag, 2 for one argument, 4 for --poke), or 0 if it is not ours. */
int  recomp_trace_arg(int argc, char** argv, int i);

/* Flush the --calltrace file. Call before printing a fault report: the trace
 * is buffered, because unbuffered it is slower than the code it traces. */
void recomp_trace_flush(void);

/* Print the last RECOMP_ENTER_SIZE function entries, oldest first. Safe to
 * call without -DRECOMP_TRACE (prints nothing). Declared in recomp_types.h. */
/* void recomp_dump_trace(const char* why); */

/* Per-entry hook for a target's own diagnostics; called after the generic
 * work, with g_cur_func already set to va. NULL by default. */
extern void (*recomp_trace_extra)(uint32_t va);

/* The name of the import the machine is currently inside, or "(none)".
 * The host import bridge sets it; --poison prints it, because a host pointer
 * appearing in target memory was put there by whichever shim ran last. */
extern const char* g_cur_import;

/* One line per option, for a host's --help. */
void recomp_trace_help(void);

#endif /* RECOMP_TRACE_H */
