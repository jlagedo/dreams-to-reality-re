"""Menu sprite bundles: the ``ICONES.BF`` members (`.ALP` / menu `.SPR`).

Format recovered from the loader ``FUN_00426c46`` and verified against every
member on both discs: 512-byte RGB555 palette, pixel blobs, ``"TABLE"`` marker,
28-byte descriptors, per-file 1 or 2 bytes per pixel.
"""

from __future__ import annotations

import pytest

from dreams import paths
from dreams.formats.image import (
    MENU_SPRITE_NAMES,
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


def test_16bit_pixels_are_byte_swapped_rgb555(members):
    """The menu's corner markers are blue/cyan in the real game.

    A little-endian read puts the sprite's brightness ramp in the green
    channel and renders them green; the pixels are stored most-significant
    byte first. Regression pin for the swap in ``menu_sprite_rgba``.
    """
    sheet = members["interf"]
    uplf = sheet.sprites[2]
    rgba = menu_sprite_rgba(sheet, uplf)
    counts: dict[tuple[int, int, int], int] = {}
    for i in range(0, len(rgba), 4):
        r, g, b = rgba[i], rgba[i + 1], rgba[i + 2]
        if max(r, g, b) - min(r, g, b) >= 60:
            counts[(r, g, b)] = counts.get((r, g, b), 0) + 1
    dominant = max(counts, key=counts.get)
    assert dominant[2] > dominant[1], f"expected blue-dominant, got {dominant}"


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
