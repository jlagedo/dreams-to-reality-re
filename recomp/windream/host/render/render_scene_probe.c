/* Explicit one-frame diagnostic. Software remains the reference renderer;
 * this hook snapshots the future direct adapter's input before visual work. */
#include "render_fatal.h"
#include <stdbool.h>
#include "imports.h"
#include "guest_win32.h"
#include "render_live.h"

extern int wd_capture_scene_file(bool (*reader)(void *, uint32_t, void *, size_t), void *context,
                                 uint32_t root, const char *path, char *error, size_t error_size);
static const char *output_path;
static int captured;
static recomp_func_t original_frame;
// Guest range of the last committed run read inside a read scope.
static RECOMP_TLS uint32_t read_begin, read_end;
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
    uint32_t va = address;
    unsigned char *target = destination;
    while (size) {
        uint64_t available;
        if (read_scope && va >= read_begin && va < read_end) {
            available = (uint64_t)read_end - va;
        } else {
            uint32_t base, bytes;
            if (vm_state(va, &base, &bytes) != W32_MEM_COMMIT)
                return false;
            if (read_scope) {
                read_begin = base;
                read_end = base + bytes;
            }
            available = (uint64_t)base + bytes - va;
        }
        size_t count = size < available ? size : (size_t)available;
        if (!count)
            return false;
        memcpy(target, PTR(va), count);
        va += (uint32_t)count;
        target += count;
        size -= count;
    }
    return true;
}
void wd_render_write_arena_at(uint32_t entry, uint32_t address, const void *source, size_t size) {
    if ((uint64_t)address + size > WD_ARENA_SIZE)
        wd_render_fatalf("metadata write at %08x leaves the guest arena", address);
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
            wd_render_fatalf("[render-capture] %s", error);
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
        wd_render_fatal("[render-capture] cannot install frame observer");
    }
}
