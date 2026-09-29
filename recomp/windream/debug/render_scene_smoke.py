"""Exercise the native scene adapter on independent retail process dumps.

Build with direct_render_validate.py first. Captures and reports stay under
DREAMS_OUT/recomp/scene-adapter; no retail geometry is stored in the repository.
"""

import argparse
import bisect
import collections
import ctypes
import hashlib
import json
import struct
import sys
from pathlib import Path

from mdmp import Dump
from render_smoke import DRAW_SCENE, Replay, node_inventory, player_contract
from unicorn import UC_HOOK_CODE
from unicorn import x86_const as xr

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402


def read_snapshot(path):
    raw = path.read_bytes()
    position = 0

    def take(fmt):
        nonlocal position
        fmt = "<" + fmt
        values = struct.unpack_from(fmt, raw, position)
        position += struct.calcsize(fmt)
        return values

    magic, nn, nv, nf = take("4I")
    assert (
        magic in (0x31534457, 0x32534457, 0x33534457, 0x34534457, 0x35534457, 0x36534457)
        and nn <= 10000
        and nv <= 1000000
        and nf <= 1000000
    )
    material_version = magic != 0x31534457
    camera = {
        "address": take("I")[0],
        "eye": take("3i"),
        "rotation": take("9i"),
        "view": take("12f"),
        "screen": take("2I"),
        "viewport": take("4I"),
        "focal": take("2f"),
        "projection": take("4i"),
    }
    nodes = []
    for _ in range(nn):
        address, flags, first, count, active, parent = take("5Ii")
        nodes.append(
            dict(
                address=address,
                flags=flags,
                first=first,
                count=count,
                active=bool(active),
                parent=parent,
                local=take("12f"),
            )
        )
        if material_version:
            nodes[-1]["shade"], nodes[-1]["light_count"] = take("2I")
    vertices = [(take("I")[0], take("3f")) for _ in range(nv)]
    faces = []
    for _ in range(nf):
        a, owner, flags, slot, colour, kind, shade, nx, ny, nz, distance = take("5IiI4i")
        corners = [take("2I2f") for _ in range(3)]
        faces.append(
            dict(
                address=a,
                owner=owner,
                flags=flags,
                slot=slot,
                colour=colour,
                kind=kind,
                shade=shade,
                normal=(nx, ny, nz),
                distance=distance,
                corners=corners,
            )
        )
        if material_version:
            faces[-1]["block"], faces[-1]["material"] = take("2I")
    materials = []
    if material_version:
        nm = take("I")[0]
        assert nm <= 1024
        for _ in range(nm):
            slot, page = take("2I")
            palette = take("8192I")
            indices = raw[position : position + 65536]
            assert len(indices) == 65536
            position += 65536
            materials.append(dict(slot=slot, page=page, palette=palette, indices=indices))
    camera["materials"] = materials
    if magic >= 0x33534457:
        camera["fog"] = take("66I")
    if magic >= 0x34534457:
        camera["source_vertices"] = [take("3i") for _ in range(nv)]
        for node in nodes:
            node["light_indices"] = take("8I")
        camera["lights"] = [take("I15i") for _ in range(100)]
    if magic >= 0x35534457:
        for face in faces:
            face["normal_address"] = take("I")[0]
    if magic >= 0x36534457:
        camera["light_transform_count"] = take("I")[0]
    assert position == len(raw)
    return camera, nodes, vertices, faces


def compose(nodes):
    world = []
    for n in nodes:
        local = n["local"]
        if n["parent"] == -1:
            world.append(local)
            continue
        assert 0 <= n["parent"] < len(world)
        p = world[n["parent"]]
        world.append(
            tuple(
                sum(p[r * 4 + k] * local[k * 4 + c] for k in range(3))
                + (p[r * 4 + 3] if c == 3 else 0)
                for r in range(3)
                for c in range(4)
            )
        )
    return world


def point(matrix, xyz):
    return tuple(
        sum(matrix[r * 4 + k] * xyz[k] for k in range(3)) + matrix[r * 4 + 3] for r in range(3)
    )


