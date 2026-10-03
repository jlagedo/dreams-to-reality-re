"""Find DirectX interface GUIDs in a PE image and the code that references them.

Every DEFINE_GUID in the Windows SDK's ddraw.h, d3d.h, dinput.h and dsound.h is
searched for. Presence alone only shows what dxguid.lib linked in; with a
Ghidra feature dump, the instructions that load each GUID's address show what
the program actually asks for.

  uv run --with pefile --with capstone python re/guardian/tools/guids.py EXE [FEATURES_JSON]
"""

from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

import pefile

SDK = Path(r"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um")
HEADERS = ("ddraw.h", "d3d.h", "dinput.h", "dsound.h")


def sdk_guids() -> dict[bytes, str]:
    out = {}
    pat = re.compile(r"DEFINE_GUID\(\s*(\w+)\s*,((?:\s*0x[0-9a-fA-F]+\s*,?){11})\s*\)")
    for h in HEADERS:
        text = (SDK / h).read_text(encoding="latin-1")
        for name, nums in pat.findall(text):
            v = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", nums)]
            out.setdefault(struct.pack("<IHH8B", *v), name)
    return out


def main() -> None:
    path = sys.argv[1]
    pe = pefile.PE(path)
    base = pe.OPTIONAL_HEADER.ImageBase
    data = open(path, "rb").read()
    found = {}
    for raw, name in sdk_guids().items():
        o = data.find(raw)
        if o >= 0:
            found[base + pe.get_rva_from_offset(o)] = name
    if len(sys.argv) < 3:
        for va, name in sorted(found.items()):
            print(f"{va:08x}  {name}")
        return
    from capstone import CS_ARCH_X86, CS_MODE_32, Cs

    img = pe.get_memory_mapped_image()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    used: dict[int, list[str]] = {}
    for f in json.load(open(sys.argv[2])):
        a = int(f["entry"], 16)
        for ins in md.disasm(bytes(img[a - base : a - base + f["size"]]), a):
            for h in re.findall(r"0x[0-9a-f]{6,8}", ins.op_str):
                if int(h, 16) in found:
                    used.setdefault(int(h, 16), []).append(f"{ins.address:08x}")
    for va, name in sorted(found.items(), key=lambda kv: kv[1]):
        refs = used.get(va)
        mark = f"used at {', '.join(refs[:6])}" if refs else "linked, no code reference"
        print(f"{va:08x}  {name:<36} {mark}")


if __name__ == "__main__":
    main()
