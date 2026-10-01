/* launcher_demo: runs the launcher and prints what it returns, one NAME=VALUE
 * per line on stdout. The test target for the launcher library; the game's own
 * main does the same with these pairs, then puts them in the environment.
 *
 *   launcher_demo [--disc1 P] [--disc2 P] [--data D] [--play]
 *
 * Exit code: 0 the user chose Play (or --play validated), 1 the user quit,
 * 2 an error (the text is on stderr). */
#include <stdio.h>

#include <SDL3/SDL_main.h> /* UTF-8 argv on Windows; the launcher takes UTF-8 */

#include "launcher.h"

int main(int argc, char** argv) {
    LauncherResult r;
    int rc = launcher_run(argc, argv, &r);
    int code;
    if (rc < 0 || r.error[0]) {
        fprintf(stderr, "launcher: %s\n", r.error[0] ? r.error : "failed");
        code = 2;
    } else if (r.play) {
        for (int i = 0; i < r.count; i++) printf("%s=%s\n", r.vars[i].name, r.vars[i].value);
        code = 0;
    } else {
        code = 1;
    }
    fflush(stdout);
    launcher_free(&r);
    return code;
}
