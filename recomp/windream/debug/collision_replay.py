"""Replay the sweep call that phys_hook.c reported, on real x86 semantics
(Unicorn), from the live game instead of a Windows full dump.

With WD_PHYS_INVARIANT=1 the hook prints the first PHYS_SweepAxis call that
re-adds a triangle, or the first one after which the overlap invariant fails:
`[sweep] call #N world W col C axis A c=.. r=.. entry lo=L hi=H exit lo=.. hi=..`
and its PHYS_AddCandidate / PHYS_RemoveCandidate events. The sweep's decisions
depend only on the endpoint arrays, the triangles and the collider's centre,
radius and stored positions (replay_sweep.py), and those arrays and triangles
do not change within a level, so the call can be replayed later: this starts
the project (bank_patch slot 0), waits for the hook's report, pauses, reads
the guest image and the heap regions the sweep touches, puts the collider back
to the reported entry state and runs the retail bytes of PHYS_SweepAxis
(0x45D420) in Unicorn with Add and Remove logged and skipped, as
replay_sweep.py does. Agreement means the recomp made retail's decisions;
a difference points at the lifted code.

usage: uv run --with unicorn python recomp/windream/debug/collision_replay.py --slot 2 [--wait 90]
       ... --run-dir out/recomp/windream/run-<tag>     (a running game, after its report)
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import bank_patch  # noqa: E402
import collision_check as cc  # noqa: E402
import game_nav as nav  # noqa: E402
import recomp_env  # noqa: E402

wdctl = nav.wdctl
IMAGE, IMAGE_SIZE = 0x400000, 0x2C1000  # guest image span ([*] image: 6 sections, span 0x2C1000)
SWEEP, ADD, REMOVE = 0x45D420, 0x45D10C, 0x45D25C
CALL = re.compile(
    r"\[sweep\] call #(\d+) world ([0-9A-F]{8}) col ([0-9A-F]{8}) axis (\d) c=(-?\d+) r=(\d+) "
    r"entry lo=(\d+) hi=(\d+) exit lo=(\d+) hi=(\d+) events (\d+)"
)
EVENT = re.compile(r"\[sweep\]\s+([AR]) ([0-9A-F]{8}) ret ([0-9A-F]{8})")


def reported_call(text: str) -> dict | None:
    m = CALL.search(text)
    if not m:
        return None
    events = [(e[1], int(e[2], 16), int(e[3], 16)) for e in EVENT.finditer(text[m.end() :])]
    return {
        "call": int(m[1]),
        "world": int(m[2], 16),
        "col": int(m[3], 16),
        "axis": int(m[4]),
        "c": int(m[5]),
        "r": int(m[6]),
        "entry": (int(m[7]), int(m[8])),
        "exit": (int(m[9]), int(m[10])),
        "events": events[: int(m[11])],
    }


def capture(ctl, call: dict) -> dict[int, bytes]:
    """The memory the sweep reads: the image (code and globals), the three
    endpoint arrays, every triangle record and the collider."""
    regions = {IMAGE: cc.read_big(ctl, IMAGE, IMAGE_SIZE)}
    n, a0, a1, a2 = struct.unpack("<4I", ctl.read(cc.WORLD, 16))
    for arr in (a0, a1, a2):
        regions[arr] = cc.read_big(ctl, arr, 2 * n * 8)
    triangles = sorted({struct.unpack_from("<I", regions[a0], i * 8)[0] for i in range(2 * n)})
    lo, hi = triangles[0] - 8, triangles[-1] + 0x60
    regions[lo] = cc.read_big(ctl, lo, hi - lo)
    regions[call["col"]] = ctl.read(call["col"], 0x40)
    return regions


def replay(regions: dict[int, bytes], call: dict) -> dict:
    from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc, UcError
    from unicorn.x86_const import (
        UC_X86_REG_EAX,
        UC_X86_REG_EBX,
        UC_X86_REG_EDX,
        UC_X86_REG_EIP,
        UC_X86_REG_ESP,
    )

    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    mapped = []
    for addr, data in sorted(regions.items()):
        start, end = addr & ~0xFFF, (addr + len(data) + 0xFFF) & ~0xFFF
        for a, b in mapped:  # regions may share pages: map only the new ones
            if start < b and end > a:
                start, end = max(start, b), max(end, b)
        if end > start:
            uc.mem_map(start, end - start)
            mapped.append((start, end))
        uc.mem_write(addr, data)
    stack = 0x7FF00000
    uc.mem_map(stack, 0x10000)
    esp, ret = stack + 0xF000, 0x7FF0FF00
    uc.mem_write(esp, struct.pack("<I", ret))
    col, axis = call["col"], call["axis"]
    uc.mem_write(col + axis * 4, struct.pack("<i", call["c"]))
    uc.mem_write(col + 0xC, struct.pack("<I", call["r"]))
    uc.mem_write(col + 0x14 + axis * 8, struct.pack("<II", *call["entry"]))
    uc.reg_write(UC_X86_REG_EAX, call["world"])
    uc.reg_write(UC_X86_REG_EDX, col)
    uc.reg_write(UC_X86_REG_EBX, axis)
    uc.reg_write(UC_X86_REG_ESP, esp)
    events = []

    def hook(uc, addr, size, _):
        if addr in (ADD, REMOVE):
            sp = uc.reg_read(UC_X86_REG_ESP)
            back = struct.unpack("<I", uc.mem_read(sp, 4))[0]
            events.append(("A" if addr == ADD else "R", uc.reg_read(UC_X86_REG_EBX), back))
            uc.reg_write(UC_X86_REG_ESP, sp + 4)
            uc.reg_write(UC_X86_REG_EIP, back)

    uc.hook_add(UC_HOOK_CODE, hook, begin=ADD, end=REMOVE)
    error = None
    try:
        uc.emu_start(SWEEP, ret, count=5_000_000)
    except UcError as e:
        error = f"{e} at {uc.reg_read(UC_X86_REG_EIP):#x}"
    exit_lo, exit_hi = struct.unpack("<II", uc.mem_read(col + 0x14 + axis * 8, 8))
    return {"events": events, "exit": (exit_lo, exit_hi), "error": error}


def compare(call: dict, retail: dict) -> dict:
    mine = [(k, t, r) for k, t, r in call["events"]]
    theirs = retail["events"]
    return {
        "same_events": mine == theirs,
        "same_exit": tuple(call["exit"]) == tuple(retail["exit"]),
        "recomp": {
            "exit": call["exit"],
            "events": [f"{k} {t:08X} ret {r:08X}" for k, t, r in mine],
        },
        "retail": {
            "exit": retail["exit"],
            "events": [f"{k} {t:08X} ret {r:08X}" for k, t, r in theirs],
            "error": retail["error"],
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--slot", type=int, help="project to start (bank_patch slot 0)")
    ap.add_argument("--run-dir", type=Path, help="a running game's directory instead")
    ap.add_argument("--wait", type=float, default=90, help="seconds to wait for the hook's report")
    ap.add_argument("--walk", action="store_true", help="hold UP and jump while waiting")
    ap.add_argument(
        "--target",
        choices=("invariant", "readd"),
        default="invariant",
        help="which call the hook reports: the first overlap-invariant violation "
        "(WD_PHYS_INVARIANT=1) or the first re-adding call (the variable unset)",
    )
    options = ap.parse_args()
    out = recomp_env.out_dir("collision-regression")
    if options.run_dir:
        game = None
        ctl = wdctl.Ctl.connect(run_dir=options.run_dir)
        text = (options.run_dir / "stderr.txt").read_text(errors="replace")
        tag = options.run_dir.name
    else:
        tag = f"collreplay-{options.slot:03d}-{options.target}"
        run_dir = recomp_env.out_dir("windream") / f"run-{tag}"
        shutil.rmtree(run_dir, ignore_errors=True)
        bank = bank_patch.Bank.from_disc(1)
        scene = bank.parsed(options.slot).scene
        if options.slot:
            bank.copy(options.slot, 0)
        bank.write(run_dir / "sandbox")
        game = wdctl.start_game(
            tag=tag,
            headless=True,
            args=["--renderer", "software"],
            extra_env={"WD_PHYS_INVARIANT": "1"} if options.target == "invariant" else {},
        )
        ctl = game.ctl
        nav.boot_into(ctl, scene)
        deadline = time.monotonic() + options.wait
        if options.walk:
            ctl.key_down("UP")
        text = ""
        while time.monotonic() < deadline:
            ctl.wait(ms=500)
            if options.walk:
                ctl.tap("CTRL")
            text = game.stderr_text
            if "[sweep] call #" in text:
                ctl.wait(ms=300)  # its event lines follow
                text = game.stderr_text
                break
        if options.walk:
            ctl.key_up("UP")
    call = reported_call(text)
    try:
        if call is None:
            print(f"[replay] {tag}: the hook reported no call")
            return 2
        ctl.pause()
        try:
            regions = capture(ctl, call)
        finally:
            ctl.resume()
    finally:
        if game:
            game.close()
    retail = replay(regions, call)
    result = {"tag": tag, "call": call} | compare(call, retail)
    path = out / f"replay-{tag}.json"
    path.write_text(json.dumps(result, indent=1, default=str))
    print(
        f"[replay] {tag}: call #{call['call']} axis {call['axis']} c={call['c']} r={call['r']} "
        f"entry {call['entry']}: recomp exit {tuple(call['exit'])} {len(call['events'])} events; "
        f"retail exit {retail['exit']} {len(retail['events'])} events"
        f"{' (' + retail['error'] + ')' if retail['error'] else ''}"
    )
    print(
        f"[replay] {'SAME' if result['same_events'] and result['same_exit'] else 'DIFFERENT'}; "
        f"{path}"
    )
    return 0 if result["same_events"] and result["same_exit"] else 1


if __name__ == "__main__":
    sys.exit(main())
