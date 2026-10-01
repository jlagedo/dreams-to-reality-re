"""Read embedded Watcom v3 debug symbols from a DOS LE executable.

The 14-byte EOF header, section tables, and Pascal strings are described by
Open Watcom bld/dip/watcom/h/dbginfo.h. Offsets in symbols are object-relative;
the returned address uses the LE object's preferred base, as our Ghidra loader does.
Local variables, types and line tables are not decoded here.

    uv run python re/tools/watcom_debug.py <demo/DREAMS.EXE> --out out/demo-symbols.tsv
"""

from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path


def le_objects(data: bytes) -> tuple[int, list[tuple[int, ...]]]:
    header = struct.unpack_from("<I", data, 0x3C)[0]
    if data[header : header + 2] != b"LE":
        raise ValueError("expected an LE executable")
    table, count = struct.unpack_from("<II", data, header + 0x40)
    objects = [struct.unpack_from("<6I", data, header + table + i * 24) for i in range(count)]
    return header, objects


def read_le_bytes(data: bytes, address: int, size: int) -> bytes:
    """Read original file bytes through the page map, without applying fixups."""
    header, objects = le_objects(data)
    page_size = struct.unpack_from("<I", data, header + 0x28)[0]
    page_map = header + struct.unpack_from("<I", data, header + 0x48)[0]
    page_data = struct.unpack_from("<I", data, header + 0x80)[0]
    for obj_size, base, _, first, count, _ in objects:
        if not base <= address < base + obj_size:
            continue
        offset = address - base
        if offset + size > min(obj_size, count * page_size):
            raise ValueError("read leaves initialized object")
        result = bytearray()
        while len(result) < size:
            page, within = divmod(offset, page_size)
            at = page_map + (first - 1 + page) * 4
            physical = int.from_bytes(data[at : at + 3], "big")
            if data[at + 3] != 0 or physical == 0:
                raise ValueError("unsupported LE page")
            at = page_data + (physical - 1) * page_size + within
            n = min(size - len(result), page_size - within)
            chunk = data[at : at + n]
            if len(chunk) != n:
                raise ValueError("truncated LE page")
            result.extend(chunk)
            offset += n
        return bytes(result)
    raise ValueError(f"address {address:08x} is outside LE objects")


def read_debug(data: bytes) -> tuple[list[dict], list[dict]]:
    """Return module and global-symbol records; reject malformed section boundaries."""
    _, objects = le_objects(data)
    signature, major, minor, obj_major, obj_minor, langs, segments, size = struct.unpack_from(
        "<H4BHHI", data, len(data) - 14
    )
    if (signature, major, minor, obj_major, obj_minor) != (0x8386, 3, 0, 1, 3):
        raise ValueError("expected Watcom debug format 3.0 / object format 1.3")
    start = len(data) - size
    end = len(data) - 14
    if not 0 <= start < end or start + langs + segments > end:
        raise ValueError("invalid debug trailer size")
    section = start + langs + segments
    modules, symbols = [], []
    while section < end:
        mod, glob, addr, section_size, section_id = struct.unpack_from("<4IH", data, section)
        if not 18 <= mod <= glob <= addr <= section_size <= end - section:
            raise ValueError("invalid debug section offsets")
        section_modules = []
        pos = section + mod
        while pos < section + glob:
            if pos + 21 > section + glob:
                raise ValueError("truncated module header")
            n = data[pos + 20]
            if pos + 21 + n > section + glob:
                raise ValueError("truncated module name")
            name = data[pos + 21 : pos + 21 + n].decode("latin-1")
            record = {"section": section_id, "index": len(section_modules), "name": name}
            for i, kind in enumerate(("locals", "types", "lines")):
                offset, count = struct.unpack_from("<IH", data, pos + 2 + i * 6)
                record[kind + "_offset"] = offset
                record[kind + "_entries"] = count
            section_modules.append(record)
            pos += 21 + n
        modules.extend(section_modules)
        pos = section + glob
        while pos < section + addr:
            if pos + 10 > section + addr:
                raise ValueError("truncated global header")
            offset, segment, owner, kind, n = struct.unpack_from("<IHHBB", data, pos)
            if pos + 10 + n > section + addr or owner >= len(section_modules):
                raise ValueError("invalid global symbol record")
            if not 1 <= segment <= len(objects):
                raise ValueError("symbol references an unknown LE object")
            name = data[pos + 10 : pos + 10 + n].decode("latin-1")
            symbols.append(
                {
                    "address": f"{objects[segment - 1][1] + offset:08x}",
                    "name": name,
                    "module": section_modules[owner]["name"],
                    "module_index": owner,
                    "section": section_id,
                    "segment": segment,
                    "offset": f"{offset:08x}",
                    "kind": kind,
                }
            )
            pos += 10 + n
        section += section_size
    if section != end:
        raise ValueError("debug sections do not end at the master header")
    return modules, symbols


def read_contributions(data: bytes) -> list[dict]:
    """Exact linked object contributions from Watcom's address-info sections.

    Each segment group supplies a base and a run of (byte size, module index)
    records. The next contribution begins exactly where the previous one ends;
    these are linker records, not inferred intervals between function symbols.
    """
    modules, _ = read_debug(data)
    _, objects = le_objects(data)
    owners = {(m["section"], m["index"]): m["name"] for m in modules}
    header = struct.unpack_from("<H4BHHI", data, len(data) - 14)
    section = len(data) - header[-1] + header[5] + header[6]
    output = []
    while section < len(data) - 14:
        _, _, address_offset, size, section_id = struct.unpack_from("<4IH", data, section)
        pos, end = section + address_offset, section + size
        while pos < end:
            if pos + 8 > end:
                raise ValueError("truncated address-info group")
            offset, segment, count = struct.unpack_from("<IHH", data, pos)
            pos += 8
            if not 1 <= segment <= len(objects):
                raise ValueError("address-info references an unknown object")
            obj_size, base, flags, _, _, _ = objects[segment - 1]
            for _ in range(count & 0x7FFF):
                if pos + 6 > end:
                    raise ValueError("truncated address-info contribution")
                length, module = struct.unpack_from("<IH", data, pos)
                pos += 6
                if offset + length > obj_size:
                    raise ValueError("contribution leaves its LE object")
                if module != 0xFFFF and (section_id, module) not in owners:
                    raise ValueError("contribution references an unknown module")
                output.append(
                    {
                        "section": section_id,
                        "segment": segment,
                        "start": base + offset,
                        "end": base + offset + length,
                        "size": length,
                        "module_index": module,
                        "module": owners.get((section_id, module), "<padding>"),
                        "executable": bool(flags & 4),
                        "data_flag": bool(count & 0x8000),
                    }
                )
                offset += length
        section += size
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    modules, symbols = read_debug(args.binary.read_bytes())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(symbols[0]), delimiter="\t")
        writer.writeheader()
        writer.writerows(symbols)
    print(f"{len(modules)} modules, {len(symbols)} symbols -> {args.out}")


if __name__ == "__main__":
    main()
