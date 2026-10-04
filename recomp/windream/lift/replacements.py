"""Generated entry wrappers; all binary-derived bodies remain under DREAMS_OUT.

The public sub_VA name remains the single entry for direct calls, dispatcher
calls and tails. Native handlers own precisely the original guest ABI. Calling
the original body consumes the existing guest return address (no second pop).
"""

RENDER_ENTRIES = frozenset(
    {
        0x459320,
        0x41F9DB,
        0x41F9BA,
        0x436AB6,
        0x4593A4,
        0x47E498,
        0x401935,
        0x403BCD,
        0x40368B,
        0x427B8C,
        0x4018E4,
        0x4368A1,
        0x417E18,
        0x417E7E,
        0x417F19,
        0x417F80,
        0x417FE7,
        0x418060,
        0x417EB9,
        0x417EEE,
        0x4268AC,
        0x42665A,
        0x45C278,
        0x45FD36,
        0x465C80,
        0x41D2CF,
        0x42E8B1,
    }
)

# Host replacements outside the renderer, installed by launch mode
# (docs/specs/008-editor-restoration/spec.md): CD_OpenAudio (host/sdl/
# launch_mode.c); the sprite blit the Windows build left empty, which the
# editor's sliders call (host/sdl/editor_menu.c, the July DOS _ZoomSpriteL16);
# the project bank's functions (host/sdl/editor_bank.c, spec 008 phase 4):
# free slot, project list, empty bank, Create, Load, Delete, Save, DDAT_Load,
# and SaveDiskScene_ (phase 5, the bank written to the developer folder);
# the pickers (host/sdl/editor_pickers.c, spec 008 phase 3): the five fillers,
# the five asset pages and the project page, and the objet, link,
# link-adventure and box lists, pages and finds.
HOST_ENTRIES = frozenset(
    {
        0x4042F1,
        0x402406,
        0x449822,
        0x44988F,
        0x448C6D,
        0x449E7A,
        0x449EE9,
        0x449F80,
        0x449F42,
        0x448F5F,
        0x44900A,
        # phase 3: fillers
        0x4486BD,
        0x4487ED,
        0x4488BF,
        0x448937,
        0x4489AB,
        # phase 3: asset pages and the project page
        0x44A6E4,
        0x44A990,
        0x449577,
        0x44902B,
        0x4492D1,
        0x44991E,
        # phase 3: objet, link, link-adventure, box (list, page, find)
        0x44A14A,
        0x44A1D3,
        0x44A4A9,
        0x44B3DF,
        0x44B452,
        0x44B6DF,
        0x44AD7D,
        0x44ADED,
        0x44B07A,
        0x44BB5A,
        0x44BBD0,
        0x44BE5D,
        # phase 7: the save guard (host/sdl/save_guard.c, every mode) and
        # MENU_RunGameMenu counted in Develop (host/sdl/dev_tools.c, the recorder)
        0x40F94A,
        0x4337C0,
    }
)

# Every entry that gets a wrapper, a reference original and a wd_try_replace call.
REPLACEABLE_ENTRIES = RENDER_ENTRIES | HOST_ENTRIES


def wrap_entry(body: str, address: int) -> str:
    public = f"sub_{address:08X}"
    original = f"wd_original_{address:08X}"
    declaration = f"void {public}(void)"
    if body.count(declaration) != 1:
        raise ValueError(f"replacement entry {address:08x} has no unique declaration")
    body = body.replace(declaration, f"void {original}(void)", 1)
    return body + (
        f"\nvoid {public}(void) {{\n    if (!wd_try_replace(0x{address:08X}u)) {original}();\n}}\n"
    )
