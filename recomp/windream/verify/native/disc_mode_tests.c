/*
 * Disc mode of the file bridges (host/sdl/files.c), driven through the bridges
 * themselves: tests/recomp/test_disc_mode.py builds this with the production
 * files.c, kernel.c, threads.c and runtime.c, makes two small "discs" (extracted
 * directories, which the disc library takes like an image) and a data
 * directory, names them in WD_DISC1, WD_DISC2 and WD_DATA_DIR, and runs it.
 *
 * The tree it expects (file contents in quotes):
 *   disc 1: DATA\1CD.ID  DATA\HD.ID  DATA\FULL.ID  DATA\3DC\DIALOG.DRD "dialog"
 *           DATA\3DC\A.DSN "one-a"  DATA\3DC\ONLY1.DSN  DREAMS.DAT "bank1"
 *           GDIDREAM.EXE "MZ-one"  BIG.BIN (70000 bytes, byte i = i * 7 + 3)
 *   disc 2: DATA\2CD.ID  DATA\HD.ID  DATA\3DC\A.DSN "two-a"  DATA\3DC\B.DSN
 *           DATA\GAME\GAME.DAT "stale"  DATA\GAME\GAME0.DAT  DREAMS.DAT "bank2"
 * It leaves in the data directory exactly: CRYO\DREAMS\data\game\game.dat
 * "mine", CRYO\DREAMS\DATA\FULL.ID "x" and DREAMS.DAT "bank2!".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imports.h"
#include "guest_win32.h"

static unsigned char arena[1 << 18];
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- what runtime.c links against but this host never runs ---- */
const recomp_dispatch_entry_t recomp_dispatch_table[] = {{0, NULL}};
const uint32_t recomp_dispatch_count = 0;
const recomp_dispatch_entry_t wd_import_bridges[] = {{0, NULL}};
const uint32_t wd_import_bridge_count = 0;
void host_init(void) { abort(); }
void recomp_install_crash_handler(void) { abort(); }
void recomp_set_region_describer(const char* (*fn)(uint32_t)) { (void)fn; abort(); }
int recomp_trace_arg(int argc, char** argv, int i) { (void)argc; (void)argv; (void)i; abort(); }
void recomp_trace_flush(void) { abort(); }
void recomp_trace_help(void) { abort(); }
void wd_scene_probe_init(void) { abort(); }
void wd_render_install(void) { abort(); }
void wd_render_close(void) { abort(); }
uint32_t wd_surface_invalidate_range(uint32_t base, uint32_t bytes) { (void)base; (void)bytes; return 0; }

void imp_CreateFileA(void);
void imp_CloseHandle(void);
void imp_ReadFile(void);
void imp_WriteFile(void);
void imp_SetFilePointer(void);
void imp_GetFileAttributesA(void);
void imp_FindFirstFileA(void);
void imp_FindNextFileA(void);
void imp_FindClose(void);
void imp_DeleteFileA(void);

enum { NAME = 0x1000, DATA = 0x2000, COUNT = 0x1F00, FIND = 0x1800 };
static int switches;
static void switched(void) { switches++; }

static void arguments(const uint32_t* values, unsigned count) {
    g_esp = 256;
    memcpy(arena + g_esp + 4, values, count * 4);
}
static uint32_t name(const char* path) {
    strcpy((char*)arena + NAME, path);
    return NAME;
}
static uint32_t open_as(const char* path, uint32_t access, uint32_t disposition) {
    const uint32_t a[] = {name(path), access, 0, 0, disposition, 0x80, 0};
    g_last_error = 0;
    arguments(a, 7); imp_CreateFileA();
    return g_eax;
}
static uint32_t open_read(const char* path) { return open_as(path, W32_GENERIC_READ, W32_OPEN_EXISTING); }
static void close_file(uint32_t h) { arguments(&h, 1); imp_CloseHandle(); }
/* Does the path open for reading? */
static int opens(const char* path) {
    uint32_t h = open_read(path);
    if (h == W32_INVALID_HANDLE_VALUE) return 0;
    close_file(h);
    return 1;
}
static uint32_t read_at(uint32_t h, uint32_t count) {   /* into arena + DATA; returns the bytes read, -1 on failure */
    const uint32_t a[] = {h, DATA, count, COUNT, 0};
    arguments(a, 5); imp_ReadFile();
    return g_eax ? MEM32(COUNT) : 0xFFFFFFFFu;
}
static uint32_t seek(uint32_t h, int32_t distance, uint32_t method) {
    const uint32_t a[] = {h, (uint32_t)distance, 0, method};
    arguments(a, 4); imp_SetFilePointer();
    return g_eax;
}
/* The whole text of a small file, "" with (none) when it does not open. */
static const char* text(const char* path) {
    static char out[64];
    uint32_t h = open_read(path);
    if (h == W32_INVALID_HANDLE_VALUE) return "(none)";
    uint32_t n = read_at(h, sizeof out - 1);
    if (n == 0xFFFFFFFFu) n = 0;
    memcpy(out, arena + DATA, n);
    out[n] = 0;
    close_file(h);
    return out;
}
static int is(const char* path, const char* want) { return !strcmp(text(path), want); }
static void write_text(const char* path, uint32_t disposition, const char* value, int at_end) {
    uint32_t h = open_as(path, W32_GENERIC_READ | W32_GENERIC_WRITE, disposition);
    CHECK(h != W32_INVALID_HANDLE_VALUE);
    if (h == W32_INVALID_HANDLE_VALUE) return;
    if (at_end) seek(h, 0, 2);
    strcpy((char*)arena + DATA, value);
    const uint32_t a[] = {h, DATA, (uint32_t)strlen(value), COUNT, 0};
    arguments(a, 5); imp_WriteFile();
    CHECK(g_eax && MEM32(COUNT) == strlen(value));
    close_file(h);
}
static uint32_t attributes(const char* path) {
    const uint32_t a[] = {name(path)};
    g_last_error = 0;
    arguments(a, 1); imp_GetFileAttributesA();
    return g_eax;
}
/* The names a pattern finds, space-separated, in the order given. */
static const char* find(const char* pattern) {
    static char out[512];
    const uint32_t a[] = {name(pattern), FIND};
    out[0] = 0;
    arguments(a, 2); imp_FindFirstFileA();
    uint32_t h = g_eax;
    if (h == W32_INVALID_HANDLE_VALUE) return "(none)";
    for (;;) {
        if (out[0]) strcat(out, " ");
        strcat(out, (char*)arena + FIND + W32_FIND_DATA_NAME);
        const uint32_t next[] = {h, FIND};
        arguments(next, 2); imp_FindNextFileA();
        if (!g_eax) break;
    }
    arguments(&h, 1); imp_FindClose();
    return out;
}
static int active(void) {
    int n = 0;
    files_active_disc(&n, NULL);
    return n;
}

