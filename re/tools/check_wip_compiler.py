"""Reproduce named editor helpers with optimized and debug Watcom 11.0 code.

The same abstract C operation must reproduce the entire demo and retail bodies.
Only actual OMF fixups may differ; extern-global roles, PE/LE relocations and
the debug-profile __CHK/memcpy calls are checked. Generated C/OBJ files stay in out/recomp.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path

from relocated_image import Image
from watcom_debug import read_debug

from dreams import paths
from dreams.watcom import _index, _parse_module

ROOT = Path(__file__).resolve().parents[2]
TEMPLATES = {
    "to_current": "void f(Block *p) { memcpy(&current,p,sizeof(current)); }",
    "from_current": "void f(Block *p) { memcpy(p,&current,sizeof(current)); }",
    "global_copy": "void f(void) { memcpy(&current,&backup,sizeof(current)); }",
}


def extern_fixups(data, code_segment):
    """Read the explicit external fixups emitted by these small compiler models."""
    names, sites = [None], []
    pos, last = 0, None
    while pos + 3 <= len(data):
        kind, length = data[pos], struct.unpack_from("<H", data, pos + 1)[0]
        body = data[pos + 3 : pos + length + 2]
        pos += length + 3
        if kind == 0x8C:
            at = 0
            while at < len(body):
                count = body[at]
                names.append(body[at + 1 : at + 1 + count].decode("ascii"))
                _, at = _index(body, at + 1 + count)
        elif kind in (0xA0, 0xA1):
            segment, at = _index(body, 0)
            width = 4 if kind & 1 else 2
            last = (segment, int.from_bytes(body[at : at + width], "little"))
        elif kind in (0x9C, 0x9D) and last and last[0] == code_segment:
            at = 0
            while at < len(body):
                first, second, flags = body[at : at + 3]
                if not first & 0x80 or flags & 0x88:
                    raise ValueError("threaded compiler fixup is unsupported")
                location = ((first & 3) << 8) | second
                if (first >> 2) & 15 not in (9, 13):
                    raise ValueError("compiler fixup is not a 32-bit offset")
                at += 3
                frame = (flags >> 4) & 7
                if frame <= 2:
                    _, at = _index(body, at)
                elif frame == 3:
                    at += 2
                target, at = _index(body, at)
                if flags & 3 != 2:
                    raise ValueError("compiler fixup does not reference an external")
                if not flags & 4:
                    width = 4 if kind & 1 else 2
                    if int.from_bytes(body[at : at + width], "little"):
                        raise ValueError("unexpected compiler fixup addend")
                    at += width
                sites.append((last[1] + location, names[target], bool(first & 0x40)))
    return sites


def compile_model(template, size, flags, suffix):
    compiler = paths.get("watcom_compiler")
    work = paths.out_dir("recomp", "wip-compiler-check", f"{template}-{size:x}-{suffix}")
    work.mkdir(parents=True, exist_ok=True)
    source, obj = work / "model.c", work / "model.obj"
    source.write_text(
        "#include <string.h>\n"
        f"typedef struct {{ unsigned char bytes[{size}]; }} Block;\n"
        "extern Block current, backup;\n" + TEMPLATES[template] + "\n",
        encoding="ascii",
    )
    environment = dict(os.environ, WATCOM=str(compiler), INCLUDE=f"{compiler}/H;{compiler}/H/NT")
    command = [
        str(compiler / "BINNT/WCC386.EXE"),
        "-zq",
        "-bt=nt",
        *flags.split(),
        f"-fo={obj.resolve()}",
        str(source.resolve()),
    ]
    run = subprocess.run(command, env=environment, capture_output=True, text=True)
    if run.returncode:
        raise ValueError(run.stdout + run.stderr)
    raw = obj.read_bytes()
    module, _ = _parse_module(raw, 0)
    _, segment, begin = next(p for p in module.publics if p[0] == "f_")
    other = [offset for _, seg, offset in module.publics if seg == segment and offset > begin]
    end = min(other) if other else module.segments[segment].length
    code = bytes(module.segments[segment].data[begin:end])
    mask = bytes(module.segments[segment].fixed[begin:end])
    fixups = [
        (offset - begin, name, absolute)
        for offset, name, absolute in extern_fixups(raw, segment)
        if begin <= offset < end
    ]
    if {i for i, fixed in enumerate(mask) if not fixed} != {
        i for off, _, _ in fixups for i in range(off, off + 4)
    }:
        raise ValueError("OMF relocation masks and external records disagree")
    return code, mask, fixups


def match(image, entry, size, compiled, callees=None):
    code, mask, fixups = compiled
    actual = image.read(entry, size)
    if (
        len(code) != size
        or len(actual) != size
        or any(m and a != b for a, b, m in zip(actual, code, mask, strict=True))
    ):
        raise ValueError(f"compiler reproduction differs at {entry:08x}")
    globals_found, absolute_sites = {}, set()
    for offset, name, absolute in fixups:
        if absolute:
            if entry + offset not in image.relocs or name not in ("_current", "_backup"):
                raise ValueError("global fixup role differs")
            globals_found[name] = int.from_bytes(actual[offset : offset + 4], "little")
            absolute_sites.add(entry + offset)
        else:
            target = (entry + offset + 4 + struct.unpack_from("<i", actual, offset)[0]) & 0xFFFFFFFF
            if not callees or callees.get(name) != target or actual[offset - 1] != 0xE8:
                raise ValueError(f"compiler helper call differs: {name}")
    if {r for r in image.relocs if entry <= r < entry + size} != absolute_sites:
        raise ValueError("unaccounted-for executable relocation")
    return globals_found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--check", type=Path, required=True)
    args = parser.parse_args()
    proof = json.loads(args.check.read_text())
    source = Image(args.demo / "DREAMS.EXE")
    targets = [Image(paths.disc(1) / name) for name in ("WINDREAM.EXE", "GDIDREAM.EXE")]
    for key, image in zip(
        ("source_sha256", "target_sha256", "twin_sha256"), (source, *targets), strict=True
    ):
        if hashlib.sha256(image.raw).hexdigest() != proof[key]:
            raise ValueError(f"executable differs: {key}")
    symbols = read_debug(source.raw)[1]
    source_callees = {
        s["name"]: int(s["address"], 16)
        for s in symbols
        if s["kind"] & 4 and s["name"] in ("__CHK", "memcpy_")
    }
    cache, address_map, reverse_map = {}, {}, {}
    for row in proof["rows"]:
        if not any(
            s["address"] == f"{row['source_entry']:08x}"
            and s["name"] == row["name"]
            and s["kind"] & 4
            for s in symbols
        ):
            raise ValueError("original symbol differs")
        key = row["template"], row["record_size"]
        if key not in cache:
            cache[key] = [
                compile_model(*key, flags, role)
                for role, flags in (("demo", "-5r -otexan -s"), ("windows", "-5r -d2"))
            ]
        a, b = cache[key]
        source_globals = match(source, row["source_entry"], row["source_size"], a, source_callees)
        if (
            hashlib.sha256(source.read(row["source_entry"], row["source_size"])).hexdigest()
            != row["source_body_sha256"]
        ):
            raise ValueError("source body hash differs")
        for image in targets:
            target_globals = match(
                image, row["entry"], row["size"], b, {"__CHK": 0x454FEB, "memcpy_": 0x45C278}
            )
            if (
                hashlib.sha256(image.read(row["entry"], row["size"])).hexdigest()
                != row["target_body_sha256"]
            ):
                raise ValueError("target body hash differs")
            for name, address in source_globals.items():
                mapped = target_globals[name]
                if address in address_map and address_map[address] != mapped:
                    raise ValueError("compiler model global roles are inconsistent")
                if mapped in reverse_map and reverse_map[mapped] != address:
                    raise ValueError("compiler model merges distinct source globals")
                address_map[address] = mapped
                reverse_map[mapped] = address
        print(f"PASS {row['entry']:08x} {row['name']}: {row['source_size']} -> {row['size']} bytes")


if __name__ == "__main__":
    main()
