"""List functions in address ranges with their COM vtable calls and direct callees.

uv run --with pefile --with capstone python re/guardian/tools/vcalls.py EXE JSON LO-HI [LO-HI ...]
"""

import json
import re
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

pe = pefile.PE(sys.argv[1])
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
funcs = json.load(open(sys.argv[2]))
ranges = [tuple(int(x, 16) for x in r.split("-")) for r in sys.argv[3:]]
md = Cs(CS_ARCH_X86, CS_MODE_32)
for f in sorted(funcs, key=lambda f: int(f["entry"], 16)):
    a = int(f["entry"], 16)
    if not any(lo <= a < hi for lo, hi in ranges):
        continue
    v, d, imm = [], [], set()
    for ins in md.disasm(bytes(img[a - base : a - base + f["size"]]), a):
        if ins.mnemonic == "call":
            m = re.fullmatch(r"dword ptr \[(e[a-z]{2}) \+ (0x[0-9a-f]+)\]", ins.op_str)
            if m:
                v.append(m.group(2))
            elif ins.op_str.startswith("0x"):
                d.append(ins.op_str)
            else:
                v.append(ins.op_str)
        for g in re.findall(r"0x4[bc][0-9a-f]{4}\b", ins.op_str):
            imm.add(g)
    print(f"{a:08x} {f['size']:6d} {f['name']:<14} vcalls={','.join(v)}  calls={','.join(d[:8])}")
