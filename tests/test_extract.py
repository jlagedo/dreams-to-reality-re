"""Tests for the extraction pipeline and the formats it depends on.

Anything needing real game data is marked ``needs_discs`` and skips when the
configured paths are missing, so CI stays green without shipping assets.
Whole-corpus sweeps are also marked ``corpus`` and only run with ``-m corpus``.
"""

from __future__ import annotations

import json
import struct
import zlib

import pytest

from dreams import extract, paths, png
from dreams.formats import audio, cdaudio, image, lz, scene, video

DISC1 = paths.configured("disc1")
DISCS_PRESENT = DISC1 is not None and DISC1.exists()
needs_discs = pytest.mark.skipif(not DISCS_PRESENT, reason="disc images not configured")
# Sweeps over every scene or record: minutes, not seconds. Opt in with -m corpus.
corpus = pytest.mark.corpus


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


def test_cd_byte_rate_matches_redbook(tmp_path):
    assert cdaudio.BYTE_RATE == 176400
    t = cdaudio.Track(1, 2, tmp_path, cdaudio.BYTE_RATE * 90, True)
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


def test_disambiguate_promotes_colliding_stems(tmp_path):
    """Five ICONES generations differ only by extension and must not overwrite."""
    srcs = [
        extract.Source("DATA/ICONE/ICONES.BF", tmp_path, 1),
        extract.Source("DATA/ICONE/ICONES.BAK", tmp_path, 1),
        extract.Source("DATA/ICONE/OLD/ICONES.BAK", tmp_path, 1),
    ]
    assert len({s.stem for s in srcs}) == 1  # all collapse to "icones"
    extract.disambiguate(srcs)
    assert [s.stem for s in srcs] == [
        "icone_icones_bf",
        "icone_icones_bak",
        "icone_old_icones_bak",
    ]
    assert len({s.stem for s in srcs}) == 3


def test_disambiguate_leaves_unique_stems_short(tmp_path):
    srcs = [
        extract.Source("DATA/HNM/INTRO.HNM", tmp_path, 1),
        extract.Source("DATA/HNM/ARENE.HNM", tmp_path, 1),
    ]
    extract.disambiguate(srcs)
    assert [s.stem for s in srcs] == ["intro", "arene"]


def test_sprite_record_size_rounds_up_to_four():
    assert image.Sprite(0, 0, 10, 11, 0, 0).record_size == 128  # 16 + 110 -> 128
    assert image.Sprite(0, 0, 4, 4, 0, 0).record_size == 32  # 16 + 16, already aligned


# ------------------------------------------------------------ needs discs ---


@needs_discs
def test_dsn_span_formula_holds_for_every_scene():
    """A = 31*name_count + 7, and the body starts at 9 + A == 16 + 31*name_count."""
    scenes = extract.merge_discs("*.DSN")
    assert scenes, "no .DSN files found"
    for s in scenes:
        sc = scene.read_dsn(s.path)
        assert sc.span_ok, f"{s.rel}: A={sc.count_a} names={sc.name_count}"
        assert sc.size_ok
        assert sc.body_offset == 16 + 31 * sc.name_count == 9 + sc.count_a


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
        s for s in extract.merge_discs("ICONES.*") if s.path.open("rb").read(4) == image.UBIK_MAGIC
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
def test_no_group_produces_colliding_output_names():
    """A collision silently overwrites assets - this is how the icon bug slipped in."""
    for group, srcs in extract.plan(list(extract.ALL_GROUPS)).items():
        stems = [s.stem for s in srcs]
        assert len(stems) == len(set(stems)), f"{group}: duplicate output names"


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


@needs_discs
def test_scene_body_is_a_complete_record_chain():
    """u8 tag + u32 size must walk to EOF exactly - a short walk means a bad offset."""
    for s in extract.merge_discs("*.DSN"):
        sc = scene.read_dsn(s.path)
        recs = scene.read_records(s.path)
        assert recs, s.rel
        consumed = recs[-1].offset + recs[-1].size
        assert consumed == sc.body_size, f"{s.rel}: {consumed} of {sc.body_size}"


