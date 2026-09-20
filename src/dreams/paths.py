"""Where the discs live.

Nothing in this repo contains game data. Point these at your local extraction.

Resolution order (first hit wins):
  1. ``DREAMS_DISC1`` / ``DREAMS_DISC2`` environment variables
  2. ``dreams.local.toml`` in the repo root (gitignored)
  3. the defaults below
"""

from __future__ import annotations

import os
import tomllib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
LOCAL_CONFIG = REPO_ROOT / "dreams.local.toml"

DEFAULTS = {
    "disc1": r"E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted",
    "disc2": r"E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-2\extracted",
    # Watcom 10.6 reference material: the stock libraries, headers and the
    # startup sources off the compiler CD. See docs/toolchain.md.
    "watcom": r"E:\dev_game\watcom",
    "out": str(REPO_ROOT / "out"),
    # Where `dreams extract` writes decoded assets. Outside the repo on purpose:
    # it is derived game content and must never reach git.
    "extract": r"E:\dreams-work\extract",
}


def _from_config() -> dict[str, str]:
    if not LOCAL_CONFIG.is_file():
        return {}
    with LOCAL_CONFIG.open("rb") as fh:
        return tomllib.load(fh).get("paths", {})


def get(key: str) -> Path:
    """Resolve a configured path by key: ``disc1``, ``disc2`` or ``out``."""
    env = os.environ.get(f"DREAMS_{key.upper()}")
    if env:
        return Path(env)
    cfg = _from_config().get(key)
    if cfg:
        return Path(cfg)
    return Path(DEFAULTS[key])


def disc(n: int) -> Path:
    return get(f"disc{n}")


def out_dir(*parts: str) -> Path:
    """Return a path under the output directory, creating it."""
    p = get("out").joinpath(*parts)
    p.mkdir(parents=True, exist_ok=True)
    return p


def describe() -> list[tuple[str, Path, bool]]:
    """(key, resolved path, exists) for every configured path."""
    return [(k, get(k), get(k).exists()) for k in ("disc1", "disc2", "watcom", "out", "extract")]
