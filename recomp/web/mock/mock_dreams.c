/* A stand-in for the game engine, for testing the web shell without the real
 * build. It follows the contract in recomp/web/CONTRACT.md: createDreams, the
 * pack under /dreams (WD_INSTALL_ROOT), saves in /dreams/DATA/GAME, a pthread,
 * Module.onDreamsStatus. It lists the files it finds and draws them on the
 * canvas, and counts its starts in a save file to show that saves persist. */
#include <dirent.h>
#include <emscripten.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

EM_JS(void, js_status, (const char *kind, const char *text), {
  if (Module.onDreamsStatus) Module.onDreamsStatus(UTF8ToString(kind), UTF8ToString(text));
});
EM_JS(void, js_line, (const char *text), {
  (Module.mockLines = Module.mockLines || []).push(UTF8ToString(text));
});
EM_JS(void, js_draw, (int frame), {
  const c = Module.canvas; if (!c) return;
  if (c.width !== 640) { c.width = 640; c.height = 480; }
  const g = c.getContext('2d');
  g.fillStyle = 'hsl(' + (frame * 2 % 360) + ',55%,18%)'; g.fillRect(0, 0, 640, 480);
  g.fillStyle = '#fff'; g.font = '16px monospace';
  g.fillText('MOCK DREAMS ENGINE  frame ' + frame, 12, 24);
  const L = Module.mockLines || [];
  for (let i = 0; i < L.length && i < 24; i++) g.fillText(L[i], 12, 52 + i * 18);
  Module.mockFrame = frame;
});

static void walk(const char *dir, int *n) {
  DIR *d = opendir(dir);
  if (!d) return;
  struct dirent *e;
  while ((e = readdir(d))) {
    if (e->d_name[0] == '.') continue;
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    struct stat st;
    if (stat(p, &st)) continue;
    if (S_ISDIR(st.st_mode)) { walk(p, n); continue; }
    char line[1200];
    snprintf(line, sizeof line, "%s  %ld bytes", p, (long)st.st_size);
    js_line(line);
    (*n)++;
  }
  closedir(d);
}

static void *worker(void *arg) { *(int *)arg = 42; return NULL; }

static int frame;
static void tick(void) { js_draw(frame++); }

int main(void) {
  js_status("boot", "mock engine starting");
  const char *root = getenv("WD_INSTALL_ROOT");
  char line[1200];
  snprintf(line, sizeof line, "WD_INSTALL_ROOT=%s", root ? root : "(unset)");
  js_line(line);
  int n = 0;
  walk(root ? root : "/dreams", &n);
  snprintf(line, sizeof line, "%d files", n);
  js_line(line);

  int v = 0;
  pthread_t t;
  if (pthread_create(&t, NULL, worker, &v) == 0) pthread_join(t, NULL);
  snprintf(line, sizeof line, "pthread result %d", v);
  js_line(line);

  /* Start counter in the save directory: it only grows if saves persist. */
  int starts = 0;
  FILE *f = fopen("/dreams/DATA/GAME/GAME9.DAT", "rb");
  if (f) { if (fread(&starts, sizeof starts, 1, f) != 1) starts = 0; fclose(f); }
  starts++;
  f = fopen("/dreams/DATA/GAME/GAME9.DAT", "wb");
  if (f) { fwrite(&starts, sizeof starts, 1, f); fclose(f); }
  snprintf(line, sizeof line, "save starts %d", starts);
  js_line(line);

  emscripten_set_main_loop(tick, 0, 0);
  js_status("running", "mock engine running");
  return 0;
}
