"""OMF parsing checks, built on a synthetic library so no reference CD is needed."""

from __future__ import annotations

import struct

from dreams import watcom

PAGE = 512


def _record(rec_type: int, body: bytes) -> bytes:
    return bytes([rec_type]) + struct.pack("<H", len(body) + 1) + body + b"\x00"


def _pstr(s: str) -> bytes:
    return bytes([len(s)]) + s.encode("latin-1")


CODE = bytes(range(0x40, 0x40 + 40))  # 40 distinct bytes; 5..8 get relocated


def _library() -> bytes:
    header = _record(watcom.LIB_HEADER, b"\x00" * (PAGE - 4))

    module = b"".join(
        [
            _record(0x80, _pstr("synth")),
            _record(0x96, _pstr("") + _pstr("_TEXT") + _pstr("CODE")),
            # ACBP: byte-aligned, public combine, 32-bit. Then length, name, class.
            _record(0x99, b"\x29" + struct.pack("<I", len(CODE)) + b"\x02\x03\x01"),
            _record(0xA1, b"\x01" + struct.pack("<I", 0) + CODE),
            # One 32-bit offset fixup covering bytes 5..8 of the LEDATA payload.
            _record(0x9D, b"\xa4\x05\x54\x01"),
            _record(0x91, b"\x00\x01" + _pstr("_synth_fn_") + struct.pack("<I", 0) + b"\x00"),
            _record(0x8A, b"\x00"),
        ]
    )
    module += b"\x00" * (-len(module) % PAGE)
    return header + module + _record(watcom.LIB_END, b"\x00" * 4)


def test_parses_module_segment_and_public():
    mods = watcom.read_library_bytes(_library())
    assert len(mods) == 1
    mod = mods[0]
    assert mod.name == "synth"
    assert mod.segments[1].class_name == "CODE"
    assert mod.publics == [("_synth_fn_", 1, 0)]


def test_fixups_become_wildcards():
    (mod,) = watcom.read_library_bytes(_library())
    (sig,) = watcom.signatures([mod])
    assert sig.name == "_synth_fn_"
    assert sig.data == CODE
    assert sig.mask[4] == 1 and sig.mask[9] == 1
    assert sig.mask[5:9] == bytes(4)  # the relocated field
    assert not sig.exact


def test_matches_through_a_changed_relocation():
    (mod,) = watcom.read_library_bytes(_library())
    sigs = watcom.signatures([mod])
    linked = bytearray(b"\xcc" * 64 + CODE + b"\xcc" * 64)
    linked[64 + 5 : 64 + 9] = b"\xde\xad\xbe\xef"  # what the linker would patch in

    (hit,) = watcom.match(sigs, bytes(linked))
    assert hit.offset == 64
    assert hit.names == ["_synth_fn_"]
    assert hit.full

    # A byte the fixup does not cover must still be respected.
    linked[64 + 20] ^= 0xFF
    assert watcom.match(sigs, bytes(linked)) == []