@needs_discs
def test_animation_body_uses_the_same_chain():
    """.DAN shares the container: same framing, tags 1-3, never tag 4."""
    for s in extract.merge_discs("*.DAN"):
        an = scene.read_dan(s.path)
        recs = scene.read_records(s.path, kind="dan")
        assert recs, s.rel
        assert recs[-1].offset + recs[-1].size == an.body_size, s.rel
        assert all(r.tag != scene.TAG_TILES for r in recs), s.rel


@needs_discs
def test_texture_records_are_fixed_size_and_count_64():
    """tag 3 x1 and tag 4 x64, each exactly 5 + 1024*N. Fixed size => uncompressed."""
    for s in extract.merge_discs("*.DSN"):
        n = scene.read_dsn(s.path).name_count
        recs = scene.read_records(s.path)
        pal = [r for r in recs if r.tag == scene.TAG_PALETTE]
        tiles = [r for r in recs if r.tag == scene.TAG_TILES]
        assert len(pal) == 1, s.rel
        assert len(tiles) == scene.TILES_PER_OBJECT, f"{s.rel}: {len(tiles)}"
        for r in pal + tiles:
            assert r.size == 5 + scene.TILE_BYTES * n, s.rel


@needs_discs
def test_every_object_gets_a_palette_and_64_tiles():
    for s in extract.merge_discs("*.DSN"):
        sc = scene.read_dsn(s.path)
        banks = scene.read_textures(s.path)
        assert len(banks) == sc.name_count, s.rel
        for b in banks:
            assert len(b.palette) == scene.PALETTE_ENTRIES
            assert len(b.tiles) == scene.TILES_PER_OBJECT
            assert all(len(t) == scene.TILE_BYTES for t in b.tiles)


def test_rgb565_expands_to_full_range():
    assert scene.rgb565_to_rgb(0xFFFF) == (255, 255, 255)
    assert scene.rgb565_to_rgb(0x0000) == (0, 0, 0)
    # green has the extra bit, so 565 and 555 disagree - this is the bug that
    # put cyan speckles through every texture when they were read as 555.
    assert scene.rgb565_to_rgb(0x07E0) == (0, 255, 0)


@corpus
@needs_discs
def test_lz_decodes_every_packed_record():
    """Tags 1 and 2 are LZ streams; all 610 across both formats must decode."""
    total = 0
    for pat, kind in (("*.DSN", "dsn"), ("*.DAN", "dan")):
        for s in extract.merge_discs(pat):
            for r in scene.read_records(s.path, kind=kind):
                if r.tag in (scene.TAG_GEOMETRY, scene.TAG_PACKED):
                    out = lz.decompress(r.payload)
                    assert len(out) > len(r.payload), f"{s.rel} tag {r.tag}"
                    total += 1
    assert total == 610, total


@corpus
@needs_discs
def test_tag1_carries_the_object_names():
    """Decompressed tag 1 holds the material table, so the names reappear in it.

    69 of 95 scenes carry every one of their header names. The other 26 are
    short a few because a header name table can reference objects belonging to
    another scene - ``E30_CERV`` asks for ``E14_COT2`` - so this asserts the
    measured coverage rather than perfection.
    """
    full = 0
    for s in extract.merge_discs("*.DSN"):
        sc = scene.read_dsn(s.path)
        pay = next(r.payload for r in scene.read_records(s.path) if r.tag == scene.TAG_GEOMETRY)
        blob = lz.decompress(pay)
        if all(n.encode() in blob for n in sc.names if n):
            full += 1
    assert full == 69, full


def test_lz_rejects_a_truncated_stream():
    with pytest.raises(lz.LZError):
        lz.decompress(b"\xff\xff\xff\xff")  # all-literal control, no data


