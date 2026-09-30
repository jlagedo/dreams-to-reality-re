"""Synthetic LE pages and Watcom tables; no game-derived bytes in the fixtures."""

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

from watcom_debug import read_contributions, read_debug, read_le_bytes  # noqa: E402


def executable():
    data = bytearray(0x240)
    struct.pack_into("<I", data, 0x3C, 0x40)
    data[0x40:0x42] = b"LE"
    struct.pack_into("<I", data, 0x40 + 0x28, 16)
    struct.pack_into("<II", data, 0x40 + 0x40, 0xC0, 1)
    struct.pack_into("<I", data, 0x40 + 0x48, 0xE0)
    struct.pack_into("<I", data, 0x40 + 0x80, 0x200)
    struct.pack_into("<6I", data, 0x100, 32, 0x10000, 0x2045, 1, 2, 0)
    # Logical pages are deliberately stored in reverse physical order.
    data[0x120:0x128] = b"\x00\x00\x02\x00\x00\x00\x01\x00"
    data[0x200:0x210] = b"qrstuvwxyz012345"
    data[0x210:0x220] = b"abcdefghijklmnop"
    module = struct.pack("<H", 0) + bytes(18) + b"\x06test.c"
    symbol = struct.pack("<IHHBB", 4, 1, 0, 4, 5) + b"Test_"
    section_size = 18 + len(module) + len(symbol)
    section = struct.pack("<4IH", 18, 18 + len(module), section_size, section_size, 0)
    trailer = b"C\0" + b"\x01\0" + section + module + symbol
    master = struct.pack("<H4BHHI", 0x8386, 3, 0, 1, 3, 2, 2, len(trailer) + 14)
    return bytes(data) + trailer + master


def test_page_crossing_uses_physical_map():
    assert read_le_bytes(executable(), 0x1000C, 8) == b"mnopqrst"


def test_read_cannot_escape_object():
    with pytest.raises(ValueError, match="initialized object"):
        read_le_bytes(executable(), 0x1001F, 2)


def test_symbol_has_object_base_and_module_owner():
    modules, symbols = read_debug(executable())
    assert modules[0]["name"] == "test.c"
    assert symbols[0] == {
        "address": "00010004",
        "name": "Test_",
        "module": "test.c",
        "module_index": 0,
        "section": 0,
        "segment": 1,
        "offset": "00000004",
        "kind": 4,
    }


def test_reject_wrong_debug_version():
    data = bytearray(executable())
    data[-12] = 2
    with pytest.raises(ValueError, match="debug format"):
        read_debug(bytes(data))


def test_reject_symbol_name_crossing_table_boundary():
    data = bytearray(executable())
    data[-20] = 255  # Pascal length immediately before the five-byte symbol name.
    with pytest.raises(ValueError, match="global symbol"):
        read_debug(bytes(data))


def with_contributions(second_size=20):
    data = bytearray(executable())
    block = struct.pack("<IHHIHIH", 0, 1, 2, 12, 0, second_size, 0xFFFF)
    section = 0x244
    size = struct.unpack_from("<I", data, section + 12)[0]
    struct.pack_into("<I", data, section + 12, size + len(block))
    header = bytearray(data[-14:])
    size = struct.unpack_from("<I", header, 10)[0]
    struct.pack_into("<I", header, 10, size + len(block))
    return bytes(data[:-14] + block + header)


def test_exact_object_contributions_include_padding():
    rows = read_contributions(with_contributions())
    assert [(r["start"], r["end"], r["module"]) for r in rows] == [
        (0x10000, 0x1000C, "test.c"),
        (0x1000C, 0x10020, "<padding>"),
    ]
    assert all(r["executable"] for r in rows)


def test_contribution_cannot_leave_object():
    with pytest.raises(ValueError, match="leaves its LE object"):
        read_contributions(with_contributions(21))