def validate(dump_path, dll, out):
    dump = Dump(dump_path, 0)
    memory = dump
    root, original_nodes = node_inventory(dump)
    reader_type = ctypes.CFUNCTYPE(
        ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t
    )
    poison = []
    starts = []
    patches = {}
    patch_starts = []

    @reader_type
    def reader(_context, address, destination, size):
        data = memory.read(address, size)
        if data is None or len(data) != size:
            return False
        if poison:
            data = bytearray(data)
            at = max(0, bisect.bisect_right(starts, address) - 1)
            while at < len(poison) and poison[at][0] < address + size:
                base, count = poison[at]
                low, high = max(base, address), min(base + count, address + size)
                if high > low:
                    data[low - address : high - address] = b"\xcd" * (high - low)
                at += 1
        if patches:
            data = bytearray(data)
            at = max(0, bisect.bisect_right(patch_starts, address) - 1)
            while at < len(patch_starts) and patch_starts[at] < address + size:
                base = patch_starts[at]
                replacement = patches[base]
                low, high = max(base, address), min(base + len(replacement), address + size)
                if high > low:
                    data[low - address : high - address] = replacement[low - base : high - base]
                at += 1
        ctypes.memmove(destination, bytes(data), size)
        return True

    capture_native = dll.wd_capture_scene_file
    capture_native.argtypes = [
        reader_type,
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_char_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
    ]
    capture_native.restype = ctypes.c_int

    def capture(*args):
        nonlocal patch_starts
        patch_starts = sorted(patches)
        return capture_native(*args)

    path = out / (dump_path.stem + ".wds")
    error = ctypes.create_string_buffer(512)
    assert capture(reader, None, root, str(path).encode(), error, len(error)), error.value
    camera, nodes, vertices, faces = read_snapshot(path)
    assert len(nodes) == len(original_nodes) and set(original_nodes) == {
        n["address"] for n in nodes
    }
    assert camera["address"] == root
    assert nodes[0]["parent"] == -1
    assert nodes[0]["local"] == (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0)
    composed_root = struct.unpack("<12i", dump.read(root + 0x4C, 48))
    assert max(abs(camera["view"][i * 4 + 3] - composed_root[i]) for i in range(3)) <= 1.01
    for r in range(3):
        for c in range(3):
            assert camera["view"][r * 4 + c] == composed_root[3 + r * 3 + c] / 32768
    cross = sum(len({c[0] for c in f["corners"]}) > 1 for f in faces)
    assert len(faces) == 6439 and cross == 1169
    for n in nodes:
        for i in range(n["first"], n["first"] + n["count"]):
            address, xyz = vertices[i]
            assert xyz == struct.unpack("<3i", dump.read(address + 4, 12))
    for f in faces:
        for k, (owner, vertex, _u, _v) in enumerate(f["corners"]):
            assert vertices[vertex][0] == dump.u32(f["address"] + 8 + 12 * k)
            n = nodes[owner]
            assert n["first"] <= vertex < n["first"] + n["count"]
    owner_index = next(i for i, n in enumerate(nodes) if n["active"])
    patches[nodes[owner_index]["address"] + 0xC4] = struct.pack("<I", 1)
    patches[nodes[owner_index]["address"] + 0xC8] = b"\0"
    light_input = (1, 10, -20, 30, 32768, 0, 0, 0, 32768, 0, 0, 0, 32768)
    patches[0x672700] = struct.pack("<I12i", *light_input)
    patches[0x672788] = struct.pack("<3i", 50, 2000, 31)
    patches[0x4AC758] = struct.pack("<I", 1)
    light_path = out / (dump_path.stem + "-lights.wds")
    assert capture(reader, None, root, str(light_path).encode(), error, len(error)), error.value
    lit_camera, lit_nodes, _, _ = read_snapshot(light_path)
    assert (
        lit_nodes[owner_index]["light_count"] == 1
        and lit_nodes[owner_index]["light_indices"][0] == 0
    )
    assert lit_camera["lights"][0] == (*light_input, 50, 2000, 31)
    poison = [(0x672734, 0x54)]
    starts = [0x672734]
    light_poison_path = out / (dump_path.stem + "-lights-poison.wds")
    assert capture(reader, None, root, str(light_poison_path).encode(), error, len(error)), (
        error.value
    )
    assert light_poison_path.read_bytes() == light_path.read_bytes()
    poison = []
    starts = []
    patches.clear()
    # These are the old visual outputs. Poisoning them must leave the entire
    # input packet unchanged, including the camera and all world transforms.
    poison = sorted(
        [(n["address"] + 0x4C, 48) for n in nodes]
        + [(address + 0x10, 24) for address, _xyz in vertices]
    )
    starts = [p[0] for p in poison]
    patches.update({f["address"]: struct.pack("<I", f["flags"] | 3) for f in faces})
    patches.update({n["address"] + 12: struct.pack("<I", n["flags"] | 0x6A) for n in nodes})
    patches.update({address: struct.pack("<I", 0xDEADBEEF) for address, _xyz in vertices})
    patches.update({f["address"] + 4: struct.pack("<I", 0xDEADBEEF) for f in faces})
    for n in nodes:
        block = dump.u32(n["address"] + 0xA4)
        while block:
            patches[block + 0x24] = struct.pack("<I", 0xDEADBEEF)
            block = dump.u32(block)
    poisoned_path = out / (dump_path.stem + "-poisoned.wds")
    assert capture(reader, None, root, str(poisoned_path).encode(), error, len(error)), error.value
    assert path.read_bytes() == poisoned_path.read_bytes(), "adapter consumed old visual output"
    poison.clear()
    patches.clear()
    # Bad graph inputs fail explicitly; they cannot become omitted geometry.
    bad_path = out / (dump_path.stem + "-invalid.wds")
    patches[nodes[1]["address"] + 0x10] = struct.pack("<I", 1)
    assert not capture(reader, None, root, str(bad_path).encode(), error, len(error))
    assert b"parent" in error.value
    patches.clear()
    patches[root + 0x14] = struct.pack("<I", root)
    assert not capture(reader, None, root, str(bad_path).encode(), error, len(error))
    assert b"cyclic" in error.value
    patches.clear()
    patches[faces[0]["address"] + 8] = struct.pack("<I", 1)
    assert not capture(reader, None, root, str(bad_path).encode(), error, len(error))
    assert b"corner" in error.value
    patches.clear()
    # Root visibility is not tested by REND_DrawScene; the root is an anchor.
    patches[root + 12] = struct.pack("<I", nodes[0]["flags"] | 1)
    root_flag_path = out / (dump_path.stem + "-root-flag.wds")
    assert capture(reader, None, root, str(root_flag_path).encode(), error, len(error)), error.value
    root_flag_nodes = read_snapshot(root_flag_path)[1]
    assert [n["active"] for n in root_flag_nodes] == [n["active"] for n in nodes]
    patches.clear()
    # Run retail placement, then recapture through the native adapter. The
    # player's root must be in world space at the project spawn, with the
    # camera absent from the pose hierarchy and applied only in projection.
    placed = Replay(dump)
    placed.run(0x41D581, 0x4FBA78)
    _, resolved = placed.run(0x455358, dump.u32(0x4FBA78 + 0x74))
    memory = placed
    spawn_path = out / (dump_path.stem + "-spawn.wds")
    assert capture(reader, None, root, str(spawn_path).encode(), error, len(error)), error.value
    spawn_nodes = read_snapshot(spawn_path)[1]
    player_index = next(i for i, n in enumerate(spawn_nodes) if n["address"] == resolved["eax"])
    native_world = compose(spawn_nodes)[player_index]
    spawn = struct.unpack("<3i", dump.read(dump.u32(0x661E04) + 0xB4, 12))
    assert tuple(native_world[i * 4 + 3] for i in range(3)) == spawn
    memory = dump
    # Independent original-x86 projection oracle. Track actual projection
    # calls so stale screen coordinates from culled nodes are never compared.
    replay = Replay(dump, "full")
    projected = set()

    def project_hook(uc, address, _size, _data):
        if address == 0x47B228:
            projected.add(uc.reg_read(xr.UC_X86_REG_EAX))

    replay.uc.hook_add(UC_HOOK_CODE, project_hook, begin=0x47B228, end=0x47B228)
    replay.run(DRAW_SCENE, root)
    world = compose(nodes)
    errors = []
    fx, fy = camera["focal"]
    cx, cy, near, _far = camera["projection"]
    for index, n in enumerate(nodes):
        if n["address"] not in projected:
            continue
        for address, xyz in vertices[n["first"] : n["first"] + n["count"]]:
            raw = replay.read(address, 40)
            flags = struct.unpack_from("<I", raw)[0]
            _rx, _ry, rz = struct.unpack_from("<3f", raw, 16)
            sx, sy = struct.unpack_from("<2i", raw, 28)
            if not flags & 0x40 or rz < near + 50 or not (-1280 < sx < 1920 and -960 < sy < 1440):
                continue
            x, y, z = point(camera["view"], point(world[index], xyz))
            if z <= 0:
                raise AssertionError("new camera sent a projected point behind the eye")
            errors.append(max(abs(cx + fx * x / z - sx), abs(cy + fy * y / z - sy)))
    assert errors, "oracle did not project any eligible source vertices"
    errors.sort()
    # Regression envelope for these two fixed captures, not a promised error
    # bound for arbitrary levels or poses.
    assert max(errors) < 3.0, max(errors)
    assert errors[int(len(errors) * 0.95)] < 1.25
    # Float transforms intentionally remove per-node Q15/truncation steps.
    # This is a measured discrepancy, not a claim of integer projection parity.
    report = {
        "input": str(dump_path),
        "capture": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "nodes": len(nodes),
        "vertices": len(vertices),
        "faces": len(faces),
        "cross_node_faces": cross,
        "active_nodes": sum(n["active"] for n in nodes),
        "types": dict(collections.Counter(f["kind"] for f in faces)),
        "camera_inverse_matches_retail": True,
        "native_model_root_at_spawn": True,
        "independent_of_old_visual_outputs": True,
        "lighting_source_bindings_verified": True,
        "lighting_capture_independent_of_transform_scratch": True,
        "projection_samples": len(errors),
        "projection_error_max_pixels": max(errors),
        "projection_error_p95_pixels": errors[int(len(errors) * 0.95)],
        "player": player_contract(dump, original_nodes),
    }
    print(json.dumps(report, indent=2), flush=True)
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dumps", nargs="+", type=Path)
    args = ap.parse_args()
    build = recomp_env.out_dir("direct-render", "build")
    dll = ctypes.CDLL(str(build / "WDSceneAdapterOracle.dll"))
    out = recomp_env.out_dir("scene-adapter")
    reports = [validate(dump, dll, out) for dump in args.dumps]
    (out / "results.json").write_text(json.dumps(reports, indent=2) + "\n")
    print(f"PASS: {out / 'results.json'}")


if __name__ == "__main__":
    main()
