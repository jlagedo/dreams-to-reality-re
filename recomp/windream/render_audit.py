"""Read/write probes emitted before explicit x86 memory operations.

Compile with WD_RENDER_AUDIT to enable. The probes observe guest addresses,
never materialize GPU pixels. Implicit stack accesses and host PTR() transfers
require their own boundary checks; this is not a complete access proof alone.
"""

from capstone import CS_AC_READ, CS_AC_WRITE
from capstone.x86 import X86_OP_MEM


def memory_probes(insn, address_expression):
    mnemonic = insn.mnemonic.split()[-1]
    if mnemonic == "lea":
        return []
    prefixes = (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3)
    opcode = next((b for b in insn.bytes if b not in prefixes), 0)
    if opcode in (0xA4, 0xA5, 0xAA, 0xAB, 0xAC, 0xAD):
        width = {"b": 1, "w": 2, "d": 4}[mnemonic[-1]]
        count = (
            "ecx"
            if any(b in (0xF2, 0xF3) for b in insn.bytes[: list(insn.bytes).index(opcode)])
            else "1u"
        )
        read = mnemonic.startswith(("movs", "lods"))
        write = mnemonic.startswith(("movs", "stos"))
        return [
            f"WD_AUDIT_STRING(0x{insn.address:08X}u, esi, edi, {count}, "
            f"{width}u, _df, {int(read)}, {int(write)});"
        ]
    lines = []
    for operand in insn.operands:
        if operand.type != X86_OP_MEM:
            continue
        access = operand.access or (CS_AC_READ | CS_AC_WRITE)
        address = address_expression(operand.mem)
        for flag, write in ((CS_AC_READ, 0), (CS_AC_WRITE, 1)):
            if access & flag:
                lines.append(
                    f"WD_AUDIT_MEMORY(0x{insn.address:08X}u, (uint32_t)({address}), "
                    f"{operand.size}u, {write});"
                )
    return lines
