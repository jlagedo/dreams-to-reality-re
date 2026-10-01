"""Synthetic Watcom types: indexed records, field offsets and trailing cue data."""

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "re" / "tools"))

from test_watcom_debug import executable  # noqa: E402
from watcom_types import read_types, type_index  # noqa: E402


def typed_executable():
    prefix = executable()[:0x240]
    types = (
        b"\x07\x10\x10char"
        + struct.pack("<BBHI", 8, 0x60, 1, 1)
        + b"\x08\x61\x00\x01flag"
        + b"\x0a\x12\x00\x02Record"
        + b"\x02\x14"
        + bytes(8)  # Cue/file data after TYPE_EOF is not a type record.
    )
    module = struct.pack("<H", 0) + struct.pack("<IHIHIH", 0, 0, 18, 1, 0, 0) + b"\x06test.c"
    module_at = 26 + len(types)
    end = module_at + len(module)
    section = (
        struct.pack("<4IH", module_at, end, end, end, 0)
        + struct.pack("<II", 26, module_at)
        + types
        + module
    )
    trailer = b"C\0\x01\0" + section
    return prefix + trailer + struct.pack("<H4BHHI", 0x8386, 3, 0, 1, 3, 2, 2, len(trailer) + 14)


@pytest.mark.parametrize("encoded,expected", [(b"\x7f", 127), (b"\x81\x23", 291)])
def test_variable_type_index(encoded, expected):
    assert type_index(encoded, 0) == (expected, len(encoded))


def test_fields_do_not_consume_type_indices_and_eof_stops_before_cues():
    modules = read_types(typed_executable())
    layout = modules[0]["structures"][0]
    assert layout["id"] == 2
    assert layout["name"] == "Record"
    assert layout["size"] == 1
    assert layout["fields"] == [{"offset": 0, "type": 1, "name": "flag"}]


def test_reject_invalid_demand_boundary():
    data = bytearray(typed_executable())
    struct.pack_into("<I", data, 0x240 + 4 + 18, 0xFFFFFFFF)
    with pytest.raises(ValueError, match="demand block"):
        read_types(bytes(data))
