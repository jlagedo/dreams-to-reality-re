"""Adversarial checks for relocated identity, rather than similarity matching."""

import hashlib
import struct
import sys
from pathlib import Path

import pytest

pytest.importorskip("capstone")
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "re" / "tools"))

from check_wip_windows import address_conflicts, fingerprint, profiles  # noqa: E402


class BytesImage:
    def __init__(self, code, base=0x1000, relocations=()):
        self.code = code
        self.base = base
        self.relocs = set(relocations)
        self.unsupported = set()

    def read(self, address, size):
        return self.code[address - self.base : address - self.base + size]


def test_actual_relocations_may_move_but_keep_their_roles():
    a = BytesImage(b"\xa1" + struct.pack("<I", 0xA000) + b"\xc3", relocations=[0x1001])
    b = BytesImage(b"\xa1" + struct.pack("<I", 0xB000) + b"\xc3", relocations=[0x1001])
    fa, fb = fingerprint(a, 0x1000, 6), fingerprint(b, 0x1000, 6)
    assert fa[0] == fb[0]
    assert fa[1] == [("abs", 0xA000)]
    assert fb[1] == [("abs", 0xB000)]


def test_high_scalar_constants_are_not_wildcards():
    a = BytesImage(b"\xb8" + struct.pack("<I", 0x12345678) + b"\xc3")
    b = BytesImage(b"\xb8" + struct.pack("<I", 0x12345679) + b"\xc3")
    assert fingerprint(a, 0x1000, 6)[0] != fingerprint(b, 0x1000, 6)[0]


def test_one_sided_relocation_cannot_match_a_constant():
    code = b"\xb8" + struct.pack("<I", 0xA000) + b"\xc3"
    a, b = BytesImage(code, relocations=[0x1001]), BytesImage(code)
    assert fingerprint(a, 0x1000, 6)[0] != fingerprint(b, 0x1000, 6)[0]


def test_changed_internal_branch_target_is_not_hidden():
    a = BytesImage(b"\x75\x02\x40\x90\xc3")
    b = BytesImage(b"\x75\x00\x40\x90\xc3")
    assert fingerprint(a, 0x1000, 5)[0] != fingerprint(b, 0x1000, 5)[0]


def test_branch_into_middle_of_instruction_is_rejected():
    a = BytesImage(b"\x75\x01\xb8\x01\x00\x00\x00\xc3")
    assert fingerprint(a, 0x1000, 8) is None


def test_relocation_over_opcode_is_rejected():
    a = BytesImage(b"\xb8\x00\x00\x00\x00\xc3", relocations=[0x1000])
    assert fingerprint(a, 0x1000, 6) is None


def test_disassembly_must_cover_every_byte():
    assert fingerprint(BytesImage(b"\xc3\x0f"), 0x1000, 2) is None


def test_conflicting_global_mapping_holds_every_affected_body():
    rows = [
        {"source_entry": 0x1000, "entry": 0x2000, "edges": [("abs", 0xA000, 0xB000)]},
        {"source_entry": 0x1100, "entry": 0x2100, "edges": [("abs", 0xA000, 0xB100)]},
    ]
    address_conflicts(rows, {0x1000, 0x1100}, {0x2000, 0x2100})
    assert all(r["problems"] for r in rows)


def test_call_must_land_at_entry_and_respect_matched_callee():
    rows = [
        {"source_entry": 0x1000, "entry": 0x2000, "edges": [("call", 0x1100, 0x2101)]},
        {"source_entry": 0x1100, "entry": 0x2100, "edges": []},
    ]
    address_conflicts(rows, {0x1000, 0x1100}, {0x2000, 0x2100})
    assert len(rows[0]["problems"]) == 2


def test_contiguous_body_requires_independent_ghidra_memory_hash():
    image = BytesImage(b"\x90" * 8 + b"\xc3")
    rows = {0x1000: {"size": "9", "ranges": "00001000-00001008", "sha256": "wrong"}}
    with pytest.raises(ValueError, match="Ghidra memory"):
        profiles(image, rows)
    rows[0x1000]["sha256"] = hashlib.sha256(image.code).hexdigest()
    assert 0x1000 in profiles(image, rows)
    rows[0x1000]["ranges"] = "00001000-00001003,00001005-00001008"
    assert profiles(image, rows) == {}
