"""Disassemble N instructions at each ADDR:N of a PE image: disasm.py EXE ADDR:N ..."""

import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

pe = pefile.PE(sys.argv[1])
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = Cs(CS_ARCH_X86, CS_MODE_32)
for spec in sys.argv[2:]:
    a, n = spec.split(":")
    a = int(a, 16)
    n = int(n)
    print("---", hex(a))
    for i, ins in enumerate(md.disasm(bytes(img[a - base : a - base + n * 8]), a)):
        if i >= n:
            break
        print(f"  {ins.address:08x}  {ins.mnemonic:<8} {ins.op_str}")
