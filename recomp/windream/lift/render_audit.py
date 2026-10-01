"""Read/write probes for guest memory operations.

Compile with WD_RENDER_AUDIT to enable. The probes observe guest addresses,
never materialize GPU pixels. Compare/scan probes are inserted at each executed
iteration by render_bulk. Host PTR() transfers still need boundary checks.
"""

from capstone import CS_AC_READ, CS_AC_WRITE
from capstone.x86 import X86_OP_MEM, X86_REG_ESP


def instruction_prefixes(insn):
    for byte in insn.bytes:
        if byte not in (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3):
            break
        yield byte


def stack_probes(insn):
    """Implicit stack footprints, evaluated before the instruction changes ESP."""
    mnemonic = insn.mnemonic
    width = 2 if 0x66 in instruction_prefixes(insn) else 4
    ranges = []
    if mnemonic in ("push", "pushf", "pushfd", "call"):
        ranges = [(f"esp - {width}u", width, 1)]
    elif mnemonic in ("pop", "popf", "popfd", "ret", "retn"):
        ranges = [("esp", width, 0)]
    elif mnemonic in ("pushal", "pushad", "pusha", "pushaw"):
        ranges = [(f"esp - {8 * width}u", 8 * width, 1)]
    elif mnemonic in ("popal", "popad", "popa", "popaw"):
        # POPA skips the saved SP slot rather than reading it.
        ranges = [("esp", 3 * width, 0), (f"esp + {4 * width}u", 4 * width, 0)]
    elif mnemonic == "leave":
        ranges = [("ebp", width, 0)]
    elif mnemonic == "enter":
        level = insn.operands[1].imm & 31
        ranges = [(f"esp - {width}u", width, 1)]
        if level:
            ranges += [(f"ebp - {i * width}u", width, 0) for i in range(1, level)]
            ranges += [(f"esp - {(level + 1) * width}u", level * width, 1)]
    return [
        f"WD_AUDIT_MEMORY(0x{insn.address:08X}u, (uint32_t)({address}), {size}u, {write});"
        for address, size, write in ranges
    ]


def memory_probes(insn, address_expression):
    mnemonic = insn.mnemonic.split()[-1]
    if mnemonic == "lea":
        return []
    prefixes = (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3)
    opcode = next((b for b in insn.bytes if b not in prefixes), 0)
    if opcode in (0xA6, 0xA7, 0xAE, 0xAF):
        # ECX is only an upper bound: REPE/REPNE can stop on the first pair.
        return []
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
    lines = stack_probes(insn)
    for operand in insn.operands:
        if operand.type != X86_OP_MEM:
            continue
        access = operand.access or (CS_AC_READ | CS_AC_WRITE)
        address = address_expression(operand.mem)
        if mnemonic == "pop" and operand.mem.base == X86_REG_ESP:
            # The destination effective address uses ESP after consuming the
            # stack slot, although probes execute before that state change.
            address = f"({address}) + {2 if 0x66 in instruction_prefixes(insn) else 4}u"
        for flag, write in ((CS_AC_READ, 0), (CS_AC_WRITE, 1)):
            if access & flag:
                lines.append(
                    f"WD_AUDIT_MEMORY(0x{insn.address:08X}u, (uint32_t)({address}), "
                    f"{operand.size}u, {write});"
                )
    return lines
