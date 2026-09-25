"""Tests for pack: a subset of the data root plus the app build."""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from dreams import pack
from dreams.bake import write_index


def _touch(path: Path, data: str = "x") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def _project(baked: Path, n: int, scene: str) -> None:
    needs = [f"projects/{n}.json", f"scenes/{scene}/"]
    doc = {"index": n, "id": f"Project{n}", "name": "", "scene": scene, "needs": needs}
    _touch(baked / "projects" / f"{n}.json", json.dumps(doc))
    _touch(baked / "scenes" / scene / "scene.gltf", "{}")


@pytest.fixture
def tree(tmp_path):
    baked = tmp_path / "baked"
    _project(baked, 0, "a")
    _project(baked, 1, "b")
    _touch(baked / "ui" / "menu" / "bracket_uplf.png")
    _touch(baked / "audio" / "sfx" / "sfx_001.opus")
    _touch(baked / "video" / "cutscenes" / "intro_d1.mp4")
    _touch(baked / "manifest.json", "{}")
    write_index(baked)
    web = tmp_path / "web"
    _touch(web / "dist" / "index.html", "<html></html>")
    _touch(web / "dist" / "assets" / "index-abc.js", "app")
    return baked, tmp_path / "releases", web


def test_pack_copies_what_the_projects_need(tree):
    baked, releases, web = tree
    manifest = pack.run(baked, releases, "demo", projects=[0], web_dir=web, build=False)

    data = releases / "demo" / "site" / "data"
    assert (data / "scenes" / "a" / "scene.gltf").exists()
    assert not (data / "scenes" / "b").exists()  # project 1 was not asked for
    assert (data / "ui" / "menu" / "bracket_uplf.png").exists()  # shared by every screen
    assert (data / "video" / "cutscenes" / "intro_d1.mp4").exists()  # a boot video
    assert not (data / "manifest.json").exists()
    index = json.loads((data / "index.json").read_text(encoding="utf-8"))
    assert [p["index"] for p in index["projects"]] == [0]
    assert list(index["scenes"]) == ["a"]
    site = releases / "demo" / "site"
    assert (site / "index.html").exists() and (site / "_headers").exists()
    assert manifest["projects"] == [0] and manifest["problems"] == []


def test_pack_without_projects_ships_everything(tree):
    baked, releases, web = tree
    pack.run(baked, releases, "full", web_dir=web, build=False)
    data = releases / "full" / "site" / "data"
    assert (data / "scenes" / "b" / "scene.gltf").exists()


def test_pack_reports_files_over_the_host_limit(tree, monkeypatch):
    baked, releases, web = tree
    monkeypatch.setattr(pack, "MAX_FILE_BYTES", 2)
    manifest = pack.run(baked, releases, "demo", projects=[0], web_dir=web, build=False)
    assert manifest["problems"]


def test_pack_refuses_names_that_are_not_folders(tree):
    baked, releases, web = tree
    with pytest.raises(ValueError, match="plain folder name"):
        pack.run(baked, releases, "../escape", web_dir=web, build=False)


def test_pack_requires_a_baked_project(tree):
    baked, releases, web = tree
    with pytest.raises(ValueError, match="project 7 is not baked"):
        pack.run(baked, releases, "demo", projects=[7], web_dir=web, build=False)
