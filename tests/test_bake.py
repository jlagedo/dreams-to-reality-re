"""Tests for the bake pipeline."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from dreams import bake


def test_bake_requires_existing_extract_root(tmp_path):
    non_existent = tmp_path / "not_there"
    with pytest.raises(RuntimeError, match="extract root does not exist"):
        bake.run(non_existent, tmp_path / "baked", ["music"])


def test_bake_manifest_generation_with_mocked_ffmpeg(tmp_path, monkeypatch):
    extract_root = tmp_path / "extract"
    baked_root = tmp_path / "baked"

    music_dir = extract_root / "audio" / "music"
    music_dir.mkdir(parents=True)
    fake_track = music_dir / "d1_track02.flac"
    fake_track.write_bytes(b"RIFFFAKEFLACDATA" * 64)

    monkeypatch.setattr(bake, "_which_ffmpeg", lambda: "mock_ffmpeg")

    def mock_bake_audio(ffmpeg, src, dest, group, audio_format):
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(b"FAKEMOCKOPUSDATA")
        return True, ""

    monkeypatch.setattr(bake, "_bake_audio", mock_bake_audio)

    manifest = bake.run(extract_root, baked_root, ["music"], audio_format="opus")

    assert manifest["totals"]["files_written"] == 1
    assert (baked_root / "audio" / "music" / "d1_track02.opus").exists()
    assert (baked_root / "manifest.json").exists()

    data = json.loads((baked_root / "manifest.json").read_text(encoding="utf-8"))
    assert len(data["items"]) == 1
    assert data["items"][0]["status"] == "ok"
    assert data["items"][0]["output"] == str(Path("audio/music/d1_track02.opus"))


def test_bake_skips_existing_files_when_force_false(tmp_path, monkeypatch):
    extract_root = tmp_path / "extract"
    baked_root = tmp_path / "baked"

    sfx_dir = extract_root / "audio" / "sfx"
    sfx_dir.mkdir(parents=True)
    fake_sfx = sfx_dir / "sfx_001.flac"
    fake_sfx.write_bytes(b"FAKESFX" * 10)

    dest_file = baked_root / "audio" / "sfx" / "sfx_001.opus"
    dest_file.parent.mkdir(parents=True, exist_ok=True)
    dest_file.write_bytes(b"ALREADY_BAKED")

    monkeypatch.setattr(bake, "_which_ffmpeg", lambda: "mock_ffmpeg")

    called = False

    def mock_bake_audio(*args, **kwargs):
        nonlocal called
        called = True
        return True, ""

    monkeypatch.setattr(bake, "_bake_audio", mock_bake_audio)

    manifest = bake.run(extract_root, baked_root, ["sfx"], audio_format="opus", force=False)
    assert not called
    assert manifest["items"][0]["status"] == "skipped"
    assert manifest["totals"]["files_written"] == 0


# ------------------------------------------------------------- data groups ---


def _record() -> bytes:
    """A synthetic decompressed DREAMS.DAT record: header, one link, three objects."""
    import struct

    from dreams.formats import project

    rec = bytearray(project.RECORD_SIZE)
    rec[0:9] = b"Project0\x00"
    struct.pack_into("<3i", rec, 0xB4, -319, -625, -3187)
    struct.pack_into("<i", rec, 0x10C, 3046)
    struct.pack_into("<i", rec, 0x138, 16)
    struct.pack_into("<i", rec, 0x1F8, 2)
    link = project.LINK_AT
    rec[link : link + 6] = b"LINK0\x00"
    rec[link + 12 : link + 23] = b"Project134\x00"
    struct.pack_into("<6i", rec, link + 0x24, -1227, -1796, 237, -1152, -1640, 339)

    def objet(slot: int, asset: bytes, pos=(0, 0, 0), flags=1) -> int:
        at = project.OBJET_AT + slot * project.OBJET_SIZE
        name = b"OBJET%d\x00" % slot
        rec[at : at + len(name)] = name
        rec[at + 12 : at + 12 + len(asset)] = asset
        struct.pack_into("<H", rec, at + 0x34, flags)
        struct.pack_into("<3i", rec, at + 0x40, *pos)
        return at

    objet(0, b"H18ANGKR.DSN")
    gnome = objet(2, b"F07BLEU.DAN", (-695, -875, -210), 0x13)
    struct.pack_into("<i", rec, gnome + 0x6C, 113)
    struct.pack_into("<i", rec, gnome + 0x78, 6250)
    objet(3, b"GHOST.DAN")
    return bytes(rec)


def _touch(path: Path, data: bytes | str = b"") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        path.write_text(data, encoding="utf-8")
    else:
        path.write_bytes(data)
    return path


def test_bake_projects_keeps_raw_units_and_lists_needs(tmp_path):
    ex, baked = tmp_path / "extract", tmp_path / "baked"
    _touch(ex / "projects" / "0.bin", _record())
    _touch(ex / "scenes" / "h18angkr.gltf", "{}")
    _touch(ex / "models" / "f07bleu.gltf", "{}")
    _touch(ex / "audio" / "music" / "d1_track02.flac")
    _touch(ex / "text" / "dreams.ini", "[PROJECT]\nProject0\nIle d'Angkor\n[NEW]\n")

    bake.run(ex, baked, ["projects"])

    pj = json.loads((baked / "projects" / "0.json").read_text(encoding="utf-8"))
    assert pj["name"] == "Ile d'Angkor" and pj["scene"] == "h18angkr"
    assert pj["spawn"] == [-319, -625, -3187] and pj["heading"] == 3046  # raw, not converted
    assert pj["x138"] == 16 and pj["music"] == "audio/music/d1_track02.opus"
    assert pj["links"] == [
        {
            "slot": 0,
            "name": "LINK0",
            "destination": "Project134",
            "to": 134,
            "min": [-1227, -1796, 237],
            "max": [-1152, -1640, 339],
        }
    ]
    gnome = next(o for o in pj["objects"] if o["slot"] == 2)
    assert gnome["model"] == "f07bleu" and gnome["flags"] == 0x13
    assert gnome["x6c"] == 113 and gnome["x78"] == 6250  # unverified fields keep offset names
    assert pj["needs"] == [
        "projects/0.json",
        "projects/0.bin",
        "scenes/h18angkr/",
        "models/f07bleu/",
        "audio/music/d1_track02.opus",
    ]
    assert pj["missing"] == ["GHOST.DAN"]
    assert (baked / "projects" / "0.bin").read_bytes() == _record()
    index = json.loads((baked / "index.json").read_text(encoding="utf-8"))
    assert index["projects"] == [
        {"index": 0, "id": "Project0", "name": "Ile d'Angkor", "scene": "h18angkr"}
    ]


def test_bake_scene_gets_a_folder_with_relative_names(tmp_path):
    ex, baked = tmp_path / "extract", tmp_path / "baked"
    doc = {
        "buffers": [{"uri": "foo.bin", "byteLength": 3}],
        "images": [{"uri": "foo_foo_sol.png"}],
        "textures": [{"source": 0}],
    }
    _touch(ex / "scenes" / "foo.gltf", json.dumps(doc))
    _touch(ex / "scenes" / "foo.bin", b"abc")
    _touch(ex / "scenes" / "foo_foo_sol.png", b"png")

    bake.run(ex, baked, ["scenes"])

    folder = baked / "scenes" / "foo"
    out = json.loads((folder / "scene.gltf").read_text(encoding="utf-8"))
    assert out["buffers"][0]["uri"] == "scene.bin"
    assert out["images"][0]["uri"] == "tex/foo_sol.png"
    assert (folder / "scene.bin").read_bytes() == b"abc"
    assert (folder / "tex" / "foo_sol.png").read_bytes() == b"png"
    index = json.loads((baked / "index.json").read_text(encoding="utf-8"))
    assert index["scenes"] == {"foo": {"textured": True}}

    manifest = bake.run(ex, baked, ["scenes"])  # nothing changed: nothing rewritten
    assert [i["status"] for i in manifest["items"]] == ["skipped"]


def test_bake_ui_names_the_menu_sprites(tmp_path):
    ex, baked = tmp_path / "extract", tmp_path / "baked"
    icons = ex / "images" / "icons" / "icone_icones_bf_d2"
    _touch(icons / "interf_UpLfNA_64x63.png", b"corner")
    _touch(icons / "titres_005_107x43.png", b"title")

    bake.run(ex, baked, ["ui"])

    assert (baked / "ui" / "menu" / "bracket_uplfna.png").read_bytes() == b"corner"
    # TITRES holds four titles in three states: 5 is the second title, second state.
    assert (baked / "ui" / "menu" / "title_load_game_active.png").read_bytes() == b"title"
    assert (baked / "ui" / "icons" / "titres" / "005_107x43.png").exists()


def test_bake_manifest_keeps_other_groups(tmp_path):
    ex, baked = tmp_path / "extract", tmp_path / "baked"
    _touch(ex / "projects" / "0.bin", _record())
    _touch(ex / "scenes" / "foo.gltf", "{}")
    bake.run(ex, baked, ["scenes"])
    bake.run(ex, baked, ["projects"])
    data = json.loads((baked / "manifest.json").read_text(encoding="utf-8"))
    assert {i["group"] for i in data["items"]} == {"scenes", "projects"}
