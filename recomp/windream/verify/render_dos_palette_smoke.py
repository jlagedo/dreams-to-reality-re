"""Compare the host port of the DOS 3dfx palette rows with DREAMSFX.EXE's x86.

uv run --with unicorn python recomp/windream/verify/render_dos_palette_smoke.py

Replays REND_UpdatePaletteRows (0x3fca4) with the build's own
REND_ApplyPaletteOffsets (0x2f933) and the add-clamp table its 0x2e952 fills,
and requires od_dos_palette_rows to leave the same 32 rows. DREAMSFX.EXE is an
LE executable; its code comes from the Ghidra project as an Inspect.java
`bytes:` dump (fix-ups applied, image base 0), cached under
DREAMS_OUT/recomp/dos-palette/. Build the oracle library first:
uv run python recomp/windream/verify/direct_render_validate.py
"""

import ctypes as c
import json
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

from unicorn import UC_ARCH_X86, UC_MODE_32, Uc
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_ECX,
    UC_X86_REG_EDX,
    UC_X86_REG_ESP,
)

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "recomp"))
import recomp_env  # noqa: E402

RANGES = ("0002e952-0002e985", "0002f933-0002fa34", "0003fca4-0003ff3c")
TABLE_INIT, UPDATE = 0x2E952, 0x3FCA4
FIND_MATERIAL, LOADING = 0x779E0, 0x5136C  # stubbed: return a test-supplied value in EAX
STOP, STACK = 0x10000, 0x900000
PAGE, SLOT, ACTOR, PROJECT, RESULTS = 0x408000, 0x18EF00, 0x1F0000, 0x1F8000, 0x1FC000
EFFECT, CURSOR, ACTOR_SCALE, SCALE = 0xFE790, 0xF6354, 0x19F710, 0x19F718
PROJECT_POINTER, TIMER = 0x188A20, 0x159FD8


class Update(c.Structure):
    _fields_ = [
        ("rgb", c.c_int32 * 3),
        ("actor_rgb", c.c_int32 * 3),
        ("actor_scale", c.c_int32),
        ("scale", c.c_int32),
        ("cursor", c.c_uint32),
        ("actor_bound", c.c_uint8),
        ("effect_light", c.c_uint8),
        ("rebuild", c.c_uint8),
        ("lit_project", c.c_uint8),
    ]


def code_bytes(out: Path) -> dict[int, bytes]:
    cache = out / "dreamsfx-code.txt"
    if not cache.is_file():
        result = subprocess.run(
            [sys.executable, str(ROOT / "re" / "tools" / "ghidra_headless.py"),
             "-process", "DREAMSFX.EXE", "-noanalysis", "-readOnly",
             "-postScript", "Inspect.java", *(f"bytes:{r}" for r in RANGES)],
            cwd=ROOT, capture_output=True, text=True, errors="replace", check=True,
        )  # fmt: skip
        cache.write_text(
            "\n".join(line for line in result.stdout.splitlines() if " BYTES " in line) + "\n",
            encoding="utf-8",
        )
    image: dict[int, int] = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        match = re.search(r"BYTES ([0-9a-f]{8}) ([0-9a-f]+)", line)
        if match:
            base = int(match.group(1), 16)
            for i, byte in enumerate(bytes.fromhex(match.group(2))):
                image[base + i] = byte
    chunks = {}
    for item in RANGES:
        lo, hi = (int(v, 16) for v in item.split("-"))
        chunks[lo] = bytes(image[a] for a in range(lo, hi + 1))
    return chunks


