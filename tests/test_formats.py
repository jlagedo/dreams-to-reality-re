"""Unit tests that do not need the discs.

Tests requiring real game data are marked ``needs_discs`` and skip when the
configured paths are missing, so CI stays green without shipping assets.
"""

from __future__ import annotations

import struct

import pytest

from dreams import binio, paths, probe
from dreams.formats import audio, dialog, disc, node, project, resource, scene, video

DISC1 = paths.configured("disc1")
DISCS_PRESENT = DISC1 is not None and DISC1.exists()
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
    assert len(sc.objects) == 26
    assert sc.objects[0].name == "E01_ME1"
    assert sc.objects[0].role_id == 3


@needs_discs
def test_dan_declared_size_is_exact():
    an = scene.read_dan(paths.disc(1) / "DATA/3DC/AR0.DAN")
    assert an.size_ok
    assert an.name == "YARAIN1"
    assert any(r.endswith(".3DA") for r in an.frame_refs)


# ------------------------------------------------ scene-graph nodes (tag 1) ---


def _synth_node(base: int = 1000, count: int = 3, parent: int = 0) -> bytes:
    """A minimal node: header, then ``count`` 40-byte vertex records."""
    buf = bytearray(node.ARRAY + node.STRIDE * count)
    struct.pack_into("<I", buf, node.PARENT, parent)
    struct.pack_into("<3i", buf, node.TRANSLATION, 10, 20, 30)
    for r in range(3):  # identity in Q15
        row = [0, 0, 0]
        row[r] = 32768
        struct.pack_into("<3i", buf, node.ROTATION + 12 * r, *row)
    struct.pack_into("<II", buf, node.COUNT, count, base)
    struct.pack_into("<I", buf, node.END, base + node.STRIDE * count)
    struct.pack_into("<I", buf, node.STRIDE_AT, node.STRIDE)
    for i in range(count):
        at = node.ARRAY + node.POSITION + node.STRIDE * i
        struct.pack_into("<3i", buf, at, i, i * 2, i * 3)
    return bytes(buf)


def test_node_found_by_signature():
    nodes = node.find_nodes(_synth_node())
    assert len(nodes) == 1
    assert nodes[0].base == 1000
    assert nodes[0].count == 3


def test_node_address_is_base_minus_220():
    """Child and sibling pointers hold ``base - 220``, not ``base``."""
    nd = node.find_nodes(_synth_node(base=1000))[0]
    assert nd.address == 780


def test_node_rejects_wrong_end_field():
    """The ``+0x9c == +0x94 + 40*count`` identity is what rejects false hits."""
    buf = bytearray(_synth_node())
    struct.pack_into("<I", buf, node.END, 999999)
    assert node.find_nodes(bytes(buf)) == []


def test_world_transform_applies_parent_translation():
    """A child's ``+0x30`` is relative; the parent's must compose onto it."""
    parent = _synth_node(base=1000, count=1)
    child = _synth_node(base=2000, count=1, parent=1000 - node.NODE_BIAS)
    nodes = node.find_nodes(parent + child)
    world = node.world_transforms(nodes)
    assert world[1000][1] == [10, 20, 30]
    assert world[2000][1] == [20, 40, 60]


def test_address_delta_round_trips():
    buf = _synth_node(base=1000)
    nodes = node.find_nodes(buf)
    delta = node.address_delta(nodes)
    assert nodes[0].base + delta == nodes[0].offset + node.ARRAY


@needs_discs
def test_models_decode_with_bridging_faces():
    """Faces may span two nodes; dropping them leaves models in pieces."""
    hero = paths.disc(1) / "DATA" / "3DC" / "XH_.DAN"
    if not hero.exists():
        pytest.skip("XH_.DAN not present")
    m = node.read_model(hero)
    assert len(m.nodes) == 27
    assert m.bridge_count > 0
    assert len(m.faces) > len(m.nodes)


