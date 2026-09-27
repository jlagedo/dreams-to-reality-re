"""The versioned port map tracks behavior coverage and owner review separately."""

from __future__ import annotations

import csv
import importlib.util
from pathlib import Path

_source = Path(__file__).resolve().parents[1] / "tools" / "check_port_map.py"
_spec = importlib.util.spec_from_file_location("check_port_map", _source)
assert _spec is not None and _spec.loader is not None
check_port_map = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(check_port_map)


def test_port_map_is_consistent() -> None:
    assert check_port_map.validate() == 0


def test_known_missing_retail_paths_remain_partial() -> None:
    expected = {
        "VFS_Open",
        "BF_Mount",
        "DSN_LoadHeader",
        "DAN_ReadAnimChunks",
        "DRD_LoadEntry",
        "VID_Open",
        "VID_Close",
    }
    with check_port_map.PORT_MAP.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source, delimiter="\t"))
    for program in ("WINDREAM.EXE", "GDIDREAM.EXE"):
        actual = {
            row["checked_name"]
            for row in rows
            if row["program"] == program and row["coverage"] == "partial"
        }
        assert actual == expected


def test_partial_port_requires_remaining_work(tmp_path, monkeypatch) -> None:
    source = check_port_map.PORT_MAP.read_text(encoding="utf-8")
    lines = source.splitlines()
    changed = 0
    for index, line in enumerate(lines):
        fields = line.split("\t")
        if fields[2] == "VID_Open":
            assert fields[9] == "partial"
            fields[10] = "-"
            lines[index] = "\t".join(fields)
            changed += 1
    assert changed == 2
    path = tmp_path / "port-map.tsv"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    monkeypatch.setattr(check_port_map, "PORT_MAP", path)
    assert check_port_map.validate() == 1


def test_owner_review_must_match_across_windows_twins(tmp_path, monkeypatch) -> None:
    source = check_port_map.PORT_MAP.read_text(encoding="utf-8")
    lines = source.splitlines()
    for index, line in enumerate(lines):
        fields = line.split("\t")
        if fields[0] == "WINDREAM.EXE" and fields[2] == "VFS_Open":
            fields[11] = "no" if fields[11] == "yes" else "yes"
            lines[index] = "\t".join(fields)
            break
    else:
        raise AssertionError("VFS_Open row missing")
    path = tmp_path / "port-map.tsv"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    monkeypatch.setattr(check_port_map, "PORT_MAP", path)
    assert check_port_map.validate() == 1


def test_partial_port_can_be_owner_reviewed(tmp_path, monkeypatch) -> None:
    source = check_port_map.PORT_MAP.read_text(encoding="utf-8")
    lines = source.splitlines()
    changed = 0
    for index, line in enumerate(lines):
        fields = line.split("\t")
        if fields[2] == "VFS_Open":
            assert fields[9] == "partial"
            fields[11] = "yes"
            lines[index] = "\t".join(fields)
            changed += 1
    assert changed == 2
    path = tmp_path / "port-map.tsv"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    monkeypatch.setattr(check_port_map, "PORT_MAP", path)
    assert check_port_map.validate() == 0
