/* See render_fatal.h. */
#include "render_fatal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __EMSCRIPTEN__
void wd_web_status(const char* kind, const char* text);
#endif
static wd_render_fatal_hook report_hook, notify_hook;

void wd_render_fatal_hooks(wd_render_fatal_hook report, wd_render_fatal_hook notify) {
    report_hook = report;
    notify_hook = notify;
}
void wd_render_fatal(const char *message) {
    static int entered;
    if (!message || !*message)
        message = "unspecified failure";
    fprintf(stderr, "[direct] FATAL: %s\n", message);
    fflush(stderr);
    if (!entered++) { /* a failure inside a hook must still reach abort() */
        FILE *file = fopen("direct-fatal.txt", "w");
        if (file) {
            fprintf(file,
                    "The direct renderer stopped on a case it does not support:\n\n  %s\n\n"
                    "The game state at that moment is in the log (stderr). "
                    "--renderer software avoids this renderer.\n",
                    message);
            fclose(file);
        }
        if (report_hook)
            report_hook(message);
        if (notify_hook)
            notify_hook(message);
    }
#ifdef __EMSCRIPTEN__
    wd_web_status("fatal", message);
#endif
    abort();
}
void wd_render_fatalf(const char *format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    wd_render_fatal(message);
}
