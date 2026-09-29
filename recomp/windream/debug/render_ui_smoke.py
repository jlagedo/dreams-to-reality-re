"""Compare native UI request normalization and metadata with original x86.

Build direct_render_validate.py first. GPU comparison uses the shared core.
"""

import argparse
import ctypes
import json
import struct
import subprocess
import sys
from pathlib import Path

import render_2d_smoke as reference
from mdmp import Dump
from render_smoke import REGS
from unicorn import UC_HOOK_CODE

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402


class Registers(ctypes.Structure):
    _fields_ = [
        (name, ctypes.c_uint32) for name in ("eax", "edx", "ebx", "ecx", "esi", "edi", "esp", "ebp")
    ]


class Oracle:
    def __init__(self, out):
        self.out = out
        self.dll = ctypes.CDLL(
            str(recomp_env.out_dir("direct-render", "build") / "WDUiAdapterOracle.dll")
        )
        self.reader_type = ctypes.CFUNCTYPE(
            ctypes.c_bool, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t
        )
        self.function = self.dll.wd_normalize_ui_file
        self.function.argtypes = [
            self.reader_type,
            ctypes.c_void_p,
            ctypes.POINTER(Registers),
            ctypes.c_uint32,
            ctypes.c_char_p,
            ctypes.c_void_p,
            ctypes.c_size_t,
        ]
        self.function.restype = ctypes.c_int
        self.count = 0

    def normalize(self, suite, entry, regs):
        @self.reader_type
        def reader(_context, address, destination, size):
            # A CPU scene-pixel dependency is forbidden even in normalization.
            if any(
                address < base + suite.width * suite.height * 2 and address + size > base
                for base in reference.FB
            ):
                return False
            data = suite.r.read(address, size)
            if data is None or len(data) != size:
                return False
            ctypes.memmove(destination, bytes(data), size)
            return True

        path = self.out / "native-plan.bin"
        error = ctypes.create_string_buffer(512)
        assert self.function(
            reader, None, ctypes.byref(regs), entry, str(path).encode(), error, len(error)
        ), error.value
        data = path.read_bytes()
        magic, target, kind, fmt, param, x, y, w, h, count, nwrites = struct.unpack_from(
            "<5I4i2I", data
        )
        assert magic == 0x32495557 and count <= 16_000_000 and nwrites < 256
        pixels = struct.unpack_from(f"<{count}I", data, 44)
        writes = [struct.unpack_from("<3I", data, 44 + count * 4 + i * 12) for i in range(nwrites)]
        table_at = 44 + count * 4 + nwrites * 12
        ntable = struct.unpack_from("<I", data, table_at)[0]
        assert ntable == (7168 if kind == 10 else 0)
        lookup = struct.unpack_from(f"<{ntable}I", data, table_at + 4)
        assert len(data) == table_at + 4 + ntable * 4
        before = {
            0x49D0E8: bytearray(suite.r.read(0x49D0E8, 0x7B)),
            0x5ECDFC: bytearray(suite.r.read(0x5ECDFC, 4)),
        }
        for address, value, size in writes:
            for base, memory in before.items():
                if base <= address and address + size <= base + len(memory):
                    memory[address - base : address - base + size] = struct.pack("<I", value)[:size]
                    break
            else:
                raise AssertionError(f"unexpected metadata store {address:x} + {size}")
        self.count += 1
        return dict(
            kind=kind,
            target=target,
            fmt=fmt,
            param=param,
            rect=(x, y, w, h),
            data=pixels + lookup,
            expected_metadata=before,
        )

    def check_metadata(self, suite, plan):
        for base, expected in plan["expected_metadata"].items():
            actual = suite.r.read(base, len(expected))
            bad = [
                (hex(base + i), a, b)
                for i, (a, b) in enumerate(zip(actual, expected, strict=True))
                if a != b
            ]
            assert not bad, f"native metadata differs from retail: {bad[:12]}"


