"""A disc-2 level from the developer folder (spec 008 phase M, "Still to check").

Live, on a hard-linked copy of the developer folder under DREAMS_OUT/recomp/developer-disc2
whose DREAMS.DAT has Project116 (H15ARENE.DSN, level 3, CD track 2) in slot 0, so New
Game enters a disc-2 level. Two games, software renderer:

- Develop: every file comes from the folder; CD_PrepareLevel finds 1CD.ID and then 2CD.ID
  in the folder (the one-frame swap prompt, B1), no disc change and no `cd` event (no CD
  music in Develop);
- Play edits: the files still come from the folder, the host's disc marker changes to 2
  and CD audio plays disc 2's track 2 from the disc 2 image, checked against the image's
  samples (cd_audio_check, as test_disc_play.py does for Play).
"""

import importlib.util
import os
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative, register=False):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    if register:
        sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


nav = load("recomp/windream/debug/game_nav.py")
wdctl = nav.wdctl
editor_save = load("tests/recomp/test_editor_save.py")  # requirements(), tree_copy()
recomp_env = editor_save.recomp_env
run = editor_save.run

RATE = 44100
ONSET_TOLERANCE = RATE // 5
SCENE = "H15ARENE.DSN"
SOURCE, TRACK = 116, 2


@pytest.fixture(scope="module")
def tree():
    source = editor_save.requirements()
    bank_patch = load("recomp/windream/debug/bank_patch.py", register=True)
    target = recomp_env.out_dir("developer-disc2") / f"tree-{os.getpid()}"
    editor_save.tree_copy(source, target)
    bank = bank_patch.Bank((source / "DREAMS.DAT").read_bytes())
    assert bank.parsed(SOURCE).scene == SCENE and bank.level(SOURCE) == 3
    bank.copy(SOURCE, 0)
    (target / "DREAMS.DAT").unlink()  # a copy, not the source's file
    (target / "DREAMS.DAT").write_bytes(bank.data())
    yield target
    shutil.rmtree(target, ignore_errors=True)


def start(tree, mode):
    args = ["--mode", mode, "--tree", str(tree), "--renderer", "software", "--scale", "1"]
    return wdctl.start_game(tag=f"developer-disc2-{mode}-{os.getpid()}", args=args, wait=60.0)


def from_folder(tree, events):
    """Every file the game opened was looked for in the folder, none on an image."""
    root = tree.as_posix().lower()
    hosts = [e.get("host") or "" for e in nav.opens(events, "")]
    assert hosts and all(h.lower().startswith(root) for h in hosts if h), hosts[:5]


def markers(events):
    return [(e["path"], e["ok"]) for e in nav.opens(events, "CD.ID")]


def assert_clean(game):
    text = game.stderr_text
    assert game.process.poll() is None, text[-2000:]
    assert "=== recomp: CRASH" not in text
    assert "MessageBox" not in text and "MCI Error" not in text


def test_develop_plays_a_disc2_level_from_the_folder(tree):
    game = start(tree, "dev")
    try:
        ctl = game.ctl
        opened = nav.boot_into(ctl, SCENE)
        assert opened["open_ok"]
        ctl.wait(ms=5000)
        events = nav.events_after(ctl, 0)
        from_folder(tree, events)
        assert markers(events)[-2:] == [("DATA\\1CD.ID", True), ("DATA\\2CD.ID", True)]
        assert nav.disc_changes(events) == []
        assert not [e for e in events if e["kind"] == "cd"]
        project = ctl.current_project()
        assert (project["name"], project["level"], project["objet0_asset"]) == (
            f"Project{SOURCE}", 3, SCENE,
        )  # fmt: skip
        assert_clean(game)
    finally:
        game.close(remove=True)


def test_play_edits_plays_disc2s_track_from_the_image(tree):
    cues = run.disc_sources(True, None, None)
    audio = load("recomp/windream/debug/cd_audio_check.py", register=True)
    if not audio.disc_list_tool().is_file():
        pytest.skip("the disc listing tool is not built: uv run python recomp/disc/build.py")
    game = start(tree, "edited")
    try:
        ctl = game.ctl
        menu = nav.to_main_menu(ctl)
        wav = game.run_dir / "disc2.wav"
        started = ctl.audio_dump(wav)
        opened = nav.new_game(ctl, SCENE, menu["event"])
        assert opened["open_ok"]
        ctl.wait_until_cd_track(TRACK, timeout_ms=20000)
        ctl.wait(ms=5500)
        ctl.audio_dump_stop()
        events = nav.events_after(ctl, 0)
        from_folder(tree, events)
        assert nav.disc_changes(events) == ["active disc 1 -> 2"]
        status = ctl.status()
        assert (status["disc"], status["cd_track"], status["cd_disc"]) == (2, TRACK, 2)
        play = [e for e in events if e.get("text") == f"play track {TRACK} (disc 2)"]
        assert len(play) == 1
        expected = round((play[0]["ms"] - started["ms"]) * RATE / 1000)
        dump = audio.read_wav(wav)
        ours = audio.track_table(cues[1])[TRACK]
        pcm = audio.track_pcm(ours, 0, 6 * RATE)
        best = max(
            (audio.align(dump, pcm, expected, at=at * RATE) for at in (1, 2, 3, 4)),
            key=lambda match: match.score,
        )
        assert best.score > 0.9, best
        assert abs(best.lag - expected) < ONSET_TOLERANCE, (best, expected)
        other = audio.track_pcm(audio.track_table(cues[0])[TRACK], 0, 6 * RATE)
        assert audio.align(dump, other, expected, at=best.track_frame).score < 0.5
        assert_clean(game)
    finally:
        game.close(remove=True)
