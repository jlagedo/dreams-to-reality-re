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
