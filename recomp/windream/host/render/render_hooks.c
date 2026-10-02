/* Guest ABI/control-flow half of the direct renderer. Gameplay traversal,
 * composition, callbacks and node-box maintenance remain lifted. */
#include "imports.h"
#include "render_fatal.h"
#include "render_live.h"
#include "render_movie.h"

static recomp_func_t function(uint32_t address) {
    recomp_func_t fn = recomp_lookup(address);
    if (!fn)
        wd_render_fatalf("missing guest function %08x", address);
    return fn;
}
static void call(uint32_t address, uint32_t return_address) {
    uint32_t stack = g_esp, caller = g_cur_func;
    PUSH32(g_esp, return_address);
    function(address)();
    g_cur_func = caller;
    if (g_esp != stack)
        wd_render_fatalf("guest call %08x unbalanced stack", address);
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
static void line(void) {
    const uint32_t destination = g_eax;
    if (!wd_render_surface_owned(destination)) {
        recomp_func_t original = recomp_lookup_reference(0x465c80);
        if (!original)
            wd_render_fatalf("missing reference function %08x", 0x465c80u);
        original();
        return;
    }
    /* C3D_Line_: EAX destination, EDX/EBX start, ECX/end y on stack,
     * packed colour on stack. Keep the retail clipper, including its unusual
     * right/bottom edge and the write back to the stack's end-y argument. */
    const uint32_t stack = g_esp, colour = WD_HOST_READ32(stack + 8);
    g_esp -= 12;
    WD_HOST_WRITE32(g_esp) = g_edx;
    WD_HOST_WRITE32(g_esp + 4) = g_ebx;
    WD_HOST_WRITE32(g_esp + 8) = g_ecx;
    g_eax = g_esp;
    g_edx = g_esp + 4;
    g_ebx = g_esp + 8;
    g_ecx = stack + 4;
    call(0x46569c, 0x465caa);
    if (g_eax) {
        const int32_t x0 = WD_HOST_READ32(g_esp), y0 = WD_HOST_READ32(g_esp + 4);
        const int32_t x1 = WD_HOST_READ32(g_esp + 8), y1 = WD_HOST_READ32(stack + 4);
        const int32_t width = WD_HOST_READ32(0x661ebc), height = WD_HOST_READ32(0x661ec8);
        if (x0 >= 0 && y0 >= 0 && x1 >= 0 && y1 >= 0 &&
            x0 <= width && x1 <= width && y0 <= height && y1 <= height) {
            wd_render_line(destination, x0, y0, x1, y1, colour);
            // The original swaps to an increasing major axis in its own
            // stack arguments, even though RET 8 discards those slots.
            const int steep = abs(x1 - x0) < abs(y1 - y0);
            if (steep ? y1 < y0 : x1 < x0) WD_HOST_WRITE32(stack + 4) = (uint32_t)y0;
        }
    }
    g_esp = stack + 12; // RET 8: return address plus two stack arguments
}
static void compose_object(void) {
    // The helper consumes the existing return address, exactly once.
    function(0x47e634)();
}
static void frame(int alternate) {
    const uint32_t caller = WD_HOST_READ32(g_esp);
    uint32_t destination = alternate ? g_edx : g_eax;
    uint32_t root;
    uint32_t post_hook = WD_HOST_READ32(0x4ac8d0);
    const int collector = WD_HOST_READ8(0x4ac8c8) != 0;
    if (!alternate)
        PUSH32(g_esp, g_edx);
    PUSH32(g_esp, g_ebp);
    if (alternate)
        call(0x455358, 0x4593aa);
    else {
        g_edx = destination;
        g_eax = WD_HOST_READ32(0x661ee8);
    }
    root = g_eax;
    if (collector) {
        PUSH32(g_esp, g_esi);
        PUSH32(g_esp, g_ebx);
        g_ebx = 0;
        g_esi = 0x478800;
        WD_HOST_WRITE32(0x4ac8cc) = g_esi;
    }
    WD_HOST_WRITE32(0x4ac8d0) = 0; // the old post-order hook only builds software raster work
    call(0x47e700, collector ? (alternate ? 0x4593cd : 0x45934c)
                            : (alternate ? 0x4593ff : 0x459384));
    const uint32_t frame_callback = WD_HOST_READ32(0x4aa704);
    const int main_frame = !alternate && WD_HOST_READ32(0x661ebc) >= 320;
    if (collector) wd_render_collect_scene(root);
    if (frame_callback) {
        if (!collector) wd_render_prepare_callback(root, destination, main_frame, caller);
        call(frame_callback, collector ? (alternate ? 0x4593dc : 0x45935b)
                                        : (alternate ? 0x45940e : 0x459393));
    }
    call(0x4610e8, collector ? (alternate ? 0x4593e1 : 0x459360)
                            : (alternate ? 0x459413 : 0x459398));
    if (collector) {
        // The Windows diagnostic branch restores the standard hooks and
        // deliberately leaves the previous image untouched (no flush/clear).
        WD_HOST_WRITE32(0x4ac8cc) = 0x473014;
        WD_HOST_WRITE32(0x4ac8d0) = 0x4731b8;
        g_ebx = WD_HOST_READ32(g_esp);
        g_esp += 4;
        g_esi = WD_HOST_READ32(g_esp);
        g_esp += 4;
    } else {
        WD_HOST_WRITE32(0x4ac8d0) = post_hook;
        wd_render_scene(root, destination, main_frame, caller, frame_callback);
    }
    g_eax = collector ? 0x4731b8 : destination;
    g_ebp = WD_HOST_READ32(g_esp);
    g_esp += 4;
    if (!alternate) {
        g_edx = WD_HOST_READ32(g_esp);
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
    wd_render_text_band((int32_t)WD_HOST_READ32(g_esp + 4));
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
            wd_render_fatalf("missing reference function %08x", 0x45fd36u);
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
    wd_render_forget_surface(WD_HOST_READ32(0x5e1094));
    recomp_func_t original = recomp_lookup_reference(0x417e7e);
    if (!original)
        wd_render_fatalf("missing reference function %08x", 0x417e7eu);
    original();
}
static void reset_scene(void) {
    wd_render_reset_scene();
    recomp_func_t original = recomp_lookup_reference(0x41f9db);
    if (!original)
        wd_render_fatalf("missing reference function %08x", 0x41f9dbu);
    original();
}
uint32_t wd_render_copy_caller(uint32_t instruction) {
    // memcpy_ saves ECX, ESI, EDI, ES and the original destination (20 bytes).
    return instruction == 0x45c28c || instruction == 0x45c293 ? WD_HOST_READ32(g_esp + 20) : 0;
}
static void captions(void) {
    wd_render_caption_scope_begin();
    recomp_func_t original = recomp_lookup_reference(0x436ab6);
    if (!original)
        wd_render_fatalf("missing reference function %08x", 0x436ab6u);
    original();
    wd_render_caption_scope_end();
}
static void fog_update(void) {
    wd_render_fog_update();
    recomp_func_t original = recomp_lookup_reference(0x41f9ba);
    if (!original)
        wd_render_fatalf("missing reference function %08x", 0x41f9bau);
    original(); // retain the Windows stub's ABI and stack-check effects
}
static void find_sky_node(void) {
    /* The DOS 3dfx build's SCENE_FindSkyNode first runs the level-load table
     * that retypes 39 named scene nodes to -7 (translucent) or 9 (wrap).
     * Windows links the identical routine at 0x41ce67 and never calls it.
     * It takes the scene actor in EAX and preserves every other register. */
    const uint32_t actor = g_eax;
    call(0x41ce67, 0x41d2cf);
    g_eax = actor;
    recomp_func_t original = recomp_lookup_reference(0x41d2cf);
    if (!original)
        wd_render_fatalf("missing reference function %08x", 0x41d2cfu);
    original();
}
static void palette_rows(void) {
    /* REND_UpdatePaletteRows(EAX slot, EDX/EBX/ECX the R/G/B offsets) writes
     * the Windows rows of one lighting-material slot. The DOS 3dfx twin
     * (DREAMSFX 0x3fca4) writes different ones: the host keeps those, from the
     * same arguments, and the guest's stay as Windows makes them. The page
     * comes from the routine's own lookup, MDL_FindMaterial(slot + 8), which
     * reads tables only and preserves every register but EAX. */
    const uint32_t slot = g_eax;
    g_eax = slot + 8;
    call(0x466040, 0x42e8dd);
    const uint32_t page = g_eax;
    g_eax = slot;
    wd_render_palette_rows(slot, page, (int32_t)g_edx, (int32_t)g_ebx, (int32_t)g_ecx);
    recomp_func_t original = recomp_lookup_reference(0x42e8b1);
    if (!original)
        wd_render_fatalf("missing reference function %08x", 0x42e8b1u);
    original();
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
        {0x465c80, line},            {0x41d2cf, find_sky_node}, {0x42e8b1, palette_rows},
        {0x42665a, wd_render_hnm5},
    };
    for (size_t i = 0; i < sizeof entries / sizeof entries[0]; ++i)
        if (!wd_install_replacement(entries[i].address, entries[i].fn))
            wd_render_fatalf("cannot install the replacement of %08x", entries[i].address);
}
