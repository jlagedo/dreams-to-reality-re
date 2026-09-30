"""Acceptance reports must not turn an uneventful timeout into route coverage."""

import argparse
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


acceptance = load("recomp/windream/debug/render_acceptance.py")
launcher = load("recomp/windream/run.py")
ROUTE = {"checkpoints": [r"scene=25\b"], "evidence_kind": "controlled", "remaining": ["visual"]}


def test_timeout_is_not_coverage():
    assert acceptance.analyze("", ROUTE, 0)["status"] == "not-exercised"


def test_checkpoints_do_not_grant_acceptance():
    result = acceptance.analyze("scene=25 resources=41 routine_readbacks=0", ROUTE, 0)
    assert result["status"] == "checkpoints-reached"
    assert result["acceptance"] == "pending"
    assert result["evidence_kind"] == "controlled"


@pytest.mark.parametrize(
    "failure",
    [
        "[direct] FATAL: stale surface",
        "[render] unclassified CPU read",
        "routine_readbacks=1",
        "=== recomp: CRASH",
    ],
)
def test_failure_overrides_checkpoint(failure):
    assert (
        acceptance.analyze("scene=25 routine_readbacks=0\n" + failure, ROUTE, 0)["status"]
        == "failed"
    )


def test_inventory_does_not_claim_execution(tmp_path):
    (tmp_path / "E01.DSN").write_bytes(b"fixture")
    result = acceptance.content_inventory("| `E01.DSN` | P0 Start; P1 Return |\n", [tmp_path])
    assert result["project_count"] == 2
    assert result["scene_count"] == 1
    assert result["assets"][0]["bytes"] == 7
    assert "no gameplay" in result["coverage"]


@pytest.mark.parametrize(
    ("value", "kind"),
    [
        ("10:640x480,9:1920x1080", "resize"),
        ("1:0x480", "resize"),
        ("1:click:2:3", "mouse"),
        ("1:move:32768:0", "mouse"),
    ],
)
def test_bad_schedules(value, kind):
    with pytest.raises(argparse.ArgumentTypeError, match="schedule|dimensions"):
        launcher.schedule(value, kind)


def test_valid_schedules():
    assert launcher.schedule("1:640x480,1:1920x1080", "resize")
    assert launcher.schedule("1:move:-1:0,2:left-down:320:240,3:left-up:320:240", "mouse")
