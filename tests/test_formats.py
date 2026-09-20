"""Unit tests that do not need the discs.

Tests requiring real game data are marked ``needs_discs`` and skip when the
configured paths are missing, so CI stays green without shipping assets.
"""

from __future__ import annotations

import struct

import pytest

from dreams import binio, paths, probe
from dreams.formats import audio, disc, resource, scene

DISCS_PRESENT = paths.disc(1).exists()
needs_discs = pytest.mark.skipif(not DISCS_PRESENT, reason="disc images not configured")


# ------------------------------------------------------------ pure logic ---


def test_reader_fixed_point():
    r = binio.Reader(struct.pack("<i", 4669872))
    assert round(r.fixed16_16(), 3) == 71.257


def test_reader_fixed_str_strips_padding():
    r = binio.Reader(b"E01_ME1\0\0\0\0")
    assert r.fixed_str(11) == "E01_ME1"


def test_rgb555_key_colour_is_magenta():
    rgb = binio.unpack_pixels(struct.pack("<H", 0x7C1F), 1, "rgb555")
    assert rgb == bytes((255, 0, 255))


def test_rgb555_default_grey():
    # 0x3DEF appears in .3DC material blocks
    r, g, b = binio.unpack_pixels(struct.pack("<H", 0x3DEF), 1, "rgb555")
    assert r == g == b


def test_png_roundtrip_header():
    out = binio.write_png(paths.get("out") / "test" / "tiny.png", 2, 2, bytes(range(12)))
    assert out.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    out.unlink()


def test_stats_flags_random_data_as_packed():
    import os

    st = probe.stats(os.urandom(20000))
    assert st.zlib_ratio >= 85
    assert st.verdict == "packed/compressed"


def test_stats_flags_repetitive_data_as_raw():
    st = probe.stats(b"\0" * 20000)
    assert st.verdict == "raw"


def test_identify_falls_back_to_extension(tmp_path):
    p = tmp_path / "X.SPR"
    p.write_bytes(b"\x1f\x1c\xff\x7f")
    assert probe.identify(p).kind == "spr"


def test_identify_fsb_bank(tmp_path):
    p = tmp_path / "FSB.DAT"
    p.write_bytes(b"DREAMS FSB  " + struct.pack("<I", 0))
    assert probe.identify(p).kind == "fsb"


def test_parse_cue(tmp_path):
    cue = tmp_path / "x.cue"
    cue.write_text(
        'FILE "track01.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
        'FILE "track02.bin" BINARY\n  TRACK 02 AUDIO\n'
    )
    tracks = disc.parse_cue(cue)
    assert tracks == [(1, "MODE1/2352", "track01.bin"), (2, "AUDIO", "track02.bin")]


def test_to_iso_strips_sector_headers(tmp_path):
    src = tmp_path / "t.bin"
    src.write_bytes((b"H" * 16 + b"D" * 2048 + b"E" * 288) * 3)
    dst = tmp_path / "t.iso"
    assert disc.to_iso(src, dst) == 2048 * 3
    assert dst.read_bytes() == b"D" * 2048 * 3


def test_classify_scene_names():
    assert "east" in scene.classify_name("E01_ME1")
    assert "floor" in scene.classify_name("E01_SOL3")
    assert "column" in scene.classify_name("E02_COL1")


def test_resource_parser(tmp_path):
    p = tmp_path / "DREAMS.INI"
    p.write_text(
        "# comment\n[OBJECT]\n[NEW]\nEpee\nMagic sword\n[NEW]\nMine\nBOOM\n"
        "[PROJECT]\n[NEW]\nProject0\nIle d'Angkor\n",
        encoding="cp1252",
    )
    res = resource.read(p)
    assert res.items == [("Epee", "Magic sword"), ("Mine", "BOOM")]
    assert res.levels == [("Project0", "Ile d'Angkor")]


def test_audio_rejects_foreign_file(tmp_path):
    p = tmp_path / "nope.dat"
    p.write_bytes(b"XXXX" * 8)
    with pytest.raises(ValueError):
        audio.read_bank(p)


# ------------------------------------------------- integration, real data ---


@needs_discs
def test_fsb_bank_matches_documented_layout():
    bank = audio.read_fsb(paths.disc(1) / "DATA/SOUND/FSB.DAT")
    assert bank.declared_count == 24
    assert len(bank.clips) == 24
    first = bank.clips[0]
    assert (first.channels, first.rate, first.bits) == (1, 11025, 16)
    assert first.offset == 0x70


@needs_discs
def test_drd_bank_is_178_eight_bit_clips():
    bank = audio.read_drd(paths.disc(1) / "DATA/3DC/DIALOG.DRD")
    assert bank.declared_count == 178
    assert len(bank.clips) == 178
    assert bank.clips[0].bits == 8


@needs_discs
def test_dsn_declared_size_is_exact():
    sc = scene.read_dsn(paths.disc(1) / "DATA/3DC/E01GROTT.DSN")
    assert sc.size_ok
    assert sc.name_count == len(sc.names) == 26
    assert sc.names[0] == "E01_ME1"


@needs_discs
def test_dan_declared_size_is_exact():
    an = scene.read_dan(paths.disc(1) / "DATA/3DC/AR0.DAN")
    assert an.size_ok
    assert an.name == "YARAIN1"
    assert any(r.endswith(".3DA") for r in an.frame_refs)
