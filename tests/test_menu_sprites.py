"""Menu sprite bundles: the ``ICONES.BF`` members (`.ALP` / menu `.SPR`).

Format recovered from the loader ``FUN_00426c46`` and renderer
``FUN_00401935``: 512-byte RGB555 palette, ``"TABLE"`` marker, 28-byte
descriptors, one-byte palette indices or two-byte index/blend texels, and
PYRAM's special layer-reference markers.
"""

from __future__ import annotations

import pytest

from dreams import extract, paths
from dreams.formats.image import (
    MENU_SPRITE_NAMES,
    PYRAM_LAYER_MARKERS,
    menu_sprite_rgba,
    read_bundle,
    read_menu_sheet,
)

DISC = paths.configured("disc1")
DISC2 = paths.configured("disc2")
pytestmark = pytest.mark.skipif(
    DISC is None or not (DISC / "DATA/ICONE/ICONES.BF").exists(),
    reason="Disc 1 unavailable",
)

EXPECTED = {
    # bank: (bytes/pixel, sprite count on disc 1, uniform size or None)
    "magie": (2, 34, (40, 40)),
    "anim": (2, 16, (32, 32)),
    "pyram": (2, 11, None),
    "touches": (1, 11, (24, 24)),
    "interf": (2, 14, None),
}


@pytest.fixture(scope="module")
def members(tmp_path_factory) -> dict[str, object]:
    """The five disc-1 members parsed straight out of the bundle.

    Returns bank name -> MenuSheet; sheets re-read their bytes from a
    snapshot file under the pytest tmp dir.
    """
    from dreams.formats.image import extract_bundle

    bundle = read_bundle(DISC / "DATA/ICONE/ICONES.BF")
    out = tmp_path_factory.mktemp("icones")
    snap = out / bundle.path.name
    snap.write_bytes(bundle.path.read_bytes())
    extracted = extract_bundle(read_bundle(snap), out)
    return {p.stem.lower(): read_menu_sheet(p, bank=p.stem.lower()) for p in extracted}


def test_bundle_has_five_members_and_exact_chain():
    bundle = read_bundle(DISC / "DATA/ICONE/ICONES.BF")
    assert [e.name.lower() for e in bundle.entries] == [
        "magie.alp",
        "anim.alp",
        "pyram.alp",
        "touches.spr",
        "interf.alp",
    ]
    assert bundle.chain_is_exact


def test_all_members_parse_with_expected_depth_and_counts(members):
    for bank, (bpp, count, size) in EXPECTED.items():
        sheet = members[bank]
        assert sheet.bytes_per_pixel == bpp, bank
        assert len(sheet.sprites) == count, bank
        if size:
            for s in sheet.sprites:
                assert (s.width, s.height) == size, (bank, s.index)


def test_every_sprite_lies_between_palette_and_table(members):
    for _bank, sheet in members.items():
        for s in sheet.sprites:
            assert 0x200 <= s.offset
            assert s.offset + s.width * s.height * sheet.bytes_per_pixel <= sheet.table_offset


def test_engine_name_table_slots_exist_in_sheets(members):
    for (bank, slot), name in MENU_SPRITE_NAMES.items():
        if name == "block":  # engine maps block -> magie[34], past the 34 records
            continue
        sheet = members[bank]
        assert slot < len(sheet.sprites), (bank, name)
        assert sheet.sprites[slot].name == name


def test_renders_are_not_degenerate(members):
    sheet = members["interf"]
    uplf = sheet.sprites[2]  # UpLf corner marker
    rgba = menu_sprite_rgba(sheet, uplf)
    assert len(rgba) == uplf.width * uplf.height * 4
    colors = {rgba[i : i + 3] for i in range(0, len(rgba), 4)}
    assert len(colors) > 16  # a glowing ornament, not a flat block


