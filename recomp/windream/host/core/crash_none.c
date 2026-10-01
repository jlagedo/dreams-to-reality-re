/*
 * crash_none.c - the crash handler for hosts with nothing to install.
 *
 * Some hosts (Emscripten) have no signals or exceptions for a memory fault to
 * arrive through: a bad access traps in the embedder, outside the program. The
 * installer is empty there. recomp_report_state() in crash_report.c still
 * works when a shim calls it directly.
 */
#include "crash_report.h"

void recomp_install_crash_handler(void) {
}
