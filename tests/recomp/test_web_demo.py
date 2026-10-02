"""The browser demo pack (recomp/web/demo): its manifest, and that the cut boots.

Three parts:

* DIALOG.DRD trimming and chunking on synthetic data: no game files needed.
* The manifest written by `recomp/web/demo/make_demo.py` follows
  recomp/web/CONTRACT.md: chunks of at most 20 MiB, sizes and sha256 that
  match the chunk files and the loose `demo-root`, forward-slash paths, the
  program image and the bank present, and a size budget. Skipped without
  retail data (`DREAMS_DISC1`); the pack is built if it is not there yet.
* The native recomp boots the loose root as the browser build will see it (no
  intro, menu or talking-head movie, no CD music, trimmed dialogue bank): a new
  game reaches Project0, the dialogue the level starts with plays, nothing
  crashes, and every file the game asked for and the pack lacks is one the
  retail game is also fine without. Then the level exit leading to the next
  project of the pack loads it. Skipped without a development build with
  WD_DEVTOOLS or the data. `DREAMS_DEMO_SWEEP=1` boots every project of the
  pack and walks every link (about ten minutes).

Run the whole thing: uv run pytest tests/recomp/test_web_demo.py
"""

import hashlib
import json
import os
import struct
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DEMO = ROOT / "recomp" / "web" / "demo"
sys.path.insert(0, str(DEMO))

import demo_common as dc  # noqa: E402
import make_demo  # noqa: E402

MAX_CHUNK = 20 * 1024 * 1024
BUDGET = 40 * 1024 * 1024  # the pack is held in the browser's memory


# ---- synthetic ----


def fake_drd(count=6):
    """A DIALOG.DRD with `count` entries whose first byte after the header is the index."""
    entries = []
    for i in range(count):
        body = (
            bytes([2])
            + struct.pack("<I", 5 + 9)
            + bytes([i]) * 9
            + bytes([3])
            + struct.pack("<I", 5)
        )
        entries.append(bytes([1]) + struct.pack("<II", 9 + len(body), 0) + body)
    table_at = 0x15
    body_at = table_at + 4 * count
    offsets, at = [], body_at
    for e in entries:
        offsets.append(at)
        at += len(e)
    head = b"DRDF" + struct.pack("<III", at, count, max(map(len, entries)))
    head += bytes([0]) + struct.pack("<I", 5 + 4 * count) + struct.pack(f"<{count}I", *offsets)
    return head + b"".join(entries), entries


def test_drd_trim_keeps_requested_entries_and_stubs_the_rest():
    data, entries = fake_drd()
    out = dc.drd_trim(data, {1, 4})
    assert out[:4] == b"DRDF"
    size, count, biggest = struct.unpack_from("<III", out, 4)
    assert size == len(out) and count == 6
    offsets = struct.unpack_from("<6I", out, 0x15)
    for i in (1, 4):
        n = struct.unpack_from("<I", out, offsets[i] + 1)[0]
        assert out[offsets[i] : offsets[i] + n] == entries[i]
    stub = offsets[0]
    assert {offsets[i] for i in (0, 2, 3, 5)} == {stub}
    assert out[stub] == 1 and struct.unpack_from("<I", out, stub + 5)[0] == 0  # no caption lines
    assert out[stub + 9] == 2 and out[stub + 14 : stub + 18] == b"RIFF"
    assert stub + struct.unpack_from("<I", out, stub + 1)[0] == len(out)
    assert biggest >= max(len(entries[1]), len(entries[4]))
    assert len(out) < len(data) + 700


def test_pack_chunks_and_manifest_roundtrip(tmp_path, monkeypatch):
    monkeypatch.setattr(make_demo, "CHUNK", 1000)
    root, out = tmp_path / "root", tmp_path / "pack"
    plan = make_demo.Plan()
    plan.add("DATA/3DC/BIG.DAN", "big", data=bytes(range(256)) * 10)  # 2560 bytes: 3 chunks
    plan.add("SMALL.ID", "small", data=b"toto")
    plan.add("EMPTY", "empty", data=b"")
    make_demo.write_root(plan, root)
    manifest = make_demo.write_pack(root, out, plan, {"profile": "t"})
    assert manifest == json.loads((out / "manifest.json").read_text())
    by_path = {f["path"]: f for f in manifest["files"]}
    assert [c["size"] for c in by_path["DATA/3DC/BIG.DAN"]["chunks"]] == [1000, 1000, 560]
    assert len(by_path["EMPTY"]["chunks"]) == 1 and by_path["EMPTY"]["size"] == 0
    for f in manifest["files"]:
        data = b"".join((out / c["url"]).read_bytes() for c in f["chunks"])
        assert len(data) == f["size"] and hashlib.sha256(data).hexdigest() == f["sha256"]
    assert manifest["total"] == 2560 + 4


# ---- the pack ----


@pytest.fixture(scope="module")
def pack():
    if not dc.have_data():
        pytest.skip("DREAMS_DISC1 is not set or does not hold the extracted disc 1")
    manifest = dc.DEMO_OUT / "manifest.json"
    if not manifest.is_file() or not (dc.DEMO_ROOT / "DREAMS.DAT").is_file():
        sys.argv = ["make_demo.py"]
        assert make_demo.main() == 0
    return json.loads(manifest.read_text()), dc.DEMO_OUT, dc.DEMO_ROOT


