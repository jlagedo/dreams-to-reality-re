"""GPU-aware bulk transfers; ordinary RAM keeps the lifted body."""

import re

from render_audit import instruction_prefixes


def wrap_bulk(insn, body, probes):
    prefixes = (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3)
    opcode = next((b for b in insn.bytes if b not in prefixes), 0)
    if opcode in (0xA6, 0xA7, 0xAE, 0xAF):
        # Insert immediately before the loads, inside the lifter's REP loop.
        # Auditing ECX elements before the loop falsely rejects early exits.
        width = 1 if opcode in (0xA6, 0xAE) else (2 if 0x66 in instruction_prefixes(insn) else 4)
        addresses = ("esi", "edi") if opcode in (0xA6, 0xA7) else ("edi",)
        audit = " ".join(
            f"WD_AUDIT_MEMORY(0x{insn.address:08X}u, {address}, {width}u, 0);"
            for address in addresses
        )
        result = []
        matches = 0
        for line in body:
            if "_a = " in line and f"MEM{width * 8}(edi)" in line:
                # Exclude the initial '_a = 0' and the '_flag_a' assignment.
                load = re.search(r"\b_a = (?!0\b)", line)
                if load is None:
                    raise ValueError(f"missing compare/scan load at {insn.address:08x}")
                offset = load.start()
                line = line[:offset] + audit + " " + line[offset:]
                matches += 1
            result.append(line)
        if matches != 1:
            raise ValueError(f"unrecognized compare/scan lift at {insn.address:08x}")
        return result
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