def test_two_byte_pixels_are_palette_index_and_blend(members):
    """Two-byte ALP texels are (palette index, blend), not RGB555 words."""
    sheet = members["interf"]
    uplf = sheet.sprites[2]
    data = sheet.path.read_bytes()
    rgba = menu_sprite_rgba(sheet, uplf)
    saw_transparent = saw_opaque = False
    for i in range(uplf.width * uplf.height):
        index = data[uplf.offset + i * 2]
        blend = data[uplf.offset + i * 2 + 1]
        color = sheet.palette[index]
        channels = tuple((color >> shift) & 0x1F for shift in (10, 5, 0))
        expected = tuple((channel << 3) | (channel >> 2) for channel in channels)
        weight = blend >> 1
        alpha = 0 if index == 0 else 255 if blend >= 63 else (weight * 255 + 16) // 32
        assert rgba[i * 4 : i * 4 + 4] == bytes((*expected, alpha))
        saw_transparent |= (index == 0 or blend == 0) and alpha == 0
        saw_opaque |= index != 0 and blend >= 63 and alpha == 255
    assert saw_transparent and saw_opaque


def test_pyram_layer_references_render_as_diagnostics(members):
    sheet = members["pyram"]
    data = sheet.path.read_bytes()
    found_by_sprite = {}
    for sprite in sheet.sprites:
        pixels = data[sprite.offset : sprite.offset + sprite.width * sprite.height * 2]
        found = {
            marker
            for index, marker in zip(pixels[::2], pixels[1::2], strict=True)
            if index and marker in PYRAM_LAYER_MARKERS
        }
        if found:
            found_by_sprite[sprite.index] = found
            rgba = menu_sprite_rgba(sheet, sprite)
            for i, (index, marker) in enumerate(zip(pixels[::2], pixels[1::2], strict=True)):
                if index and marker in PYRAM_LAYER_MARKERS:
                    assert rgba[i * 4 : i * 4 + 4] == bytes((*PYRAM_LAYER_MARKERS[marker], 255))
    assert found_by_sprite == {0: {0xFD, 0xFE, 0xFF}, 5: {0xFE, 0xFF}}


def test_disc2_adds_titres_with_twelve_title_sprites(tmp_path):
    if DISC2 is None or not (DISC2 / "DATA/ICONE/ICONES.BF").exists():
        pytest.skip("Disc 2 unavailable")
    bundle = read_bundle(DISC2 / "DATA/ICONE/ICONES.BF")
    names = [e.name.lower() for e in bundle.entries]
    assert "titres.spr" in names
    from dreams.formats.image import extract_bundle

    out = tmp_path / "d2"
    path = next(p for p in extract_bundle(bundle, out) if p.name.lower() == "titres.spr")
    sheet = read_menu_sheet(path)
    # four titles, three states each; every state group re-renders the same
    # four titles, so the leading "NEW GAME" width repeats at the group starts
    widths = [s.width for s in sheet.sprites]
    assert len(sheet.sprites) == 12
    assert sheet.bytes_per_pixel == 1
    assert widths[0] == widths[4] == widths[8] == 128
    assert all(100 <= w <= 130 for w in widths)


def test_icon_extractor_renders_all_disc2_menu_sprites(tmp_path):
    source = next(s for s in extract.merge_discs("ICONES.BF") if s.disc == 2)
    item = next(extract.extract_icons(tmp_path, source, force=False))
    pngs = [name for name in item.outputs if name.endswith(".png")]
    assert item.status == "ok"
    assert len(pngs) == sum(count for _bpp, count, _size in EXPECTED.values()) + 12
    assert any(name.startswith("interf_UpLf_") for name in pngs)


def test_icon_extractor_renders_the_standalone_cursor(tmp_path):
    source = next(s for s in extract.plan(["icons"])["icons"] if s.path.name == "SOUR.ALP")
    item = next(extract.extract_icons(tmp_path, source, force=False))
    assert item.status == "ok"
    assert len([name for name in item.outputs if name.endswith(".png")]) == 2