@needs_discs
def test_model_texture_page_is_fixed_size():
    hero = paths.disc(1) / "DATA" / "3DC" / "XH_.DAN"
    if not hero.exists():
        pytest.skip("XH_.DAN not present")
    bank = node.texture_page(hero)
    assert bank is not None
    palette, page = bank
    assert len(palette) == 256
    assert len(page) == node.TEX_PAGE


@needs_discs
def test_model_uvs_cover_the_texture_page():
    """A UV is an exact reference to two 16.16 texels - no byte fudge.

    The earlier decode subtracted 5 from the reference, which is ``.DSN``'s
    relocation delta and not a field offset. It left every corner inside the
    pool, so a bounds check passed, while the bytes read were wrong: the
    values collapsed towards zero and the model rendered as flat colour. The
    test that catches that is coverage, not validity.
    """
    hero = paths.disc(1) / "DATA" / "3DC" / "XH_.DAN"
    if not hero.exists():
        pytest.skip("XH_.DAN not present")
    uvs = [uv for f in node.read_model(hero).faces for uv in f.uvs]
    assert len(uvs) == 3 * 504
    assert all(0.0 <= c <= 1.0 for uv in uvs for c in uv)
    # Both axes must span most of the page; the broken decode reached 0.5 in
    # u and nothing in v.
    for axis in (0, 1):
        values = [uv[axis] for uv in uvs]
        assert max(values) > 0.95, axis
        assert len(set(values)) > 100, axis


@needs_discs
def test_model_preview_renders_textured(tmp_path):
    """The only check that distinguishes a wrong UV from a right one."""
    from dreams import preview

    hero = paths.disc(1) / "DATA" / "3DC" / "XH_.DAN"
    if not hero.exists():
        pytest.skip("XH_.DAN not present")
    banks = node.texture_pages(hero)
    out = preview.render_model(node.read_model(hero), banks, tmp_path / "p.png")
    assert out.exists() and out.stat().st_size > 2000


@needs_discs
def test_model_has_one_face_group_per_texture_page():
    """A face block is named for the image it samples, not for a copy.

    ``XH_IMG_A`` and ``XH_IMG_B`` are image A and image B. The two groups
    share vertex positions, so they are one mesh split by texture page - and
    the model carries two banks, which are never the same image. Treating the
    names as two copies of the model and texturing everything from bank 0 put
    the chest on the character's back.
    """
    hero = paths.disc(1) / "DATA" / "3DC" / "XH_.DAN"
    if not hero.exists():
        pytest.skip("XH_.DAN not present")
    m = node.read_model(hero)
    groups = sorted({f.group for f in m.faces})
    assert groups == ["XH_IMG_A", "XH_IMG_B"]
    banks = node.texture_pages(hero)
    assert len(banks) == len(groups)
    assert banks[0][1] != banks[1][1]
    assert node.page_for_group(groups) == {"XH_IMG_A": 0, "XH_IMG_B": 1}
    a = {c for f in m.faces if f.group == groups[0] for c in f.corners}
    b = {c for f in m.faces if f.group == groups[1] for c in f.corners}
    assert a & b, "the groups must share geometry, or they are not one mesh"


@needs_discs
def test_face_block_must_point_at_its_own_records():
    """``u32 68`` and a plausible count are not enough to be a face block.

    ``F01.DAN`` holds a run of bytes that passes both and is not one. Its
    faces resolve to real geometry but their UV references are small negative
    numbers, so they rendered as black holes. The block's pointer at ``+0x14``
    rejects it: relocated, a real block's equals ``off + 40`` exactly.
    """
    boy = next(
        (
            d / "DATA" / "3DC" / "F01.DAN"
            for d in (paths.disc(1), paths.disc(2))
            if (d / "DATA" / "3DC" / "F01.DAN").exists()
        ),
        None,
    )
    if boy is None:
        pytest.skip("F01.DAN not present")  # it is on the data disc only
    m = node.read_model(boy)
    assert len(m.faces) == 440  # 644 before the check, 204 of them impostors
    assert all(f.group for f in m.faces), "a real block always carries a name"
    uvs = [c for f in m.faces for uv in f.uvs for c in uv]
    assert all(0.0 <= c <= 1.0 for c in uvs)


