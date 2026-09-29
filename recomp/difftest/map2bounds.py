"""Watcom linker map -> bounds.csv (start,end hex VAs) for lift.py.

Every symbol in segment 0001 (the code segment, mapped at image base + the
first section's RVA) starts a function that runs to the next symbol.

usage: python map2bounds.py prog.map prog.exe bounds.csv
"""

import re
import sys

import pefile


def main():
    mp, exe, out = sys.argv[1:4]
    pe = pefile.PE(exe, fast_load=True)
    code = pe.sections[0]
    base = pe.OPTIONAL_HEADER.ImageBase + code.VirtualAddress
    end = base + (code.Misc_VirtualSize or code.SizeOfRawData)
    starts = set()
    for line in open(mp, errors="replace"):
        m = re.match(r"0001:([0-9a-fA-F]{8})[*+ ]\s*\S", line)
        if m:
            starts.add(base + int(m.group(1), 16))
    starts = sorted(s for s in starts if s < end)
    with open(out, "w") as f:
        for i, s in enumerate(starts):
            e = starts[i + 1] if i + 1 < len(starts) else end
            f.write(f"{s:x},{e:x}\n")
    print(f"{len(starts)} functions, code 0x{base:08X}-0x{end:08X}")


if __name__ == "__main__":
    main()
