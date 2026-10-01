"""2D renderer-boundary oracle and command-stream fixtures for the GPU smoke.

Run with uv run --with unicorn python recomp/windream/verify/render_2d_smoke.py.
Original machine code executes on a retail capture plus controlled scratch
buffers. Game-derived fixtures/reports go under DREAMS_OUT/recomp/render-2d-smoke.
This is an isolated experiment; it does not replace the running game's UI.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from mdmp import Dump
from render_smoke import ESP, REGS, STOP, Replay
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn import x86_const as xr

from dreams import paths

FB = (0x18000000, 0x18100000, 0x18200000)
SOURCE, DESC, REQUEST, PALETTE = 0x18300000, 0x18400000, 0x18400100, 0x18401000


def words(values):
    return struct.pack("<" + "I" * len(values), *(v & 0xFFFFFFFF for v in values))


class Suite:
    def __init__(self, dump, width, height, fmt=0):
        self.r = Replay(dump, pixels=True)
        # memcpy_ reloads ES from DS. Supply real flat 32-bit descriptors;
        # Unicorn's initial null selectors are insufficient for that reload.
        gdt = 0x10000100
        self.r.uc.mem_write(gdt, struct.pack("<3Q", 0, 0x00CF9B000000FFFF, 0x00CF93000000FFFF))
        self.r.uc.reg_write(xr.UC_X86_REG_GDTR, (0, gdt, 23, 0))
        for reg in (xr.UC_X86_REG_DS, xr.UC_X86_REG_ES, xr.UC_X86_REG_SS):
            self.r.uc.reg_write(reg, 16)
        self.r.uc.reg_write(xr.UC_X86_REG_CS, 8)
        self.width, self.height, self.fmt = width, height, fmt
        self.commands = []
        self.target = 0
        self.r.uc.mem_map(FB[0], 0x500000)
        for page in range(FB[0], FB[0] + 0x500000, 4096):
            self.r.pages[page] = bytes(4096)
        for va in (0x445BF2, 0x445C3F):
            self.r.at(va, self.r._return)  # synchronization only, no pixel substitution
        self.r.put(0x49D9FC, width)
        self.r.put(0x49DA00, height)
        self.r.put(0x49DA1C, fmt)
        self.r.put(0x5E549C, FB[0])
        self.r.put(0x5E1090, FB[1])
        self.r.put(0x5E1094, FB[1])
        self.r.put(0x49DA18, fmt)
        for va, value in ((0x4A2F11, width), (0x4A2F15, height), (0x4A2F19, 1), (0x4A2F1D, 1)):
            self.r.put(va, value)
        self.initial = []
        for target, ptr in enumerate(FB):
            data = [
                ((x * 239 + y * 191 + target * 977) ^ 0xAC53) & (0x7FFF if fmt else 0xFFFF)
                for y in range(height)
                for x in range(width)
            ]
            self.initial.append(data)
            self.r.write(ptr, struct.pack("<" + "H" * len(data), *data))
        self.call(0x424F7E)

    def call(self, entry, eax=0, edx=0, ebx=0, ecx=0, esi=0, stack=()):
        for name, value in dict(
            eax=eax, edx=edx, ebx=ebx, ecx=ecx, esi=esi, edi=0xD1D1D1D1, ebp=0xBEBEBEBE, esp=ESP
        ).items():
            self.r.uc.reg_write(REGS[name], value & 0xFFFFFFFF)
        self.r.uc.mem_write(ESP, words([STOP, *stack]))
        self.r.uc.reg_write(xr.UC_X86_REG_EFLAGS, 2)
        self.r.ensure(entry, 1)
        self.r.uc.emu_start(entry, STOP, count=20_000_000)
        assert self.r.uc.reg_read(xr.UC_X86_REG_EIP) == STOP, f"budget at {entry:x}"

    def snapshot(self):
        return self.r.read(FB[self.target], self.width * self.height * 2)

    def add(self, label, kind, source_target=0, rect=None, param=0, data=()):
        rect = rect or (0, 0, self.width, self.height)
        self.commands.append(
            {
                "label": label,
                "kind": kind,
                "target": self.target,
                "source_target": source_target,
                "rect": rect,
                "param": param,
                "data": list(data),
                "expected": self.snapshot(),
            }
        )

    def set_target(self, target):
        self.target = target
        self.r.put(0x5E549C, FB[target])

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
        self.r.write(SOURCE, bytes(pixels))
        self.r.write(PALETTE, struct.pack("<256H", *palette))
        self.r.write(DESC, words([PALETTE, w, h, *origin, 0, SOURCE]))
        self.r.write(REQUEST, words([x, y, flags, DESC]))
        self.call(0x403BCD if faded else 0x401935, factor, ebx=scale, ecx=dark, esi=REQUEST)
        # Independent input normalization. This creates source/coverage texels,
        # never reads destination pixels; composition happens in the GPU shader.
        sx, sy = (2 if scale & 255 else 1), (2 if scale >> 8 else 1)
        x, y = x - origin[0], y - origin[1]
        right, bottom = x + w // sx, y + h // sy
        left, top = max(0, x), max(0, y)
        cw, ch = min(self.width, right) - left, min(self.height, bottom) - top
        if cw <= 0 or ch <= 0:
            self.add(label, 0, rect=(0, 0, 0, 0))
            return
        paired = not faded and bool(flags & (8 | 16))
        # Retail's clipped source origin is in unscaled source coordinates.
        bpp = 2 if paired else 1
        start = max(0, -y) * w * (2 if flags & 16 and not faded else 1)
        skip = w if sy == 2 else 0
        if right >= self.width:
            skip = right - self.width  # overwrites the half-height skip
        lead = max(0, -x)
        if paired or faded:
            lead *= 2
            skip *= 2
        step = sx * bpp
        span = cw + (1 if not faded and flags == 0 else 0)
        payload = [0] * (span * ch)
        row = start
        for yy in range(ch):
            for xx in range(cw):
                at = row + lead + xx * step
                index = pixels[at]
                color = palette[index]
                alpha = 64
                if faded:
                    alpha = 64 // factor
                elif paired:
                    alpha = pixels[at + 1]
                    if not flags & 16:
                        alpha //= factor
                if alpha >= 128:
                    raise ValueError("signed-high coverage requires the unported retail branch")
                if not index and (faded or not flags & 1):
                    continue
                payload[yy * span + xx] = color | (alpha << 16)
                if flags == 0 and not faded:
                    # The original keyed font branch writes a DWORD at each
                    # nonzero texel: colour plus the right-neighbour shadow.
                    payload[yy * span + xx + 1] = (0 if dark or self.fmt else 0x19E7) | (64 << 16)
            row += lead + cw * step + skip
        if left + span > self.width:
            # The DWORD neighbour store can wrap to the first pixel of the
            # following memory row. Represent that footprint explicitly.
            expanded = [0] * (self.width * (ch + 1))
            for yy in range(ch):
                for xx in range(span):
                    pos = yy * self.width + left + xx
                    if payload[yy * span + xx]:
                        expanded[pos] = payload[yy * span + xx]
            payload, left, span, ch = expanded, 0, self.width, ch + 1
        kind = 2 if not faded and flags & 2 and not paired else 1
        self.add(label, kind, rect=(left, top, span, ch), param=dark, data=payload)

    def fill(self, label, rect, color=0):
        x, y, w, h = rect
        for row in range(y, y + h):
            # Execute retail memset, at the same memory primitive used by bars.
            self.call(0x45FD36, FB[self.target] + 2 * (row * self.width + x), color, w * 2)
        self.add(label, 3, rect=rect, param=(color & 255) * 257)

    def background(self, action, level=0):
        if action == "capture":
            self.call(0x417F80)
            current = self.target
            self.target = 1
            self.add("background capture", 4, source_target=current)
            self.target = current
        elif action == "restore":
            self.call(0x417FE7)
            self.add("background restore", 4, source_target=1)
        else:
            self.call(0x418060, level)
            self.add(f"background dim {level}", 5, source_target=1, param=level)

    def caption_band(self):
        self.call(0x4368A1)
        self.add("caption dim and final black row", 7, rect=(0, 64, self.width, 132))

    def text_band(self, y):
        # This assembly helper takes y at [entry ESP+4], unlike Watcom's
        # register-argument helpers; the caller owns that argument slot.
        self.call(0x4018E4, stack=(y,))
        self.add(
            "text background dim band", 8, rect=(0, y, self.width, 16 if self.width >= 640 else 9)
        )

    def masked64(self, x, y):
        mask = [0xAAAAAAAA ^ (0x11111111 * (i % 8)) for i in range(128)]
        source = [(i * 271) & 65535 for i in range(4096)]
        self.r.write(SOURCE, struct.pack("<4096H", *source))
        self.r.write(0x49FD1A, words(mask))
        self.call(0x427B8C, SOURCE, x, y)
        sx = 2 if self.width < 401 else 1
        sy = 2 if self.height < 400 else 1
        data = []
        for yy in range(0, 64, sy):
            for xx in range(0, 64, sx):
                enabled = (mask[yy * 2 + xx // 32] >> (xx % 32)) & 1
                data.append(source[yy * 64 + xx] | ((64 if enabled else 0) << 16))
        self.add("masked 64-pixel menu image", 9, rect=(x, y, 64 // sx, 64 // sy), data=data)

    def movie(self, source):
        assert self.width == 640 and self.height == 480 and len(source) == 0x5F000
        self.r.write(SOURCE, source)
        self.r.put(0x60CE18, SOURCE)
        hook = self.r.uc.hook_add(UC_HOOK_CODE, self.r._return, begin=0x45C2C0, end=0x45C2C0)
        # Decoder is deliberately replaced by a supplied decoded frame; all
        # original movie placement, copies and bars execute in x86.
        self.call(0x4268AC)
        self.r.uc.hook_del(hook)
        # One upload-and-place command includes the exact 88/304/88 row layout.
        self.add(
            "HNM6 decoded-frame placement",
            6,
            rect=(0, 88, 640, 304),
            data=struct.unpack("<" + "H" * (len(source) // 2), source),
        )

    def write(self, path):
        with path.open("wb") as f:
            f.write(
                words([0x31443244, self.width, self.height, self.fmt, len(FB), len(self.commands)])
            )
            for data in self.initial:
                f.write(words(data))
            for cmd in self.commands:
                f.write(
                    words(
                        [
                            cmd["kind"],
                            cmd["target"],
                            cmd["source_target"],
                            *cmd["rect"],
                            cmd["param"],
                            len(cmd["data"]),
                        ]
                    )
                )
                f.write(words(cmd["data"]))
                f.write(cmd["expected"])
        return [
            {k: v for k, v in cmd.items() if k not in ("data", "expected")} for cmd in self.commands
        ]


def make_suite(dump, width=64, height=48, fmt=0):
    s = Suite(dump, width, height, fmt)
    palette = [((i * 197) ^ 0xF83F) & 65535 for i in range(256)]
    pixels = bytes([0, 1, 2, 3, 0, 7, 8, 9] * 6)
    alpha = bytes(v for i in range(48) for v in (i % 8, [0, 1, 2, 15, 31, 32, 62, 63][i % 8]))
    s.sprite("opaque including index zero", 2, 2, 8, 6, 1, pixels, palette)
    s.sprite("keyed font and neighbour shadow", 4, 3, 8, 6, 0, pixels, palette)
    s.sprite("50 percent overlap", 6, 5, 8, 6, 2, pixels, palette)
    s.sprite("coverage alpha overlap", 8, 7, 8, 6, 16, alpha, palette)
    s.sprite("divided alpha", 10, 9, 8, 6, 8, alpha, palette, factor=2)
    s.sprite("faded glyph", 4, 18, 8, 6, 0, pixels, palette, factor=3, faded=True)
    s.sprite("left and top clipping", -2, -2, 8, 6, 16, alpha + bytes(128), palette)
    s.sprite("right and bottom clipping", width - 3, height - 2, 8, 6, 16, alpha, palette)
    s.sprite("keyed right edge wraps neighbour store", width - 2, 5, 8, 6, 0, pixels, palette)
    s.sprite("half size", 18, 4, 8, 6, 16, alpha + bytes(128), palette, scale=0x101)
    s.sprite("dark sprite", 21, 10, 8, 6, 16, alpha, palette, dark=1)
    s.sprite("scaled left/top clipping", -2, -1, 8, 6, 16, alpha + bytes(128), palette, scale=0x101)
    s.sprite(
        "scaled right clipping", width - 2, 3, 8, 6, 16, alpha + bytes(128), palette, scale=0x101
    )
    s.sprite("clipped faded glyph", -2, -1, 8, 6, 0, pixels + bytes(128), palette, faded=True)
    s.fill("white blend reference", (0, 28, width, 2), 255)
    alphas = bytes(v for i in range(64) for v in (1, i))
    s.sprite("every coverage 0..63 over white", 0, 28, 64, 1, 16, alphas, [0xFFFF] * 256)
    s.text_band(32)
    s.background("capture")
    s.fill("letterbox top", (0, 0, width, height // 8))
    s.fill("letterbox bottom", (0, height - height // 8, width, height // 8))
    for level in range(4):
        s.background("dim", level)
        s.sprite(f"draw over dim {level}", 14, 16, 8, 6, 16, alpha, palette)
    s.background("restore")
    s.call(0x417EB9)  # retail redirect to saved-background memory
    s.target = 1
    s.sprite("draw into saved background", 1, 1, 8, 6, 1, pixels, palette)
    s.call(0x417EEE)
    s.target = 0
    s.background("restore")
    s.set_target(2)
    s.sprite("second offscreen destination", 3, 4, 8, 6, 16, alpha, palette)
    s.masked64(2, 2)
    s.set_target(0)
    changed = [p ^ 0xFFFF for p in palette]
    s.sprite("same source address new palette", 26, 17, 8, 6, 1, pixels, changed)
    s.sprite("same source address new pixels", 26, 17, 8, 6, 16, alpha[::-1], changed)
    return s


def capture_hud(dump, out):
    s = Suite(dump, 640, 480)
    captured = []
    writes = collections.Counter()

    def draw(uc, address, size, _):
        q = uc.reg_read(REGS["esi"])
        x, y, flags, descriptor = struct.unpack("<iiII", s.r.read(q, 16))
        palette, w, h, ox, oy, unused, pixels = struct.unpack("<IIIiiII", s.r.read(descriptor, 28))
        captured.append(
            dict(
                x=x,
                y=y,
                w=w,
                h=h,
                flags=flags,
                palette=struct.unpack("<256H", s.r.read(palette, 512)),
                pixels=s.r.read(pixels, w * h * (2 if flags & (8 | 16) else 1)),
                factor=uc.reg_read(REGS["eax"]) & 255,
                scale=uc.reg_read(REGS["ebx"]) & 65535,
                dark=uc.reg_read(REGS["ecx"]) & 255,
                origin=(ox, oy),
            )
        )

    def written(uc, access, address, size, value, _):
        writes[uc.reg_read(xr.UC_X86_REG_EIP)] += 1

    s.r.at(0x401935, draw)
    s.r.uc.hook_add(UC_HOOK_MEM_WRITE, written, begin=FB[0], end=FB[0] + 640 * 480 * 2 - 1)
    s.call(0x434596, 1)
    # Run the observed sprite requests independently. The procedural gauge is
    # inventoried as a bypass, not silently counted as GPU-covered.
    replay = Suite(dump, 640, 480)
    for i, packet in enumerate(captured):
        replay.sprite(f"captured HUD sprite {i}", **packet)
    # Actual loaded retail font descriptors, including anchored glyph origin.
    for font in range(4):
        sid = replay.r.u32(0x5EA8C4 + font * 0x40C)
        replay.call(0x425593, sid, 65)
        desc = replay.r.uc.reg_read(REGS["eax"])
        pal, w, h, ox, oy, unused, pix = struct.unpack("<IIIiiII", replay.r.read(desc, 28))
        palette = struct.unpack("<256H", replay.r.read(pal, 512))
        pixels = replay.r.read(pix, w * h + 256)
        replay.sprite(
            f"captured font {font} keyed",
            100 + font * 50,
            100,
            w,
            h,
            0,
            pixels,
            palette,
            origin=(ox, oy),
        )
        replay.sprite(
            f"captured font {font} faded",
            100 + font * 50,
            120,
            w,
            h,
            0,
            pixels,
            palette,
            origin=(ox, oy),
            faded=True,
            factor=3,
        )
    replay.caption_band()
    replay.text_band(210)
    (out / "hud-framebuffer-writers.json").write_text(
        json.dumps({f"{a:08x}": n for a, n in sorted(writes.items())}, indent=2) + "\n"
    )
    return replay, len(captured)


def run_gpu(out, files):
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import recomp_env

    env = recomp_env.build_env()
    compiler = shutil.which("clang-cl", path=env["PATH"])
    assert compiler, "clang-cl required"
    source = Path(__file__).parent / "native" / "render_2d_gpu_smoke.cpp"
    headers = paths.out_dir("recomp", "windream", "build") / "_deps/sokol-src"
    subprocess.run(
        [
            compiler,
            "/nologo",
            "/O2",
            "/W3",
            "/std:c++17",
            f"/I{headers}",
            str(source),
            f"/Fo{out / 'gpu.obj'}",
            f"/Fe{out / 'gpu.exe'}",
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
    reports = {}
    for name in files:
        run = subprocess.run(
            [str(out / "gpu.exe"), str(out / (name + ".bin"))],
            capture_output=True,
            text=True,
            timeout=45,
        )
        print(name, run.stdout.strip(), run.stderr[:1800], flush=True)
        assert run.returncode == 0, (name, run.returncode, run.stderr)
        reports[name] = json.loads(run.stdout)
    (out / "gpu-results.json").write_text(json.dumps(reports, indent=2) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--dump", type=Path, default=paths.REPO_ROOT / "out/scratch/retail-gdidream-222659.dmp"
    )
    ap.add_argument("--gpu", action="store_true")
    ap.add_argument("--tag", help="optional single-directory result tag")
    ap.add_argument(
        "--movie-rgb565", type=Path, default=paths.REPO_ROOT / "out/generic-first.rgb565"
    )
    args = ap.parse_args()
    if args.tag and (Path(args.tag).name != args.tag or args.tag in (".", "..")):
        ap.error("--tag must be a single directory name")
    out = paths.out_dir("recomp", "render-2d-smoke", *([args.tag] if args.tag else []))
    dump = Dump(args.dump, 0)
    report = {}
    for fmt in (0, 1):
        suite = make_suite(dump, fmt=fmt)
        report[f"format-{fmt}"] = suite.write(out / f"format-{fmt}.bin")
    pitched = make_suite(dump, width=62)
    report["pitch-124"] = pitched.write(out / "pitch-124.bin")
    movie = Suite(dump, 640, 480)
    source = struct.pack("<" + "H" * (640 * 304), *[(i * 157) & 65535 for i in range(640 * 304)])
    movie.movie(source)
    movie.background("capture")
    movie.background("dim", 1)
    movie.background("restore")
    report["movie"] = movie.write(out / "movie.bin")
    actual = Suite(dump, 640, 480)
    source = args.movie_rgb565.read_bytes()
    actual.movie(source)
    actual.caption_band()
    actual.background("capture")
    actual.background("dim", 2)
    actual.background("restore")
    report["movie-captured"] = actual.write(out / "movie-captured.bin")
    (out / "movie-input.json").write_text(
        json.dumps(
            {
                "path": str(args.movie_rgb565),
                "sha256": hashlib.sha256(source).hexdigest(),
                "scope": "existing decoded RGB565 frame; decoder not retested",
            },
            indent=2,
        )
        + "\n"
    )
    hud, count = capture_hud(dump, out)
    report["captured-hud"] = hud.write(out / "captured-hud.bin")
    (out / "commands.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "inputs.json").write_text(
        json.dumps(
            {
                "retail_dump": str(args.dump.resolve()),
                "decoded_movie": str(args.movie_rgb565.resolve()),
                "captured_hud_sprite_calls": count,
                "scope": "isolated x86/GPU pixel comparison; no integrated game execution",
            },
            indent=2,
        )
        + "\n"
    )
    print(f"wrote {sum(map(len, report.values()))} original-x86 checkpoints to {out}")
    if args.gpu:
        run_gpu(out, report)


if __name__ == "__main__":
    main()
