"""Replay retail Glide type-1 submission with controlled visible-face lists.

uv run --with unicorn python recomp/windream/verify/render_scene_mode_smoke.py
Only external Glide calls are intercepted. Geometry visibility is separately
tested against the direct renderer; this test does not assert old raster parity.
"""

import hashlib
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc
from unicorn import x86_const as xr

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402


def main():
    out = recomp_env.out_dir("scene-modes")
    evidence = out / "retail-glide.txt"
    with evidence.open("w") as file:
        subprocess.run(
            [
                sys.executable,
                "re/tools/ghidra_headless.py",
                "-process",
                "DREAMSFX.EXE",
                "-noanalysis",
                "-readOnly",
                "-postScript",
                "Inspect.java",
                "bytes:00067568-00068127",
            ],
            check=True,
            stdout=file,
        )
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0, 0x400000)
    code = bytearray()
    for address, data in re.findall(r"BYTES ([0-9a-fA-F]+) ([0-9a-fA-F]+)", evidence.read_text()):
        raw = bytes.fromhex(data)
        uc.mem_write(int(address, 16), raw)
        code.extend(raw)
    assert len(code) == 0x68128 - 0x67568

    def put(address, *values):
        uc.mem_write(address, struct.pack("<" + "I" * len(values), *values))

    def word(address):
        return int.from_bytes(uc.mem_read(address, 4), "little")

    colours = []
    draws = []
    combine = []

    def call(_uc, address, _size, _context):
        sp = uc.reg_read(xr.UC_X86_REG_ESP)
        if address == 0xB89F0:
            colours.append(word(sp + 4))
            count = 1
        elif address == 0xB89E1:
            combine.append([word(sp + 4 + i * 4) for i in range(5)])
            count = 5
        elif address in (0xB8B8F, 0xB8A36):
            draws.append(
                [struct.unpack("<15f", uc.mem_read(word(sp + 4 + i * 4), 60)) for i in range(3)]
            )
            count = 3
        else:
            raise AssertionError(hex(address))
        uc.reg_write(xr.UC_X86_REG_EIP, word(sp))
        uc.reg_write(xr.UC_X86_REG_ESP, sp + 4 + count * 4)

    for address in (0xB89F0, 0xB89E1, 0xB8B8F):
        uc.hook_add(UC_HOOK_CODE, call, begin=address, end=address)
    node, block, faces, vertices = 0x200000, 0x201000, 0x202000, 0x210000
    put(node + 0xA4, block)
    for b in range(2):
        at = block + b * 0x40
        put(at, at + 0x40 if b == 0 else 0, 1, 0xFFFF)
        put(at + 0x24, faces + b * 0x200)
        for i in range(3):
            f = faces + b * 0x200 + i * 56
            put(f, 1 if i == 1 else 0, f + 56 if i < 2 else 0)
            for corner in range(3):
                v = vertices + (b * 9 + i * 3 + corner) * 40
                put(f + 8 + corner * 12, v)
                uc.mem_write(v + 0x18, struct.pack("<fii", 4.0, 20 + corner * 10, 30 + corner * 5))
    stack, stop = 0x3F0000, 0x3FF000
    put(stack, stop)
    uc.reg_write(xr.UC_X86_REG_ESP, stack)
    uc.reg_write(xr.UC_X86_REG_EAX, node)
    uc.emu_start(0x67568, stop, count=100000)
    assert uc.reg_read(xr.UC_X86_REG_EIP) == stop
    assert uc.reg_read(xr.UC_X86_REG_ESP) == stack + 4
    assert colours == [0, 0x24BF9, 0, 0x24BF9], colours
    assert combine == [[1, 0, 1, 2, 0]] * 2, combine
    assert len(draws) == 4
    for triangle in draws:
        assert [v[:2] for v in triangle] == [(20.0, 30.0), (30.0, 35.0), (40.0, 40.0)]
    binds = []

    def material_call(_uc, address, _size, _context):
        sp = uc.reg_read(xr.UC_X86_REG_ESP)
        if address == 0x672A8:
            binds.append((uc.reg_read(xr.UC_X86_REG_EAX), uc.reg_read(xr.UC_X86_REG_EDX)))
            count = 0
        else:
            count = {0xB8B0D: 3, 0xB8B17: 2, 0xB8B85: 1}[address]
        uc.reg_write(xr.UC_X86_REG_EIP, word(sp))
        uc.reg_write(xr.UC_X86_REG_ESP, sp + 4 + count * 4)

    for address in (0x672A8, 0xB8B0D, 0xB8B17, 0xB8B85):
        uc.hook_add(UC_HOOK_CODE, material_call, begin=address, end=address)
    uc.hook_add(UC_HOOK_CODE, call, begin=0xB8A36, end=0xB8A36)
    put(node + 0xC4, 1)
    put(node + 0xD0, 29)
    put(block, 0, 3, 0x230000)
    put(0x230000, 0x240000)
    put(block + 0x24, faces)
    for i in range(2):
        f = faces + i * 0x80
        uc.mem_write(f, bytes(68))
        put(f, 1 if i == 0 else 0, faces + 0x80 if i == 0 else 0)
        uc.mem_write(f + 0x40, bytes([19 if i == 0 else 31]))
        for corner in range(3):
            v = vertices + corner * 40
            put(f + 8 + corner * 12, v)
            put(f + 0x34 + corner * 4, 0x250000)
    put(stack, stop)
    uc.reg_write(xr.UC_X86_REG_ESP, stack)
    uc.reg_write(xr.UC_X86_REG_EAX, node)
    uc.emu_start(0x67568, stop, count=100000)
    assert uc.reg_read(xr.UC_X86_REG_EIP) == stop and uc.reg_read(xr.UC_X86_REG_ESP) == stack + 4
    assert binds == [(0x240000, 19)], binds
    assert len(draws) == 5
    report = {
        "original_code_sha256": hashlib.sha256(code).hexdigest(),
        "colour_calls": colours,
        "draws": 4,
        "lit_block_requested_row_from_culled_head": binds[0][1],
        "lit_block_submitted_faces": len(draws) - 4,
        "block_resets": 2,
        "flagged_faces_skipped": 2,
        "stack_balanced": True,
        "scope": "retail submission; GPU visibility tested separately",
    }
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
