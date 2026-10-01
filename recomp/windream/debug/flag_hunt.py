"""Hunt retail debug switches in GDIDREAM.EXE (spec 005).

  flag_hunt.py              globals that code tests (cmp/test) but no
                            instruction writes by absolute address, with their
                            initial value: candidate unwritten debug flags
  flag_hunt.py --ptr VA...  every little-endian copy of each VA in the file: a
                            function with no caller and no copy is unreachable
                            (a global's copies are its operand references)

Function bounds come from recomp/windream/lift/bounds.csv and names from
re/names/WINDREAM.EXE.tsv (same addresses in both Windows programs). Arrays
written through a pointer (the key table at 0x6308d8, structure fields) show
up as false candidates: check each hit's code before poking it.

usage: uv run --with capstone --with pefile python recomp/windream/debug/flag_hunt.py
"""

import argparse
import collections
import csv
import re
import struct
from pathlib import Path

import capstone
import pefile
from capstone.x86 import X86_OP_MEM, X86_REG_INVALID

from dreams import paths

ROOT = Path(__file__).resolve().parents[3]
DATA_START = 0x49C000  # first data address past the code section
WRITERS = {
    "mov", "add", "sub", "inc", "dec", "or", "and", "xor", "not", "neg", "shl",
    "shr", "sar", "rol", "ror", "adc", "sbb", "xchg", "pop", "bts", "btr", "btc",
    "sete", "setne", "setg", "setl", "setge", "setle", "seta", "setb", "setae", "setbe",
}  # fmt: skip
FPU_STORES = {"fst", "fstp", "fist", "fistp"}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    ap.add_argument("--ptr", nargs="+", help="search the file for these addresses")
    args = ap.parse_args()

    exe = paths.disc(1) / "GDIDREAM.EXE"
    pe = pefile.PE(str(exe))
    base = pe.OPTIONAL_HEADER.ImageBase

    if args.ptr:
        raw = exe.read_bytes()

        def va(off: int) -> str:
            for s in pe.sections:
                if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
                    return f"0x{base + s.VirtualAddress + off - s.PointerToRawData:x}"
            return f"file+0x{off:x}"

        for a in (int(x, 16) for x in args.ptr):
            hits = [va(m.start()) for m in re.finditer(re.escape(struct.pack("<I", a)), raw)]
            print(f"0x{a:x}: {', '.join(hits) if hits else 'no copies'}")
        return

    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    with open(ROOT / "recomp/windream/lift/bounds.csv") as f:
        bounds = [tuple(int(x, 16) for x in r) for r in csv.reader(f)]
    names = {}
    for r in csv.reader(open(ROOT / "re/names/WINDREAM.EXE.tsv", encoding="utf-8"), delimiter="\t"):
        if r and r[0][:1].isdigit():
            names[int(r[0], 16)] = r[1]

    writes, tests = collections.defaultdict(list), collections.defaultdict(list)
    for lo, hi in bounds:
        for i in md.disasm(img[lo - base : hi - base], lo):
            for k, op in enumerate(i.operands):
                if (
                    op.type != X86_OP_MEM
                    or op.mem.base != X86_REG_INVALID
                    or op.mem.index != X86_REG_INVALID
                    or op.mem.disp < DATA_START
                ):
                    continue
                a = op.mem.disp & 0xFFFFFFFF
                if (k == 0 and i.mnemonic in WRITERS) or i.mnemonic in FPU_STORES:
                    writes[a].append(lo)
                elif i.mnemonic in ("cmp", "test"):
                    tests[a].append(lo)

    def initial(a: int) -> int:
        rva = a - base
        for s in pe.sections:
            size = max(s.Misc_VirtualSize, s.SizeOfRawData)
            if s.VirtualAddress <= rva < s.VirtualAddress + size:
                b = s.get_data()[rva - s.VirtualAddress :][:4]
                return struct.unpack("<I", b)[0] if len(b) == 4 else 0
        return 0

    hits = [a for a in sorted(tests) if not writes.get(a)]
    print(f"{len(hits)} globals tested but never written by absolute address")
    for a in hits:
        where = ", ".join(sorted({names.get(f, f"0x{f:x}") for f in tests[a]}))
        print(f"0x{a:x}  init=0x{initial(a):x}  tests={len(tests[a])}  in {where}")


if __name__ == "__main__":
    main()
