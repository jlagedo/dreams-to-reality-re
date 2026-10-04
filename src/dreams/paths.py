"""Local paths shared by the Python toolkit and its CLI.

Process environment takes precedence over .dreams.local.env. Generated output
defaults are relative to the configured work root or this repository.
"""

from __future__ import annotations

import os
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOCAL_ENV = REPO_ROOT / ".dreams.local.env"

VARIABLES = {
    "disc1": "DREAMS_DISC1",
    "disc2": "DREAMS_DISC2",
    "install_root": "DREAMS_INSTALL_ROOT",
    "watcom": "DREAMS_WATCOM",
    "watcom_compiler": "DREAMS_WATCOM_COMPILER",
    "watcom_compiler_106": "DREAMS_WATCOM_COMPILER_106",
    "work_root": "DREAMS_WORK_ROOT",
    "extract": "DREAMS_EXTRACT",
    "out": "DREAMS_OUT",
    "ghidra": "DREAMS_GHIDRA_ROOT",
    "na_game_tool": "DREAMS_NA_GAME_TOOL",
    "pcrecomp": "DREAMS_PCRECOMP",
    "wip_dir": "DREAMS_WIP_DIR",
}

# Installed Watcom compilers under DREAMS_WATCOM, used when the compiler
# variables are unset. The Windows builds are 11.0; the DOS builds link 10.6.
COMPILERS = {
    "watcom_compiler": Path("wc110", "11.0"),
    "watcom_compiler_106": Path("wc106", "watcom10.6"),
}


def _local_values() -> dict[str, str]:
    values: dict[str, str] = {}
    if not LOCAL_ENV.is_file():
        return values
    for number, raw in enumerate(LOCAL_ENV.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{LOCAL_ENV}:{number}: expected NAME=VALUE")
        name, value = line.split("=", 1)
        name, value = name.strip(), value.strip()
        if not name:
            raise ValueError(f"{LOCAL_ENV}:{number}: environment name is empty")
        if len(value) >= 2 and value[0] == value[-1] and value[0] in {"'", '"'}:
            value = value[1:-1]
        values[name] = value
    return values


def _value(name: str, local: dict[str, str]) -> str | None:
    return os.environ.get(name, "").strip() or local.get(name, "").strip() or None


def configured(key: str) -> Path | None:
    """Resolve a path, or return None when its required root is not configured."""
    name = VARIABLES[key]
    local = _local_values()
    if key == "ghidra":
        raw = (
            os.environ.get(name, "").strip()
            or os.environ.get("GHIDRA_INSTALL_DIR", "").strip()
            or local.get(name, "").strip()
            or local.get("GHIDRA_INSTALL_DIR", "").strip()
        )
    else:
        raw = _value(name, local)
    if raw:
        return Path(raw).expanduser().resolve()
    if key == "out":
        return REPO_ROOT / "out"
    if key == "pcrecomp":
        return get("out") / "recomp" / "pcrecomp"
    if key == "extract":
        work = _value("DREAMS_WORK_ROOT", local)
        return Path(work).expanduser().resolve() / "extract" if work else None
    if key in COMPILERS:
        watcom = _value("DREAMS_WATCOM", local)
        return Path(watcom).expanduser().resolve() / COMPILERS[key] if watcom else None
    return None


def get(key: str) -> Path:
    path = configured(key)
    if path is None:
        raise RuntimeError(
            f"{VARIABLES[key]} is not configured; copy .dreams.example.env "
            "to .dreams.local.env and set the local path"
        )
    return path


def disc(n: int) -> Path:
    return get(f"disc{n}")


def out_dir(*parts: str) -> Path:
    path = get("out").joinpath(*parts)
    path.mkdir(parents=True, exist_ok=True)
    return path


def describe() -> list[tuple[str, Path | None, bool]]:
    """(key, resolved path, exists) for every configured path."""
    keys = (
        "disc1",
        "disc2",
        "install_root",
        "watcom",
        "watcom_compiler",
        "watcom_compiler_106",
        "work_root",
        "extract",
        "out",
        "ghidra",
        "na_game_tool",
        "pcrecomp",
        "wip_dir",
    )
    rows = []
    for key in keys:
        path = configured(key)
        rows.append((key, path, path.exists() if path else False))
    return rows
