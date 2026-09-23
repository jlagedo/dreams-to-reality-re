from __future__ import annotations

import json

import pytest

from dreams import paths
from dreams.animation_export import bind_clip, export_library, skin_payload
from dreams.formats.animation import read_dan_animations

DISC = paths.configured("disc1")
pytestmark = pytest.mark.skipif(DISC is None or not DISC.exists(), reason="Disc 1 unavailable")


def test_complete_rig_includes_helper_parent_and_binding_uses_directory_slot():
    skin = skin_payload(DISC / "DATA/3DC/CH0.DAN")
    assert len(skin["nodes"]) == 18
    assert skin["nodes"][17]["name"] == "bassin01"
    assert skin["nodes"][17]["hasGeometry"] is False
    assert any(n["parent"] == 17 for n in skin["nodes"])
    xh = skin_payload(DISC / "DATA/3DC/XH_.DAN")
    assert xh["nodes"][2]["name"] == "avbras-d"
    assert any(v[0] == 2 for primitive in xh["primitives"].values() for v in primitive)


def test_unresolved_clip_does_not_become_playable_with_equal_length_prefix():
    path = DISC / "DATA/3DC/F03.DAN"
    skin = skin_payload(path)
    clip = next(c for c in read_dan_animations(path) if c.name == "F03AN002.3DA")
    bound = bind_clip(clip, skin)
    assert bound["bindingStatus"] == "unresolved"
    assert "Binding unresolved" in bound["bindingError"]


def test_export_library_writes_matching_schema_and_complete_catalog(tmp_path):
    report = export_library(
        tmp_path, [DISC / "DATA/3DC/XH_.DAN", DISC / "DATA/3DC/CH0.DAN"], tmp_path / "models"
    )
    assert report == {"models": 2, "clips": 51, "playable": 51, "issues": []}
    rig = json.loads((tmp_path / "xh_skin.json").read_text())
    clip = json.loads((tmp_path / "xh_/xh_an000.json").read_text())
    assert rig["rigId"] == clip["rigId"]
    assert clip["tracks"][2]["nodeIndex"] == 2
    assert clip["tracks"][2]["boneName"] == rig["nodes"][2]["name"]
    catalog = json.loads((tmp_path / "catalog.json").read_text())
    assert catalog[0]["model"] == "xh_" and catalog[0]["assetStem"] == "xh"
    assert catalog[0]["defaultClipId"] == "xh_an000"
    geometry = json.loads((tmp_path / "models/xh.gltf").read_text())
    for mesh in geometry["meshes"]:
        accessor = mesh["primitives"][0]["attributes"]["POSITION"]
        assert geometry["accessors"][accessor]["count"] == len(rig["primitives"][mesh["name"]])
