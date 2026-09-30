"""Retail x86 oracle for post-draw sphere UVs and exact parent rotation chains.

Runs original REND_ComputeEnvMapUVs and MATH_MulMat3 from a retail dump.
Generated reports live under DREAMS_OUT/recomp/environment-mapping.
"""

import ctypes as c
import json
import random
import struct
from pathlib import Path

from mdmp import Dump
from render_light_smoke import recomp_env
from render_smoke import Replay, check_abi


def main():
    replay = Replay(Dump(Path("out/scratch/retail-gdidream-222659.dmp"), 0), pixels=True)
    base = 0x18000000
    replay.uc.mem_map(base, 0x10000)
    for page in range(base, base + 0x10000, 4096):
        replay.pages[page] = bytes(4096)
    owner, block, face, normals, uv, parent = [base + i * 4096 for i in range(6)]
    replay.put(owner + 0x10, parent)
    replay.put(owner + 0xA4, block)
    replay.put(block + 4, 3)
    replay.put(block + 0x24, face)
    for corner in range(3):
        replay.put(face + 12 + corner * 12, normals + corner * 16)
        replay.put(face + 0x34 + corner * 4, uv + corner * 8)
    dll = c.CDLL(str(recomp_env.out_dir("direct-render", "build") / "WDSceneAdapterOracle.dll"))
    fn = dll.wd_environment_uv
    fn.argtypes = [c.POINTER(c.c_int32), c.POINTER(c.c_int32), c.c_uint, c.POINTER(c.c_int32)]
    fn.restype = c.c_int
    chain = dll.wd_feedback_rotation_chain
    chain.argtypes = [
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
        c.POINTER(c.c_int32),
        c.c_size_t,
        c.POINTER(c.c_int32),
    ]
    chain.restype = c.c_int
    rng = random.Random(609)
    identity = (32768, 0, 0, 0, 32768, 0, 0, 0, 32768)
    cases = [
        (identity, [0, 0, 32768] * 3),
        (identity, [16777217, -16777217, 32768] * 3),
        (identity, [2147483647, -2147483648, 0] * 3),
    ]
    for _ in range(1000):
        cases.append(
            (
                tuple(rng.randint(-65536, 65536) for _ in range(9)),
                [rng.randint(-32768, 32768) for _ in range(9)],
            )
        )
    for _ in range(100):
        cases.append(
            (
                tuple(rng.randint(-2147483648, 2147483647) for _ in range(9)),
                [rng.randint(-2147483648, 2147483647) for _ in range(9)],
            )
        )
    for index, (rotation, ns) in enumerate(cases):
        replay.write(parent + 0x58, struct.pack("<9i", *rotation))
        for corner in range(3):
            replay.write(
                normals + corner * 16, struct.pack("<3i", *ns[corner * 3 : corner * 3 + 3])
            )
        before, after = replay.run(0x47E094, eax=owner)
        check_abi(before, after)
        for corner in range(3):
            actual = struct.unpack("<2i", replay.read(uv + corner * 8, 8))
            result = (c.c_int32 * 2)()
            assert fn(
                (c.c_int32 * 9)(*rotation),
                (c.c_int32 * 3)(*ns[corner * 3 : corner * 3 + 3]),
                corner,
                result,
            )
            assert tuple(result) == actual, (index, corner, tuple(result), actual)
    supported = [
        *range(-15, -2),
        2,
        3,
        6,
        9,
        0xB,
        0xC,
        0x12,
        *range(0x14, 0x1B),
        *range(0x1C, 0x1F),
    ]
    dispatch_cases = 0
    replay.write(parent + 0x58, struct.pack("<9i", *identity))
    for kind in range(-20, 36):
        for culled in (0, 1):
            replay.put(block + 4, kind)
            replay.put(face, culled)
            source = struct.pack("<6i", 123, 456, 789, -1, -2, -3)
            replay.write(uv, source)
            replay.run(0x47E094, eax=owner)
            assert (replay.read(uv, 24) != source) == (kind in supported and not culled), (
                kind,
                culled,
            )
            dispatch_cases += 1
    chain_cases = 0
    for _ in range(200):
        count = rng.randint(2, 12)
        camera = tuple(rng.randint(-65536, 65536) for _ in range(9))
        parents = [-1] + [rng.randrange(i) for i in range(1, count)]
        locals_ = [identity] + [
            tuple(rng.randint(-65536, 65536) for _ in range(9)) for _ in range(count - 1)
        ]
        matrices = [tuple(camera[col * 3 + row] for row in range(3) for col in range(3))]
        for i in range(1, count):
            replay.write(parent, struct.pack("<9i", *matrices[parents[i]]))
            replay.write(parent + 0x100, struct.pack("<9i", *locals_[i]))
            replay.run(0x45B86C, eax=parent, edx=parent + 0x100, ebx=parent + 0x200)
            matrices.append(struct.unpack("<9i", replay.read(parent + 0x200, 36)))
            chain_cases += 1
        result = (c.c_int32 * (count * 9))()
        assert chain(
            (c.c_int32 * 9)(*camera),
            (c.c_int32 * (count * 9))(*(v for matrix in locals_ for v in matrix)),
            (c.c_int32 * count)(*parents),
            count,
            result,
        )
        assert list(result) == [v for matrix in matrices for v in matrix]
    report = {
        "normal_triplets": len(cases),
        "corner_uv_comparisons": len(cases) * 3,
        "uv_mismatches": 0,
        "type_and_cull_cases": dispatch_cases,
        "rotation_chain_multiplications": chain_cases,
        "rotation_mismatches": 0,
        "gpr_stack_x87_preserved": True,
        "corner_0_integer_vs_1_2_float_spill_checked": True,
    }
    out = recomp_env.out_dir("environment-mapping")
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
