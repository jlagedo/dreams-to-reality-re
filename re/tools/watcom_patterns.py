"""Narrow, instruction-proven Watcom idioms used during binary comparison."""

from __future__ import annotations


def preincremented_loop_biases(instructions) -> dict[int, int]:
    """Relocation operand -> first-iteration index bias for a simple leaf loop.

    Accept only: index=0; loop: index+=stride; memory[index+address];
    cmp index,limit; jne loop. No calls, other branches or other index writes
    may intervene. The compiler encodes array_base-stride in such a loop.
    Keeping that encoded displacement as a global identity can collide with
    an unrelated symbol immediately before the array in another object file.
    """
    from capstone import CS_GRP_CALL, CS_GRP_JUMP
    from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

    positions = {ins.address: i for i, ins in enumerate(instructions)}
    output = {}
    for end, branch in enumerate(instructions):
        if branch.mnemonic not in ("jne", "jnz") or not branch.operands:
            continue
        if branch.operands[0].type != X86_OP_IMM:
            continue
        begin = positions.get(branch.operands[0].imm)
        if begin is None or begin >= end or end - begin < 2:
            continue
        step, compare = instructions[begin], instructions[end - 1]
        if step.mnemonic != "add" or len(step.operands) != 2:
            continue
        register, amount = step.operands
        if register.type != X86_OP_REG or amount.type != X86_OP_IMM or amount.imm <= 0:
            continue
        reg, stride = register.reg, amount.imm
        if (
            compare.mnemonic != "cmp"
            or len(compare.operands) != 2
            or compare.operands[0].type != X86_OP_REG
            or compare.operands[0].reg != reg
            or compare.operands[1].type != X86_OP_IMM
            or compare.operands[1].imm <= 0
            or compare.operands[1].imm % stride
        ):
            continue
        prefix = instructions[:begin]
        writes = [i for i, ins in enumerate(prefix) if reg in ins.regs_access()[1]]
        if not writes:
            continue
        initializer = prefix[writes[-1]]
        if not (
            initializer.mnemonic == "xor"
            and len(initializer.operands) == 2
            and all(op.type == X86_OP_REG and op.reg == reg for op in initializer.operands)
        ):
            continue
        region = instructions[begin + 1 : end]
        if any(
            ins.group(CS_GRP_CALL) or ins.group(CS_GRP_JUMP) for ins in prefix[writes[-1] + 1 :]
        ):
            continue
        if any(
            ins.group(CS_GRP_CALL) or ins.group(CS_GRP_JUMP) or reg in ins.regs_access()[1]
            for ins in region
        ):
            continue
        for ins in region:
            if ins.disp_size != 4:
                continue
            if any(
                op.type == X86_OP_MEM and op.mem.base == reg and op.mem.index == 0
                for op in ins.operands
            ):
                output[ins.address + ins.disp_offset] = stride
    return output
