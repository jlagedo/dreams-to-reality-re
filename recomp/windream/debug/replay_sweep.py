"""Replay one PHYS_SweepAxis call on real x86 semantics (Unicorn) from a recomp full dump.

usage: python replay_sweep.py <dump> <arena-hex> <collider-hex> <axis> <entry-lo> <entry-hi>
Calls to PHYS_AddCandidate / PHYS_RemoveCandidate are logged and skipped: the
sweep's own decisions depend only on the endpoint arrays, the triangles and the
collider's centre, radius and stored positions, and Watcom callers never read
the callee's parameter registers (EAX EDX EBX) after a call.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "verify"))
from mdmp import Dump  # noqa: E402
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc, UcError
from unicorn.x86_const import (
    UC_X86_REG_EAX,
    UC_X86_REG_EBX,
    UC_X86_REG_EDX,
    UC_X86_REG_EIP,
    UC_X86_REG_ESP,
)

dump, arena, col, axis, lo, hi = (
    sys.argv[1],
    int(sys.argv[2], 16),
    int(sys.argv[3], 16),
    int(sys.argv[4]),
    int(sys.argv[5]),
    int(sys.argv[6]),
)
d = Dump(dump, arena)
uc = Uc(UC_ARCH_X86, UC_MODE_32)
# map every dumped page that lies inside the 4 GB guest window
pages = {}
for start, size, _rva in d.ranges:
    if arena <= start < arena + (1 << 32):
        va = start - arena
        for p in range(va & ~0xFFF, va + size, 0x1000):
            pages[p] = True
runs, prev = [], None
for p in sorted(pages):
    if prev is not None and p == runs[-1][1]:
        runs[-1][1] = p + 0x1000
    else:
        runs.append([p, p + 0x1000])
    prev = p
for a, b in runs:
    uc.mem_map(a, b - a)
    data = d.read(a, b - a)
    if data is None:  # partial: copy page by page
        for p in range(a, b, 0x1000):
            pg = d.read(p, 0x1000)
            if pg:
                uc.mem_write(p, pg)
    else:
        uc.mem_write(a, data)
print(f"mapped {len(runs)} runs, {sum(b - a for a, b in runs) >> 20} MB")

STACK = 0x7FF00000
uc.mem_map(STACK, 0x10000)
esp = STACK + 0xF000
RET = 0x7FF0FF00
uc.mem_write(esp, struct.pack("<I", RET))
uc.mem_write(col + 0x14 + axis * 8, struct.pack("<II", lo, hi))
uc.reg_write(UC_X86_REG_EAX, 0x66E01C)
uc.reg_write(UC_X86_REG_EDX, col)
uc.reg_write(UC_X86_REG_EBX, axis)
uc.reg_write(UC_X86_REG_ESP, esp)

events = []


def hook(uc, addr, size, _):
    if addr in (0x45D10C, 0x45D25C):
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret = struct.unpack("<I", uc.mem_read(sp, 4))[0]
        events.append(("A" if addr == 0x45D10C else "R", uc.reg_read(UC_X86_REG_EBX), ret))
        uc.reg_write(UC_X86_REG_ESP, sp + 4)
        uc.reg_write(UC_X86_REG_EIP, ret)


uc.hook_add(UC_HOOK_CODE, hook, begin=0x45D10C, end=0x45D25C)
try:
    uc.emu_start(0x45D420, RET, count=5_000_000)
except UcError as e:
    print("emulation error", e, hex(uc.reg_read(UC_X86_REG_EIP)))
lo2, hi2 = struct.unpack("<II", uc.mem_read(col + 0x14 + axis * 8, 8))
print(f"retail semantics: exit lo={lo2} hi={hi2}, events {len(events)}")
for k, t, r in events:
    print(f"   {k} {t:08X} ret {r:08X}")
