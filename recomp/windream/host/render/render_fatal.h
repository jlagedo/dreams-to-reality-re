#ifndef WD_RENDER_FATAL_H
#define WD_RENDER_FATAL_H
/* The one way the direct renderer stops on a case it does not support. It
 * never skips or guesses: it says why, loudly, and aborts.
 *
 *   1. stderr: "[direct] FATAL: <message>" (tests grep for FATAL);
 *   2. direct-fatal.txt in the working directory (the run directory);
 *   3. the report hook: the guest state report, titled with the message
 *      (abort() does not reach the crash handler: on Windows it ends in a
 *      fast-fail, which no vectored handler sees, and SIGABRT is not one of
 *      the POSIX handler's signals);
 *   4. the notify hook: a message box, unless the run is unattended;
 *   5. abort().
 *
 * No dependencies beyond the C library, so the native tests link it as is;
 * the host installs the hooks (render_live.cpp, before main). */
#ifdef __cplusplus
extern "C" {
#endif
#if defined(__cplusplus)
#define WD_NORETURN [[noreturn]]
#elif defined(_MSC_VER) && !defined(__clang__)
#define WD_NORETURN __declspec(noreturn)
#else
#define WD_NORETURN __attribute__((noreturn))
#endif
typedef void (*wd_render_fatal_hook)(const char *message);
void wd_render_fatal_hooks(wd_render_fatal_hook report, wd_render_fatal_hook notify);
WD_NORETURN void wd_render_fatal(const char *message);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
WD_NORETURN void wd_render_fatalf(const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif
