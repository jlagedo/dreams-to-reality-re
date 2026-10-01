/* Explicit one-frame diagnostic. Software remains the reference renderer;
 * this hook snapshots the future direct adapter's input before visual work. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>
#include "imports.h"
#include "render_live.h"

extern int wd_capture_scene_file(bool (*reader)(void *, uint32_t, void *, size_t), void *context,
                                 uint32_t root, const char *path, char *error, size_t error_size);
static const char *output_path;
static int captured;
static recomp_func_t original_frame;
static RECOMP_TLS uintptr_t read_begin, read_end;
static RECOMP_TLS int read_scope;
void wd_render_read_scope_begin(void) {
    read_begin = read_end = 0;
    read_scope = 1;
}
void wd_render_read_scope_end(void) {
    read_begin = read_end = 0;
    read_scope = 0;
}
bool wd_render_read_arena(void *context, uint32_t address, void *destination, size_t size) {
    if ((uint64_t)address + size > WD_ARENA_SIZE)
        return false;
    wd_surface_check(context ? (uint32_t)(uintptr_t)context : 0x459320, address, (uint32_t)size, 0);
    const unsigned char *source = PTR(address);
    unsigned char *target = destination;
    while (size) {
        if (read_scope && (uintptr_t)source >= read_begin && (uintptr_t)source < read_end) {
            size_t available = read_end - (uintptr_t)source;
            size_t count = size < available ? size : available;
            memcpy(target, source, count);
            source += count;
            target += count;
            size -= count;
            continue;
        }
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(source, &info, sizeof info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        if (read_scope) {
            read_begin = (uintptr_t)info.BaseAddress;
            read_end = read_begin + info.RegionSize;
        }
        size_t available = (const unsigned char *)info.BaseAddress + info.RegionSize - source;
        size_t count = size < available ? size : available;
        if (!count)
            return false;
        memcpy(target, source, count);
        source += count;
        target += count;
        size -= count;
    }
    return true;
}
void wd_render_write_arena_at(uint32_t entry, uint32_t address, const void *source, size_t size) {
    if ((uint64_t)address + size > WD_ARENA_SIZE)
        abort();
    wd_surface_check(entry, address, (uint32_t)size, 1);
    memcpy(PTR(address), source, size);
}
void wd_render_write_arena(uint32_t address, const void *source, size_t size) {
    wd_render_write_arena_at(0x401935, address, source, size);
}
static void capture_main_frame(void) {
    if (!captured && MEM32(0x00661ebcu) >= 320 && MEM32(0x00661ec8u) >= 240) {
        uint32_t root = MEM32(0x00661ee8u);
        char error[256];
        wd_render_read_scope_begin();
        if (!wd_capture_scene_file(wd_render_read_arena, NULL, root, output_path, error,
                                   sizeof error)) {
            fprintf(stderr, "[render-capture] %s\n", error);
            abort();
        }
        wd_render_read_scope_end();
        captured = 1;
        fprintf(stderr, "[render-capture] captured original scene inputs to %s\n", output_path);
    }
    // No guest CALL/push here: the original consumes the caller's existing
    // return address. Nested reference scope keeps comparison rendering intact.
    wd_call_reference(original_frame);
}
void wd_scene_probe_init(void) {
    if (wd_render_requested())
        return; // direct submission owns optional input capture
    output_path = getenv("WD_SCENE_CAPTURE");
    if (!output_path || !*output_path)
        return;
    original_frame = recomp_lookup_reference(0x00459320);
    if (!original_frame || !wd_install_replacement(0x00459320, capture_main_frame)) {
        fprintf(stderr, "[render-capture] cannot install frame observer\n");
        abort();
    }
}
