"""Replay the renderer cut on retail x86 and saved original-game memory.

uv run --with unicorn python recomp/windream/debug/render_smoke.py --lifted --gpu dump.dmp

No running process or original data is modified. Reports and captured pages
stay in DREAMS_OUT/recomp/render-smoke. The optional GPU test uses synthetic
triangles, not game pixel parity. Retail-process dumps have arena base zero.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from mdmp import Dump
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_MODE_32, Uc
from unicorn import x86_const as xr

from dreams import paths
from dreams.formats import node as source_node

REGS = {
    n: getattr(xr, "UC_X86_REG_" + n.upper())
    for n in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp")
}
STACK = 0x10000000
ESP = STACK + 0xF000
STOP = STACK + 0xFF00
DRAW_OBJECT = 0x47E498
UPDATE_OBJECT = 0x47E634
DRAW_SCENE = 0x47E700
UPDATE_SCENE = 0x47E7AC
ROOT = 0x661EE8


class Replay:
    """Demand-map captured pages; fail on access to uncaptured memory."""

    def __init__(self, dump: Dump, mode: str = "full", *, pixels=False):
        self.dump = dump
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.pages: dict[int, bytes] = {}
        self.visits: list[int] = []
        self.flushes: list[dict] = []
        self.mode = mode
        self.uc.mem_map(STACK, 0x10000)
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        self.uc.reg_write(xr.UC_X86_REG_FPCW, 0x027F)
        self.at(DRAW_OBJECT, self._object)
        self.at(0x47E561, self._after_composition)
        # Only the pixel backend is stubbed; full mode runs the retail front end.
        if not pixels:
            self.at(0x473014, self._return)
            self.at(0x4731B8, self._return)
            self.at(0x4768CC, self._flush)
            self.at(0x476DEC, self._flush)
        self.at(0x460D5F, self._unexpected)

    def at(self, address, callback):
        self.uc.hook_add(UC_HOOK_CODE, callback, begin=address, end=address)

    def _unmapped(self, uc, access, address, size, value, _):
        self.ensure(address, size)
        return True

    def ensure(self, address, size):
        for page in range(address & ~0xFFF, (address + size + 4095) & ~0xFFF, 4096):
            if STACK <= page < STACK + 0x10000 or page in self.pages:
                continue
            data = self.dump.read(page, 4096)
            if data is None:
                raise RuntimeError(f"uncaptured page {page:08x}")
            self.uc.mem_map(page, 4096)
            self.uc.mem_write(page, data)
            self.pages[page] = data

    def read(self, address, size):
        self.ensure(address, size)
        return bytes(self.uc.mem_read(address, size))

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def write(self, address, data):
        self.ensure(address, len(data))
        self.uc.mem_write(address, data)

    def put(self, address, value):
        self.write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def _return(self, uc, address, size, _):
        esp = uc.reg_read(REGS["esp"])
        target = self.u32(esp)
        uc.reg_write(REGS["esp"], esp + 4)
        uc.reg_write(xr.UC_X86_REG_EIP, target)

    def _unexpected(self, uc, address, size, _):
        raise RuntimeError(f"unexpected retail printf at {address:08x}")

    def _object(self, uc, address, size, _):
        self.visits.append(uc.reg_read(REGS["eax"]))
        if self.mode == "helper":
            uc.reg_write(xr.UC_X86_REG_EIP, UPDATE_OBJECT)

    def _after_composition(self, uc, address, size, _):
        if self.mode == "prefix":
            # Original epilogue restores all five saved registers and returns.
            uc.reg_write(xr.UC_X86_REG_EIP, 0x47E586)

    def _flush(self, uc, address, size, _):
        self.flushes.append(
            {
                "destination": uc.reg_read(REGS["eax"]),
                "width": self.u32(0x661EBC),
                "height": self.u32(0x661EC8),
            }
        )
        self._return(uc, address, size, _)

    def run(self, entry, eax=0, edx=0, ebx=0xB0B0B0B0, ecx=0xC0C0C0C0, stack=()):
        initial = dict(
            eax=eax,
            ebx=ebx,
            ecx=ecx,
            edx=edx,
            esi=0x51515151,
            edi=0xD1D1D1D1,
            ebp=0xBEBEBEBE,
            esp=ESP,
        )
        for name, value in initial.items():
            self.uc.reg_write(REGS[name], value)
        self.uc.mem_write(ESP, struct.pack("<" + "I" * (1 + len(stack)), STOP, *stack))
        self.uc.reg_write(xr.UC_X86_REG_EFLAGS, 2)
        self.ensure(entry, 1)
        self.uc.emu_start(entry, STOP, count=30_000_000)
        if self.uc.reg_read(xr.UC_X86_REG_EIP) != STOP:
            raise AssertionError(
                f"instruction budget exhausted at {self.uc.reg_read(xr.UC_X86_REG_EIP):x}"
            )
        result = {name: self.uc.reg_read(reg) for name, reg in REGS.items()}
        result["fpcw"] = self.uc.reg_read(xr.UC_X86_REG_FPCW)
        result["fptop"] = (self.uc.reg_read(xr.UC_X86_REG_FPSW) >> 11) & 7
        # These functions preserve all GPRs except EAX; stack args, if any,
        # use Watcom callee cleanup (checked separately by each caller).
        return initial, result

    def composed(self, nodes):
        return {n: self.read(n + 0x4C, 48).hex() for n in nodes}


def check_abi(before, after, *, stack_bytes=0):
    for name in ("ebx", "ecx", "edx", "esi", "edi", "ebp"):
        assert before[name] == after[name], (name, before, after)
    assert after["esp"] == before["esp"] + 4 + stack_bytes, after
    assert after["fpcw"] == 0x027F and after["fptop"] == 0, after


def node_inventory(dump):
    root = dump.u32(ROOT)
    todo, seen = [root], set()
    while todo:
        node = todo.pop()
        if not node or node in seen:
            continue
        assert len(seen) < 10000
        seen.add(node)
        todo.extend(dump.dwords(node + 0x14, 2))
    return root, sorted(seen)


def lifted_build(out):
    """Extract only the tested closure from the current lift; do not edit it."""
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import recomp_env

    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from replacements import wrap_entry

    bodies = {}
    gen = paths.out_dir("recomp", "windream", "gen")
    for file in gen.glob("recomp_*.c"):
        for m in re.finditer(
            r"void sub_([0-9A-F]{8})\(void\) \{.*?^}", file.read_text(), re.M | re.S
        ):
            bodies[int(m[1], 16)] = m[0]
    stubs = {DRAW_OBJECT, 0x4731B8, 0x460D5F}
    pending, selected = [DRAW_SCENE, UPDATE_SCENE, UPDATE_OBJECT], {}
    while pending:
        va = pending.pop()
        if va in selected or va in stubs:
            continue
        body = bodies[va]
        selected[va] = body
        pending.extend(int(v, 16) for v in re.findall(r"sub_([0-9A-F]{8})", body))
        pending.extend(
            int(v, 16)
            for v in re.findall(r"RECOMP_(?:ITAIL|ICALL)(?:_RA)?\(0x([0-9A-F]{8})u", body)
        )
    declarations = "\n".join(
        f"void sub_{v:08X}(void);" for v in sorted(selected | dict.fromkeys(stubs))
    )
    dispatch = "recomp_func_t recomp_lookup(uint32_t va) { switch (va) {\n"
    dispatch += "\n".join(
        f"case 0x{v:08X}: return sub_{v:08X};" for v in sorted(selected | dict.fromkeys(stubs))
    )
    dispatch += '\ndefault: fprintf(stderr, "unexpected dispatch %08x\\n", va); exit(3); } }\n'
    (out / "render_smoke_lifted.inc").write_text(
        declarations
        + "\n"
        + "\n".join(selected.values())
        + "\n"
        + dispatch
        + wrap_entry(
            'void sub_0047E498(void) { fputs("replacement not installed\\n", stderr); abort(); }',
            DRAW_OBJECT,
        )
    )
    env = recomp_env.build_env()
    compiler = shutil.which("clang-cl", path=env.get("PATH"))
    if not compiler:
        raise RuntimeError("clang-cl not found")
    exe = out / "render_smoke_host.exe"
    source = Path(__file__).with_name("render_smoke_host.c")
    subprocess.run(
        [
            compiler,
            "/nologo",
            "/Od",
            "/w",
            f"/I{source.parent.parent / 'runtime'}",
            f"/I{out}",
            f"/Fe{exe}",
            f"/Fo{out}/",
            str(source),
            str(source.parent.parent / "runtime" / "render_boundary.cpp"),
        ],
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    print(
        f"  compiled {len(selected)} lifted functions + production replacement dispatch", flush=True
    )
    return exe


def gpu_smoke(out, sokol_dir):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import recomp_env

    env = recomp_env.build_env()
    compiler = shutil.which("clang-cl", path=env.get("PATH"))
    if not compiler or not (sokol_dir / "sokol_gfx.h").is_file():
        raise RuntimeError("GPU smoke requires clang-cl and the pinned sokol headers")
    exe = out / "render_gpu_smoke.exe"
    source = Path(__file__).with_name("render_gpu_smoke.cpp")
    subprocess.run(
        [
            compiler,
            "/nologo",
            "/O2",
            "/W3",
            "/std:c++17",
            f"/I{sokol_dir}",
            str(source),
            f"/Fo{out / 'render_gpu_smoke.obj'}",
            f"/Fe{exe}",
            "/link",
            "d3d11.lib",
            "dxgi.lib",
            "d3dcompiler.lib",
        ],
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    assert run.returncode == 0 and not run.stderr, (run.returncode, run.stderr)
    report = json.loads(run.stdout)
    (out / "gpu-readback.json").write_text(json.dumps(report, indent=2) + "\n")
    print("  hardware GPU: four targets, depth, orientation and readback OK", flush=True)
    return report


def geometry_contract(dump, nodes):
    owners = {}
    for n in nodes:
        count, start = dump.u32(n + 0x7C), dump.u32(n + 0x80)
        assert count < 1_000_000
        if count:
            assert dump.u32(n + 0xC0) == 40, "unexpected vertex stride"
        for i in range(count):
            assert start + i * 40 not in owners, "ambiguous vertex owner"
            owners[start + i * 40] = n
    modes = collections.Counter()
    total = cross = 0
    for n in nodes:
        block, seen = dump.u32(n + 0xA4), set()
        while block:
            assert block not in seen, "cyclic face-block list"
            seen.add(block)
            kind = struct.unpack("<i", dump.read(block + 4, 4))[0]
            count, start = dump.u32(block + 0x1C), dump.u32(block + 0x20)
            stride = dump.u32(block + 0x2C)
            assert stride in (56, 68) and count < 1_000_000, (kind, count, stride)
            modes[f"{kind}:{stride}"] += count
            for i in range(count):
                face = start + i * stride
                corners = [dump.u32(face + offset) for offset in (8, 20, 32)]
                assert all(v in owners for v in corners), (face, corners)
                cross += len({owners[v] for v in corners}) > 1
                total += 1
            block = dump.u32(block)
    return {
        "faces": total,
        "cross_node_triangles": cross,
        "unresolved_corners": 0,
        "type_stride_counts": dict(modes),
    }


def player_contract(dump, nodes):
    model = source_node.read_model(paths.get("disc1") / "DATA/3DC/XH_.DAN")
    assert model.nodes[0].address == 1
    parent_one = [n for n in model.nodes if n.parent == 1]
    assert parent_one
    r = Replay(dump)
    _, resolved = r.run(0x455358, dump.u32(0x4FBA78 + 0x74))
    player = resolved["eax"]
    assert dump.u32(player + 0x10) == dump.u32(ROOT)
    for n in parent_one:
        assert dump.u32(player - 1 + n.address + 0x10) == player
    record = dump.u32(0x661E04)
    spawn = struct.unpack("<3i", dump.read(record + 0xB4, 12))
    results = []
    for mode in ("full", "helper"):
        r = Replay(dump, mode)
        # Execute the original placement routine in the private replay only.
        r.run(0x41D581, 0x4FBA78)
        assert struct.unpack("<3i", r.read(player + 0x1C, 12)) == spawn
        r.run(DRAW_SCENE, dump.u32(ROOT))
        results.append(r.composed(nodes))
    assert results[0] == results[1]
    return {
        "encoded_parent_one_links": len(parent_one),
        "resolve_to_first_node": True,
        "spawn": spawn,
        "model_root_at_spawn": True,
        "composed_fields_equal": True,
    }


def shadow_smoke(path, arena, out):
    """Optional real-shadow capture: original instructions, including rasterization.

    The capture may come from the recomp; distinguish it from the independent
    original-process dumps used by the main checks. Slot zero must own Duncan.
    """
    dump = Dump(path, arena)
    _, nodes = node_inventory(dump)
    assert dump.u32(0x62B528) == dump.u32(0x4FBAEC), "slot zero is not the player shadow"
    dest = dump.u32(0x62B9A8)
    original = Replay(dump, pixels=True)
    original.write(dest, bytes([0x5A]) * 65536)
    original.run(0x43EB67, 0x4FBA78, 0x62B51C)
    mask = original.read(dest, 65536)
    assert set(mask) == {0, 1}, "unexpected shadow palette indices or unwritten sentinel"
    assert all(mask[i] == mask[i + 1] for i in range(0, len(mask), 2))
    replacement = Replay(dump, "helper")
    replacement.run(0x43EB67, 0x4FBA78, 0x62B51C)
    assert original.visits == replacement.visits and original.visits
    assert original.composed(nodes) == replacement.composed(nodes)
    assert replacement.flushes == [{"destination": dest, "width": 128, "height": 256}]
    for address in (0x661EBC, 0x661EC8):
        assert original.u32(address) == replacement.u32(address)
    (out / "shadow-mask.p8").write_bytes(mask)
    report = {
        "dump": str(path),
        "arena": arena,
        "visited_nodes": len(original.visits),
        "destination": dest,
        "bytes": len(mask),
        "palette_index_counts": dict(collections.Counter(mask)),
        "horizontal_pairs_equal": True,
        "all_sentinel_bytes_overwritten": True,
        "composed_bytes_equal": True,
        "viewport_restored": [original.u32(0x661EBC), original.u32(0x661EC8)],
        "scope": "original x86 shadow rasterizer on captured state; no GPU shadow implementation",
    }
    dump.f.close()
    (out / "shadow-contract.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        f"  original shadow rasterizer: {len(original.visits)} nodes, paired P8 mask OK", flush=True
    )
    return report


def check_lifted(exe, out, name, replay, entry, before, after, nodes, patches=()):
    fixture, result = out / f"{name}.rsm", out / f"{name}.result"
    replay.composed(nodes)  # include every compared node's page
    pages = {va: bytearray(data) for va, data in replay.pages.items()}
    for va, data in patches:
        offset = va & 4095
        pages[va & ~4095][offset : offset + len(data)] = data
    with fixture.open("wb") as f:
        f.write(struct.pack("<12I", 0x314D5352, entry, *before.values(), len(nodes), len(pages)))
        f.write(struct.pack("<" + "I" * len(nodes), *nodes))
        for va, data in sorted(pages.items()):
            f.write(struct.pack("<I", va))
            f.write(data)
    run = subprocess.run(
        [str(exe), str(fixture), str(result)], capture_output=True, text=True, timeout=20
    )
    assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
    assert not run.stderr, run.stderr
    data = result.read_bytes()
    regs = dict(zip([*REGS, "fpcw", "fptop"], struct.unpack("<10I", data[:40]), strict=True))
    assert regs == after, (regs, after)
    expected = b"".join(bytes.fromhex(v) for v in replay.composed(nodes).values())
    assert data[40:] == expected, f"lifted node mismatch: {name}"


def audit_dump(path, out, lifted=None):
    dump = Dump(path, 0)
    root, nodes = node_inventory(dump)
    print(f"{path.name}: {len(nodes)} nodes, root {root:08x}", flush=True)
    report = {"dump": str(path), "root": root, "nodes": len(nodes)}
    report["geometry"] = geometry_contract(dump, nodes)
    report["player"] = player_contract(dump, nodes)
    renders = {}
    for mode in ("full", "prefix", "helper"):
        replay = Replay(dump, mode)
        before, after = replay.run(DRAW_SCENE, root)
        check_abi(before, after)
        renders[mode] = replay
        if lifted and mode == "helper":
            check_lifted(
                lifted, out, path.stem + "-scene", replay, DRAW_SCENE, before, after, nodes
            )
        print(f"  {mode}: {len(replay.visits)} visits; ABI OK", flush=True)
    for mode in ("prefix", "helper"):
        assert renders[mode].visits == renders["full"].visits
        assert renders[mode].composed(nodes) == renders["full"].composed(nodes)
    report["scene"] = {
        "visits": len(renders["full"].visits),
        "composed_bytes_equal": True,
        "registers_stack_x87_preserved": True,
    }

    # The dead update walker has a cross-function shared epilogue. Run its
    # original machine code with handle zero and compare *all* nodes, including
    # the hidden ones whose previous composed fields must stay untouched.
    update = Replay(dump)
    before, after = update.run(UPDATE_SCENE, 0)
    check_abi(before, after)
    assert update.composed(nodes) == renders["full"].composed(nodes)
    if lifted:
        check_lifted(lifted, out, path.stem + "-update", update, UPDATE_SCENE, before, after, nodes)
    report["update_hierarchy"] = {"composed_bytes_equal": True, "shared_tail_abi_ok": True}

    # Force a visible subtree hidden in private replay memory. Both walkers
    # must leave all its descendants' old composed fields untouched.
    candidate = next(n for n in renders["full"].visits if dump.u32(n + 0x14))
    hidden = []
    for mode in ("full", "helper"):
        r = Replay(dump, mode)
        r.put(candidate + 0xC, r.u32(candidate + 0xC) | 1)
        b, a = r.run(DRAW_SCENE, root)
        check_abi(b, a)
        hidden.append(r)
        if lifted and mode == "helper":
            patch = struct.pack("<I", dump.u32(candidate + 0xC) | 1)
            check_lifted(
                lifted,
                out,
                path.stem + "-hidden",
                r,
                DRAW_SCENE,
                b,
                a,
                nodes,
                [(candidate + 0xC, patch)],
            )
    assert hidden[0].visits == hidden[1].visits
    assert candidate not in hidden[0].visits
    assert hidden[0].composed(nodes) == hidden[1].composed(nodes)
    report["hidden_subtree"] = {
        "node": candidate,
        "remaining_visits": len(hidden[0].visits),
        "composed_bytes_equal": True,
    }

    # Main-frame wrapper forwards the requested destination, not a hard-coded
    # g_frameBuffer. Exercise both normal and thumbnail dimensions with the
    # original screen/viewport setters; only pixel flush is replaced.
    targets = []
    for width, height, dest in [(640, 480, dump.u32(0x5E549C)), (64, 64, 0x5D6B98)]:
        r = Replay(dump, "helper")
        r.run(0x4569CC, width, height)
        r.run(0x456B94, width, height, 0, 0, stack=(0x4298B852,))
        r.run(0x459320, dest)
        assert r.flushes == [{"destination": dest, "width": width, "height": height}]
        targets.extend(r.flushes)
    # This probes Ex's ABI, not the full shadow setup or shadow pixel format.
    r = Replay(dump, "helper")
    r.run(0x4569CC, 128, 256)
    r.run(0x456A58, 128, 256, 0, 0, stack=(800,))
    r.run(0x4593A4, dump.u32(0x62B9A4), dump.u32(0x62B9A8))
    assert r.flushes == [{"destination": dump.u32(0x62B9A8), "width": 128, "height": 256}]
    targets.extend(r.flushes)
    report["target_forwarding"] = targets
    report["shadow_target_note"] = "ABI-only; full shadow setup and pixel format not tested"
    report["lifted_closure"] = "three cases passed" if lifted else "not run"
    print("  hierarchy, hiding and three frame destinations OK", flush=True)
    dump.f.close()
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dumps", type=Path, nargs="+")
    ap.add_argument(
        "--lifted", action="store_true", help="also compile and compare the current lift"
    )
    ap.add_argument("--gpu", action="store_true", help="also run the hardware D3D11 smoke")
    ap.add_argument("--shadow-dump", type=Path, help="optional dump with real shadow enabled")
    ap.add_argument(
        "--shadow-arena",
        type=lambda v: int(v, 0),
        default=0,
        help="guest arena base for --shadow-dump (zero for retail)",
    )
    ap.add_argument(
        "--sokol-dir",
        type=Path,
        default=paths.REPO_ROOT / "opendreams/build/win-msvc-x64-debug/_deps/sokol-src",
    )
    args = ap.parse_args()
    out = paths.out_dir("recomp", "render-smoke")
    lifted = lifted_build(out) if args.lifted else None
    reports = [audit_dump(p.resolve(), out, lifted) for p in args.dumps]
    gpu = gpu_smoke(out, args.sokol_dir.resolve()) if args.gpu else None
    shadow = (
        shadow_smoke(args.shadow_dump.resolve(), args.shadow_arena, out)
        if args.shadow_dump
        else None
    )
    path = out / "cpu-boundary.json"
    path.write_text(json.dumps({"reports": reports, "gpu": gpu, "shadow": shadow}, indent=2) + "\n")
    print(f"PASS: {path}")


if __name__ == "__main__":
    main()