@needs_discs
def test_exported_palette_row_loses_no_colour():
    """Row 0 of the light ramp is over-brightened, not the artwork.

    Darkening can only merge colours, so if row 0 were the original no darker
    row could hold more distinct ones. Row 20 holds all 256 where row 0 holds
    221, which is only possible if row 0 has clipped. Exporting row 0 turned
    the player's navy shorts teal.
    """
    import struct

    hero = paths.disc(1) / "DATA" / "3DC" / "XH_.DAN"
    if not hero.exists():
        pytest.skip("XH_.DAN not present")
    from dreams.formats import lz, scene

    bank = next(
        b
        for b in (lz.decompress(r.payload) for r in scene.read_records(hero, "dan") if r.tag == 2)
        if len(b) >= node.TEX_TOTAL
    )

    def distinct(row):
        return len(
            {
                struct.unpack_from("<H", bank, node.TEX_HEADER + 4 * (row * 256 + i) + 2)[0]
                for i in range(256)
            }
        )

    row = node.neutral_row(bank)
    assert row != 0
    assert distinct(row) == 256, "the chosen row must resolve every entry"
    assert distinct(0) < distinct(row), "row 0 must be the clipped one"
    palette, _ = node.texture_pages(hero)[0]
    assert len(set(palette)) == 256


@needs_discs
def test_level_decodes_through_the_node_with_names():
    """A level is decodable by the same node as a model, names and all.

    The first map, `H18ANGKR` - Project 0, Ile d'Angkor. Exported through tag
    2 it is one nameless merged object that draws as a blank sphere; through
    the node it is 31 named objects with UVs, including `H18RACIN` (racine,
    root) and `H18TETA1`-`5` (tete, the face towers).
    """
    from dreams.formats import mesh as meshmod

    angkor = next(
        (
            d / "DATA" / "3DC" / "H18ANGKR.DSN"
            for d in (paths.disc(1), paths.disc(2))
            if (d / "DATA" / "3DC" / "H18ANGKR.DSN").exists()
        ),
        None,
    )
    if angkor is None:
        pytest.skip("H18ANGKR.DSN not present")
    m, source = meshmod.read_scene(angkor)
    assert source == "nodes"
    names = {o.name for o in m.objects}
    assert "H18RACIN" in names and "H18TETA1" in names
    assert len(m.objects) == 31
    assert all(o.uvs for o in m.objects)
    # Geometry must still agree with tag 2, which is known correct.
    assert m.face_count == meshmod.read_tri_mesh(angkor).face_count


@needs_discs
def test_node_level_score_needs_a_rounding_tolerance():
    """Exact integer equality rejects a correct level decode.

    Composing Q15 transforms with a flooring shift lands a unit or two from
    the engine. Scored exactly, `H18ANGKR` is 51%; at +/-2 it is 100%, and
    widening further changes nothing - a fixed offset, not a misplaced object.
    """
    from dreams.formats import mesh as meshmod

    angkor = next(
        (
            d / "DATA" / "3DC" / "H18ANGKR.DSN"
            for d in (paths.disc(1), paths.disc(2))
            if (d / "DATA" / "3DC" / "H18ANGKR.DSN").exists()
        ),
        None,
    )
    if angkor is None:
        pytest.skip("H18ANGKR.DSN not present")
    assert meshmod.verify_nodes(angkor, tol=0) < 0.6
    assert meshmod.verify_nodes(angkor, tol=2) == 1.0
    assert meshmod.verify_nodes(angkor, tol=32) == 1.0


def _scene(name: str):
    for d in (paths.disc(1), paths.disc(2)):
        p = d / "DATA" / "3DC" / f"{name}.DSN"
        if p.exists():
            return p
    pytest.skip(f"{name}.DSN not present")


