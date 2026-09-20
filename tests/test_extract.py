"""Tests for the extraction pipeline and the formats it depends on.

Anything needing real game data is marked ``needs_discs`` and skips when the
configured paths are missing, so CI stays green without shipping assets.
"""

from __future__ import annotations

import struct
import zlib

import pytest

from dreams import extract, paths, png
from dreams.formats import audio, cdaudio, image, scene, video

DISCS_PRESENT = paths.disc(1).exists()
needs_discs = pytest.mark.skipif(not DISCS_PRESENT, reason="disc images not configured")


# ------------------------------------------------------------- pure logic ---


def test_png_is_structurally_valid(tmp_path):
    out = png.write(tmp_path / "t.png", 2, 2, bytes(range(12)))
    blob = out.read_bytes()
    assert blob.startswith(png.SIGNATURE)
    assert blob[12:16] == b"IHDR"
    assert blob.endswith(b"IEND\xae\x42\x60\x82")
    width, height, depth, colour = struct.unpack_from(">IIBB", blob, 16)
    assert (width, height, depth, colour) == (2, 2, 8, 2)


def test_png_rejects_wrong_buffer_size(tmp_path):
    with pytest.raises(ValueError, match="expected 12 bytes"):
        png.write(tmp_path / "bad.png", 2, 2, b"\x00" * 11)


def test_png_alpha_channel_roundtrips(tmp_path):
    pixels = bytes([255, 0, 0, 0, 0, 255, 0, 255])  # one transparent, one opaque
    out = png.write(tmp_path / "a.png", 2, 1, pixels, alpha=True)
    blob = out.read_bytes()
    assert struct.unpack_from(">B", blob, 25)[0] == 6  # colour type RGBA
    idat = blob[blob.index(b"IDAT") + 4 : -12]
    assert zlib.decompress(idat) == b"\x00" + pixels


def test_rgb555_expands_to_full_range():
    # 0x7FFF is white; each channel must reach 0xFF, not 0xF8.
    assert png.rgb555_to_rgb(struct.pack("<H", 0x7FFF), 1) == b"\xff\xff\xff"
    assert png.rgb555_to_rgb(struct.pack("<H", 0x0000), 1) == b"\x00\x00\x00"


def test_vga6_palette_expands_to_full_range():
    assert png.vga6_to_rgb(bytes([0x3F, 0x3F, 0x3F, 0])) == (255, 255, 255)
    assert png.vga6_to_rgb(bytes([0, 0, 0, 0])) == (0, 0, 0)


def test_clip_detects_impossible_format_tag():
    # 16-bit IEEE float does not exist - FSB.DAT clip 12 ships exactly this.
    bad = audio.Clip(0, 0, 100, 1, 11025, 16, audio.WAVE_IEEE_FLOAT)
    good = audio.Clip(0, 0, 100, 1, 11025, 16, audio.WAVE_PCM)
    real_float = audio.Clip(0, 0, 100, 1, 11025, 32, audio.WAVE_IEEE_FLOAT)
    assert bad.needs_repair
    assert not good.needs_repair
    assert not real_float.needs_repair


def test_cd_byte_rate_matches_redbook():
    assert cdaudio.BYTE_RATE == 176400
    t = cdaudio.Track(1, 2, paths.disc(1), cdaudio.BYTE_RATE * 90, True)
    assert t.seconds == 90.0
    assert t.duration == "1:30.0"


def test_every_group_has_a_layout_and_description():
    for group in extract.ALL_GROUPS:
        assert group in extract.LAYOUT
        assert extract.DESCRIPTIONS.get(group)


def test_video_groups_are_real_groups():
    assert extract.VIDEO_GROUPS <= set(extract.ALL_GROUPS)


def test_plugin_table_covers_every_known_magic():
    assert set(video.PLUGIN) == {"HNM4", "UBB2", "UBS2", "HNM6", "HNS6"}


def test_sprite_record_size_rounds_up_to_four():
    assert image.Sprite(0, 0, 10, 11, 0, 0).record_size == 128  # 16 + 110 -> 128
    assert image.Sprite(0, 0, 4, 4, 0, 0).record_size == 32  # 16 + 16, already aligned


# ------------------------------------------------------------ needs discs ---


@needs_discs
def test_dsn_span_formula_holds_for_every_scene():
    """A = 31*name_count + 7, and the body starts at 24 + 31*name_count."""
    scenes = extract.merge_discs("*.DSN")
    assert scenes, "no .DSN files found"
    for s in scenes:
        sc = scene.read_dsn(s.path)
        assert sc.span_ok, f"{s.rel}: A={sc.count_a} names={sc.name_count}"
        assert sc.size_ok
        assert sc.body_offset == 24 + 31 * sc.name_count


@needs_discs
def test_dan_body_offset_formula_holds_for_every_animation():
    """16 + 11N + 2 + 13F == 9 + A."""
    anims = extract.merge_discs("*.DAN")
    assert anims, "no .DAN files found"
    for a in anims:
        an = scene.read_dan(a.path)
        assert an.span_ok, f"{a.rel}: body={an.body_offset} span={an.span}"
        assert an.size_ok


@needs_discs
def test_referenced_3da_files_do_not_exist():
    """The .DAN frames are embedded; the labels are authoring residue."""
    refs = set()
    for a in extract.merge_discs("*.DAN"):
        refs.update(scene.read_dan(a.path).frame_refs)
    assert refs, "no .3DA references found"
    assert not extract.merge_discs("*.3DA")


@needs_discs
def test_ubik_payload_chain_is_exact():
    bundles = [
        s for s in extract.merge_discs("ICONES.*")
        if s.path.open("rb").read(4) == image.UBIK_MAGIC
    ]
    assert bundles
    for s in bundles:
        b = image.read_bundle(s.path)
        assert b.entries and len(b.entries) == b.entry_count
        assert b.chain_is_exact, s.rel


@needs_discs
def test_indexed_sprites_decode_to_expected_counts():
    expected = {"ALPHABET": 64, "ALPHABE2": 64, "PARTICL2": 64, "PARTICLE": 32, "OBJET0": 8}
    seen = {}
    for s in extract.merge_discs("*.SPR"):
        try:
            sheet = image.read_spritesheet(s.path)
        except ValueError:
            continue  # the HI* font family is a different layout
        seen[s.path.stem.upper()] = len(sheet.sprites)
    assert seen == expected


@needs_discs
def test_font_spr_is_not_an_indexed_bundle():
    fonts = extract.merge_discs("HI*.SPR")
    assert fonts
    for s in fonts:
        with pytest.raises(ValueError, match="not an indexed"):
            image.read_spritesheet(s.path)


@needs_discs
def test_merge_discs_deduplicates_identical_files():
    """Same name on both discs with identical bytes must yield one Source."""
    sources = extract.merge_discs("*.DSN")
    rels = [s.rel for s in sources]
    assert len(rels) == len(set(rels)) or all(s.suffix for s in sources if rels.count(s.rel) > 1)


@needs_discs
def test_fsb_clip_12_needs_repair():
    """A real defect in the shipped data, not a parsing artefact."""
    src = next(s for s in extract.merge_discs("FSB.DAT") if "SOUND" in s.rel)
    bank = audio.read_bank(src.path)
    assert [c.index for c in bank.clips if c.needs_repair] == [12]
