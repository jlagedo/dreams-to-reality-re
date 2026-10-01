"""Compare shared fog control with original DREAMSFX x86 call arguments.

uv run --with unicorn python recomp/windream/verify/render_fog_smoke.py
The external GLIDE_SetFog call is observed, not simulated as game logic.
"""

import ctypes as c
import json
import math
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
    out = recomp_env.out_dir("fog")
    evidence = out / "retail-fog.txt"
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
                "bytes:00029288-00029358",
                "bytes:000f0aa0-000f0ac7",
            ],
            stdout=file,
            check=True,
        )
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0, 0x400000)
    for address, data in re.findall(r"BYTES ([0-9a-fA-F]+) ([0-9a-fA-F]+)", evidence.read_text()):
        uc.mem_write(int(address, 16), bytes.fromhex(data))
    dll = c.CDLL(str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"))
    native = dll.wd_fog_plan
    native.argtypes = [
        c.c_void_p,
        c.c_double,
        c.c_int32,
        c.POINTER(c.c_float),
        c.POINTER(c.c_uint32),
        c.POINTER(c.c_float),
        c.c_void_p,
    ]
    calls = []

    def hook(_uc, _address, _size, _context):
        sp = uc.reg_read(xr.UC_X86_REG_ESP)
        calls.append(
            (uc.reg_read(xr.UC_X86_REG_EAX), struct.unpack("<f", uc.mem_read(sp + 4, 4))[0])
        )
        uc.reg_write(xr.UC_X86_REG_EIP, int.from_bytes(uc.mem_read(sp, 4), "little"))
        uc.reg_write(xr.UC_X86_REG_ESP, sp + 8)

    uc.hook_add(UC_HOOK_CODE, hook, begin=0x671D0, end=0x671D0)
    cases = [
        (word, water, mode, delta, phase)
        for word in (0, 1, 32, -1, 32767)
        for water in (0, 1000)
        for mode in (0, 1, 2)
        for delta, phase in ((0.2, 0.0), (5.0, 3.13))
    ]
    boundary_phase = c.c_float(3.1).value
    cases.append((0, 1000, 2, (math.pi - 1e-8 - boundary_phase) / 0.02, boundary_phase))
    for word, water, mode, delta, phase in cases:
        record = bytearray(0x1D0)
        struct.pack_into("<i", record, 0xD4, water)
        struct.pack_into("<4i", record, 0x1C0, 23, 97, 211, word)
        uc.mem_write(0x200000, bytes(record))
        uc.mem_write(0x188A20, struct.pack("<I", 0x200000))
        uc.mem_write(0x151D3C, struct.pack("<i", mode))
        uc.mem_write(0x159E20, struct.pack("<d", delta))
        uc.mem_write(0x159FA0, struct.pack("<f", phase))
        uc.mem_write(0x3F0000, struct.pack("<I", 0x3FF000))
        uc.reg_write(xr.UC_X86_REG_ESP, 0x3F0000)
        uc.reg_write(xr.UC_X86_REG_FPCW, 0x027F)
        before = len(calls)
        uc.emu_start(0x29288, 0x3FF000, count=10000)
        assert len(calls) == before + 1 and uc.reg_read(xr.UC_X86_REG_ESP) == 0x3F0004
        original_phase = struct.unpack("<f", uc.mem_read(0x159FA0, 4))[0]
        native_phase, colour, density = c.c_float(phase), c.c_uint32(), c.c_float()
        table = (c.c_uint8 * 64)()
        native(
            c.create_string_buffer(bytes(record)),
            delta,
            mode,
            c.byref(native_phase),
            c.byref(colour),
            c.byref(density),
            table,
        )
        assert colour.value == calls[-1][0]
        assert abs(density.value - calls[-1][1]) <= 1e-9, (word, mode, density.value, calls[-1])
        assert native_phase.value == original_phase, (
            delta,
            phase,
            native_phase.value,
            original_phase,
        )
    report = {
        "control_cases": len(cases),
        "colour_density_phase_match": True,
        "scope": "original controller arguments; table hardware interpolation remains separate",
    }
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
