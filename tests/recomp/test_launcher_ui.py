"""The launcher window (recomp/launcher), driven by its test-only event script.

DREAMS_LAUNCHER_SCRIPT=<file> makes launcher_demo run a text script through the window's own
frame loop (recomp/launcher/testscript.h, grammar in recomp/launcher/launcher.h): synthetic SDL
key, mouse, text and drop events pushed with SDL_PushEvent, the file dialog's callback called
directly, a virtual SDL gamepad, and `expect` steps on state the window publishes. Nothing is
sent to the OS, so the tests do not depend on which window has focus, and a scripted run ignores
real keyboard and mouse input.

Each test runs a script with DREAMS_LAUNCHER_HOME in a pytest temp directory, then checks the exit
code (0 Play, 1 quit, 2 error with the script's `script:<line>: ...` text on stderr), the NAME=VALUE
pairs and the saved dreams.ini. Screenshots (BMP, `shot` steps) go to out/recomp/launcher/shots/ for
a person to look at. Needs the demo built (uv run python recomp/launcher/build.py) and, for the
tests that open the discs, DREAMS_DISC1 and DREAMS_DISC2 as in test_launcher.py. The OS file dialog,
real gamepad hardware and "Open folder" cannot be tested here.
"""

import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import pytest
from test_launcher import (
    DEFAULT_RENDERER_INI,
    OTHER_RENDERER_INI,
    OTHER_RENDERER_VAR,
    cue_of,
    disc1_dir,
    disc2_dir,
    load,
    read_ini,
)

from dreams import paths

SHOTS = paths.out_dir("recomp", "launcher", "shots")
# PortSettings::kInterpolates: where the GPU renderer draws between game frames by default,
# so the Camera smoothing slider is on with smooth motion and that renderer.
INTERPOLATES = sys.platform == "win32"


@pytest.fixture(scope="session")
def demo():
    exe = load("recomp/launcher/build.py").demo_path()
    if not exe.is_file():
        pytest.skip(
            f"launcher_demo is not built ({exe}); run: uv run python recomp/launcher/build.py"
        )
    return exe


@pytest.fixture
def home(tmp_path):
    return tmp_path / "home"


@dataclass
class Result:
    rc: int
    pairs: dict
    out: str
    err: str


def q(value):
    """A script word: quoted, so spaces stay and backslashes are not escapes."""
    return '"' + str(value) + '"'


def shot(name):
    SHOTS.mkdir(parents=True, exist_ok=True)
    return f"shot {q(SHOTS / ('ui-' + name + '.bmp'))}"


def run_ui(demo, home, script, *args, env=None, timeout=120):
    """Run launcher_demo (the window) on `script`, a list of lines or one string."""
    if not isinstance(script, str):
        script = "\n".join(script)
    home.parent.mkdir(parents=True, exist_ok=True)
    path = home.parent / "script.txt"
    path.write_text("wait 5\n" + script + "\n", encoding="utf-8")
    full = {k: v for k, v in os.environ.items() if not k.startswith("DREAMS_LAUNCHER_")} | {
        "DREAMS_LAUNCHER_HOME": str(home),
        "DREAMS_LAUNCHER_SCRIPT": str(path),
    }
    full.update(env or {})
    p = subprocess.run([str(demo), *map(str, args)], capture_output=True, env=full, timeout=timeout)
    pairs = {}
    for line in p.stdout.decode("utf-8").splitlines():
        name, _, value = line.partition("=")
        pairs[name] = value
    return Result(
        p.returncode,
        pairs,
        p.stdout.decode("utf-8", "replace"),
        p.stderr.decode("utf-8", "replace").replace("\r\n", "\n"),
    )


