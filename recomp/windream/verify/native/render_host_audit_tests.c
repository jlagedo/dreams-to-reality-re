/* CPU-only production host bridge checks; see test_render_codegen.py. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include "imports.h"

static unsigned char arena[8192];
static unsigned violations;
static wd_surface_access last;
/* The linked runtime's startup/dispatch paths are not part of this test. */
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
void host_mode_install(void) { abort(); }
void wd_render_close(void) { abort(); }
void imp_CreateFileA(void);
void imp_CloseHandle(void);
void imp_DeleteFileA(void);
void imp_SetFilePointer(void);
void imp_ReadFile(void);
void imp_WriteFile(void);
void imp_MultiByteToWideChar(void);
void imp_WideCharToMultiByte(void);
static void violation(const wd_surface_access* access, void* unused) {
    (void)unused;
    last = *access;
    ++violations;
}
static void arguments(const uint32_t* values, unsigned count) {
    g_esp = 256;
    memcpy(arena + g_esp + 4, values, count * 4);
}
static void surface(uint32_t base) {
    wd_surface_reset();
    wd_surface_desc desc = {base, 4, 2, 1, 4, WD_SURFACE_565, 1};
    wd_surface_id id = wd_surface_register(&desc);
    assert(id && wd_surface_set_authority(id, WD_SURFACE_GPU));
    wd_surface_set_violation_handler(violation, NULL);
    violations = 0;
}
int main(void) {
    g_mem_base = (ptrdiff_t)arena;
    g_cur_func = 0x123456;
    memcpy(arena + 1024, "A", 2);
    surface(1026);
    char text[64];
    assert(guest_str(1024, text, sizeof text) == 1 && !strcmp(text, "A"));
    assert(!violations);
    assert(guest_str(1026, text, 1) == 0 && !violations);
    guest_str(1026, text, 2);
    assert(violations == 1 && !last.write && last.bytes == 1);

    // Optimized push-label/jump calls and host callbacks bypass codegen probes.
    surface(2048);
    uint32_t stack = 2052;
    PUSH32(stack, 0x1234);
    assert(violations == 1 && last.write && last.address == 2048 && last.bytes == 4);
    assert(POP32_VAL(stack) == 0x1234 && stack == 2052);
    assert(violations == 2 && !last.write && last.address == 2048);
    PUSH16(stack, 0xabcd);
    assert(violations == 3 && last.write && last.address == 2050 && last.bytes == 2);
    assert(POP16_VAL(stack) == 0xabcd && stack == 2052);
    assert(violations == 4 && !last.write && last.address == 2050);

    surface(2052);
    const uint32_t mb[] = {1252, 0, 1024, UINT32_MAX, 2048, 32};
    arguments(mb, 6); imp_MultiByteToWideChar();
    assert(g_eax == 2 && !violations && MEM16(2048) == 'A' && MEM16(2050) == 0);
    surface(1026);
    arguments(mb, 6); imp_MultiByteToWideChar();
    assert(g_eax == 2 && !violations); /* no scan past NUL */
    surface(2048);
    uint32_t small[] = {1252, 0, 1024, UINT32_MAX, 2048, 1};
    arguments(small, 6); imp_MultiByteToWideChar();
    assert(!g_eax && !violations); /* failure has no destination writes */

    MEM16(1024) = 'A'; MEM16(1026) = 0;
    const uint32_t wc[] = {1252, 0, 1024, UINT32_MAX, 2048, 32, 0, 3000};
    surface(2050);
    arguments(wc, 8); imp_WideCharToMultiByte();
    assert(g_eax == 2 && !violations && !strcmp((char*)arena + 2048, "A"));
    surface(1028);
    arguments(wc, 8); imp_WideCharToMultiByte();
    assert(g_eax == 2 && !violations && !MEM32(3000));

    wd_surface_reset();
    // Compare actual Windows validation: staging must preserve in-place errors,
    // while equal pointers remain valid for a sizing-only request.
    const int capacities[] = {0, 1, 16};
    for (unsigned wide = 0; wide < 2; ++wide) {
        for (unsigned c = 0; c < sizeof capacities / sizeof capacities[0]; ++c) {
            unsigned char native[64] = {'A', 0, 0, 0}, before[64];
            memcpy(before, native, sizeof before);
            SetLastError(12345);
            int expected = wide
                ? WideCharToMultiByte(1252, 0, (WCHAR*)native, -1, (char*)native, capacities[c], NULL, NULL)
                : MultiByteToWideChar(1252, 0, (char*)native, -1, (WCHAR*)native, capacities[c]);
            DWORD error = GetLastError();
            assert(!memcmp(native, before, sizeof before));
            memcpy(arena + 1024, before, sizeof before);
            uint32_t alias[] = {1252, 0, 1024, UINT32_MAX, 1024, (uint32_t)capacities[c], 0, 0};
            arguments(alias, wide ? 8 : 6);
            g_last_error = 12345;
            if (wide) imp_WideCharToMultiByte(); else imp_MultiByteToWideChar();
            assert(g_eax == (uint32_t)expected && g_last_error == error);
            assert(!memcmp(arena + 1024, before, sizeof before));
        }
    }

    /* A sandbox file through the bridges themselves (./sandbox, next to the test). */
    files_init("host-audit-root/GDIDREAM.EXE");
    memcpy(arena + 3200, "HOST-AUDIT.TMP", 15);
    memcpy(arena + 3300, "AB", 2);
    const uint32_t create[] = {3200, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0x80, 0};
    arguments(create, 7); imp_CreateFileA();
    uint32_t file = g_eax;
    assert(file != (uint32_t)-1);
    const uint32_t seed[] = {file, 3300, 2, 3000, 0};
    arguments(seed, 5); imp_WriteFile();
    assert(g_eax && MEM32(3000) == 2);
    const uint32_t rewind[] = {file, 0, 0, FILE_BEGIN};
    arguments(rewind, 4); imp_SetFilePointer();
    assert(g_eax == 0);
    surface(2050);
    const uint32_t read[] = {file, 2048, 32, 3000, 0};
    arguments(read, 5); imp_ReadFile();
    assert(g_eax && !violations && MEM32(3000) == 2 && !memcmp(arena + 2048, "AB", 2));
    const uint32_t eof[] = {file, 2050, 32, 3000, 0};
    arguments(eof, 5); imp_ReadFile();
    assert(g_eax && !violations && MEM32(3000) == 0);
    const uint32_t write[] = {file, 2050, 2, 3000, 0};
    arguments(write, 5); imp_WriteFile();
    assert(violations == 1 && !last.write && last.bytes == 2);
    wd_surface_reset();
    arguments(&file, 1); imp_CloseHandle();
    assert(g_eax);
    const uint32_t name[] = {3200};
    arguments(name, 1); imp_DeleteFileA();
    assert(g_eax);
    /* Exercise production vm_free against real Windows page state. */
    void* host_arena = VirtualAlloc(NULL, 32u * 1024 * 1024, MEM_RESERVE, PAGE_READWRITE);
    assert(host_arena);
    g_mem_base = (ptrdiff_t)host_arena;
    uint32_t allocation = vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(allocation);
    wd_surface_desc left_desc = {allocation + 512, 4, 2, 1, 4, WD_SURFACE_565, 1};
    wd_surface_desc spanning_desc = {allocation + 4096, 8192, 256, 16, 512, WD_SURFACE_565, 1};
    wd_surface_id left = wd_surface_register(&left_desc), spanning = wd_surface_register(&spanning_desc);
    wd_surface_desc description;
    assert(left && spanning);
    assert(vm_free(allocation + 5000, 1, MEM_DECOMMIT));
    assert(!wd_surface_describe(spanning, &description));
    assert(wd_surface_describe(left, &description));
    MEMORY_BASIC_INFORMATION page;
    assert(VirtualQuery(PTR(allocation + 4096), &page, sizeof page) && page.State == MEM_RESERVE);
    assert(VirtualQuery(PTR(allocation + 8192), &page, sizeof page) && page.State == MEM_COMMIT);
    assert(vm_alloc(allocation + 4096, 4096, MEM_COMMIT, PAGE_READWRITE));
    spanning_desc.allocation_generation++;
    wd_surface_id next = wd_surface_register(&spanning_desc);
    assert(next && next != spanning && !wd_surface_set_authority(spanning, WD_SURFACE_GPU));
    assert(!vm_free(allocation + 512, 0, MEM_DECOMMIT));
    assert(!vm_free(allocation + 512, UINT32_MAX, MEM_DECOMMIT));
    assert(wd_surface_describe(left, &description) && wd_surface_describe(next, &description));
    assert(vm_free(allocation, 0, MEM_RELEASE));
    assert(!wd_surface_describe(left, &description) && !wd_surface_describe(next, &description));
    uint32_t later = vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    left_desc.base = later;
    left = wd_surface_register(&left_desc);
    assert(left && vm_free(later, 0, MEM_DECOMMIT));
    assert(!wd_surface_describe(left, &description));
    assert(vm_alloc(later, 0x10000, MEM_COMMIT, PAGE_READWRITE));
    left = wd_surface_register(&left_desc);
    assert(left);
    // Force an actual host failure: invalidation/bookkeeping must not pretend
    // this final release succeeded after the backing arena disappeared.
    assert(VirtualFree(host_arena, 0, MEM_RELEASE));
    assert(!vm_free(later, 0, MEM_RELEASE));
    assert(wd_surface_describe(left, &description));
    wd_surface_reset();
    puts("host audit: NUL scans, short reads, EOF, conversion footprints and write consumers passed");
    return 0;
}
