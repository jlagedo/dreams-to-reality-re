"""Retail HI320/HI480/HI640 indexed font glyphs."""

import pytest

from dreams import extract, paths
from dreams.formats.image import font_glyph_rgba, read_font_sheet

DISC = paths.configured("disc1")
FONT_DIR = DISC / "DATA/FONT" if DISC else None
pytestmark = pytest.mark.skipif(
    FONT_DIR is None or not (FONT_DIR / "HI640.SPR").exists(),
    reason="Disc 1 fonts unavailable",
)


@pytest.fixture(scope="module")
def fonts():
    assert FONT_DIR is not None
    return {
        name: read_font_sheet(FONT_DIR / f"{name}.SPR")
        for name in ("HI320", "HI480", "HI640")
    }


def test_font_tables_hold_256_indexed_glyphs(fonts):
    assert {name: len(sheet.glyphs) for name, sheet in fonts.items()} == {
        "HI320": 256,
        "HI480": 256,
        "HI640": 256,
    }
    assert {name: sum(g.renderable for g in sheet.glyphs) for name, sheet in fonts.items()} == {
        "HI320": 255,
        "HI480": 256,
        "HI640": 256,
    }
    assert [g.codepoint for g in fonts["HI320"].glyphs if not g.renderable] == [37]


def test_font_glyph_preview_uses_rgb555_palette_and_zero_key(fonts):
    for sheet in fonts.values():
        glyph = sheet.glyphs[ord("A")]
        data = sheet.path.read_bytes()[glyph.offset : glyph.offset + glyph.width * glyph.height]
        rgba = font_glyph_rgba(sheet, glyph)
        assert len(rgba) == glyph.width * glyph.height * 4
        assert any(index == 0 for index in data)
        assert any(index != 0 for index in data)
        for i, index in enumerate(data):
            color = sheet.palette[index]
            channels = tuple((color >> shift) & 0x1F for shift in (10, 5, 0))
            rgb = tuple((channel << 3) | (channel >> 2) for channel in channels)
            alpha = 0 if index == 0 else 255
            assert rgba[i * 4 : i * 4 + 4] == bytes((*rgb, alpha))


def test_font_text_advance_matches_runtime_special_case(fonts):
    for sheet in fonts.values():
        assert sheet.advance(0x20) == sheet.advance(ord("0"))
        assert sheet.advance(ord("A")) > 0


def test_sprite_extractor_writes_font_glyph_pngs(tmp_path):
    source = next(s for s in extract.merge_discs("HI320.SPR") if s.disc == 1)
    item = next(extract.extract_sprites(tmp_path, source, force=False))
    assert item.status == "ok"
    assert len(item.outputs) == 255
    assert (tmp_path / "images/sprites/hi320/065_7x8.png").exists()
