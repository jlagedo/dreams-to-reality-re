/* Guest ABI/control-flow half of the direct renderer. Gameplay traversal,
 * composition, callbacks and node-box maintenance remain lifted. */
#include "imports.h"
#include "render_live.h"

static recomp_func_t function(uint32_t address) {
    recomp_func_t fn = recomp_lookup(address);
    if (!fn) {
        fprintf(stderr, "[direct] missing guest function %08x\n", address);
        abort();
    }
    return fn;
}
static void call(uint32_t address, uint32_t return_address) {
    uint32_t stack = g_esp, caller = g_cur_func;
    PUSH32(g_esp, return_address);
    function(address)();
    g_cur_func = caller;
    if (g_esp != stack) {
        fprintf(stderr, "[direct] guest call %08x unbalanced stack\n", address);
        abort();
    }
}
static void ui(uint32_t entry) {
    const uint32_t registers[8] = {g_eax, g_edx, g_ebx, g_ecx, g_esi, g_edi, g_esp, g_ebp};
    wd_render_ui(registers, entry);
    g_esp += 4;
}
static void sprite(void) { ui(0x401935); }
static void faded(void) { ui(0x403bcd); }
static void gauge(void) { ui(0x40368b); }
static void masked64(void) { ui(0x427b8c); }
static void compose_object(void) {
    // The helper consumes the existing return address, exactly once.
    function(0x47e634)();
}
static void frame(int alternate) {
    const uint32_t caller = MEM32(g_esp);
    uint32_t destination = alternate ? g_edx : g_eax;
    uint32_t root;
    uint32_t post_hook = MEM32(0x4ac8d0);
    if (MEM8(0x4ac8c8)) {
        fprintf(stderr, "[direct] diagnostic collector frame not implemented\n");
        abort();
    }
    if (!alternate)
        PUSH32(g_esp, g_edx);
    PUSH32(g_esp, g_ebp);
    if (alternate)
        call(0x455358, 0x4593aa);
    else {
        g_edx = destination;
        g_eax = MEM32(0x661ee8);
    }
    root = g_eax;
    MEM32(0x4ac8d0) = 0; // the old post-order hook only builds software raster work
    call(0x47e700, alternate ? 0x4593ff : 0x459384);
    const uint32_t frame_callback = MEM32(0x4aa704);
    if (frame_callback)
        call(frame_callback, alternate ? 0x45940e : 0x459393);
    call(0x4610e8, alternate ? 0x459413 : 0x459398);
    MEM32(0x4ac8d0) = post_hook;
    wd_render_scene(root, destination, !alternate && MEM32(0x661ebc) >= 320, caller,
                    frame_callback);
    g_eax = destination;
    g_ebp = MEM32(g_esp);
    g_esp += 4;
    if (!alternate) {
        g_edx = MEM32(g_esp);
        g_esp += 4;
    }
    g_esp += 4;
}
static void main_frame(void) { frame(0); }
static void alternate_frame(void) { frame(1); }
static void dim_background(void) {
    uint32_t level = g_eax;
    call(0x445bf2, 0x418074);
    wd_render_dim_background(level);
    call(0x445c3f, 0x4184b0);
    g_esp += 4;
}
static void text_band(void) {
    wd_render_text_band((int32_t)MEM32(g_esp + 4));
    g_esp += 4;
}
static void caption_band(void) {
    wd_render_caption_band();
    g_esp += 4;
}
static void fill_memory(void) {
    const uint32_t destination = g_eax, count = g_ebx, byte = g_edx & 255;
    if (!wd_render_fill(0x45fd36, destination, byte, count, 1, 1)) {
        recomp_func_t original = recomp_lookup_reference(0x45fd36);
        if (!original)
            abort();
        original();
        return;
    }
    // Watcom memset_ preserves EAX/ECX and returns EDX's repeated byte.
    // Its unrolled helper, not REP STOS, otherwise touches GPU-stale RAM.
    g_edx = byte * 0x01010101u;
    uint32_t prefix = (4 - (destination & 3)) & 3;
    if (prefix > count)
        prefix = count;
    g_flag_k = FK_EFLAGS;
    g_flag_a = ((count - prefix) & 3) == 3 ? 2 : 0x46;
    g_flag_b = 0;
    g_flag_cf = 0;
    g_esp += 4;
}
static void free_background(void) {
    wd_render_forget_surface(MEM32(0x5e1094));
    recomp_func_t original = recomp_lookup_reference(0x417e7e);
    if (!original)
        abort();
    original();
}
static void reset_scene(void) {
    wd_render_reset_scene();
    recomp_func_t original = recomp_lookup_reference(0x41f9db);
    if (!original)
        abort();
    original();
}
uint32_t wd_render_copy_caller(uint32_t instruction) {
    // memcpy_ saves ECX, ESI, EDI, ES and the original destination (20 bytes).
    return instruction == 0x45c28c || instruction == 0x45c293 ? MEM32(g_esp + 20) : 0;
}
static void captions(void) {
    wd_render_caption_scope_begin();
    recomp_func_t original = recomp_lookup_reference(0x436ab6);
    if (!original)
        abort();
    original();
    wd_render_caption_scope_end();
}
static void fog_update(void) {
    wd_render_fog_update();
    recomp_func_t original = recomp_lookup_reference(0x41f9ba);
    if (!original)
        abort();
    original(); // retain the Windows stub's ABI and stack-check effects
}
void wd_render_install(void) {
    if (!wd_render_requested())
        return;
    const struct {
        uint32_t address;
        wd_guest_replacement fn;
    } entries[] = {
        {0x401935, sprite},          {0x403bcd, faded},       {0x40368b, gauge},
        {0x47e498, compose_object},  {0x459320, main_frame},  {0x4593a4, alternate_frame},
        {0x418060, dim_background},  {0x4018e4, text_band},   {0x4368a1, caption_band},
        {0x417e7e, free_background}, {0x41f9db, reset_scene}, {0x436ab6, captions},
        {0x45fd36, fill_memory},     {0x427b8c, masked64},    {0x41f9ba, fog_update},
    };
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; ++i)
        if (!wd_install_replacement(entries[i].address, entries[i].fn))
            abort();
}