int main(void) {
    g_mem_base = (ptrdiff_t)arena;
    CHECK(files_open_discs() == 1);
    if (fails) { puts("disc mode: the discs did not open"); return 1; }
    files_active_disc(NULL, switched);
    files_init(NULL);

    /* the executable comes from disc 1 */
    size_t size = 0;
    char* exe = (char*)files_disc_read(1, "GDIDREAM.EXE", &size);
    CHECK(exe && size == 6 && !memcmp(exe, "MZ-one", 6));
    free(exe);
    CHECK(!files_disc_read(1, "NOPE.EXE", &size) && !files_disc_read(1, "DATA", &size));

    /* startup, as CD_FindDrive and CD_FindCacheDrive ask: any drive letter is the CD and the install */
    CHECK(active() == 1);
    CHECK(opens("Z:\\DATA\\1CD.ID") && active() == 1);
    CHECK(opens("C:\\CRYO\\DREAMS\\DATA\\HD.ID"));
    CHECK(is("dreams.dat", "bank1") && is("D:\\DREAMS.DAT", "bank1"));

    /* FULL.ID: there at the CD root, never at the install root */
    CHECK(opens("DATA\\FULL.ID"));
    CHECK(!opens("C:\\CRYO\\DREAMS\\DATA\\FULL.ID") && g_last_error == W32_ERROR_FILE_NOT_FOUND);
    CHECK(attributes("CRYO\\DREAMS\\DATA\\FULL.ID") == W32_INVALID_FILE_ATTRIBUTES);
    write_text("CRYO\\DREAMS\\DATA\\FULL.ID", W32_CREATE_ALWAYS, "x", 0);   /* not even the sandbox's */
    CHECK(!opens("CRYO\\DREAMS\\DATA\\FULL.ID"));

    /* the install root falls back to the active disc, then the other one */
    CHECK(is("CRYO\\DREAMS\\DATA\\3DC\\DIALOG.DRD", "dialog") && is("cryo\\dreams\\data\\3dc\\a.dsn", "one-a"));
    CHECK(opens("CRYO\\DREAMS\\DATA\\3DC\\B.DSN"));              /* disc 2 only */
    CHECK(!opens("DATA\\3DC\\B.DSN") && g_last_error == W32_ERROR_FILE_NOT_FOUND);   /* the CD root is the active disc alone */
    CHECK(!opens("NOPE\\B.DSN") && g_last_error == W32_ERROR_PATH_NOT_FOUND);
    CHECK(!opens("CRYO\\B.DSN"));

    /* the saves are the data directory's alone, whatever a disc carries */
    CHECK(!opens("CRYO\\DREAMS\\data\\game\\game.dat") && !opens("CRYO\\DREAMS\\DATA\\GAME\\GAME0.DAT"));
    CHECK(!strcmp(find("CRYO\\DREAMS\\DATA\\GAME\\*.DAT"), "(none)"));
    write_text("CRYO\\DREAMS\\data\\game\\game.dat", W32_CREATE_ALWAYS, "mine", 0);
    CHECK(is("CRYO\\DREAMS\\DATA\\GAME\\GAME.DAT", "mine"));
    CHECK(!strcmp(find("CRYO\\DREAMS\\DATA\\GAME\\*.DAT"), "game.dat"));

    /* attributes and directory listings */
    CHECK(attributes("DATA\\3DC") == W32_FILE_ATTRIBUTE_DIRECTORY && attributes("DATA\\1CD.ID") == W32_FILE_ATTRIBUTE_ARCHIVE);
    CHECK(attributes("DATA\\2CD.ID") == W32_INVALID_FILE_ATTRIBUTES && g_last_error == W32_ERROR_FILE_NOT_FOUND);
    CHECK(attributes("CRYO\\DREAMS") == W32_FILE_ATTRIBUTE_DIRECTORY && attributes("CRYO\\DREAMS\\DATA\\3DC") == W32_FILE_ATTRIBUTE_DIRECTORY);
    CHECK(!strcmp(find("DATA\\3DC\\*.DSN"), "A.DSN ONLY1.DSN"));
    CHECK(!strcmp(find("C:\\CRYO\\DREAMS\\DATA\\3DC\\*.DRD"), "DIALOG.DRD"));
    CHECK(!strcmp(find("CRYO\\DREAMS\\DATA\\3DC\\B.*"), "B.DSN"));   /* the other disc answers when the active one has no match */
    CHECK(!strcmp(find("DATA\\3DC\\*.XYZ"), "(none)") && g_last_error == W32_ERROR_FILE_NOT_FOUND);
    CHECK(!strncmp(find("DATA\\3DC\\*.*"), ". .. A.DSN DIALOG.DRD", 21));

    /* a disc file through ReadFile and SetFilePointer */
    uint32_t big = open_read("BIG.BIN");
    CHECK(big != W32_INVALID_HANDLE_VALUE);
    CHECK(seek(big, 0, 2) == 70000 && seek(big, -4, 2) == 69996);
    CHECK(read_at(big, 100) == 4 && arena[DATA] == (unsigned char)(69996 * 7 + 3) && read_at(big, 100) == 0);
    CHECK(seek(big, 80000, 0) == 80000 && read_at(big, 10) == 0);    /* past the end: nothing, no error */
    CHECK(seek(big, -1, 0) == W32_INVALID_SET_FILE_POINTER);
    CHECK(seek(big, 2040, 0) == 2040 && seek(big, 5, 1) == 2045);
    CHECK(read_at(big, 65000) == 65000);
    int same = 1;
    for (uint32_t i = 0; i < 65000; i++) same &= arena[DATA + i] == (unsigned char)((2045 + i) * 7 + 3);
    CHECK(same && seek(big, 0, 1) == 67045);
    uint32_t again = open_read("BIG.BIN");                           /* a second handle has its own position */
    CHECK(again != W32_INVALID_HANDLE_VALUE && read_at(again, 1) == 1 && arena[DATA] == 3 && seek(big, 0, 1) == 67045);
    close_file(again);
    const uint32_t refused[] = {big, DATA, 1, COUNT, 0};
    arguments(refused, 5); imp_WriteFile();
    CHECK(!g_eax);

    /* the marker rule. The query (one open) sees the truth and changes nothing ... */
    CHECK(!opens("DATA\\2CD.ID") && g_last_error == W32_ERROR_FILE_NOT_FOUND && active() == 1);
    CHECK(opens("dreams.dat"));
    /* ... also twice, with another open in between ... */
    CHECK(!opens("DATA\\2CD.ID") && active() == 1 && switches == 0);
    /* ... and the prompt's second poll in a row swaps the disc */
    CHECK(opens("X:\\DATA\\2CD.ID") && active() == 2 && switches == 1);
    CHECK(opens("DATA\\2CD.ID") && active() == 2 && switches == 1);
    CHECK(is("dreams.dat", "bank2") && is("DATA\\3DC\\A.DSN", "two-a") && is("CRYO\\DREAMS\\DATA\\3DC\\A.DSN", "two-a"));
    CHECK(!opens("DATA\\3DC\\ONLY1.DSN") && opens("CRYO\\DREAMS\\DATA\\3DC\\ONLY1.DSN"));
    CHECK(is("CRYO\\DREAMS\\data\\3dc\\dialog.drd", "dialog"));      /* disc 1 only */
    CHECK(!opens("DATA\\FULL.ID") && !opens("CRYO\\DREAMS\\DATA\\FULL.ID"));
    CHECK(is("CRYO\\DREAMS\\data\\game\\game.dat", "mine"));          /* not disc 2's "stale" */
    CHECK(is("DATA\\GAME\\GAME.DAT", "stale"));                       /* the CD root shows the disc as it is */
    /* a file opened before the swap still reads the disc it came from */
    CHECK(seek(big, 0, 0) == 0 && read_at(big, 2) == 2 && arena[DATA] == 3 && arena[DATA + 1] == 10);
    close_file(big);

    /* on disc 2 and asked for disc 2: CD_GetDiscNumber's 1CD.ID probe fails, level files follow */
    CHECK(!opens("DATA\\1CD.ID") && active() == 2);
    CHECK(opens("DATA\\3DC\\B.DSN"));
    CHECK(!opens("DATA\\1CD.ID") && active() == 2);
    CHECK(attributes("DATA\\1CD.ID") == W32_INVALID_FILE_ATTRIBUTES);   /* not an open: the count stands */
    /* back to disc 1: that failed probe, then the prompt's first poll */
    CHECK(opens("DATA\\1CD.ID") && active() == 1 && switches == 2);
    CHECK(is("dreams.dat", "bank1") && !opens("DATA\\2CD.ID"));
    /* a write between two polls is an open too */
    write_text("CRYO\\DREAMS\\data\\game\\game.dat", W32_CREATE_ALWAYS, "mine", 0);
    CHECK(!opens("DATA\\2CD.ID") && active() == 1);
    CHECK(opens("DATA\\2CD.ID") && active() == 2 && switches == 3);

    /* a disc file opened for update is copied to the data directory first, which then wins */
    write_text("DREAMS.DAT", W32_OPEN_EXISTING, "!", 1);
    CHECK(is("dreams.dat", "bank2!"));
    /* a marker is never the data directory's, and deletes stay there */
    const uint32_t gone[] = {name("DATA\\3DC\\B.DSN")};
    arguments(gone, 1); imp_DeleteFileA();
    CHECK(!g_eax && opens("DATA\\3DC\\B.DSN"));

    if (!fails) puts("disc mode: resolution, the marker rule both ways, saves, listings and reads passed");
    return fails != 0;
}