class NativeSuite(reference.Suite):
    oracle = None

    def __init__(self, *args, **kwargs):
        self.pending = None
        super().__init__(*args, **kwargs)

    def sprite(
        self,
        label,
        x,
        y,
        w,
        h,
        flags,
        pixels,
        palette,
        *,
        factor=2,
        scale=0,
        dark=0,
        faded=False,
        origin=(0, 0),
    ):
        # The command plan comes only from the native adapter; expected pixels
        # come only from original x86. Do not run the old Python normalizer.
        self.r.write(reference.SOURCE, bytes(pixels))
        self.r.write(reference.PALETTE, struct.pack("<256H", *palette))
        self.r.write(
            reference.DESC, reference.words([reference.PALETTE, w, h, *origin, 0, reference.SOURCE])
        )
        self.r.write(reference.REQUEST, reference.words([x, y, flags, reference.DESC]))
        self.call(
            0x403BCD if faded else 0x401935, factor, ebx=scale, ecx=dark, esi=reference.REQUEST
        )
        self.add(label, 0)

    def call(self, entry, eax=0, edx=0, ebx=0, ecx=0, esi=0, stack=()):
        plan = None
        if entry in (0x401935, 0x403BCD, 0x427B8C):
            regs = Registers(eax, edx, ebx, ecx, esi, 0xD1D1D1D1, reference.ESP, 0xBEBEBEBE)
            plan = self.oracle.normalize(self, entry, regs)
        super().call(entry, eax, edx, ebx, ecx, esi, stack)
        if plan:
            self.oracle.check_metadata(self, plan)
            self.pending = plan

    def add(self, label, kind, source_target=0, rect=None, param=0, data=()):
        if self.pending:
            plan, self.pending = self.pending, None
            assert plan["target"] == reference.FB[self.target] and plan["fmt"] == self.fmt
            kind, rect, param, data = plan["kind"], plan["rect"], plan["param"], plan["data"]
        super().add(label, kind, source_target, rect, param, data)


