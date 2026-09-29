import sys

import capstone
import pefile

from dreams import paths

pe = pefile.PE(str(paths.disc(1) / "GDIDREAM.EXE"))
img = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for arg in sys.argv[1:]:
    lo, hi = (int(x, 16) for x in arg.split("-"))
    print(f"--- {lo:x}-{hi:x}")
    for i in md.disasm(img[lo - base : hi - base], lo):
        print(f"{i.address:08x}  {i.bytes.hex():<16} {i.mnemonic} {i.op_str}")