@needs_discs
def test_collision_mesh_omits_the_sky_but_places_the_rest():
    """Tag 2 is the collision mesh, so a level's sky is not in it.

    `E19_GARD` is one node. Its four `E19_CIE*` sky objects have no vertex in
    tag 2, which held the whole scene at 77% under the old score, yet the
    node itself is anchored and the scene exports with its names and UVs.
    """
    from dreams.formats import mesh as meshmod

    gard = _scene("E19_GARD")
    assert meshmod.verify_nodes(gard) < 0.99
    check = meshmod.check_nodes(gard)
    assert check.passes and not check.displaced
    m, source = meshmod.read_scene(gard)
    assert source == "nodes"
    assert {"E19_CIE1", "E19_SOL1"} <= {o.name for o in m.objects}


@needs_discs
def test_a_node_baked_elsewhere_in_tag_2_keeps_the_fallback():
    """A node that matches tag 2 only after a shift is rejected, not guessed.

    `E12_ANGK` node 3 is a moving block: tag 2 holds its 17 vertices 408
    units lower than the file's transform puts them.
    """
    from dreams.formats import mesh as meshmod

    angk = _scene("E12_ANGK")
    check = meshmod.check_nodes(angk)
    assert [(i, off) for i, _, _, _, off in check.displaced] == [(3, (0, -408, 0))]
    assert meshmod.read_scene(angk)[1] == "tag2"


@needs_discs
@pytest.mark.corpus
def test_every_face_vertex_pointer_lands_on_a_vertex_record():
    """The node decode is pointer-exact in every scene.

    `MDL_RelocPrimitives` adds one delta to each face's vertex pointers, so a
    pointer must land on the first byte of a 40-byte vertex record inside some
    node's array. 472,299 pointers across 95 scenes all do.
    """
    from dreams.formats import lz

    scenes = sorted({p for d in (paths.disc(1), paths.disc(2)) for p in d.rglob("*.DSN")})
    total = 0
    for p in scenes:
        tag1 = lz.decompress(next(r.payload for r in scene.read_records(p) if r.tag == 1))
        nodes = node.find_nodes(tag1)
        delta = node.address_delta(nodes)
        spans = sorted((nd.base, nd.base + node.STRIDE * nd.count) for nd in nodes)
        for off in range(0, len(tag1) - 48, 4):
            if struct.unpack_from("<I", tag1, off + node.FACE_STRIDE_AT)[0] != node.FACE_RECORD:
                continue
            n = struct.unpack_from("<I", tag1, off + node.FACE_COUNT_AT)[0]
            first = struct.unpack_from("<I", tag1, off + node.FACE_FIRST_AT)[0]
            if not 0 < n < 20_000 or first + delta != off + node.FACE_ORIGIN:
                continue
            for f in range(n):
                rec = off + node.FACE_ORIGIN + node.FACE_RECORD * f
                if rec + node.FACE_RECORD > len(tag1):
                    break
                for k in (0x08, 0x14, 0x20):
                    ref = struct.unpack_from("<I", tag1, rec + k)[0]
                    lo = next((lo for lo, hi in spans if lo <= ref < hi), None)
                    assert lo is not None and (ref - lo) % node.STRIDE == 0, (p.name, rec)
                    total += 1
    assert total > 400_000


@needs_discs
@pytest.mark.corpus
def test_scene_route_counts():
    """84 of 95 scenes export through the node, with names and UVs."""
    from dreams.formats import mesh as meshmod

    scenes = {p.stem: p for d in (paths.disc(1), paths.disc(2)) for p in d.rglob("*.DSN")}
    routes = [meshmod.read_scene(p)[1] for p in scenes.values()]
    assert len(routes) == 95
    assert routes.count("nodes") == 84


