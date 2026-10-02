"""The direct renderer stops loudly on a case it does not support
(recomp/windream/host/render/render_fatal.c).

A forced failure (an unknown WD_RENDERER, the earliest one there is) in a
headless run must end the process at once, without a dialog, with a non-zero
exit code, the `[direct] FATAL:` line, the guest state report under the same
message and direct-fatal.txt in the run directory.

Skipped when the development build or the disc images are missing.
"""

import importlib.util
import os
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


wdctl = load("recomp/windream/debug/wdctl.py")
run = load("recomp/windream/run.py")
recomp_env = run.recomp_env
MESSAGE = "WD_RENDERER must be software or direct"


def test_unsupported_case_ends_a_headless_run_at_once():
    exe = recomp_env.build_dir(recomp_env.out_dir("windream")) / recomp_env.exe_name(
        "windream_recomp"
    )
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    try:
        run.disc_sources(True, None, None)
    except Exception as error:  # no DREAMS_DISC1/2, or no single .cue beside them
        pytest.skip(f"no disc images: {error}")
    game = wdctl.start_game(
        tag=f"direct-fatal-{os.getpid()}",
        headless=True,
        ctl=False,
        args=["--renderer", "direct"],
        extra_env={"WD_RENDERER": "unsupported"},
    )
    try:
        deadline = time.monotonic() + 20
        while game.process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.1)
        assert game.process.poll() is not None, "the run waited (a dialog?) instead of ending"
        assert game.process.returncode != 0
        text = game.stderr_text
        assert f"[direct] FATAL: {MESSAGE}" in text
        assert f"=== recomp: direct renderer FATAL: {MESSAGE} ===" in text
        note = game.run_dir / "direct-fatal.txt"
        assert note.is_file() and MESSAGE in note.read_text()
    finally:
        game.close(remove=True)