def test_gltf_writer_emits_a_valid_document(tmp_path):
    from dreams import gltf

    doc = gltf.Scene(name="t", materials=[("m", None)])
    doc.primitives.append(
        gltf.Primitive("p", [(0, 0, 0), (1, 0, 0), (0, 1, 0)], [(0, 0), (1, 0), (0, 1)], 0)
    )
    out = gltf.write(tmp_path / "t.gltf", doc, scale=1.0)
    g = json.loads(out.read_text())
    buf = (tmp_path / g["buffers"][0]["uri"]).read_bytes()
    assert g["asset"]["version"] == "2.0"
    assert len(buf) == g["buffers"][0]["byteLength"]
    for v in g["bufferViews"]:
        assert v["byteOffset"] + v["byteLength"] <= len(buf)
    pos = g["accessors"][0]
    assert pos["type"] == "VEC3" and pos["count"] == 3
    assert pos["min"] == [0.0, -1.0, 0.0] and pos["max"] == [1.0, 0.0, 0.0]  # Y negated


@corpus
@needs_discs
def test_every_scene_decodes_to_a_mesh():
    from dreams.formats import mesh

    for s in extract.merge_discs("*.DSN"):
        m = mesh.read_mesh(s.path)
        assert m.vertices and m.objects, s.rel
        for o in m.objects:
            assert all(0 <= i < len(m.vertices) for f in o.faces for i in f), s.rel
            assert len(o.uvs) == 3 * len(o.faces), s.rel


@corpus
@needs_discs
def test_five_scenes_verify_against_tag2():
    """Every tag-1 face must also be a tag-2 triangle - proof, not a heuristic."""
    from dreams.formats import mesh

    good = set()
    for s in extract.merge_discs("*.DSN"):
        hit, total = mesh.verify_against_tag2(s.path)
        if total and hit == total:
            good.add(s.path.stem)
    assert good == {"E01GROTT", "E98ARAI1", "L03_REQI", "L16_BOMB", "O01EAU01"}


@corpus
@needs_discs
def test_arena_directory_holds_for_every_scene():
    """tag 1 records where each source vertex array began - 95/95."""
    from dreams.formats import lz, mesh

    for s in extract.merge_discs("*.DSN"):
        sc = scene.read_dsn(s.path)
        t1 = lz.decompress(
            next(r.payload for r in scene.read_records(s.path) if r.tag == scene.TAG_GEOMETRY)
        )
        arenas = mesh.read_arenas(t1, mesh._blocks(t1, sc.names))
        assert arenas, s.rel
        assert all(c > 0 for _, c in arenas), s.rel


@needs_discs
def test_clean_scene_floors_are_planar():
    """The decisive geometric check: a SOL object must be flat in one axis."""
    from dreams.formats import mesh

    s = next(x for x in extract.merge_discs("*.DSN") if x.path.stem == "E01GROTT")
    m = mesh.read_mesh(s.path)
    assert (len(m.vertices), m.face_count, len(m.objects)) == (193, 338, 26)
    flat = 0
    for o in m.objects:
        if "SOL" not in o.name.upper():
            continue
        pts = [m.vertices[i] for f in o.faces for i in f]
        if any(len({q[a] for q in pts}) == 1 for a in range(3)):
            flat += 1
    assert flat == 8, flat  # 8 of 9; E01_SOL5 is a sculpted floor, not a tile


def test_plane_origins_tile_the_microcell_exactly():
    """64 planes must land on 64 distinct sub-positions of an 8x8 cell.

    The worker that recovered this got `col` wrong - `(r & 8) >> 1` collides
    with `(r & 2) << 1`, giving 32 pairs and a column range of 0..5. The fix is
    `>> 2`, mirroring how `row` reverses the even bits.
    """
    seen = [scene.plane_origin(r) for r in range(scene.TILES_PER_OBJECT)]
    assert len(set(seen)) == 64
    assert {r for r, _ in seen} == set(range(8))
    assert {c for _, c in seen} == set(range(8))