def test_manifest_follows_the_contract(pack):
    manifest, out, root = pack
    assert manifest["version"] == 1 and manifest["name"] == "demo"
    assert manifest["total"] == sum(f["size"] for f in manifest["files"])
    assert manifest["total"] <= BUDGET
    paths = [f["path"] for f in manifest["files"]]
    assert len(set(paths)) == len(paths)
    for need in ("GDIDREAM.EXE", "DREAMS.DAT", "DATA/3DC/DIALOG.DRD", "DATA/SOUND/FSB.DAT"):
        assert need in paths
    for f in manifest["files"]:
        p = f["path"]
        assert "\\" not in p and not p.startswith("/") and ".." not in p.split("/")
        assert f["chunks"] and sum(c["size"] for c in f["chunks"]) == f["size"]
        assert all(0 <= c["size"] <= MAX_CHUNK for c in f["chunks"])
        loose = root / p
        assert loose.stat().st_size == f["size"]
        h = hashlib.sha256()
        for c in f["chunks"]:
            assert not c["url"].startswith("/") and "\\" not in c["url"]
            data = (out / c["url"]).read_bytes()
            assert len(data) == c["size"]
            h.update(data)
        assert h.hexdigest() == f["sha256"]
        assert hashlib.sha256(loose.read_bytes()).hexdigest() == f["sha256"]


def test_pack_holds_what_the_projects_name(pack):
    """Every model, scene and dialogue entry of every project of the pack is in it."""
    manifest, _, root = pack
    present = {f["path"].upper() for f in manifest["files"]}
    bank = dc.load_bank()
    from bank_patch import Bank  # noqa: PLC0415

    cut = Bank((root / "DREAMS.DAT").read_bytes())
    for slot in manifest["projects"]:
        for o in cut.parsed(slot).objets:
            assert f"DATA/3DC/{o.asset}".upper() in present, (slot, o.asset)
        for _, cell in dc.project.slots(bytes(cut.records[slot]), "LINK"):
            dest = dc.project._cstr(cell[12:24])
            if dest.startswith("Project"):
                assert int(dest[7:]) in manifest["projects"], (slot, dest)
    assert len(bank.records) == len(cut.records) == 150


# ---- the cut, natively ----


def native_requirements():
    sys.path.insert(0, str(ROOT / "recomp" / "windream"))
    sys.path.insert(0, str(ROOT / "recomp"))
    try:
        import recomp_env  # noqa: PLC0415
    finally:
        sys.path.pop(0)
        sys.path.pop(0)
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    if "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text(errors="replace"):
        pytest.skip(f"{build} was built without WD_DEVTOOLS")


@pytest.fixture(scope="module")
def verify(pack):
    native_requirements()
    import verify_cut  # noqa: PLC0415

    return verify_cut


def test_cut_boots_into_the_first_level(verify):
    r = verify.run_project(0, 8, keys=False, tag="demo-test-boot")
    assert not r["crash"], r["stderr"][-1500:]
    assert r["project"]["name"] == "Project0" and r["project"]["objet0_asset"] == "H18ANGKR.DSN"
    assert r["problems"] == []
    opened = {e["path"].lower() for e in r["events"] if e["kind"] == "open" and e["ok"]}
    assert "cryo\\dreams\\data\\3dc\\dialog.drd" in opened
    assert any(p.endswith("h18angkr.dsn") for p in opened)
    # nothing came from outside the pack: every host path is under demo-root or its link tree
    for e in r["events"]:
        if e["kind"] == "open" and e["ok"] and not e.get("write"):
            host = e["host"].replace("\\", "/").lower()
            assert "/web/cut-" in host, host


def test_cut_loads_the_next_level_through_its_exit(verify, pack):
    manifest = pack[0]
    if 62 not in manifest["projects"]:
        pytest.skip("the pack has the first level only")
    r = verify.run_project(
        0, 20, keys=False, spawn_link=62, wait_for="Project62", tag="demo-test-link"
    )
    assert not r["crash"], r["stderr"][-1500:]
    assert r["reached"] and r["reached"]["objet0_asset"] == "E13_ANGK.DSN"
    assert r["problems"] == []


@pytest.mark.skipif(not os.environ.get("DREAMS_DEMO_SWEEP"), reason="DREAMS_DEMO_SWEEP=1 to run")
def test_every_project_and_exit(verify, pack):
    manifest = pack[0]
    from bank_patch import Bank  # noqa: PLC0415

    bank = Bank((pack[2] / "DREAMS.DAT").read_bytes())
    failures = []
    for slot in manifest["projects"]:
        r = verify.run_project(slot, 5, keys=False, tag=f"demo-test-p{slot}")
        if r["crash"] or r["problems"] or r["project"]["name"] != f"Project{slot}":
            failures.append((slot, r["problems"], r["crash"]))
        for _, cell in dc.project.slots(bytes(bank.records[slot]), "LINK"):
            dest = dc.project._cstr(cell[12:24])
            d = int(dest[7:]) if dest.startswith("Project") and dest[7:].isdigit() else None
            if not d:  # slot 0 of a variant is a copy of the start project
                continue
            r = verify.run_project(
                slot,
                15,
                keys=False,
                spawn_link=d,
                wait_for=f"Project{d}",
                tag=f"demo-test-l{slot}-{d}",
            )
            if r["crash"] or r["problems"] or not r["reached"]:
                failures.append((slot, d, r["problems"], r["crash"], bool(r["reached"])))
    assert not failures