def run_play(demo, home, *args):
    """The same demo without a window: `--play`, to see what an ini makes of a start."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("DREAMS_LAUNCHER_")} | {
        "DREAMS_LAUNCHER_HOME": str(home)
    }
    p = subprocess.run([str(demo), "--play", *map(str, args)], capture_output=True, env=env)
    pairs = dict(
        line.partition("=")[::2] for line in p.stdout.decode("utf-8").splitlines() if "=" in line
    )
    return p.returncode, pairs


def data_dir(home):
    return str(home) + os.sep + "userdata"


def write_ini(home, text):
    home.mkdir(parents=True, exist_ok=True)
    (home / "dreams.ini").write_text(text, encoding="utf-8")


def discs_ini(home, extra=""):
    write_ini(home, f"[discs]\ndisc1 = {cue_of(1)}\ndisc2 = {cue_of(2)}\n{extra}")


def clean(r):
    """The run failed neither in the window nor in the script."""
    assert "script:" not in r.err and "launcher:" not in r.err, r.err
    return r


# ---- the script mechanism itself ----


def test_a_script_error_is_reported_with_its_line_and_the_demo_exits_2(demo, home):
    r = run_ui(demo, home, ["expect play enabled"])  # nothing is chosen: Play is disabled
    assert r.rc == 2 and not r.pairs
    assert 'script:2: expect play = "enabled": got "disabled"' in r.err

    r = run_ui(demo, home, ["frobnicate 3"])
    assert r.rc == 2 and 'script:2: unknown step "frobnicate"' in r.err

    r = run_ui(demo, home, ["wait 3", "expect nosuchfact x"])
    assert r.rc == 2 and 'script:3: expect: unknown fact "nosuchfact"' in r.err

    r = run_ui(demo, home, ["click nosuchwidget"])
    assert r.rc == 2 and 'script:2: no widget "nosuchwidget" on screen' in r.err

    r = run_ui(demo, home, ['key "NO SUCH KEY"'])
    assert r.rc == 2 and "script:2: unknown key" in r.err


def test_a_script_must_end_the_window_and_may_not_outlive_it(demo, home):
    r = run_ui(demo, home, ["wait 2"])  # runs out of steps with the window open
    assert r.rc == 2 and "script:2: script ended, window still open" in r.err

    r = run_ui(demo, home, ["close", "wait 2", "expect play disabled"])  # steps left over
    assert r.rc == 2 and "script:3: the window closed before this step ran" in r.err


def test_a_syntax_error_is_refused_before_a_window_opens(demo, home):
    r = run_ui(demo, home, ['expect play "disabled'])
    assert r.rc == 2 and "script:2: unterminated quote" in r.err
    r = run_ui(demo, home, ["click"])
    assert r.rc == 2 and 'script:2: "click" takes 1..3 argument(s), got 0' in r.err
    r = run_ui(demo, home, ["close"], env={"DREAMS_LAUNCHER_SCRIPT": str(home / "missing.txt")})
    assert r.rc == 2 and "cannot read" in r.err


def test_the_window_without_a_script_is_unchanged(demo, home):
    """DREAMS_LAUNCHER_SHOT is the older test hook: the unscripted loop still runs and quits."""
    home.mkdir()
    out = home.parent / "plain.bmp"
    env = {k: v for k, v in os.environ.items() if not k.startswith("DREAMS_LAUNCHER_")} | {
        "DREAMS_LAUNCHER_HOME": str(home),
        "DREAMS_LAUNCHER_SHOT": str(out),
    }
    p = subprocess.run([str(demo)], capture_output=True, env=env, timeout=60)
    assert p.returncode == 1, p.stderr  # a window that was shown and closed: the user quit
    assert out.is_file() and out.stat().st_size > 10000
    assert not (home / "dreams.ini").exists()


# ---- discs ----


def test_first_start_pick_both_discs_and_play(demo, home):
    r = clean(
        run_ui(
            demo,
            home,
            [
                "expect play disabled",
                'expect disc1 "not set"',
                'expect disc2 "not set"',
                'expect blocker "Choose both disc images to play."',
                shot("first-start"),
                "click browse1",
                "expect picking yes",
                "expect dialog.row 1",
                f"dialog 1 {q(cue_of(1))}",
                "expect picking no",
                "expect disc1 found",
                f"expect disc1.path {q(cue_of(1))}",
                "expect play disabled",
                'expect blocker ~ "Disc 2"',
                "click browse2",
                "expect dialog.row 2",
                f"expect dialog.start {q(str(cue_of(1).parent) + os.sep)}",  # beside disc 1's image
                f"dialog 2 {q(cue_of(2))}",
                "expect disc2 found",
                "expect play enabled",
                'expect blocker ""',
                shot("both-discs"),
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs == {
        "WD_DISC1": str(cue_of(1)),
        "WD_DISC2": str(cue_of(2)),
        "WD_DATA_DIR": data_dir(home),
    }
    cfg = read_ini(home / "dreams.ini")
    assert (cfg["discs"]["disc1"], cfg["discs"]["disc2"]) == (str(cue_of(1)), str(cue_of(2)))


def test_a_cancelled_or_failed_dialog_changes_nothing(demo, home):
    r = clean(
        run_ui(
            demo,
            home,
            [
                "click browse1",
                "expect picking yes",
                'dialog 1 ""',  # the user cancelled: an empty list
                "expect picking no",
                'expect disc1 "not set"',
                "click browse2",
                "dialog-error 2",  # the dialog failed: a NULL list
                "expect picking no",
                'expect disc2 "not set"',
                "close",
            ],
        )
    )
    assert r.rc == 1
    assert not (home / "dreams.ini").exists()  # nothing was chosen, so nothing is saved


def test_the_browse_buttons_wait_for_the_dialog(demo, home):
    """While a dialog is open Browse is disabled (a second click does nothing)."""
    r = clean(
        run_ui(
            demo,
            home,
            [
                "click browse1",
                "expect picking yes",
                "click browse2",
                "expect dialog.row 1",  # the second click was ignored
                f"dialog 1 {q(cue_of(1))}",
                "click browse2",
                "expect dialog.row 2",
                "close",
            ],
        )
    )
    assert r.rc == 1


def test_a_disc_given_to_the_wrong_row_moves_to_the_right_one(demo, home):
    r = clean(
        run_ui(
            demo,
            home,
            [
                f"dialog 1 {q(cue_of(2))}",  # disc 2's image, picked for the disc 1 row
                'expect disc1 "not set"',
                'expect disc1.path ""',
                "expect disc2 found",
                f"expect disc2.path {q(cue_of(2))}",
                'expect notice ~ "moved to the disc 2 row"',
                "expect play disabled",
                'expect blocker "Disc 1: not set"',
                shot("wrong-row"),
                f"dialog 2 {q(cue_of(1))}",  # and disc 1's image for the disc 2 row
                "expect disc1 found",
                f"expect disc1.path {q(cue_of(1))}",
                "expect disc2 found",
                f"expect disc2.path {q(cue_of(2))}",
                'expect notice ~ "moved to the disc 1 row"',
                "expect play enabled",
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert (r.pairs["WD_DISC1"], r.pairs["WD_DISC2"]) == (str(cue_of(1)), str(cue_of(2)))


def test_the_same_disc_twice(demo, home):
    # Two copies in the ini: the second row says so, and Play stays disabled.
    write_ini(home, f"[discs]\ndisc1 = {cue_of(1)}\ndisc2 = {cue_of(1)}\n")
    r = clean(
        run_ui(
            demo,
            home,
            [
                "expect disc1 found",
                "expect disc2.status WrongDisc",
                'expect disc2 ~ "wrong disc"',
                'expect disc2 ~ "two copies"',
                "expect play disabled",
                'expect blocker ~ "Disc 2: wrong disc"',
                shot("two-copies"),
                "click play",  # a disabled button: nothing happens
                "wait 3",
                "expect play disabled",
                f"dialog 2 {q(cue_of(2))}",  # the right disc in row 2 fixes it
                "expect disc2 found",
                "expect play enabled",
                "close",
            ],
        )
    )
    assert r.rc == 1, r.err  # closed by the script, not by Play

    # Through the window an image that is the same disc as row 1 goes to row 1: row 2 stays empty.
    r = clean(
        run_ui(
            demo,
            home.parent / "home2",
            [
                f"dialog 1 {q(cue_of(2))}",
                f"dialog 1 {q(cue_of(2))}",
                'expect disc1 "not set"',
                "expect disc2 found",
                "expect play disabled",
                "close",
            ],
        )
    )
    assert r.rc == 1, r.err


def test_dropping_files_on_the_window(demo, home, tmp_path):
    junk = tmp_path / "notes.txt"
    junk.write_bytes(b"not a disc " * 100)
    r = clean(
        run_ui(
            demo,
            home,
            [
                f"drop {q(cue_of(1))}",  # lands on the first row without coordinates
                "expect disc1 found",
                f"expect disc1.path {q(cue_of(1))}",
                f"drop {q(cue_of(2))} @disc2.row",
                "expect disc2 found",
                "expect play enabled",
                shot("dropped"),
                # a wrong-row drop moves too: disc 1's image dropped on the disc 2 row
                f"drop {q(cue_of(2))} @disc1.row",
                'expect notice ~ "moved to the disc 2 row"',
                f"expect disc2.path {q(cue_of(2))}",
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_DISC1"] == str(cue_of(1)) and r.pairs["WD_DISC2"] == str(cue_of(2))
    cfg = read_ini(home / "dreams.ini")
    assert cfg["discs"]["disc2"] == str(cue_of(2))

    # Things that are not discs: each says what is wrong in its own row, and nothing crashes.
    home2 = home.parent / "home2"
    r = clean(
        run_ui(
            demo,
            home2,
            [
                f"drop {q(junk)}",
                "expect disc1.status NotDisc",
                'expect disc1 ~ "not a Dreams to Reality disc image"',
                f"drop {q(tmp_path / 'nothing.cue')} @disc2.row",
                'expect disc2 "file not found"',
                "expect play disabled",
                shot("dropped-junk"),
                f"drop {q(cue_of(1))}",  # a real disc replaces the junk
                "expect disc1 found",
                "expect disc2.status NotFound",
                "close",
            ],
        )
    )
    assert r.rc == 1, r.err


def test_the_dialog_answer_may_arrive_from_another_thread(demo, home):
    script = []
    # Five rounds with different delays; each round puts junk in both rows first so that a
    # callback that never arrives (or lands in the wrong row) shows as a failed expect.
    for i, (d1, d2) in enumerate([(0, 0), (1, 3), (7, 2), (4, 4), (0, 11)]):
        row1, row2 = (cue_of(1), cue_of(2)) if i % 2 == 0 else (cue_of(2), cue_of(1))
        script += [
            f"dialog 1 {q(cue_of(1).parent / 'missing.cue')}",
            f"dialog 2 {q(cue_of(2).parent / 'missing.cue')}",
            "expect play disabled",
            f"dialog-thread 1 {q(row1)} {d1}",
            f"dialog-thread 2 {q(row2)} {d2}",
            "wait 12",
            "expect disc1 found",
            "expect disc2 found",
            f"expect disc1.path {q(cue_of(1))}",
            f"expect disc2.path {q(cue_of(2))}",
            "expect play enabled",
        ]
    # A callback that arrives after the window is gone (the real dialog can outlive it).
    script += [f"dialog-thread 1 {q(cue_of(1))} 400", "close"]
    r = clean(run_ui(demo, home, script))
    assert r.rc == 1, r.err

    # The same through Play: a thread delivers both, then Play.
    home2 = home.parent / "home2"
    r = clean(
        run_ui(
            demo,
            home2,
            [
                f"dialog-thread 2 {q(cue_of(2))} 5",
                f"dialog-thread 1 {q(cue_of(1))} 2",
                "wait 12",
                "expect play enabled",
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert (r.pairs["WD_DISC1"], r.pairs["WD_DISC2"]) == (str(cue_of(1)), str(cue_of(2)))


# ---- the keyboard table ----

WASD = "W=UP,A=LEFT,S=DOWN,D=RIGHT"


def remap(*pairs):
    """Capture steps: click a row's "Press a key..." button, then press the physical key."""
    lines = []
    for game, key in pairs:
        lines += [f"click key.{game}.capture", f"expect capture {game}", f"key {key}"]
        lines += ["expect capture none"]
    return lines


