"""GPU-aware bulk transfers; ordinary RAM keeps the lifted body."""


def wrap_bulk(insn, body, probes):
    prefixes = (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3)
    opcode = next((b for b in insn.bytes if b not in prefixes), 0)
    if opcode not in (0xA4, 0xA5, 0xAA, 0xAB):
        return None
    before = insn.bytes[: list(insn.bytes).index(opcode)]
    if not any(b in (0xF2, 0xF3) for b in before):
        return None
    width = {"b": 1, "w": 2, "d": 4}[insn.mnemonic[-1]]
    move = opcode in (0xA4, 0xA5)
    call = (
        f"wd_render_copy(0x{insn.address:08X}u, esi, edi, ecx, {width}u, _df)"
        if move
        else f"wd_render_fill(0x{insn.address:08X}u, edi, eax, ecx, {width}u, _df)"
    )
    advance = ("esi += _step; " if move else "") + "edi += _step; ecx = 0;"
    return [
        f"if ({call}) {{ uint32_t _step = (uint32_t)(_df * ecx * {width}u); {advance} }} else {{",
        *probes,
        *body,
        "}",
    ]