def captured_gauge(dump, oracle, out):
    suite = reference.Suite(dump, 640, 480)
    capture = {}

    def entry(uc, _address, _size, _context):
        regs = Registers(*(uc.reg_read(REGS[name]) for name, _type in Registers._fields_))
        capture["return"] = suite.r.u32(regs.esp)
        capture["before"] = suite.snapshot()
        capture["plan"] = oracle.normalize(suite, 0x40368B, regs)

    def returned(_uc, address, _size, _context):
        if address == capture.get("return"):
            oracle.check_metadata(suite, capture["plan"])
            capture["after"] = suite.snapshot()
            del capture["return"]

    suite.r.at(0x40368B, entry)
    suite.r.uc.hook_add(UC_HOOK_CODE, returned)
    suite.call(0x434596, 1)
    assert "after" in capture
    plan = capture["plan"]
    path = out / "captured-gauge.bin"
    with path.open("wb") as file:
        file.write(reference.words([0x31443244, 640, 480, 0, 1, 1]))
        file.write(reference.words(struct.unpack("<307200H", capture["before"])))
        file.write(
            reference.words([plan["kind"], 0, 0, *plan["rect"], plan["param"], len(plan["data"])])
        )
        file.write(reference.words(plan["data"]))
        file.write(capture["after"])
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dump", type=Path, default=Path("out/scratch/retail-gdidream-222659.dmp"))
    args = ap.parse_args()
    out = recomp_env.out_dir("ui-adapter")
    dump = Dump(args.dump, 0)
    oracle = Oracle(out)
    NativeSuite.oracle = oracle
    reference.Suite = NativeSuite
    files = []
    for fmt in (0, 1):
        suite = reference.make_suite(dump, fmt=fmt)
        # Mixed flags and divided-alpha transition exercise priority/control
        # flow that the old standalone Python normalizer did not establish.
        palette = [i * 173 & 65535 for i in range(256)]
        pixels = bytes(v for i in range(16) for v in (i % 5, (126, 32, 64, 0)[i % 4]))
        suite.sprite("divided loop switches after opaque quotient", 2, 3, 8, 2, 8, pixels, palette)
        suite.sprite("mixed coverage/opaque flags", 3, 6, 8, 2, 17, pixels, palette)
        path = out / f"format-{fmt}.bin"
        suite.write(path)
        files.append(path)
    files.append(captured_gauge(dump, oracle, out))
    for fmt in (0, 1):
        suite = NativeSuite(dump, 640, 480, fmt)
        palette = [(i * 977) ^ 0x8421 for i in range(256)]
        palette = [v & 65535 for v in palette]
        pixels = bytes([0, 1, 2, 3, 4, 0, 7, 8] * 8)
        for flags, scale, dark in ((4, 0, 0), (31, 0x101, 1)):
            suite.sprite(
                "flag4 raw stride and precedence",
                2,
                2,
                8,
                6,
                flags,
                pixels,
                palette,
                scale=scale,
                dark=dark,
            )
        suite.sprite("flag4 left/top clip", -2, -2, 8, 6, 4, pixels, palette)
        suite.set_target(2)
        suite.sprite("flag4 500-wide right clip", 497, 3, 8, 6, 4, pixels, palette)
        suite.sprite("flag4 450-high bottom clip", 2, 448, 8, 6, 4, pixels, palette)
        suite.sprite("flag4 early return preserves scratch", 501, 2, 8, 6, 4, pixels, palette)
        suite.sprite("ordinary draw after flag4", 3, 6, 8, 6, 1, pixels, palette)
        path = out / f"flag4-{fmt}.bin"
        suite.write(path)
        files.append(path)

        suite = NativeSuite(dump, 256, 64, fmt)
        # Preserve the initialized 0..31 weight rows. Deliberately vary adjacent
        # memory so high coverage cannot accidentally pass as a clamped alpha.
        for first, count in ((-3072, 3072), (1024, 3072)):
            suite.r.write(
                0x5E94BC + first * 4,
                reference.words([((i + first) * 0x1234567) ^ 0x9ABCDEF0 for i in range(count)]),
            )
        palette = [((i * 1237) ^ 0x8410) & 65535 for i in range(256)]
        data = bytes(v for y in range(32) for x in range(256) for v in ((y * 7) % 255 + 1, x))
        suite.sprite("all coverage bytes and lookup rows", 0, 0, 256, 32, 16, data, palette)
        suite.sprite("dark lookup over previous draw", 0, 1, 256, 32, 16, data, palette, dark=1)
        suite.set_target(2)
        suite.sprite(
            "divided signed then opaque transition", 0, 0, 256, 32, 8, data, palette, factor=1
        )
        # Source mutation after a queued draw must make a new table snapshot.
        suite.r.write(0x5E64BC, reference.words([0x87654321] * 3072))
        suite.sprite("mutated adjacent table memory", 0, 2, 256, 32, 16, data, palette)
        transition = bytes(
            v for a in (200, 228, 229, 128, 0, 62, 255, 64, 129, 31, 1, 127) for v in (1, a)
        )
        palette[1] = 0x8410  # All channel paths can address the aliased scratch row.
        suite.sprite(
            "signed divided loop before opaque transition",
            4,
            6,
            6,
            2,
            8,
            transition,
            palette,
            factor=1,
        )
        suite.sprite(
            "signed coverage left/top clipping and half size",
            -1,
            -1,
            8,
            6,
            16,
            data,
            palette,
            scale=0x101,
        )
        path = out / f"signed-{fmt}.bin"
        suite.write(path)
        files.append(path)
        suite = NativeSuite(dump, 124, 48, fmt)
        suite.sprite(
            "flag4 independent clip width and row wrapping", 120, 0, 8, 2, 4, pixels, palette
        )
        suite.set_target(1)
        suite.sprite("flag4 nonstandard target pitch", -2, 1, 8, 2, 4, pixels, palette)
        path = out / f"flag4-pitch-{fmt}.bin"
        suite.write(path)
        files.append(path)
    executable = recomp_env.out_dir("direct-render", "build") / "ODDirectGpuTests.exe"
    result = subprocess.run(
        [str(executable), *(str(p) for p in files)], capture_output=True, text=True
    )
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    report = {
        "dump": str(args.dump),
        "native_normalizations": oracle.count,
        "pixel_and_metadata_check": result.returncode == 0,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }
    (out / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    assert result.returncode == 0
    print(f"PASS: {out / 'results.json'}")


if __name__ == "__main__":
    main()
