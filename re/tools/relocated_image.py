"""Read PE/Watcom LE bytes at preferred addresses, applying LE internal fixups.

Only 32-bit internal offset fixups are applied. Other source forms mark their
bytes unsupported for code comparison; external/additive fixups fail closed.
PE BSS with a zero raw pointer is never treated as file-backed data.
"""

from __future__ import annotations

import struct
from pathlib import Path

from match_identical import PE
from watcom_debug import le_objects


class Image:
    def __init__(self, path: Path):
        self.raw = path.read_bytes()
        self.sections: list[tuple[int, bytearray]] = []
        self.relocs: set[int] = set()
        self.unsupported: set[int] = set()
        data = self.raw
        header = struct.unpack_from("<I", data, 60)[0]
        if data[header : header + 4] == b"PE\0\0":
            pe = PE(path)
            self.relocs = pe.relocs
            for rva, _, raw_at, raw_size in pe.secs:
                if raw_size and raw_at:
                    self.sections.append(
                        (pe.base + rva, bytearray(data[raw_at : raw_at + raw_size]))
                    )
            return
        header, objects = le_objects(data)

        def u32(offset):
            return struct.unpack_from("<I", data, header + offset)[0]

        pages, page_size, last = u32(0x14), u32(0x28), u32(0x2C)
        page_addresses = {}
        for size, base, _, first, count, _ in objects:
            buffer = bytearray(min(size, count * page_size))
            for page in range(count):
                index = first + page - 1
                at = header + u32(0x48) + index * 4
                physical = int.from_bytes(data[at : at + 3], "big")
                if data[at + 3] != 0 or physical == 0:
                    raise ValueError("unsupported LE page")
                begin = u32(0x80) + (physical - 1) * page_size
                length = min(page_size, len(buffer) - page * page_size)
                if physical == pages:
                    length = min(length, last or page_size)
                chunk = data[begin : begin + length]
                if len(chunk) != length:
                    raise ValueError("truncated LE page")
                buffer[page * page_size : page * page_size + length] = chunk
                page_addresses[index] = base + page * page_size
            if buffer:
                self.sections.append((base, buffer))
        page_table, records = header + u32(0x68), header + u32(0x6C)
        for page in range(pages):
            begin, end = struct.unpack_from("<II", data, page_table + page * 4)
            pos = records + begin
            while pos < records + end:
                source, flags = data[pos : pos + 2]
                pos += 2
                if flags & 0x0F:
                    raise ValueError("external, additive or chained LE fixup")
                if source & 0x20:
                    count = data[pos]
                    pos += 1
                    locations = None
                else:
                    locations = [struct.unpack_from("<h", data, pos)[0]]
                    pos += 2
                width = 2 if flags & 0x40 else 1
                obj = int.from_bytes(data[pos : pos + width], "little")
                pos += width
                kind = source & 15
                if kind == 2:
                    offset = 0
                else:
                    width = 4 if flags & 0x10 else 2
                    offset = int.from_bytes(data[pos : pos + width], "little")
                    pos += width
                if locations is None:
                    locations = list(struct.unpack_from(f"<{count}h", data, pos))
                    pos += count * 2
                if not 1 <= obj <= len(objects):
                    raise ValueError("unknown fixup object")
                target = objects[obj - 1][1] + offset
                for location in locations:
                    address = page_addresses[page] + location
                    if kind in (7, 8):
                        value = target if kind == 7 else (target - address - 4) & 0xFFFFFFFF
                        self.write(address, struct.pack("<I", value))
                        # Self-relative fixups are handled as decoded branch operands.
                        if kind == 7:
                            self.relocs.add(address)
                    else:
                        width = {0: 1, 2: 2, 3: 4, 5: 2, 6: 6}[kind]
                        self.unsupported.update(range(address, address + width))
            if pos != records + end:
                raise ValueError("fixup record crosses its table boundary")

    def read(self, address: int, size: int) -> bytes:
        for base, data in self.sections:
            if base <= address and address + size <= base + len(data):
                return bytes(data[address - base : address - base + size])
        return b""

    def write(self, address: int, value: bytes) -> None:
        for base, data in self.sections:
            if base <= address and address + len(value) <= base + len(data):
                data[address - base : address - base + len(value)] = value
                return
        raise ValueError(f"fixup outside mapped bytes: {address:08x}")
