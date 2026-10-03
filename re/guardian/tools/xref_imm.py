"""Linear-sweep every Ghidra function of a program and print instructions mentioning a value.

uv run --with pefile --with capstone python re/guardian/tools/xref_imm.py EXE JSON 0x4c74dc
"""

import json
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

pe = pefile.PE(sys.argv[1])
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
funcs = json.load(open(sys.argv[2]))
needles = [s.lower() for s in sys.argv[3:]]
md = Cs(CS_ARCH_X86, CS_MODE_32)
for f in funcs:
    a = int(f["entry"], 16)
    for ins in md.disasm(bytes(img[a - base : a - base + f["size"]]), a):
        text = f"{ins.mnemonic} {ins.op_str}"
        if any(n in text for n in needles):
            print(f"{ins.address:08x}  {f['name']:<16} {text}")