def test_keyboard_capture_wasd_esc_reset_and_the_preset(demo, home):
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                "tab keyboard",
                "expect tab keyboard",
                'expect keymap ""',
                shot("keyboard-default"),
                *remap(("UP", "W")),
                'expect keymap "W=UP"',
                "expect key.UP W",
                "expect key.LEFT LEFT",
                *remap(("LEFT", "A"), ("DOWN", "S"), ("RIGHT", "D")),
                f'expect keymap "{WASD}"',
                "expect conflict no",
                shot("keyboard-wasd"),
                # Esc cancels the capture and binds nothing
                "click key.CTRL.capture",
                "expect capture CTRL",
                "key ESC",
                "expect capture none",
                f'expect keymap "{WASD}"',
                # a key the host has no name for (Print Screen) leaves the capture waiting
                "click key.CTRL.capture",
                "key PrintScreen",
                "expect capture CTRL",
                f'expect keymap "{WASD}"',
                # the key that is captured does not also navigate (Tab would move the focus)
                "key TAB",
                "expect capture none",
                "expect key.CTRL TAB",
                "expect focus key.CTRL.capture",
                # a row's own Reset button
                "click key.CTRL.reset",
                "expect key.CTRL CTRL",
                f'expect keymap "{WASD}"',
                # leaving the tab drops a capture in progress: the next key goes nowhere
                "click key.CTRL.capture",
                "tab display",
                "expect capture none",
                "key Q",
                "tab keyboard",
                "expect key.CTRL CTRL",
                f'expect keymap "{WASD}"',
                # Reset to defaults clears everything, also a capture in progress
                "click key.reset",
                'expect keymap ""',
                'expect var.WD_KEYMAP ""',
                "click key.CTRL.capture",
                "click key.reset",
                "expect capture none",
                # the preset gives the same table as the four captures
                "click key.wasd",
                f'expect keymap "{WASD}"',
                "expect key.UP W",
                "expect key.RIGHT D",
                shot("keyboard-preset"),
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_KEYMAP"] == WASD
    assert dict(read_ini(home / "dreams.ini")["keyboard"]) == {
        "W": "UP",
        "A": "LEFT",
        "S": "DOWN",
        "D": "RIGHT",
    }
    assert run_play(demo, home)[1]["WD_KEYMAP"] == WASD  # and the next start reads it back

    # Reset to defaults: no WD_KEYMAP any more, and nothing left in the ini's [keyboard]
    r = clean(
        run_ui(
            demo, home, ["tab keyboard", f'expect keymap "{WASD}"', "click key.reset", "click play"]
        )
    )
    assert r.rc == 0, r.err
    assert "WD_KEYMAP" not in r.pairs
    cfg = read_ini(home / "dreams.ini")
    assert not dict(cfg["keyboard"] if cfg.has_section("keyboard") else {})