def test_plane_fill_is_coarse_first_then_refines():
    assert scene.plane_fill(0) == (8, 8)
    assert scene.plane_fill(1) == (4, 8)
    assert scene.plane_fill(15) == (2, 2)
    assert all(scene.plane_fill(r) == (1, 1) for r in range(16, 64))


@needs_discs
def test_texture_surface_is_completely_filled():
    """The interleave must write all 65,536 pixels - no gaps, no overflow."""
    s = next(x for x in extract.merge_discs("*.DSN") if x.path.stem == "E01GROTT")
    for bank in scene.read_textures(s.path)[:3]:
        surf = bank.surface()
        assert len(surf) == scene.SURFACE * scene.SURFACE
        rgb = bank.surface_rgb()
        assert len(rgb) == scene.SURFACE * scene.SURFACE * 3


@needs_discs
def test_uv_records_start_five_bytes_before_the_reference():
    """UV refs are 5 mod 8, so the record begins at p-5, not p-1.

    Reading at p-1 - the rule that holds for the vertex and edge references -
    lands mid-record and textures render as diagonal streaks.
    """
    from dreams.formats import mesh

    s = next(x for x in extract.merge_discs("*.DSN") if x.path.stem == "E01GROTT")
    t1 = lz.decompress(
        next(r.payload for r in scene.read_records(s.path) if r.tag == scene.TAG_GEOMETRY)
    )
    sc = scene.read_dsn(s.path)
    refs = []
    for _name, at in mesh._blocks(t1, sc.names).items():
        n = struct.unpack_from("<I", t1, at + 16)[0]
        for f in range(n):
            row = struct.unpack_from("<17I", t1, at + 44 + 68 * f)
            refs += [row[w] for w in mesh.UV_REFS]
    assert refs and all(r % 8 == 5 for r in refs)
    assert mesh.UV_OFFSET == -5

    m = mesh.read_mesh(s.path)
    uvs = [uv for o in m.objects for uv in o.uvs]
    assert all(0.0 <= u <= 1.0 and 0.0 <= v <= 1.0 for u, v in uvs)
    # A real mapping is varied; the mid-record read collapses the spread.
    assert len({round(u, 4) for u, _ in uvs}) > 40


@needs_discs
def test_preview_renders_all_three_views(tmp_path):
    from dreams import preview
    from dreams.formats import mesh

    s = next(x for x in extract.merge_discs("*.DSN") if x.path.stem == "E01GROTT")
    out = preview.render(mesh.read_mesh(s.path), tmp_path / "p.png", size=64)
    blob = out.read_bytes()
    assert blob.startswith(png.SIGNATURE)
    width, height = struct.unpack_from(">II", blob, 16)
    assert (width, height) == (64 * len(preview.VIEWS), 64)


@corpus
@needs_discs
def test_tag2_triangle_mesh_decodes_every_scene():
    """Tag 2's own triangle array resolves arithmetically - all 95, no gate."""
    from dreams.formats import mesh

    total_v = total_f = 0
    for s in extract.merge_discs("*.DSN"):
        m = mesh.read_tri_mesh(s.path)
        assert m.vertices and m.face_count
        for o in m.objects:
            assert all(0 <= i < len(m.vertices) for f in o.faces for i in f), s.rel
        total_v += len(m.vertices)
        total_f += m.face_count
    assert (total_v, total_f) == (86450, 152536)


@needs_discs
def test_tag2_agrees_with_tag1_where_both_work():
    """On a verified scene the two paths must describe the same room."""
    from dreams.formats import mesh

    s = next(x for x in extract.merge_discs("*.DSN") if x.path.stem == "E01GROTT")
    a, b = mesh.read_mesh(s.path), mesh.read_tri_mesh(s.path)
    assert len(a.vertices) == len(b.vertices) == 193
    assert a.vertices == b.vertices  # same pool, same order
    assert b.face_count >= a.face_count  # tag 2 carries a few extra faces
