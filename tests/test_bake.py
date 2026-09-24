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
