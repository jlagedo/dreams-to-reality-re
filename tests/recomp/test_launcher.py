"""The launcher library (recomp/launcher), through its launcher_demo executable's --play path.

Needs the demo built (uv run python recomp/launcher/build.py). The tests on the real discs
need DREAMS_DISC1 and DREAMS_DISC2 (the extracted trees, with each disc's .cue in the
parent folder, as tests/recomp/test_disc.py has it); whatever is missing is skipped.

The window itself (key capture, drag and drop, the dialog's answer, keyboard and gamepad
navigation) is tested in test_launcher_ui.py, through a test-only event script. Only the OS
file dialog, real gamepad hardware and "Open folder" need a person. DREAMS_LAUNCHER_HOME,
DREAMS_LAUNCHER_VERBOSE, DREAMS_LAUNCHER_SHOT and DREAMS_LAUNCHER_SCRIPT are the library's
test-only hooks; see launcher.h.
"""

import configparser
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

from dreams import paths

ROOT = Path(__file__).resolve().parents[2]
EXE_SHA256 = "b2f053bd26627eb618f034481fbeb49c2287bec834351787385a69d74db05001"

# The renderer the launcher does not start with (PortSettings::kGpuDefault: the GPU one is the
# default on Windows, software elsewhere), as dreams.ini and the combo spell it and as
# WD_RENDERER does; a setting at its default emits no pair. DEFAULT_RENDERER_INI is the other one.
OTHER_RENDERER_INI, OTHER_RENDERER_VAR, DEFAULT_RENDERER_INI = (
    ("software", "software", "gpu") if sys.platform == "win32" else ("gpu", "direct", "software")
)


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.fixture(scope="session")
def demo():
    exe = load("recomp/launcher/build.py").demo_path()
    if not exe.is_file():
        pytest.skip(
            f"launcher_demo is not built ({exe}); run: uv run python recomp/launcher/build.py"
        )
    return exe


def run(demo, home, *args):
    """(exit code, {NAME: value}, stderr) of `launcher_demo --play args`, with its files in home."""
    env = dict(os.environ, DREAMS_LAUNCHER_HOME=str(home), DREAMS_LAUNCHER_VERBOSE="1")
    p = subprocess.run([str(demo), "--play", *map(str, args)], capture_output=True, env=env)
    pairs = {}
    for line in p.stdout.decode("utf-8").splitlines():
        name, _, value = line.partition("=")
        pairs[name] = value
    return p.returncode, pairs, p.stderr.decode("utf-8", "replace").replace("\r\n", "\n")


def disc_root(n):
    root = paths.configured(f"disc{n}")
    if root is None or not root.is_dir():
        pytest.skip(f"DREAMS_DISC{n} is not configured")
    return root


def cue_of(n):
    cues = sorted(disc_root(n).parent.glob("*.cue"))
    if len(cues) != 1:
        pytest.skip(f"no single .cue beside DREAMS_DISC{n}")
    return cues[0]


def data_dir(home, name="userdata"):
    return str(home) + os.sep + name  # how the launcher joins: the separator the path already uses


@pytest.fixture
def home(tmp_path):
    return tmp_path / "home"