def main():
    out = recomp_env.out_dir("dos-palette")
    out.mkdir(parents=True, exist_ok=True)
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0x10000, 0xB0000)  # stop page and the code object
    uc.mem_map(0xF0000, 0x110000)  # data object, zero-filled
    uc.mem_map(0x400000, 0x10000)  # one palette bank and the page address above it
    uc.mem_map(STACK - 0x10000, 0x20000)
    for address, data in code_bytes(out).items():
        uc.mem_write(address, data)

    def stub(address, cell):  # mov eax, [cell]; ret. Code is translated once, so it stays fixed.
        uc.mem_write(address, b"\xa1" + struct.pack("<I", cell) + b"\xc3")

    def run(entry, **registers):
        for name, value in registers.items():
            uc.reg_write(
                {"eax": UC_X86_REG_EAX, "edx": UC_X86_REG_EDX, "ebx": UC_X86_REG_EBX,
                 "ecx": UC_X86_REG_ECX}[name],
                value & 0xFFFFFFFF,
            )  # fmt: skip
        uc.reg_write(UC_X86_REG_ESP, STACK)
        uc.mem_write(STACK, struct.pack("<I", STOP))
        uc.emu_start(entry, STOP, count=2000000)
        assert uc.reg_read(UC_X86_REG_ESP) == STACK + 4, hex(entry)

    run(TABLE_INIT)
    stub(FIND_MATERIAL, RESULTS)
    stub(LOADING, RESULTS + 4)
    uc.mem_write(RESULTS, struct.pack("<I", PAGE))
    dll = c.CDLL(str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"))
    rows = dll.wd_dos_palette_rows
    rows.argtypes = [c.POINTER(Update), c.POINTER(c.c_uint8), c.POINTER(c.c_uint16)]
    rows.restype = c.c_uint32
    rng = random.Random(3013)
    scales = (0, 0, 1, 2, 3, 4, 8, -1, -2, -5, 300, -300, 0x7FFFFFF)
    branches: dict[str, int] = {}
    cases = 4000
    for case in range(cases):
        wide = case % 7 == 0
        limit = 0x7FFFFFFF if wide else 160
        rgb = [rng.randint(-limit, limit) for _ in range(3)]
        actor_rgb = [rng.randint(-limit, limit) for _ in range(3)]
        update = Update(
            (c.c_int32 * 3)(*rgb), (c.c_int32 * 3)(*actor_rgb), rng.choice(scales),
            rng.choice(scales), rng.randrange(32), rng.randrange(2), rng.randrange(2), 0,
            rng.randrange(2),
        )  # fmt: skip
        loading = rng.randrange(2)
        timer = rng.choice((0.0, 0.0, -1.0, 0.5, 15.0))
        update.rebuild = int(loading or timer > 0)
        source = bytes(rng.randrange(256) for _ in range(1024))
        before = [rng.randrange(65536) for _ in range(32 * 256)]
        uc.mem_write(PAGE - 0x8000, b"".join(struct.pack("<HH", 0, v) for v in before))
        flags = 2 | (0x10 if update.actor_bound else 0)
        uc.mem_write(SLOT + 0x18, struct.pack("<II", flags, ACTOR))
        uc.mem_write(SLOT + 0x20, source)
        uc.mem_write(ACTOR + 0x98, struct.pack("<3i", *actor_rgb))
        uc.mem_write(PROJECT_POINTER, struct.pack("<I", PROJECT))
        uc.mem_write(PROJECT + 0xC8, struct.pack("<I", update.lit_project))
        uc.mem_write(EFFECT, struct.pack("<I", update.effect_light))
        uc.mem_write(CURSOR, struct.pack("<I", update.cursor))
        uc.mem_write(ACTOR_SCALE, struct.pack("<i", update.actor_scale))
        uc.mem_write(SCALE, struct.pack("<i", update.scale))
        uc.mem_write(TIMER, struct.pack("<f", timer))
        uc.mem_write(RESULTS + 4, struct.pack("<I", loading))
        run(UPDATE, eax=SLOT, edx=rgb[0], ebx=rgb[1], ecx=rgb[2])
        retail = struct.unpack("<16384H", uc.mem_read(PAGE - 0x8000, 0x8000))
        assert not any(retail[0::2]), case
        bank = (c.c_uint16 * (32 * 256))(*before)
        written = rows(c.byref(update), (c.c_uint8 * 1024)(*source), bank)
        differing = [r for r in range(32) if list(bank[r * 256 : r * 256 + 256])
                     != list(retail[1 + r * 512 : 1 + (r + 1) * 512 : 2])]  # fmt: skip
        assert not differing, (
            case, differing, update.actor_bound, update.effect_light, update.rebuild,
            update.lit_project, update.scale, update.actor_scale, update.cursor, rgb, actor_rgb,
        )  # fmt: skip
        changed = sum(1 << r for r in range(32) if before[r * 256 : r * 256 + 256]
                      != list(retail[1 + r * 512 : 1 + (r + 1) * 512 : 2]))  # fmt: skip
        assert changed & ~written == 0, (case, hex(changed), hex(written))
        key = (
            f"actor={update.actor_bound} light={update.effect_light} "
            f"rebuild={update.rebuild if update.effect_light else '-'} "
            f"c8={'-' if update.actor_bound or update.effect_light else update.lit_project} "
            f"rows={bin(written).count('1')}"
        )
        branches[key] = branches.get(key, 0) + 1
    report = {"cases": cases, "branches": dict(sorted(branches.items()))}
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for key, count in report["branches"].items():
        print(f"{count:5d}  {key}")
    print(f"DOS palette rows: {cases} cases match DREAMSFX.EXE x86")


if __name__ == "__main__":
    main()