def test_a_physical_key_used_twice_blocks_play_until_it_is_fixed(demo, home):
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                "tab keyboard",
                "expect play enabled",
                *remap(("UP", "W"), ("LEFT", "W")),
                "expect conflict yes",
                'expect conflict.rows "UP,LEFT"',
                'expect conflict.text "The key W is used for both UP and LEFT."',
                "expect play disabled",
                'expect blocker "The key W is used for both UP and LEFT."',
                shot("conflict"),
                "click play",  # disabled: nothing happens
                "wait 3",
                "expect play disabled",
                *remap(("LEFT", "A")),
                "expect conflict no",
                'expect conflict.rows ""',
                "expect play enabled",
                # a clash with a key that was never remapped: UP on the physical Down key
                *remap(("UP", "DOWN")),
                "expect conflict yes",
                'expect conflict.rows "UP,DOWN"',
                "expect play disabled",
                "click key.UP.reset",
                "expect conflict no",
                "expect play enabled",
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_KEYMAP"] == "A=LEFT"


# ---- display, gamepad ----


def set_int(widget, n):
    """A slider set to an exact value the way a person can: Ctrl+click, type, Enter."""
    return ["keydown CTRL", f"click {widget}", "keyup CTRL", f"text {n}", "key RETURN"]


def test_display_settings_reach_play_and_the_next_start(demo, home):
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                'expect var.WD_RENDERER ""',
                "click renderer",
                f"click renderer.{OTHER_RENDERER_INI}",
                f"expect var.WD_RENDERER {OTHER_RENDERER_VAR}",
                "click fullscreen",
                "expect var.WD_FULLSCREEN 1",
                *set_int("scale", 3),
                "expect var.WD_SCALE 3",
                "click filter",
                "click filter.linear",
                "expect var.WD_FILTER linear",
                "click smooth",  # the original timing: the frame cap applies again
                "expect var.WD_FIXED_STEP 0",
                "click fps 0",  # the slider's left end: uncapped
                "expect var.WD_FPS 0",
                "click mute",
                "expect var.WD_MUTE 1",
                shot("display"),
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    want = {
        "WD_DISC1": str(cue_of(1)),
        "WD_DISC2": str(cue_of(2)),
        "WD_DATA_DIR": data_dir(home),
        "WD_RENDERER": OTHER_RENDERER_VAR,
        "WD_FULLSCREEN": "1",
        "WD_SCALE": "3",
        "WD_FILTER": "linear",
        "WD_FPS": "0",
        "WD_MUTE": "1",
        "WD_FIXED_STEP": "0",
    }
    assert r.pairs == want
    assert list(r.pairs)[3:] == [  # the order the library emits them in
        "WD_RENDERER",
        "WD_FULLSCREEN",
        "WD_SCALE",
        "WD_FILTER",
        "WD_FPS",
        "WD_MUTE",
        "WD_FIXED_STEP",
    ]
    assert run_play(demo, home) == (0, want)  # the second start, without a window

    # The window reads the same ini and shows the same settings; undoing each one removes its pair.
    r = clean(
        run_ui(
            demo,
            home,
            [
                f"expect var.WD_RENDERER {OTHER_RENDERER_VAR}",
                "expect var.WD_SCALE 3",
                "expect var.WD_FPS 0",
                "click renderer",
                f"click renderer.{DEFAULT_RENDERER_INI}",
                "click fullscreen",
                "click filter",
                "click filter.pixelart",
                *set_int("scale", 2),
                *set_int("fps", 25),
                "click smooth",
                "click mute",
                shot("display-defaults"),
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert set(r.pairs) == {"WD_DISC1", "WD_DISC2", "WD_DATA_DIR"}  # the defaults add nothing
    assert dict(read_ini(home / "dreams.ini")["port"]) == {
        "renderer": DEFAULT_RENDERER_INI,
        "fullscreen": "0",
        "scale": "2",
        "filter": "pixelart",
        "fps": "25",
        "mute": "0",
        "smooth": "1",
        "smooth_camera": "60",
        "mode": "retail",  # Play, the default launch mode (spec 008 phase M)
    }


def test_smooth_motion_is_the_default_and_turns_off_to_retail_timing(demo, home):
    """Smooth motion and 60 ms of camera smoothing are the defaults and add nothing; smooth
    motion off is WD_FIXED_STEP=0 and frees the frame cap. Camera smoothing needs smooth
    motion and the GPU renderer where the host interpolates; 0 turns it off."""
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                "click renderer",
                "click renderer.gpu",
                'expect var.WD_FIXED_STEP ""',  # on: the host's default
                'expect var.WD_SMOOTH_CAMERA ""',  # 60 ms: the host's default
                "click fps",  # the fixed step replaces the cap: the slider is off
                'expect var.WD_FPS ""',
                *(
                    [*set_int("smooth_camera", 0), "expect var.WD_SMOOTH_CAMERA 0"]
                    if INTERPOLATES
                    else ["click smooth_camera", 'expect var.WD_SMOOTH_CAMERA ""']
                ),
                shot("smooth"),
                "click smooth",
                "expect var.WD_FIXED_STEP 0",
                'expect var.WD_SMOOTH_CAMERA ""',  # no lag without the frames it smooths
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_FIXED_STEP"] == "0" and "WD_SMOOTH_CAMERA" not in r.pairs
    port = read_ini(home / "dreams.ini")["port"]
    assert port["smooth"] == "0"
    assert port["smooth_camera"] == ("0" if INTERPOLATES else "60")  # kept for next time
    assert run_play(demo, home) == (0, r.pairs)  # the second start, without a window

    # Back on: the saved lag returns with the GPU renderer; the software renderer draws only
    # the game's own frames, so it takes no lag.
    r = clean(
        run_ui(
            demo,
            home,
            [
                "click smooth",
                'expect var.WD_FIXED_STEP ""',
                *(
                    [
                        "expect var.WD_SMOOTH_CAMERA 0",
                        *set_int("smooth_camera", 120),
                        "expect var.WD_SMOOTH_CAMERA 120",
                    ]
                    if INTERPOLATES
                    else ['expect var.WD_SMOOTH_CAMERA ""']
                ),
                "click renderer",
                "click renderer.software",
                'expect var.WD_SMOOTH_CAMERA ""',
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert not {"WD_FIXED_STEP", "WD_SMOOTH_CAMERA"} & set(r.pairs)
    port = read_ini(home / "dreams.ini")["port"]
    assert (port["smooth"], port["smooth_camera"]) == ("1", "120" if INTERPOLATES else "60")


def test_sliders_hold_a_typed_value_to_their_range(demo, home):
    """Ctrl+click turns a slider into a text field; what is typed there is clamped, not trusted."""
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                *set_int("scale", 9),
                "expect var.WD_SCALE 6",
                *set_int("scale", 0),
                "expect var.WD_SCALE 1",
                "click smooth",  # the frame cap applies to the original timing only
                *set_int("fps", 999),
                "expect var.WD_FPS 60",
                "tab gamepad",
                *set_int("pad.dzin", 99),  # the inner deadzone stays below the outer one
                'expect var.WD_DEADZONE "94,95"',
                *set_int("pad.dzout", 5),
                'expect var.WD_DEADZONE "94,95"',
                *set_int("pad.dzin", 20),
                *set_int("pad.dzout", 80),
                'expect var.WD_DEADZONE "20,80"',
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert (r.pairs["WD_SCALE"], r.pairs["WD_FPS"], r.pairs["WD_DEADZONE"]) == ("1", "60", "20,80")


def test_gamepad_tab_mode_direction_deadzone_and_buttons(demo, home):
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                "tab gamepad",
                "expect tab gamepad",
                shot("gamepad-game"),
                # Mode "game joystick" (the default): a pad button stands for a joystick button
                "click pad.btn.a",
                "click pad.btn.a.button3",
                'expect var.WD_PADMAP "a=button3"',
                # mode: keys. The keys layout is the default one, so no WD_PADMAP; direction: d-pad
                "click pad.mode",
                "click pad.mode.keys",
                "expect var.WD_PAD keys",
                'expect var.WD_PADMAP ""',
                'expect var.WD_PAD_DIRECTION ""',  # "both" is the default in keys mode
                "click pad.dir",
                "click pad.dir.dpad",
                "expect var.WD_PAD_DIRECTION dpad",
                *set_int("pad.dzin", 20),
                'expect var.WD_DEADZONE "20,95"',
                # a key for the B button, chosen with the keyboard in the open list (default: Down)
                "click pad.btn.b",
                "expect focus pad.btn.b.DOWN",
                "key DOWN",
                "key DOWN",
                "expect focus pad.btn.b.RIGHT",
                "key RETURN",
                'expect var.WD_PADMAP "b=RIGHT"',
                shot("gamepad-keys"),
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert {
        k: v for k, v in r.pairs.items() if k not in ("WD_DISC1", "WD_DISC2", "WD_DATA_DIR")
    } == {
        "WD_PAD": "keys",
        "WD_DEADZONE": "20,95",
        "WD_PAD_DIRECTION": "dpad",
        "WD_PADMAP": "b=RIGHT",
    }
    cfg = read_ini(home / "dreams.ini")["gamepad"]
    assert cfg["mode"] == "keys" and cfg["direction"] == "dpad" and cfg["deadzone"] == "20,95"
    assert cfg["keys_b"] == "RIGHT" and cfg["a"] == "button3"  # the game-mode choice is kept too
    assert run_play(demo, home) == (0, r.pairs)  # the next start reproduces it

    # Back to game mode and "Reset buttons"; mode off sends only WD_PAD=off, and hides the rest.
    r = clean(
        run_ui(
            demo,
            home,
            [
                "tab gamepad",
                "click pad.mode",
                "click pad.mode.game",
                'expect var.WD_PADMAP "a=button3"',
                "click pad.reset",
                'expect var.WD_PADMAP ""',
                "click pad.mode",
                "click pad.mode.off",
                "expect var.WD_PAD off",
                'expect var.WD_PADMAP ""',
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_PAD"] == "off" and "WD_PADMAP" not in r.pairs
    r = run_ui(demo, home, ["tab gamepad", "click pad.dir"])  # the direction list is not shown
    assert r.rc == 2 and 'script:3: no widget "pad.dir" on screen' in r.err


# ---- keyboard only, gamepad ----

# Starting with both discs saved in dreams.ini the window opens with Play as the default focus,
# but its highlight is hidden until the first navigation key. The shortest way to start the game
# with the keyboard alone is therefore:  TAB (shows the focus on Play)  then  RETURN.
KEYS_TO_PLAY = ["key TAB", "expect focus play", "key RETURN"]


def test_keyboard_only_shortest_way_to_play(demo, home):
    discs_ini(home)
    r = clean(run_ui(demo, home, ["expect focus play", *KEYS_TO_PLAY]))
    assert r.rc == 0, r.err
    assert r.pairs["WD_DISC1"] == str(cue_of(1))


def test_keyboard_only_every_control_in_tab_order_then_play(demo, home):
    """Tab visits every control, in the window's order; Return or Space presses the focused one."""
    discs_ini(home)
    order = [
        "play",  # shown by the first key; the default focus
        "quit",
        "mode.play",  # the mode row (Play edits is off until the developer folder exists: skipped)
        "mode.dev",
        "browse1",
        "browse2",
        "tab.display",
        "renderer",
        "fullscreen",
        "scale",
        "filter",
        "smooth",
        *(["smooth_camera"] if INTERPOLATES else []),  # (the frame cap is off: not a stop)
        "mute",
        "tab.keyboard",
        "tab.gamepad",
        "tab.develop",
        "open_folder",
        "play",  # and round again
    ]
    script = ["key TAB"]
    for want in order[1:]:
        script += ["key TAB", f"expect focus {want}"]
    # Now on Play again. Back (Shift+Tab) reaches the folder button and the Develop and Gamepad
    # tabs; walk to the Mute checkbox and press it with the keyboard, then on to Play.
    script += ["key SHIFT+TAB", "expect focus open_folder"]
    script += ["key SHIFT+TAB", "expect focus tab.develop"]
    script += ["key SHIFT+TAB", "expect focus tab.gamepad"]
    script += ["key SHIFT+TAB", "expect focus tab.keyboard"]
    script += ["key SHIFT+TAB", "expect focus mute"]
    script += [
        "key SPACE",
        "expect var.WD_MUTE 1",
        "key RETURN",
        'expect var.WD_MUTE ""',
        "key SPACE",
    ]
    script += ["key TAB", "key TAB", "key TAB", "key TAB", "expect focus open_folder", "key TAB"]
    script += ["expect focus play", shot("keyboard-only"), "key RETURN"]
    r = clean(run_ui(demo, home, script))
    assert r.rc == 0, r.err
    assert r.pairs["WD_MUTE"] == "1"


def test_keyboard_only_first_start_lands_on_browse_and_play_stays_off(demo, home):
    r = clean(
        run_ui(
            demo,
            home,
            [
                "expect play disabled",
                "expect focus browse1",  # not on the disabled Play
                "key TAB",
                "expect focus browse1",  # the first key only shows the focus
                "key TAB",
                "expect focus browse2",
                # Shift+Tab goes back over the mode row (Play edits is off: skipped), then wraps
                # to the last control: Quit (Play is off: skipped).
                "key SHIFT+TAB",
                "expect focus browse1",
                "key SHIFT+TAB",
                "expect focus mode.dev",
                "key SHIFT+TAB",
                "expect focus mode.play",
                "key SHIFT+TAB",
                "expect focus quit",
                "key SHIFT+TAB",
                "expect focus open_folder",
                "key TAB",
                "expect focus quit",
                "key RETURN",  # Quit
            ],
        )
    )
    assert r.rc == 1, r.err


def test_keyboard_only_inside_the_key_table_and_a_dialog_pick(demo, home):
    """The key table is a scrolling list: Return on it enters it, the arrows walk its buttons."""
    discs_ini(home)
    r = clean(
        run_ui(
            demo,
            home,
            [
                "tab keyboard",
                "key TAB",
                "expect focus tab.keyboard",  # (the tab was clicked: the focus is on it)
                "key TAB",
                "expect focus key.wasd",
                "key RETURN",
                f'expect keymap "{WASD}"',
                "key TAB",
                "expect focus key.reset",
                "key RETURN",
                'expect keymap ""',
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert "WD_KEYMAP" not in r.pairs


def test_gamepad_navigation_flags_and_a_virtual_gamepad(demo, home):
    """ImGui's gamepad navigation is on; an SDL virtual gamepad drives the window."""
    discs_ini(home)
    r = run_ui(
        demo,
        home,
        [
            "expect nav.keyboard yes",
            "expect nav.gamepad yes",
            "pad attach",
            "wait 10",
            "expect gamepad.seen yes",  # ImGui's SDL backend has a gamepad now
            "pad press down",  # the first press only shows the focus (on Play, the default)
            "expect focus play",
            "pad press up",
            "expect focus open_folder",
            "pad press up",
            "expect focus mute",  # (Open folder is never pressed in a test)
            "pad press a",
            "expect var.WD_MUTE 1",
            "pad press up",
            f"expect focus {'smooth_camera' if INTERPOLATES else 'smooth'}",  # the cap is off
            "pad press down",
            "pad press down",
            "pad press down",
            "expect focus quit",  # directional navigation goes by position: Quit is below
            "pad press left",
            "expect focus play",
            shot("gamepad-nav"),
            "pad press a",
        ],
    )
    if "pad:" in r.err and ("cannot attach" in r.err or "subsystem" in r.err):
        pytest.skip("SDL cannot create a virtual gamepad here: " + r.err.strip())
    clean(r)
    assert r.rc == 0, r.err
    assert r.pairs["WD_MUTE"] == "1"


# ---- quit ----


@pytest.mark.parametrize(
    "step",
    ["click quit", "close", "sdlquit"],
    ids=["quit-button", "window-close", "sdl-quit"],
)
def test_quit_exits_1_and_writes_nothing_when_nothing_changed(demo, home, step):
    r = clean(run_ui(demo, home, [step]))
    assert r.rc == 1 and not r.pairs, r.err
    assert not (home / "dreams.ini").exists()

    # With a saved ini and a port setting changed in the window: Quit does not save it (Play does).
    discs_ini(home)
    before = (home / "dreams.ini").read_bytes()
    r = clean(run_ui(demo, home, ["click mute", "expect var.WD_MUTE 1", step]))
    assert r.rc == 1 and not r.pairs, r.err
    assert (home / "dreams.ini").read_bytes() == before


def test_quit_keeps_the_discs_that_were_chosen(demo, home):
    """A disc path is saved as soon as it is chosen, so a later start finds it without Play."""
    r = clean(run_ui(demo, home, [f"dialog 1 {q(cue_of(1))}", "expect disc1 found", "click quit"]))
    assert r.rc == 1
    cfg = read_ini(home / "dreams.ini")
    assert cfg["discs"]["disc1"] == str(cue_of(1)) and cfg["discs"]["disc2"] == ""
    r = clean(run_ui(demo, home, ["expect disc1 found", 'expect disc2 "not set"', "close"]))
    assert r.rc == 1  # the next start shows what was kept


# ---- launch modes (spec 008 phase M) ----


def small_discs(tmp_path):
    """Two directory sources the launcher accepts, small enough to copy in a test."""
    d1, d2 = disc1_dir(tmp_path / "d1"), disc2_dir(tmp_path / "d2")
    (d1 / "DREAMS.DAT").write_bytes(b"disc 1 bank")
    return d1, d2


def test_the_first_develop_start_makes_the_developer_folder(demo, home, tmp_path):
    d1, d2 = small_discs(tmp_path)
    write_ini(home, f"[discs]\ndisc1 = {d1}\ndisc2 = {d2}\n")
    tree = data_dir(home) + os.sep + "developer"
    r = clean(
        run_ui(
            demo,
            home,
            [
                "expect mode play",
                "expect dev.ready no",
                'expect var.WD_MODE ""',
                "click mode.edited",  # off until the folder exists
                "expect mode play",
                "click mode.dev",
                "expect mode dev",
                "expect var.WD_MODE dev",
                f"expect var.WD_TREE {q(tree)}",
                'expect var.WD_DISC1 ""',
                "expect play enabled",
                "tab develop",
                "expect tab develop",
                shot("develop-tab"),
                "click play",  # copies both discs, then plays
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_MODE"] == "dev" and r.pairs["WD_TREE"] == tree
    assert (Path(tree) / ".developer-folder").is_file()
    assert (Path(tree) / "DREAMS.DAT").read_bytes() == b"disc 1 bank"


def test_play_edits_and_reset_edits_once_the_folder_exists(demo, home, tmp_path):
    d1, d2 = small_discs(tmp_path)
    write_ini(home, f"[discs]\ndisc1 = {d1}\ndisc2 = {d2}\n[port]\nmode = dev\n")
    assert run_play(demo, home)[0] == 0  # Develop's first start makes the folder
    tree = Path(data_dir(home)) / "developer"
    (tree / "DREAMS.DAT").write_bytes(b"edited bank")
    r = clean(
        run_ui(
            demo,
            home,
            [
                "expect dev.ready yes",
                "click mode.edited",
                "expect mode edited",
                "expect var.WD_MODE edited",
                f"expect var.WD_DISC1 {q(d1)}",  # the discs' CD audio
                "tab develop",
                "click dev.reset",
                "click dev.reset.confirm",
                'expect dev.notice ~ "disc 1\'s again"',
                'expect dev.error ""',
                "click play",
            ],
        )
    )
    assert r.rc == 0, r.err
    assert r.pairs["WD_MODE"] == "edited"
    assert (tree / "DREAMS.DAT").read_bytes() == b"disc 1 bank"
    assert read_ini(home / "dreams.ini")["port"]["mode"] == "edited"
