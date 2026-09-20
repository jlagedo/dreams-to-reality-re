"""Minimal PE parser: sections, imports, exports, toolchain detection.

Written by hand rather than pulling in pefile, because we also need to look at
LE (DOS/4GW) binaries that pefile will not touch, and because the toolchain
fingerprints we care about are section-name based.

Findings this produced (docs/engine.md, docs/cryolib.md):
  * WINDREAM.EXE has Watcom's AUTO/DGROUP sections, not MSVC's .text/.data
  * CRYO.DLL exports 165 GL_* functions including an HNM6 decoder
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from pathlib import Path

WATCOM_SECTIONS = {"AUTO", "DGROUP"}
MSVC_SECTIONS = {".text", ".rdata"}


@dataclass
class Section:
    name: str
    virtual_size: int
    virtual_addr: int
    raw_size: int
    raw_ptr: int


@dataclass
class Binary:
    path: Path
    size: int
    format: str
    is_dll: bool = False
    machine: int = 0
    subsystem_version: str = ""
    sections: list[Section] = field(default_factory=list)
    imports: dict[str, list[str]] = field(default_factory=dict)
    exports: list[str] = field(default_factory=list)

    @property
    def toolchain(self) -> str:
        names = {s.name for s in self.sections}
        if names & WATCOM_SECTIONS:
            return "Watcom C/C++"
        if names & MSVC_SECTIONS:
            return "Microsoft Visual C++"
        return "unknown"


def _rva_to_offset(sections: list[Section], rva: int) -> int | None:
    for s in sections:
        if s.virtual_addr <= rva < s.virtual_addr + max(s.virtual_size, s.raw_size):
            return s.raw_ptr + (rva - s.virtual_addr)
    return None


def read(path: str | Path) -> Binary:
    p = Path(path)
    data = p.read_bytes()
    if data[:2] != b"MZ":
        return Binary(p, len(data), "not-executable")

    lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    sig = data[lfanew : lfanew + 2]
    if sig == b"LE":
        return Binary(p, len(data), "LE (DOS/4GW extender)")
    if sig != b"PE":
        return Binary(p, len(data), f"MZ only ({sig!r})")

    nsec = struct.unpack_from("<H", data, lfanew + 6)[0]
    opt_size = struct.unpack_from("<H", data, lfanew + 20)[0]
    characteristics = struct.unpack_from("<H", data, lfanew + 22)[0]
    machine = struct.unpack_from("<H", data, lfanew + 4)[0]

    opt = lfanew + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    dd = opt + (96 if magic == 0x10B else 112)
    major, minor = struct.unpack_from("<HH", data, opt + 0x48)

    sec_off = opt + opt_size
    sections = []
    for i in range(nsec):
        blob = data[sec_off + i * 40 : sec_off + (i + 1) * 40]
        name = blob[:8].rstrip(b"\0").decode("latin-1")
        vs, va, rs, ra = struct.unpack_from("<IIII", blob, 8)
        sections.append(Section(name, vs, va, rs, ra))

    binary = Binary(
        p,
        len(data),
        "PE32",
        bool(characteristics & 0x2000),
        machine,
        f"{major}.{minor}",
        sections,
    )

    exp_rva = struct.unpack_from("<I", data, dd)[0]
    if exp_rva:
        o = _rva_to_offset(sections, exp_rva)
        if o:
            n_names = struct.unpack_from("<I", data, o + 24)[0]
            names_rva = struct.unpack_from("<I", data, o + 32)[0]
            table = _rva_to_offset(sections, names_rva)
            if table:
                for i in range(n_names):
                    nr = struct.unpack_from("<I", data, table + i * 4)[0]
                    no = _rva_to_offset(sections, nr)
                    if no is None:
                        continue
                    binary.exports.append(data[no : data.index(b"\0", no)].decode("latin-1"))

    imp_rva = struct.unpack_from("<I", data, dd + 8)[0]
    if imp_rva:
        o = _rva_to_offset(sections, imp_rva)
        while o is not None:
            desc = data[o : o + 20]
            if len(desc) < 20 or desc == b"\0" * 20:
                break
            olt, _ts, _fc, name_rva, ft = struct.unpack("<IIIII", desc)
            no = _rva_to_offset(sections, name_rva)
            if no is None:
                break
            dll = data[no : data.index(b"\0", no)].decode("latin-1")
            thunk = _rva_to_offset(sections, olt or ft)
            funcs: list[str] = []
            while thunk is not None:
                v = struct.unpack_from("<I", data, thunk)[0]
                if v == 0:
                    break
                if v & 0x80000000:
                    funcs.append(f"#{v & 0xFFFF}")
                else:
                    fo = _rva_to_offset(sections, v & 0x7FFFFFFF)
                    if fo is None:
                        break
                    funcs.append(data[fo + 2 : data.index(b"\0", fo + 2)].decode("latin-1"))
                thunk += 4
            binary.imports[dll] = funcs
            o += 20
    return binary


TOOLCHAIN_MARKERS = (
    "WATCOM C/C++",
    "Microsoft Visual C++",
    "DOS/4G",
    "Miles Sound System",
    "SciTech Software",
    "RAD Game Tools",
    "CryoLib",
    "CRYO",
)


def fingerprints(path: str | Path) -> list[str]:
    """Toolchain and middleware strings present in a binary."""
    data = Path(path).read_bytes().decode("latin-1")
    found = []
    for marker in TOOLCHAIN_MARKERS:
        for m in re.finditer(re.escape(marker), data):
            line = re.search(r"[ -~]{0,80}", data[m.start() : m.start() + 90])
            if line:
                found.append(line.group().strip())
            break
    return found
