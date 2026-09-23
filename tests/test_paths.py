from __future__ import annotations

from pathlib import Path

import pytest

from dreams import paths


def test_local_file_and_process_environment_precedence(tmp_path, monkeypatch):
    local = tmp_path / ".dreams.local.env"
    local.write_text(
        "# local paths\nDREAMS_DISC1='C:/local/disc1'\nDREAMS_WORK_ROOT=C:/local/work\n",
        encoding="utf-8",
    )
    monkeypatch.setattr(paths, "LOCAL_ENV", local)
    monkeypatch.delenv("DREAMS_DISC1", raising=False)
    monkeypatch.delenv("DREAMS_WORK_ROOT", raising=False)
    monkeypatch.delenv("DREAMS_EXTRACT", raising=False)

    assert paths.disc(1) == Path("C:/local/disc1").resolve()
    assert paths.get("extract") == Path("C:/local/work").resolve() / "extract"

    monkeypatch.setenv("DREAMS_DISC1", str(tmp_path / "override"))
    assert paths.disc(1) == tmp_path / "override"


def test_missing_required_path_has_setup_hint(tmp_path, monkeypatch):
    monkeypatch.setattr(paths, "LOCAL_ENV", tmp_path / "absent.env")
    monkeypatch.delenv("DREAMS_DISC1", raising=False)

    assert paths.configured("disc1") is None
    with pytest.raises(RuntimeError, match="DREAMS_DISC1.*paths.example.env"):
        paths.disc(1)


def test_output_defaults_to_repository(tmp_path, monkeypatch):
    monkeypatch.setattr(paths, "LOCAL_ENV", tmp_path / "absent.env")
    monkeypatch.delenv("DREAMS_OUT", raising=False)

    assert paths.get("out") == paths.REPO_ROOT / "out"