def disc1_dir(target, *, flip_byte=False):
    """A directory source of disc 1: only what the launcher reads (DATA/1CD.ID, GDIDREAM.EXE)."""
    root = disc_root(1)
    (target / "DATA").mkdir(parents=True)
    shutil.copy(root / "DATA" / "1CD.ID", target / "DATA" / "1CD.ID")
    shutil.copy(root / "GDIDREAM.EXE", target / "GDIDREAM.EXE")
    if flip_byte:
        data = bytearray((target / "GDIDREAM.EXE").read_bytes())
        data[len(data) // 2] ^= 1
        (target / "GDIDREAM.EXE").write_bytes(data)
    return target


def disc2_dir(target):
    """A directory source that is disc 2 (the launcher does not hash disc 2)."""
    (target / "DATA").mkdir(parents=True)
    (target / "DATA" / "2CD.ID").write_bytes(b"DISC2")
    return target


# ---- the real discs ----


def test_both_discs_in_order(demo, home):
    rc, pairs, err = run(demo, home, "--disc1", cue_of(1), "--disc2", cue_of(2), "--ignored", "x")
    assert rc == 0, err
    assert pairs == {
        "WD_DISC1": str(cue_of(1)),
        "WD_DISC2": str(cue_of(2)),
        "WD_DATA_DIR": data_dir(home),  # the defaults add nothing else
    }
    assert "disc 1: found\n" in err and "disc 2: found\n" in err  # both have audio tracks


def test_swapped_discs_come_out_in_order(demo, home):
    rc, pairs, err = run(demo, home, f"--disc1={cue_of(2)}", f"--disc2={cue_of(1)}")
    assert rc == 0, err
    assert (pairs["WD_DISC1"], pairs["WD_DISC2"]) == (str(cue_of(1)), str(cue_of(2)))


def test_the_same_disc_twice(demo, home):
    for a, b in [(cue_of(1), cue_of(1)), (cue_of(2), cue_of(2))]:
        rc, pairs, err = run(demo, home, "--disc1", a, "--disc2", b)
        assert rc == 2 and not pairs
        assert "wrong disc" in err and "two copies" in err


def test_missing_and_unusable_paths(demo, home, tmp_path):
    rc, pairs, err = run(demo, home, "--disc1", tmp_path / "nothing.cue", "--disc2", cue_of(2))
    assert rc == 2 and not pairs
    assert "disc 1: file not found" in err and "nothing.cue" in err

    junk = tmp_path / "junk.iso"
    junk.write_bytes(b"\0" * 100000)
    rc, _, err = run(demo, home, "--disc1", junk, "--disc2", cue_of(2))
    assert rc == 2 and "not a Dreams to Reality disc" in err

    rc, _, err = run(demo, home)  # nothing given, nothing saved
    assert rc == 2 and "disc 1: not set" in err


def test_wrong_edition(demo, home, tmp_path):
    good = disc1_dir(tmp_path / "good")
    rc, pairs, err = run(demo, home, "--disc1", good, "--disc2", cue_of(2))
    assert rc == 0, err  # the copy of GDIDREAM.EXE is the supported one: a directory source works
    assert pairs["WD_DISC1"] == str(good)
    assert "disc 1: found, no music" in err  # a directory has no sibling track files here

    bad = disc1_dir(tmp_path / "bad", flip_byte=True)
    rc, pairs, err = run(demo, home, "--disc1", bad, "--disc2", cue_of(2))
    assert rc == 2 and not pairs
    assert "wrong edition" in err and "European English" in err


def test_a_bare_iso_is_valid_without_music(demo, home):
    iso = disc_root(1).parent / "disc1.iso"
    if not iso.is_file():
        pytest.skip("no disc1.iso beside DREAMS_DISC1")
    rc, pairs, err = run(demo, home, "--disc1", iso, "--disc2", cue_of(2))
    assert rc == 0, err
    assert pairs["WD_DISC1"] == str(iso)
    assert "disc 1: found, no music" in err and "disc 2: found\n" in err


# ---- dreams.ini ----


def read_ini(path):
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.optionxform = str  # keys keep their case
    cfg.read(path, encoding="utf-8")
    return cfg


def test_ini_settings_become_the_hosts_variables_and_survive_a_save(demo, home):
    home.mkdir()
    ini = home / "dreams.ini"
    ini.write_text(
        f"""[discs]
disc1 = {cue_of(1)}
disc2 = {cue_of(2)}

[data]
dir = mydata ; relative to this file

[port]
renderer = {OTHER_RENDERER_INI}
fullscreen = 1
scale = 3
filter = linear
fps = 0
mute = 1

[gamepad]
mode = keys
direction = dpad
deadzone = 20,90
a = ALT
keys_b = SPACE
keys_rs = f5

[keyboard]
W = Up
a = left
bogus key = Up

[extra]
keep = me
""",
        encoding="utf-8",
    )
    rc, pairs, err = run(demo, home)
    assert rc == 0, err
    assert pairs == {
        "WD_DISC1": str(cue_of(1)),
        "WD_DISC2": str(cue_of(2)),
        "WD_DATA_DIR": data_dir(home, "mydata"),
        "WD_RENDERER": OTHER_RENDERER_VAR,
        "WD_FULLSCREEN": "1",
        "WD_SCALE": "3",
        "WD_FILTER": "linear",
        "WD_FPS": "0",
        "WD_MUTE": "1",
        "WD_PAD": "keys",
        "WD_DEADZONE": "20,90",
        "WD_PAD_DIRECTION": "dpad",
        "WD_KEYMAP": "W=UP,A=LEFT",
        "WD_PADMAP": "a=ALT,b=SPACE,rs=F5",
    }

    cfg = read_ini(ini)  # saved by --play: the same settings, and what it does not know is kept
    assert cfg["extra"]["keep"] == "me"
    assert cfg["discs"]["disc1"] == str(cue_of(1))
    assert cfg["data"]["dir"] == "mydata"
    assert {k: cfg["port"][k] for k in cfg["port"]} == {
        "renderer": OTHER_RENDERER_INI,
        "fullscreen": "1",
        "scale": "3",
        "filter": "linear",
        "fps": "0",
        "mute": "1",
        "smooth": "1",  # not in the ini: smooth motion is the default
        "smooth_camera": "60",
    }
    assert dict(cfg["keyboard"]) == {"W": "UP", "A": "LEFT"}
    assert cfg["gamepad"]["keys_a"] == "ALT" and "a" not in cfg["gamepad"]
    assert cfg["gamepad"]["direction"] == "dpad" and cfg["gamepad"]["deadzone"] == "20,90"
    rc, again, err = run(demo, home)  # and a second start reads back what the first wrote
    assert rc == 0 and again == pairs, err


@pytest.mark.parametrize("saved", ["gpu", "software", None])
def test_a_saved_renderer_is_kept_and_a_missing_one_takes_the_default(demo, home, saved):
    home.mkdir()
    ini = home / "dreams.ini"
    port = f"\n[port]\nrenderer = {saved}\n" if saved else ""
    ini.write_text(f"[discs]\ndisc1 = {cue_of(1)}\ndisc2 = {cue_of(2)}\n{port}", encoding="utf-8")
    rc, pairs, err = run(demo, home)
    assert rc == 0, err
    chosen = saved or DEFAULT_RENDERER_INI
    # Only the renderer that is not the host's default needs a pair.
    want = OTHER_RENDERER_VAR if chosen == OTHER_RENDERER_INI else None
    assert pairs.get("WD_RENDERER") == want
    assert read_ini(ini)["port"]["renderer"] == chosen


def test_command_line_values_are_used_but_not_saved(demo, home):
    home.mkdir()
    ini = home / "dreams.ini"
    ini.write_text(f"[discs]\ndisc1 = {cue_of(1)}\ndisc2 = {cue_of(2)}\n", encoding="utf-8")
    iso = disc_root(1).parent / "disc1.iso"
    if not iso.is_file():
        pytest.skip("no disc1.iso beside DREAMS_DISC1")
    rc, pairs, err = run(demo, home, "--disc1", iso, "--data", home / "elsewhere")
    assert rc == 0, err
    assert pairs["WD_DISC1"] == str(iso) and pairs["WD_DISC2"] == str(cue_of(2))
    assert pairs["WD_DATA_DIR"] == str(home / "elsewhere")
    cfg = read_ini(ini)
    assert cfg["discs"]["disc1"] == str(cue_of(1))
    assert cfg["data"]["dir"] == ""


def test_discs_saved_in_the_wrong_order_are_put_right(demo, home):
    home.mkdir()
    ini = home / "dreams.ini"
    ini.write_text(f"[discs]\ndisc1 = {cue_of(2)}\ndisc2 = {cue_of(1)}\n", encoding="utf-8")
    rc, pairs, err = run(demo, home)
    assert rc == 0, err
    assert pairs["WD_DISC1"] == str(cue_of(1))
    cfg = read_ini(ini)
    assert (cfg["discs"]["disc1"], cfg["discs"]["disc2"]) == (str(cue_of(1)), str(cue_of(2)))


def test_a_physical_key_used_twice_blocks_play(demo, home):
    home.mkdir()
    # W would stand for Up and for Left: the window blocks Play, --play refuses.
    (home / "dreams.ini").write_text("[keyboard]\nW = Up\nw = Left\n", encoding="utf-8")
    rc, _, err = run(demo, home, "--disc1", cue_of(1), "--disc2", cue_of(2))
    assert rc == 0, err  # one key per name in an ini section: the second W replaces the first
    (home / "dreams.ini").write_text("[keyboard]\nW = Up\nUP = Down\n", encoding="utf-8")
    # UP -> DOWN while the Up row has its own pair (W): no clash. Now Down keeps its own key.
    rc, _, err = run(demo, home, "--disc1", cue_of(1), "--disc2", cue_of(2))
    assert rc == 0, err
    (home / "dreams.ini").write_text("[keyboard]\nLEFT = Up\n", encoding="utf-8")
    # LEFT -> UP, but the Left row keeps LEFT as its own key: two game keys on one physical key.
    rc, _, err = run(demo, home, "--disc1", cue_of(1), "--disc2", cue_of(2))
    assert rc == 2 and "LEFT" in err and "both" in err


# ---- UTF-8 ----


def test_non_ascii_paths_come_back_byte_exact(demo, tmp_path):
    base = tmp_path / "ção ünï"
    d1 = disc1_dir(base / "disco 1 ã")
    d2 = disc2_dir(base / "disco 2 ç")
    home = base / "hóme"
    data = base / "dados ひ"
    rc, pairs, err = run(demo, home, "--disc1", d1, "--disc2", d2, "--data", data)
    assert rc == 0, err
    assert pairs["WD_DISC1"] == str(d1)
    assert pairs["WD_DISC2"] == str(d2)
    assert pairs["WD_DATA_DIR"] == str(data)
    assert (home / "dreams.ini").is_file()  # written under the non-ASCII home
    # the raw bytes, not just a decode that agrees with itself
    env = dict(os.environ, DREAMS_LAUNCHER_HOME=str(home))
    out = subprocess.run(
        [str(demo), "--play", "--disc1", str(d1), "--disc2", str(d2), "--data", str(data)],
        capture_output=True,
        env=env,
    ).stdout
    assert f"WD_DATA_DIR={data}\n".encode() in out.replace(b"\r\n", b"\n")
