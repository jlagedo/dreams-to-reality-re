/*
 * WINDREAM recompilation - the console window of Develop's keypad 9
 * (docs/specs/008-editor-restoration/spec.md, phase 7). Host-only: Cryo's
 * DOS tools printed to the DOS screen; the Windows game's printf output goes
 * through the Watcom runtime to WriteFile on the standard handles (files.c),
 * which nobody sees in a GUI build (WD_GUI_LOG sends it to log.txt). This file
 * gives that output a window: dev_tools.c tees the guest's standard-handle
 * writes here while the console is open.
 *
 * Windows: a console is opened with AllocConsole when the process has none,
 * or the one it has is used; writes go to CONOUT$. Its close button is
 * removed (closing a console window ends the process whatever a control
 * handler returns) and Ctrl+C / Ctrl+Break are ignored. Closing (keypad 9
 * again) frees a console this file opened; a console the process already had
 * stays. Elsewhere: the controlling terminal (/dev/tty), when there is one.
 * Plain C, no host header: windows.h stays out of the guest-register macros.
 */
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static HANDLE g_out = INVALID_HANDLE_VALUE;
static int g_allocated;

static BOOL WINAPI ignore_break(DWORD type) { return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT; }

/* 1 when the console is open: `how` says whether it is new or the process's own. */
int dev_console_open(const char** how) {
    if (g_out != INVALID_HANDLE_VALUE) { *how = "already open"; return 1; }
    g_allocated = 0;
    if (!GetConsoleWindow()) {
        /* A process started without a window (CREATE_NO_WINDOW, as run.py's
         * runs) has a console with no window: AllocConsole fails until it is
         * let go. The standard streams keep their files. */
        if (!AllocConsole() && !(FreeConsole() && AllocConsole())) { *how = "AllocConsole failed"; return 0; }
        g_allocated = 1;
        SetConsoleTitleA("Dreams to Reality - Develop console (keypad 9 closes)");
    }
    HWND w = GetConsoleWindow();
    if (w) {
        HMENU menu = GetSystemMenu(w, FALSE);
        if (menu) DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
    }
    SetConsoleCtrlHandler(ignore_break, TRUE);
    g_out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_EXISTING, 0, NULL);
    if (g_out == INVALID_HANDLE_VALUE) {
        if (g_allocated) FreeConsole();
        g_allocated = 0;
        *how = "no CONOUT$";
        return 0;
    }
    *how = g_allocated ? "opened a console window" : "the process's console";
    return 1;
}

void dev_console_close(void) {
    if (g_out == INVALID_HANDLE_VALUE) return;
    CloseHandle(g_out);
    g_out = INVALID_HANDLE_VALUE;
    SetConsoleCtrlHandler(ignore_break, FALSE);
    if (g_allocated) FreeConsole();
    g_allocated = 0;
}

void dev_console_write(const void* data, unsigned n) {
    DWORD put = 0;
    if (g_out != INVALID_HANDLE_VALUE && n) WriteFile(g_out, data, n, &put, NULL);
}

#elif defined(__EMSCRIPTEN__)

int dev_console_open(const char** how) { *how = "no console in the browser"; return 0; }
void dev_console_close(void) {}
void dev_console_write(const void* data, unsigned n) { (void)data; (void)n; }

#else
#include <fcntl.h>
#include <unistd.h>

static int g_fd = -1;

int dev_console_open(const char** how) {
    if (g_fd >= 0) { *how = "already open"; return 1; }
    g_fd = open("/dev/tty", O_WRONLY | O_NOCTTY);
    if (g_fd < 0) { *how = "no terminal (/dev/tty)"; return 0; }
    *how = "the terminal";
    return 1;
}

void dev_console_close(void) {
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
}

void dev_console_write(const void* data, unsigned n) {
    if (g_fd >= 0 && n) {
        ssize_t put = write(g_fd, data, n);
        (void)put;
    }
}
#endif

void dev_console_print(const char* text) { dev_console_write(text, (unsigned)strlen(text)); }
