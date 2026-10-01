"""Replay one PHYS_SweepAxis call from a reset collider, executing PHYS_AddCandidate /
PHYS_RemoveCandidate on real x86 semantics; malloc_/free_ are emulated.
usage: python replay_full.py <dump> <arena-hex> <collider-hex> <axis> [realmalloc]
(realmalloc runs the original malloc_/free_ on the dump heap instead of emulating them)"""

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

dump, arena, col, axis = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16), int(sys.argv[4])
d = Dump(dump, arena)
uc = Uc(UC_ARCH_X86, UC_MODE_32)
pages = sorted(
    {
        p
        for start, size, _ in d.ranges
        if arena <= start < arena + (1 << 32)
        for p in range((start - arena) & ~0xFFF, start - arena + size, 0x1000)
    }
)
runs = []
for p in pages:
    if runs and runs[-1][1] == p:
        runs[-1][1] = p + 0x1000
    else:
        runs.append([p, p + 0x1000])
for a, b in runs:
    uc.mem_map(a, b - a)
    for p in range(a, b, 0x1000):
        pg = d.read(p, 0x1000)
        if pg:
            uc.mem_write(p, pg)


def rd(va):
    return struct.unpack("<I", uc.mem_read(va, 4))[0]


def s32(va):
    return struct.unpack("<i", uc.mem_read(va, 4))[0]


def wr(va, v):
    uc.mem_write(va, struct.pack("<I", v & 0xFFFFFFFF))


STACK, HEAP = 0x7FF00000, 0x7E000000
uc.mem_map(STACK, 0x10000)
uc.mem_map(HEAP, 0x800000)
heap = [HEAP + 0x10]
RET = 0x7FF0FF00
esp = STACK + 0xF000
wr(esp, RET)
# the collider as PHYS_ClearCollider leaves it (tree and lists empty, positions 0)
for off in (0x2C, 0x30, 0x34):
    wr(col + off, 0)
for off in range(0x14, 0x2C, 4):
    wr(col + off, 0)
uc.reg_write(UC_X86_REG_EAX, 0x66E01C)
uc.reg_write(UC_X86_REG_EDX, col)
uc.reg_write(UC_X86_REG_EBX, axis)
uc.reg_write(UC_X86_REG_ESP, esp)

counts = {"malloc": 0, "free": 0}


def ret_now(uc):
    sp = uc.reg_read(UC_X86_REG_ESP)
    uc.reg_write(UC_X86_REG_EIP, rd(sp))
    uc.reg_write(UC_X86_REG_ESP, sp + 4)


def hook(uc, addr, size, _):
    if addr == 0x4602D5:  # malloc_(eax=size) -> eax
        n = (uc.reg_read(UC_X86_REG_EAX) + 15) & ~7
        p = heap[0]
        heap[0] += n
        uc.mem_write(p, b"\xaa" * n)
        uc.reg_write(UC_X86_REG_EAX, p)
        counts["malloc"] += 1
        ret_now(uc)
    elif addr == 0x460589:  # free_(eax)
        counts["free"] += 1
        ret_now(uc)


if len(sys.argv) < 6:
    uc.hook_add(UC_HOOK_CODE, hook, begin=0x4602D5, end=0x460589)
SEG = {
    0x4602DA: (-4, 1),
    0x4602DB: (-4, 2),
    0x4602DD: (-4, 2),  # push es / fs / gs
    0x4603B7: (4, 2),
    0x4603B9: (4, 2),
    0x4603BB: (4, 1),
}  # pop gs / fs / es


def seg(uc, addr, size, _):
    if addr in SEG:
        dsp, ln = SEG[addr]
        sp = uc.reg_read(UC_X86_REG_ESP) + dsp
        if dsp < 0:
            wr(sp, 0)
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_EIP, addr + ln)


uc.hook_add(UC_HOOK_CODE, seg, begin=0x4602DA, end=0x4603BB)
calls = {}


def cnt(uc, addr, size, _):
    if addr in (0x4602D5, 0x460589, 0x485E80):
        calls[addr] = calls.get(addr, 0) + 1


uc.hook_add(UC_HOOK_CODE, cnt, begin=0x4602D5, end=0x485E80)
try:
    uc.emu_start(0x45D420, RET, count=50_000_000)
except UcError as e:
    print("emulation error", e, hex(uc.reg_read(UC_X86_REG_EIP)))
print(
    f"exit lo={rd(col + 0x14 + axis * 8)} hi={rd(col + 0x18 + axis * 8)}  "
    f"malloc {counts['malloc']} free {counts['free']}"
)

# invariant on the emulated result
c, r = s32(col + axis * 4), s32(col + 0xC)
L, H = c - r, c + r
W = 0x66E01C
n = rd(W)
arr = rd(W + 4)


def find(tri):
    x = rd(col + 0x2C)
    for _ in range(4096):
        if not x:
            return 0
        k = rd(x)
        if k == tri:
            return x
        x = rd(x + (8 if tri > k else 0xC))
    return 0


bad = want_n = 0
for i in range(2 * n):
    if uc.mem_read(arr + i * 8 + 4, 1)[0]:
        continue
    t = rd(arr + i * 8)
    mn, mx = s32(t + 0x48 + axis * 4), s32(t + 0x54 + axis * 4)
    if mn == H or mx == L:
        continue
    want = mn < H and mx > L
    want_n += want
    x = find(t)
    has = bool(x and uc.mem_read(x + 4, 1)[0] & (1 << axis))
    if want != has:
        if bad < 5:
            print(f"   tri {t:08X} [{mn}, {mx}] vs [{L}, {H}]: {'missing' if want else 'extra'}")
        bad += 1
print("calls", {hex(k): v for k, v in calls.items()})
print(f"retail semantics: {want_n} overlapping triangles, {bad} invariant violations")
