"""Compiler idioms must explain the difference, rather than merely mask it."""

import struct
import sys
from pathlib import Path

import pytest

pytest.importorskip("capstone")
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "re" / "tools"))

from check_wip_compiler import extern_fixups, match  # noqa: E402
from check_wip_layout import validate_graph  # noqa: E402
from check_wip_windows import disassembler, fingerprint  # noqa: E402
from test_wip_windows import BytesImage  # noqa: E402
from watcom_patterns import preincremented_loop_biases  # noqa: E402


def loop(extra=b"", initializer=b"\x31\xc0"):
    code = initializer + b"\x83\xc0\x10" + extra + b"\x89\x88\x00\x40\x00\x00"
    code += b"\x3d\x80\x00\x00\x00"
    return code + b"\x75" + bytes([(len(initializer) - len(code) - 2) & 255]) + b"\xc3"


def test_preincrement_bias_uses_first_effective_array_element():
    code = loop()
    instructions = list(disassembler().disasm(code, 0x1000))
    assert preincremented_loop_biases(instructions) == {0x1007: 16}
    image = BytesImage(code, relocations=[0x1007])
    assert fingerprint(image, 0x1000, len(code))[1] == [("abs", 0x4000)]
    assert fingerprint(image, 0x1000, len(code), normalize_index_bias=True)[1] == [("abs", 0x4010)]


@pytest.mark.parametrize(
    "extra,init",
    [
        (b"\x40", b"\x31\xc0"),  # another induction-variable write
        (b"\xe8\x00\x00\x00\x00", b"\x31\xc0"),  # call may clobber it
        (b"", b"\xb8\x01\x00\x00\x00"),  # nonzero start is not the supported idiom
    ],
)
def test_bias_requires_the_complete_proven_loop_shape(extra, init):
    assert preincremented_loop_biases(list(disassembler().disasm(loop(extra, init), 0x1000))) == {}


def test_interior_control_flow_requires_a_corresponding_proven_owner():
    rows = [
        {
            "source_entry": 0x1000,
            "entry": 0x2000,
            "size": 5,
            "offsets": [0],
            "edges": [("branch", 0x1050, 0x2050)],
            "boundary_problems": [],
        },
        {
            "source_entry": 0x1040,
            "entry": 0x2040,
            "size": 32,
            "offsets": [0, 16],
            "edges": [],
            "boundary_problems": [],
        },
    ]
    image = BytesImage(bytes(0x100), base=0x1000)
    other = BytesImage(bytes(0x100), base=0x2000)
    validate_graph(rows, {0x1000, 0x1040}, {0x2000, 0x2040}, image, other)
    assert not rows[0]["problems"]
    rows[0]["edges"] = [("branch", 0x1050, 0x2051)]
    validate_graph(rows, {0x1000, 0x1040}, {0x2000, 0x2040}, image, other)
    assert rows[0]["problems"]


def test_omf_roles_are_read_not_guessed_from_wildcard_positions():
    def record(kind, payload):
        return bytes([kind]) + struct.pack("<H", len(payload) + 1) + payload + b"\0"

    obj = record(0x8C, b"\x08_current\0\x05__CHK\0")
    obj += record(0xA1, b"\x01" + bytes(4) + bytes(16))
    obj += record(0x9D, bytes.fromhex("e404160201a40a160202"))
    assert extern_fixups(obj, 1) == [(4, "_current", True), (10, "__CHK", False)]


def test_compiler_model_checks_the_actual_helper_target():
    target = 0x2000
    code = b"\xe8" + struct.pack("<i", target - 0x1005) + b"\xc3"
    model = (b"\xe8" + bytes(4) + b"\xc3", b"\x01\0\0\0\0\x01", [(1, "__CHK", False)])
    assert match(BytesImage(code), 0x1000, 6, model, {"__CHK": target}) == {}
    with pytest.raises(ValueError, match="helper call"):
        match(BytesImage(code), 0x1000, 6, model, {"__CHK": target + 1})