def test_scene_palette_is_rgb565_full_range():
    """Pin the field layout and the expansion for `.DSN` tag-3 palettes.

    RGB565, not RGB555: read as 555 every sampled pixel of `E01GROTT` changes
    and the mean channel error is 18.6. Red is the high field and blue the low
    one - the order the executable's own table initialisers use - so BGR is
    wrong too, and choosing it renders blue skin.

    The expansion is full range: 31 must reach 255, which `v << 3` would leave
    at 248.
    """
    from dreams.formats.scene import rgb565_to_rgb

    assert rgb565_to_rgb(0xFFFF) == (255, 255, 255)
    assert rgb565_to_rgb(0x0000) == (0, 0, 0)
    assert rgb565_to_rgb(0xF800) == (255, 0, 0)  # top 5 bits are red
    assert rgb565_to_rgb(0x07E0) == (0, 255, 0)  # middle 6 are green
    assert rgb565_to_rgb(0x001F) == (0, 0, 255)  # low 5 are blue


# ---------------------------------------------------------- DREAMS.DAT ---


def test_project_codec_expands_zero_runs():
    """``00 N`` is N zero bytes; everything else is literal.

    ``c4 09 00 02`` is the int 2500 - which, read without the codec, looked
    like a three-byte value followed by a type tag, and sent the record
    grammar down the wrong road.
    """
    assert project.decompress(bytes([0xC4, 0x09, 0x00, 0x02])) == (2500).to_bytes(4, "little")
    assert project.decompress(bytes([0x41, 0x00, 0x03, 0x42])) == b"A" + bytes(3) + b"B"
    with pytest.raises(ValueError):
        project.decompress(bytes([0x41, 0x00]))  # an escape with no count


def test_project_parse_reads_fixed_slots():
    rec = bytearray(project.RECORD_SIZE)
    rec[0:8] = b"Project7"
    at = project.LINK_AT + project.LINK_SIZE  # slot 1; slot 0 stays empty
    rec[at : at + 5] = b"LINK1"
    rec[at + 12 : at + 21] = b"Project42"
    rec[at + 0x24 : at + 0x3C] = struct.pack("<6i", -5, -6, -7, 5, 6, 7)
    stale = project.LINK_AT + project.LINK_SIZE * 2
    rec[stale : stale + 4] = b"INK2"  # a fragment, as unused slots really hold
    pj = project.parse(7, bytes(rec))
    assert pj.name == "Project7"
    assert [ln.name for ln in pj.links] == ["LINK1"]
    assert pj.links[0].project == 42
    assert pj.links[0].contains((0, 0, 0)) and not pj.links[0].contains((0, 0, 8))
    with pytest.raises(ValueError):
        project.parse(0, bytes(16))


@needs_discs
def test_project_bank_is_the_level_graph():
    """All 150 records decode to exactly 0x2200, and the links are volumes.

    The floating island over the first map is a scale model of its
    destination, and the data says so directly: `F84.DAN` stands inside
    `LINK1`'s box, and `LINK1` names Project62, *Ile du Hamam*.
    """
    dat = paths.disc(1) / "DREAMS.DAT"
    if not dat.exists():
        pytest.skip("DREAMS.DAT not present")
    pjs = project.read(dat)  # raises unless every record is exactly 0x2200
    assert len(pjs) == 150
    assert sum(len(p.links) for p in pjs) == 244
    edges = [ln for p in pjs for ln in p.links if ln.project is not None]
    assert len(edges) == 239
    assert all(ln.lo[i] <= ln.hi[i] for ln in edges for i in range(3))
    assert all(p.scene.upper().endswith(".DSN") for p in pjs)
    assert len(project.reachable(pjs)) == 145

    p0 = pjs[0]
    assert p0.scene == "H18ANGKR.DSN"
    assert [ln.project for ln in p0.links] == [134, 62]
    island = next(o for o in p0.objets if o.asset == "F84.DAN")
    assert p0.links[1].contains(island.position)
    assert not p0.links[0].contains(island.position)
    assert p0.spawn_position == (-319, -625, -3187)
    assert p0.spawn_heading == 3046
    assert p0.ambient_rgb == (152, 168, 126)
    assert p0.camera_fov == 64
    assert p0.cd_track == 2
    gnome = next(o for o in p0.objets if o.asset == "F07BLEU.DAN")
    assert gnome.entity_type == 19
    assert 0 <= gnome.heading < 4096
    assert gnome.is_active_on_start
    assert gnome.is_character
    assert gnome.behavior_type == 3
    assert len(p0.advents) > 0
    assert p0.advents[0].target_object in range(len(p0.objets))


