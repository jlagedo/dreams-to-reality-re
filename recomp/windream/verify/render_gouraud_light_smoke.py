"""Compare 0x16/0x17 corner lighting and 0x18 flat dispatch against retail x86.

Controlled local inputs isolate arithmetic from the independently tested light
transform. Original code executes with its normal pool and corner pointers.
"""

import ctypes as c
import json
import random
import struct
from pathlib import Path

from mdmp import Dump
from render_light_smoke import recomp_env
from render_oriented_light_smoke import LocalLight
from render_smoke import ESP, Replay
from unicorn import UC_HOOK_MEM_WRITE


def check_adapter(
    replay,
    dll,
    owner,
    block,
    face,
    normals,
    vertices,
    kinds=(0x16, 0x17),
    counts=(0, 1, 2, 8),
    masks=(0,),
):
    # masks: bit i set makes bound slot i inactive (type 0, or 3 for odd i).
    # Those runs use weak lights so the flat sum stays above the -31 clamp.
    # The flat branches keep their contribution in [esp+0x1dc] (ESP-0x90 here)
    # and never initialise it; the adapter declares 0, so the runs seed 0.
    fn = dll.wd_gouraud_packet
    fn.argtypes = [
        c.c_uint32,
        c.c_uint32,
        c.c_uint32,
        c.POINTER(LocalLight),
        c.c_size_t,
        c.POINTER(c.c_uint8),
        c.POINTER(c.c_int32),
        c.POINTER(c.c_uint32),
        c.c_size_t,
    ]
    fn.restype = c.c_int
    writes = []

    def observe(_uc, _access, address, size, value, _context):
        if any(address == normals + i * 16 + 12 for i in range(5)) or any(
            face + i * 0x80 + 0x40 <= address <= face + i * 0x80 + 0x43 for i in range(3)
        ):
            event = (address, value & ((1 << (size * 8)) - 1), size)
            # Flat retail stores the negative sum and immediately negates that
            # byte. The adapter publishes its final byte once; no consumer runs
            # between these adjacent stores.
            if size == 1 and writes and writes[-1][0] == address:
                writes[-1] = event
            else:
                writes.append(event)

    hook = replay.uc.hook_add(UC_HOOK_MEM_WRITE, observe)
    checks = 0
    try:
        for kind, flat_first, culled, count, mask in (
            (kind, flat_first, culled, count, mask)
            for kind in kinds
            for flat_first in (0, 1)
            for culled in range(8)
            for count in counts
            for mask in masks
        ):
            replay.put(owner + 0x8C, 4)
            replay.put(owner + 0xC4, count)
            replay.write(owner + 0xC8, bytes(range(count)))
            replay.write(block, struct.pack("<2I", block + 0x40, kind))
            replay.put(block + 0x24, face)
            replay.write(block + 0x40, struct.pack("<2I", 0, 0x18))
            replay.put(block + 0x40 + 0x24, face + 0x100)
            replay.put(owner + 0xA4, block + 0x40 if flat_first else block)
            replay.put(block + 0x40, block if flat_first else 0)
            replay.put(block, 0 if flat_first else block + 0x40)
            native_shades = (c.c_uint8 * 12)(*([19, 23, 27, 29] * 3))
            native_dots = (c.c_int32 * 5)(91, 92, 93, 94, 100)
            for i in range(5):
                replay.write(
                    normals + i * 16,
                    struct.pack("<4i", 0, 0, -32768 + i * 1000, native_dots[i]),
                )
            for i in range(3):
                at = face + i * 0x80
                replay.write(at, bytes(68))
                replay.put(at, 1 if culled & (1 << i) else 0)
                replay.put(at + 4, face + 0x80 if i == 0 else 0)
                replay.put(at + 0x2C, normals)
                replay.put(at + 0x30, -10)
                replay.write(at + 0x40, bytes(native_shades[i * 4 : i * 4 + 4]))
                for corner, point in enumerate(((-1, -1, 10), (0, 1, 10), (1, -1, 10))):
                    v = vertices + (i * 3 + corner) * 40
                    replay.put(at + 8 + corner * 12, v)
                    replay.put(
                        at + 12 + corner * 12,
                        normals + (4 if corner == 2 else corner) * 16,
                    )
                    p = (point[0] + (100 if culled & (1 << i) else 0), *point[1:])
                    replay.write(v + 4, struct.pack("<3i", *p))
            lights = (LocalLight * count)()
            for i in range(count):
                light = lights[i]
                light.type = (3 if i % 2 else 0) if mask >> i & 1 else 1 + i % 2
                light.radial.position[:] = (0, 0, -10)
                light.radial.inner_radius, light.radial.outer_radius = 100, 200
                light.radial.intensity = 1 + i % 3 if masks != (0,) else 31 - i
                light.axis[:] = (0, 0, 32768)
                at = 0x672700 + i * 0x94
                replay.put(at, light.type)
                replay.write(at + 0x64, struct.pack("<3i", *light.radial.position))
                replay.write(at + 0x70, struct.pack("<3i", *light.axis))
                replay.write(at + 0x88, struct.pack("<3i", 100, 200, light.radial.intensity))
            if masks != (0,):
                replay.uc.mem_write(ESP - 0x90, bytes(4))
            writes.clear()
            replay.run(0x47B7E0, eax=owner)
            native_writes = (c.c_uint32 * (512 * 3))()
            used = fn(
                kind,
                culled,
                flat_first,
                lights,
                count,
                native_shades,
                native_dots,
                native_writes,
                512,
            )
            assert used >= 0
            actual = [replay.read(face + i * 0x80 + 0x40, 4) for i in range(3)]
            assert b"".join(actual) == bytes(native_shades), (
                kind,
                flat_first,
                culled,
                count,
                mask,
                b"".join(actual).hex(),
                bytes(native_shades).hex(),
            )
            expected_dots = [
                struct.unpack("<i", replay.read(normals + i * 16 + 12, 4))[0] for i in range(5)
            ]
            assert expected_dots == list(native_dots), (kind, culled, count)
            events = [tuple(native_writes[i * 3 : i * 3 + 3]) for i in range(used)]
            assert events == writes, (kind, flat_first, culled, count, events, writes)
            checks += 1
    finally:
        replay.uc.hook_del(hook)
    return checks


