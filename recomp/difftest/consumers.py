"""Which flag divergences can reach the game.

For every flag consumer in the game's reachable code (jcc, setcc, adc/sbb,
rcl/rcr, pushf, lahf, ...) find the instruction that last wrote each flag it
reads (walking back through the preceding instructions of its function), then
look that producer's form up in flagdiff.json (the bits t_insn found diverging).
Prints every consumer that reads a diverging bit, plus the producer forms the
suite has no verdict for.

usage:
  uv run --with capstone --with pefile python recomp/difftest/consumers.py GAME.EXE game_bounds.csv
"""

import collections
import json
import re
import sys

import coverage
import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

coverage.COARSE = True
CF, PF, AF, ZF, SF, OF = 0x1, 0x4, 0x10, 0x40, 0x80, 0x800
ALL = CF | PF | AF | ZF | SF | OF

CC = {
    "o": OF,
    "no": OF,
    "b": CF,
    "c": CF,
    "nae": CF,
    "ae": CF,
    "nb": CF,
    "nc": CF,
    "e": ZF,
    "z": ZF,
    "ne": ZF,
    "nz": ZF,
    "be": CF | ZF,
    "na": CF | ZF,
    "a": CF | ZF,
    "nbe": CF | ZF,
    "s": SF,
    "ns": SF,
    "p": PF,
    "pe": PF,
    "np": PF,
    "po": PF,
    "l": SF | OF,
    "nge": SF | OF,
    "ge": SF | OF,
    "nl": SF | OF,
    "le": ZF | SF | OF,
    "ng": ZF | SF | OF,
    "g": ZF | SF | OF,
    "nle": ZF | SF | OF,
}
NO_FLAGS = re.compile(
    r"^(mov|lea|push|pop|xchg|not|bswap|cdq|cwd|cbw|cwde|nop|jmp|j|set|cmov|stos|lods|movs|rep |"
    r"f(?!comi|ucomi)|wait|fwait|leave|enter|xlat|lahf|cld|std)"
)


def reads(insn):
    m = insn.mnemonic
    if m.startswith("j") and m not in ("jmp", "jecxz", "jcxz"):
        return CC.get(m[1:], 0)
    if m.startswith("set"):
        return CC.get(m[3:], 0)
    if m.startswith("cmov"):
        return CC.get(m[4:], 0)
    if m in ("adc", "sbb", "rcl", "rcr", "cmc", "salc"):
        return CF
    if m in ("pushf", "pushfd", "pushal", "lahf"):
        return ALL if m != "pushal" else 0
    if m in ("loope", "loopne"):
        return ZF
    if m in ("daa", "das", "aaa", "aas"):
        return CF | AF
    return 0


def writes(insn):
    """Flags the instruction writes (all for ALU ops; inc/dec keep CF)."""
    m = insn.mnemonic
    if m in ("inc", "dec"):
        return ALL & ~CF
    if m in ("clc", "stc", "cmc"):
        return CF
    if m in ("rol", "ror", "rcl", "rcr"):
        return CF | OF
    if m == "sahf":
        return CF | PF | AF | ZF | SF
    if m in ("call", "ret"):
        return ALL  # undefined afterwards: nothing to trace past a call
    if NO_FLAGS.match(m):
        return 0
    return ALL


def base_form(name):
    return re.sub(r" (#\d+|@game)$", "", name)


def main():
    game, gb = sys.argv[1], sys.argv[2]
    fd = json.load(open(coverage.WORK / "flagdiff.json"))
    diverge = collections.defaultdict(int)
    for name, v in fd.items():
        diverge[base_form(name)] |= v["flags"]
    tested = set()
    for line in open(coverage.WORK / "t_insn-run.txt", encoding="utf-8", errors="replace"):
        mm = re.match(r"\s+(ok|DIFF)\s+(.+?)\s+[0-9a-f]{8}$", line)
        if mm:
            tested.add(base_form(mm.group(2)))

    pe = pefile.PE(game, fast_load=True)
    text = next(s for s in pe.sections if s.Characteristics & 0x20)
    base = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    code = text.get_data()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    hits = collections.Counter()
    where = {}
    untested = collections.Counter()
    for s, e in coverage.function_bounds(gb, base, len(code)):
        body = coverage.reachable(md, code, base, s, e)
        for k, insn in enumerate(body):
            need = reads(insn)
            if not need:
                continue
            for j in range(k - 1, max(k - 40, -1), -1):
                p = body[j]
                w = writes(p) & need
                if not w:
                    continue
                if p.mnemonic in ("call", "ret"):
                    break
                f = coverage.form(p)
                if f not in tested:
                    untested[f"{f}  -> {insn.mnemonic}"] += 1
                elif diverge.get(f, 0) & w:
                    key = f"{f:28s} -> {insn.mnemonic:6s} reads {diverge[f] & w:03x}"
                    hits[key] += 1
                    where.setdefault(key, insn.address)
                need &= ~w
                if not need:
                    break
    print("consumers reading a diverging flag (producer form -> consumer: bits):")
    for key, n in hits.most_common():
        print(f"  {n:5d}  {key}   first at 0x{where[key]:08X}")
    if not hits:
        print("  none")
    print("producers the suite has no verdict for:")
    for key, n in untested.most_common(30):
        print(f"  {n:5d}  {key}")


if __name__ == "__main__":
    main()
