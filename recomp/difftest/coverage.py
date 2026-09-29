"""Instruction-form coverage: which forms the game's code uses that no difftest
program exercises.

A form is the mnemonic plus the shape of each operand (register width, memory
width and addressing shape, immediate), e.g. `fld m64[b+d]`, `jmp m32[i*4+d]`,
`rep movsd`. Test programs cover a form if their lifted-and-compared code
contains it; a game form missing from every test is untested lifter surface.

usage:
  uv run --with capstone --with pefile python recomp/difftest/coverage.py
      [--coarse] GAME.EXE game_bounds.csv [test_dir ...]
  test_dir: a difftest work directory (<name>-<tag>/ with <name>.exe and bounds.csv)
"""

import collections
import glob
import os
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

from dreams import paths

# Test work directories (difftest.py) and the t_insn results.
WORK = paths.get("out") / "recomp" / "difftest"

HIGH8 = {"ah", "bh", "ch", "dh"}
COARSE = "--coarse" in sys.argv
SEG = {"cs", "ds", "es", "fs", "gs", "ss"}


def opnd(insn, op):
    if op.type == X86_OP_REG:
        r = insn.reg_name(op.reg)
        if r in HIGH8:
            return "r8h"
        if r in SEG:
            return "sreg"
        if r.startswith("st"):
            return "st0" if r == "st(0)" else "sti"
        return f"r{op.size * 8}"
    if op.type == X86_OP_IMM:
        return "imm"
    if op.type == X86_OP_MEM:
        m = op.mem
        shape = "+".join(
            p for p, on in (("b", m.base), (f"i*{m.scale}", m.index), ("d", m.disp)) if on
        )
        seg = insn.reg_name(m.segment) + ":" if m.segment else ""
        return f"{seg}m{op.size * 8}" if COARSE else f"{seg}m{op.size * 8}[{shape}]"
    return "?"


def form(insn):
    prefix = ""
    if insn.mnemonic.startswith(("rep ", "repe ", "repne ")):
        prefix = ""  # capstone folds the prefix into the mnemonic
    if (
        insn.mnemonic in ("call", "jmp")
        or insn.mnemonic.startswith("j")
        or insn.mnemonic.startswith("loop")
    ):
        ops = insn.operands
        if ops and ops[0].type == X86_OP_IMM:
            return f"{insn.mnemonic} rel"
    return (prefix + insn.mnemonic + " " + ",".join(opnd(insn, o) for o in insn.operands)).strip()


def forms_of(exe, bounds):
    pe = pefile.PE(exe, fast_load=True)
    text = next(s for s in pe.sections if s.Characteristics & 0x20)
    base = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    code = text.get_data()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    count = collections.Counter()
    where = {}
    for s, e in function_bounds(bounds, base, len(code)):
        for insn in reachable(md, code, base, s, e):
            f = form(insn)
            count[f] += 1
            where.setdefault(f, insn.address)
    return count, where


def function_bounds(bounds, base, size):
    for line in open(bounds):
        s, e = (int(x, 16) for x in line.strip().split(","))
        if base <= s < base + size:
            yield s, min(e, base + size)


def u32(code, base, va):
    o = va - base
    return int.from_bytes(code[o : o + 4], "little") if 0 <= o <= len(code) - 4 else None


def reachable(md, code, base, s, e):
    """Instructions reachable from `s` inside [s, e): recursive descent over
    branches and `jmp [reg*4 + table]` jump tables, so data in the code (inline
    jump tables, padding) is never decoded as instructions."""
    seen = {}
    work = [s]
    while work:
        pc = work.pop()
        while s <= pc < e and pc not in seen:
            insn = next(md.disasm(code[pc - base : pc - base + 16], pc), None)
            if insn is None:
                break
            seen[pc] = insn
            m = insn.mnemonic
            ops = insn.operands
            if m == "ret" or m == "retf" or m == "hlt":
                break
            if m == "jmp":
                if ops[0].type == X86_OP_IMM:
                    work.append(ops[0].imm)
                elif ops[0].type == X86_OP_MEM and bool(ops[0].mem.index) != bool(ops[0].mem.base):
                    k = ops[0].mem.disp & 0xFFFFFFFF
                    while True:
                        t = u32(code, base, k)
                        if t is None or not s <= t < e:
                            break
                        work.append(t)
                        k += 4
                break
            if (m.startswith("j") or m.startswith("loop")) and ops and ops[0].type == X86_OP_IMM:
                work.append(ops[0].imm)
            pc += insn.size
    return [seen[a] for a in sorted(seen)]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    game, gb = args[0], args[1]
    tests = args[2:] or sorted(
        d for d in glob.glob(str(WORK / "*-*")) if os.path.isfile(os.path.join(d, "bounds.csv"))
    )
    gcount, gwhere = forms_of(game, gb)
    covered = collections.Counter()
    for d in tests:
        exe = next(iter(glob.glob(os.path.join(d, "*.exe"))), None)
        if exe:
            c, _ = forms_of(exe, os.path.join(d, "bounds.csv"))
            covered.update(c)
            print(f"{os.path.basename(d)}: {len(c)} forms")
    miss = [(n, f) for f, n in gcount.items() if f not in covered]
    miss.sort(reverse=True)
    tot = sum(gcount.values())
    cov = sum(n for f, n in gcount.items() if f in covered)
    print(
        f"game: {len(gcount)} forms, {tot} instructions; covered {len(gcount) - len(miss)} forms, "
        f"{cov / tot:.1%} of instructions"
    )
    print("uncovered (game count, form, first address):")
    for n, f in miss:
        print(f"  {n:6d}  {f:40s} 0x{gwhere[f]:08X}")


if __name__ == "__main__":
    main()