def main():
    replay = Replay(Dump(Path("out/scratch/retail-gdidream-222659.dmp"), 0), pixels=True)
    base = 0x18000000
    replay.uc.mem_map(base, 0x10000)
    for page in range(base, base + 0x10000, 4096):
        replay.pages[page] = bytes(4096)
    owner, block, face, normals, vertices = [base + i * 4096 for i in range(5)]
    replay.put(owner + 0xA4, block)
    replay.put(owner + 0x8C, 3)
    replay.put(owner + 0x90, normals)
    replay.put(block + 0x24, face)
    replay.put(face + 0x2C, normals)
    replay.put(face + 0x30, -10)
    for corner in range(3):
        replay.put(face + 8 + corner * 12, vertices + corner * 40)
        replay.put(face + 12 + corner * 12, normals + corner * 16)
    dll = c.CDLL(str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"))
    fn = dll.wd_gouraud_lights
    fn.argtypes = [
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
        c.POINTER(LocalLight),
        c.c_size_t,
        c.POINTER(c.c_uint8),
        c.POINTER(c.c_int32),
    ]
    fn.restype = c.c_int
    flat = dll.wd_flat_lights
    flat.argtypes = [
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
        c.c_int32,
        c.POINTER(LocalLight),
        c.c_size_t,
        c.POINTER(c.c_uint8),
        c.POINTER(c.c_int32),
    ]
    rng = random.Random(608)
    cases = []
    for distance in (0, 1, 49, 50, 51, 75, 99, 100, 101):
        for kind in (1, 2):
            for count in (1, 2, 8):
                cases.append(
                    (
                        [0] * 9,
                        [0, 0, 32768] * 3,
                        [(kind, (0, 0, distance, 50, 100, 31), (0, 0, -32768))] * count,
                    )
                )
    for _ in range(1000):
        cases.append(
            (
                [rng.randint(-2000, 2000) for _ in range(9)],
                [rng.randint(-32768, 32768) for _ in range(9)],
                [
                    (
                        rng.choice((1, 2)),
                        tuple(rng.randint(-3000, 3000) for _ in range(3))
                        + (
                            rng.choice((0, 50, 1000)),
                            rng.choice((100, 1000, 4000)),
                            rng.randrange(32),
                        ),
                        tuple(rng.randint(-32768, 32768) for _ in range(3)),
                    )
                    for _ in range(rng.randint(1, 8))
                ],
            )
        )
    # Large values deliberately exercise IMUL/ADD wrapping and byte overflow.
    cases.append(
        (
            [60000, -90000, 70000] * 3,
            [32768, -32768, 32768] * 3,
            [(1, (50000, 50000, 50000, 1000000, 2000000, 100000), (0, 0, 0))] * 8,
        )
    )
    checks = 0
    for kind in (0x16, 0x17, 0x18):
        replay.put(block + 4, kind)
        for index, (points, ns, lights) in enumerate(cases):
            replay.put(owner + 0xC4, len(lights))
            replay.write(owner + 0xC8, bytes(range(len(lights))))
            replay.write(face + 0x40, bytes((19, 23, 27, 29)))
            for corner in range(3):
                replay.write(
                    vertices + corner * 40 + 4,
                    struct.pack("<3i", *points[corner * 3 : corner * 3 + 3]),
                )
                replay.write(
                    normals + corner * 16,
                    struct.pack("<4i", *ns[corner * 3 : corner * 3 + 3], 1234 + corner),
                )
            native_lights = (LocalLight * len(lights))()
            for i, (light_kind, light, axis) in enumerate(lights):
                at = 0x672700 + i * 0x94
                replay.put(at, light_kind)
                replay.write(at + 0x64, struct.pack("<3i", *light[:3]))
                replay.write(at + 0x70, struct.pack("<3i", *axis))
                replay.write(at + 0x88, struct.pack("<3i", *light[3:]))
                native_lights[i].type = light_kind
                native_lights[i].radial.position[:] = light[:3]
                native_lights[i].radial.inner_radius = light[3]
                native_lights[i].radial.outer_radius = light[4]
                native_lights[i].radial.intensity = light[5]
                native_lights[i].axis[:] = axis
            replay.run(0x47B7E0, eax=owner)
            actual_shades = replay.read(face + 0x40, 4)
            actual_dots = [
                struct.unpack("<i", replay.read(normals + i * 16 + 12, 4))[0] for i in range(3)
            ]
            shades, dots = (c.c_uint8 * 3)(), (c.c_int32 * 3)()
            if kind == 0x18:
                shade, dot = c.c_uint8(), c.c_int32()
                assert flat(
                    (c.c_int32 * 9)(*points),
                    (c.c_int32 * 3)(*ns[:3]),
                    -10,
                    native_lights,
                    len(lights),
                    c.byref(shade),
                    c.byref(dot),
                )
                assert actual_shades == bytes((shade.value, 23, 27, 29)), (kind, index)
                assert actual_dots == [dot.value, 1235, 1236], (kind, index, actual_dots)
            else:
                assert fn(
                    (c.c_int32 * 9)(*points),
                    (c.c_int32 * 9)(*ns),
                    native_lights,
                    len(lights),
                    shades,
                    dots,
                )
                assert actual_shades == bytes((19, *shades)), (
                    kind,
                    index,
                    actual_shades,
                    list(shades),
                )
                assert actual_dots == list(dots), (kind, index, actual_dots, list(dots))
            checks += 1
    report = {
        "cases_per_type": len(cases),
        "types": [0x16, 0x17, 0x18],
        "retail_comparisons": checks,
        "shade_mismatches": 0,
        "normal_dot_mismatches": 0,
        "type_18_flat_branch_preserves_corner_bytes": True,
    }
    report["adapter_order_and_alias_cases"] = check_adapter(
        replay, dll, owner, block, face, normals, vertices
    )
    report["inactive_slot_cases"] = check_adapter(
        replay,
        dll,
        owner,
        block,
        face,
        normals,
        vertices,
        kinds=(0x16, 0x17, 0x18),
        counts=(8,),
        masks=(0b10001000, 0b00001000, 0b10000000, 0b01111110, 0b10001001, 0b00000001, 0xFF),
    )
    out = recomp_env.out_dir("gouraud-lighting")
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