# ---------------------------------------------------------- DIALOG.DRD ---


def test_drd_table_reconstructs_wrap_in_upper_24_bits():
    """The low byte lags the wrap and is not a bank number."""
    words = (0xFF24E000, 0x00EE2800, 0x02BC3B01)
    bank = 0
    previous = -1
    offsets = []
    for word in words:
        field = word >> 8
        if field < previous:
            bank += 1
        previous = field
        offsets.append((bank << 24) | field)

    assert offsets == [0x00FF24E0, 0x0100EE28, 0x0102BC3B]
    assert (words[1] & 0xFF) == 0 and (words[2] & 0xFF) == 1


@needs_discs
def test_dialogue_carries_script_and_audio():
    drd = next(paths.disc(1).rglob("DIALOG.DRD"), None)
    if drd is None:
        pytest.skip("DIALOG.DRD not present")
    entries = dialog.read(drd)
    assert len(entries) == 178
    assert sum(len(entry.lines) for entry in entries) == 589
    assert sum(1 for e in entries if e.wave) >= 177
    assert all(e.wave[:4] == b"RIFF" for e in entries if e.wave)
    first = entries[0]
    assert first.timings[0] == 0
    assert "world of dreams" in " ".join(first.lines)
    # every recovered line is printable ASCII
    assert all(ch.isprintable() for e in entries for line in e.lines for ch in line)


# ------------------------------------------------------------- HNM Audio ---


def test_extract_sd_audio_synthetic(tmp_path):
    # Header: 64 bytes HNS6
    hdr = b"HNS6" + b"\x00" * 60

    # 256 int16 delta table (e.g. index i maps to i * 10)
    table = struct.pack("<256h", *[i * 10 for i in range(256)])
    payload = bytes([1, 2, 3, 4])  # 2 stereo pairs
    sd_chunk = struct.pack("<I2sH", 8 + len(table) + len(payload), b"SD", 0x8400) + table + payload
    frame = struct.pack("<I", len(sd_chunk) + 4) + sd_chunk

    test_file = tmp_path / "test.hnm"
    test_file.write_bytes(hdr + frame)

    wav = video.extract_sd_audio(test_file)
    assert wav is not None
    # Check sample values (integrated DPCM accumulator)
    data_idx = wav.index(b"data") + 8
    samples = struct.unpack("<4h", wav[data_idx : data_idx + 8])
    assert samples == (10, 20, 40, 60)


@needs_discs
def test_extract_sd_audio_intro():
    intro = paths.disc(1) / "DATA/HNM/INTRO.HNM"
    if not intro.is_file():
        pytest.skip("INTRO.HNM not found on disc 1")
    wav = video.extract_sd_audio(intro)
    assert wav is not None
    assert len(wav) > 10_000_000
    assert wav[:4] == b"RIFF"


def test_extract_sd_audio_bit_exact_reference():
    ref_file = paths.configured("work_root")
    if not ref_file:
        pytest.skip("work_root not configured")
    ref_wav_path = ref_file / "codex/hnm-audio/PROJECT_hnm_dpcm.wav"
    disc2_project = paths.disc(2) / "DATA/HNM/PROJECT.HNM"
    if not ref_wav_path.is_file() or not disc2_project.is_file():
        pytest.skip("PROJECT_hnm_dpcm.wav or PROJECT.HNM not found")
    wav = video.extract_sd_audio(disc2_project)
    assert wav is not None
    assert wav[44:] == ref_wav_path.read_bytes()[44:]
