"""Extract Watcom v3 type records, names and explicit structure-member offsets.

Follows Open Watcom's wattype.h/wattype.c. The TYPE_EOF record ends the types;
source cue/file tables may follow it within the same demand block. This is an
evidence export, not a complete C type/prototype translator.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from watcom_debug import read_debug


def type_index(record: bytes, position: int) -> tuple[int, int]:
    value = record[position]
    position += 1
    if value & 0x80:
        value = ((value & 0x7F) << 8) + record[position]
        position += 1
    return value, position


def read_types(data: bytes) -> list[dict]:
    modules, _ = read_debug(data)
    header = struct.unpack_from("<H4BHHI", data, len(data) - 14)
    section = len(data) - header[-1] + header[5] + header[6]
    section_size = struct.unpack_from("<I", data, section + 12)[0]
    if section + section_size != len(data) - 14:
        raise ValueError("type exporter currently supports one root debug section")
    output = []
    for module in modules:
        count = module["types_entries"]
        if not count:
            continue
        offsets = struct.unpack_from(f"<{count + 1}I", data, section + module["types_offset"])
        records, number, current = [], 0, None
        for begin, end in zip(offsets, offsets[1:], strict=False):
            if not 18 <= begin <= end <= section_size:
                raise ValueError("invalid type demand block")
            pos, limit = section + begin, section + end
            while pos < limit:
                length, kind = data[pos : pos + 2]
                if length < 2 or pos + length > limit:
                    raise ValueError("invalid type record length")
                raw = data[pos : pos + length]
                pos += length
                numbered = not (
                    ((kind & 0xF0) in (0x50, 0x60) and kind & 15) or kind in (0x13, 0x14)
                )
                if numbered:
                    number += 1
                record = {"id": number if numbered else None, "kind": kind, "raw": raw.hex()}
                if kind == 0x10:
                    record.update(
                        name=raw[3:].decode("latin-1"), scalar=raw[2], size=(raw[2] & 15) + 1
                    )
                elif kind == 0x11:
                    record["name"] = raw[2:].decode("latin-1")
                elif kind == 0x12:
                    scope, at = type_index(raw, 2)
                    target, at = type_index(raw, at)
                    record.update(name=raw[at:].decode("latin-1"), scope=scope, type=target)
                elif kind == 0x60:
                    count = struct.unpack_from("<H", raw, 2)[0]
                    size = struct.unpack_from("<I", raw, 4)[0] if len(raw) > 4 else None
                    record.update(count=count, size=size, fields=[])
                    current = record
                elif kind in (0x61, 0x62, 0x63):
                    width = {0x61: 1, 0x62: 2, 0x63: 4}[kind]
                    offset = int.from_bytes(raw[2 : 2 + width], "little")
                    target, at = type_index(raw, 2 + width)
                    record.update(offset=offset, type=target, name=raw[at:].decode("latin-1"))
                    if current is not None:
                        current["fields"].append(
                            {key: record[key] for key in ("offset", "type", "name")}
                        )
                elif kind & 0xF0 == 0x40:
                    record["type"] = type_index(raw, 2)[0]
                elif kind in (0x20, 0x21, 0x22):
                    width = {0x20: 1, 0x21: 2, 0x22: 4}[kind]
                    record.update(
                        count=int.from_bytes(raw[2 : 2 + width], "little") + 1,
                        type=type_index(raw, 2 + width)[0],
                    )
                records.append(record)
                if kind == 0x14:
                    break
        names = {r["type"]: r["name"] for r in records if r["kind"] == 0x12}
        structures = [dict(r, name=names.get(r["id"], "")) for r in records if r["kind"] == 0x60]
        output.append({"module": module["name"], "records": records, "structures": structures})
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    modules = read_types(args.binary.read_bytes())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(modules, indent=2), encoding="utf-8")
    named = {(s["name"], s["size"]) for m in modules for s in m["structures"] if s["name"]}
    print(f"{len(modules)} modules with type records; {len(named)} distinct named layout sizes")


if __name__ == "__main__":
    main()
