"""The development control channel (recomp/windream/devtools, WD_CTL) and its
client (recomp/windream/debug/wdctl.py).

One headless disc-mode game is started with the channel and shared by the
tests, which run in file order: the last of them ends it with `quit`. A second,
short run checks that a devtools build without WD_CTL behaves as before. Both
use their own run directories (a --tag with the process id), removed afterwards.

Skipped when the development build, its WD_DEVTOOLS option or the two disc
images are missing. The release check (release.py) is tested on made-up files.
"""

import importlib.util
import os
import re
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = load("recomp/windream/release.py")
wdctl = load("recomp/windream/debug/wdctl.py")
run = load("recomp/windream/run.py")
recomp_env = run.recomp_env

# A retail debug flag no retail code writes (run.py --overlays, user.c keypad
# 1): GAME_TickFrame only reads it, to draw the Frame Rate/Mem readout. The
# test writes it while the guest is paused and puts the old value back before
# resuming, so the game never sees the change.
DEBUG_FLAG = 0x49D5C0
# Named, not left to run.py's default (direct on Windows): test_screenshot
# checks the software renderer's 640x480 BMP of the game's frame. The channel
# under the direct renderer is in test_wd_mcp.py and test_direct_fatal.py.
SOFTWARE = ["--renderer", "software"]


def requirements():
    """Skip unless a devtools build and the discs are there."""
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    cache = (build / "CMakeCache.txt").read_text(errors="replace")
    if "WD_DEVTOOLS:BOOL=ON" not in cache:
        pytest.skip(f"{build} was built without WD_DEVTOOLS")
    try:
        run.disc_sources(True, None, None)
    except Exception as error:  # no DREAMS_DISC1/2, or no single .cue beside them
        pytest.skip(f"no disc images: {error}")


def listening_ports(pid):
    """The TCP ports a process listens on (Windows: netstat), None elsewhere."""
    if sys.platform != "win32":
        return None
    out = subprocess.run(["netstat", "-ano", "-p", "TCP"], capture_output=True, text=True).stdout
    rows = (line.split() for line in out.splitlines())
    return {
        int(row[1].rsplit(":", 1)[1])
        for row in rows
        if len(row) == 5 and row[3] == "LISTENING" and row[4] == str(pid)
    }


def logged_opens(stderr_text):
    """(guest path, succeeded) of each "[files] open" line of a host log."""
    lines = re.findall(r'^\[files\] open [RW] "(.*)" -> .*?( \(FAILED\))?$', stderr_text, re.M)
    return [(path, not failed) for path, failed in lines]


def press_until_opened(ctl, key, path, since):
    """Tap a key until a file whose path contains `path` is opened; the
    answer of the wait. A key the game was not yet reading is pressed again."""
    for _ in range(5):
        ctl.tap(key)
        try:
            return ctl.wait_until_opened(path, since=since, timeout_ms=3000)
        except wdctl.CtlError as error:
            assert "timeout" in str(error)
    pytest.fail(f"{key} never led to an open of {path}")


@pytest.fixture(scope="module")
def game():
    requirements()
    session = wdctl.start_game(
        tag=f"devtools-test-{os.getpid()}", discs=True, headless=True, args=SOFTWARE
    )
    try:
        yield session
    finally:
        session.close(remove=True)
    assert not session.run_dir.exists()


def test_ping_and_status(game):
    ctl = game.ctl
    assert "wd-devtools" in ctl.ping()["build"]
    status = ctl.status()
    assert status["headless"] is True
    assert status["disc_mode"] is True and status["disc"] == 1
    assert status["paused"] is False
    assert status["cd_state"] == "stopped"
    assert f"[ctl] listening on 127.0.0.1:{ctl.port}" in game.stderr_text
    assert (game.run_dir / "ctl.port").read_text().strip() == str(ctl.port)
    ports = listening_ports(game.process.pid)
    assert ports is None or ports == {ctl.port}


def test_keys_reach_the_first_level(game):
    """The path of the old schedule 2000:ESC,5000:RETURN,8000:ESC,20000:ESC,
    each key pressed when the file before it has been opened: the intro movie,
    ESC to the menu (generic.hnm), RETURN for a new game (its movie tete_e~1),
    ESC to load the first level, ESC to end the opening dialogue."""
    ctl = game.ctl
    intro = ctl.wait_until_opened("intro.hnm", since=0)
    game.intro_opens = [  # for test_without_wd_ctl_nothing_changes
        (event["path"], event["ok"])
        for event in ctl.log(since=0, max=intro["event"])["events"]
        if event["kind"] == "open"
    ]
    menu = press_until_opened(ctl, "ESC", "generic.hnm", intro["event"])
    movie = press_until_opened(ctl, "RETURN", "tete_e", menu["event"])
    level = press_until_opened(ctl, "ESC", "H18ANGKR.DSN", movie["event"])
    assert level["open_ok"] is True and level["path"].upper().endswith("H18ANGKR.DSN")
    project = ctl.current_project()
    assert project["name"] == "Project0"
    assert project["level"] == 1
    assert (project["objet0"], project["objet0_asset"]) == ("OBJET0", "H18ANGKR.DSN")
    ctl.wait_until_cd_track(9, timeout_ms=20000)
    ctl.tap("ESC")
    ctl.wait(frames=25)
    assert ctl.current_project()["name"] == "Project0"
    assert ctl.status()["disc"] == 1


def test_screenshot(game, tmp_path):
    answer = game.ctl.screenshot(tmp_path / "frame.bmp")
    assert (answer["width"], answer["height"], answer["format"]) == (640, 480, "bmp")
    data = Path(answer["path"]).read_bytes()
    assert data[:2] == b"BM"
    width, height = struct.unpack_from("<ii", data, 18)  # BITMAPINFOHEADER
    assert (width, abs(height)) == (640, 480)
    assert len(data) >= 640 * 480 * 3
    assert len(set(data[54:])) > 16  # a picture, not one flat colour


def test_pause_step_resume(game):
    ctl = game.ctl
    paused_at = ctl.pause()["frame"]
    time.sleep(0.5)
    status = ctl.status()
    assert status["paused"] is True and status["frame"] == paused_at
    stepped = ctl.step(5)
    assert stepped["frame"] == paused_at + 5
    time.sleep(0.3)
    status = ctl.status()
    assert status["paused"] is True and status["frame"] == paused_at + 5
    ctl.resume()
    ctl.wait(frames=3, ms=10000)
    status = ctl.status()
    assert status["paused"] is False and status["frame"] >= paused_at + 8


def test_read_write_round_trip(game):
    ctl = game.ctl
    ctl.pause()
    try:
        before = ctl.read(DEBUG_FLAG, 4)
        assert ctl.read32(DEBUG_FLAG) == int.from_bytes(before, "little") == 0
        ctl.write32(DEBUG_FLAG, 0x12345678)
        assert ctl.read(DEBUG_FLAG, 4) == bytes.fromhex("78563412")
        assert ctl.read16(DEBUG_FLAG) == 0x5678 and ctl.read8(DEBUG_FLAG + 3) == 0x12
        ctl.write(DEBUG_FLAG, before)
        assert ctl.read(DEBUG_FLAG, 4) == before
    finally:
        ctl.resume()
    assert ctl.read_cstr(ctl.read32(wdctl.PROJECT_POINTER)) == "Project0"
    for unmapped in (0, 0x31000000, 0xFFFFFFFE):
        with pytest.raises(wdctl.CtlError, match="not committed guest memory"):
            ctl.read(unmapped, 4)
    with pytest.raises(wdctl.CtlError, match="not committed guest memory"):
        ctl.write32(0, 1)


def test_audio_dump_while_the_cd_plays(game, tmp_path):
    ctl = game.ctl
    status = ctl.status()
    assert (status["cd_track"], status["cd_disc"], status["cd_state"]) == (9, 1, "playing")
    assert "[cd] play track 9 (disc 1)" in game.stderr_text
    events = ctl.log()["events"]
    assert any(e["kind"] == "cd" and e["text"] == "play track 9 (disc 1)" for e in events)
    ctl.audio_dump(tmp_path / "mix.wav")
    assert ctl.status()["audio_dump"] is True
    ctl.wait(ms=3000)
    stopped = ctl.audio_dump_stop()
    with wave.open(str(tmp_path / "mix.wav"), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (2, 2, 44100)
        assert wav.getnframes() == stopped["frames"]
        assert 2.0 * 44100 <= wav.getnframes() <= 4.5 * 44100  # about 3 s of host time
        pcm = wav.readframes(wav.getnframes())
    samples = [int.from_bytes(pcm[i : i + 2], "little", signed=True) for i in range(0, len(pcm), 2)]
    assert max(abs(sample) for sample in samples) > 1000  # the level's music, not silence
    assert (tmp_path / "mix.wav").stat().st_size == 44 + len(pcm)


def test_wait_until_times_out(game):
    ctl = game.ctl
    started = time.monotonic()
    with pytest.raises(wdctl.CtlError, match="timeout after 1[0-9] frames"):
        ctl.wait_until_opened("NO-SUCH-FILE", timeout_frames=10)
    with pytest.raises(wdctl.CtlError, match="timeout"):
        ctl.wait_until_mem(DEBUG_FLAG, "==", 1, timeout_ms=200)
    assert time.monotonic() - started < 10
    assert ctl.wait_until_mem(DEBUG_FLAG, "==", 0)["value"] == 0  # true at once
    with pytest.raises(wdctl.CtlError, match="unknown command"):
        ctl.call("no_such_command")
    with pytest.raises(wdctl.CtlError, match="unknown key name"):
        ctl.tap("NOT-A-KEY")


def test_quit_ends_the_process_with_the_code(game):
    game.ctl.quit(7)
    assert game.process.wait(timeout=10) == 7
    assert "[ctl] quit 7" in game.stderr_text
    assert not (game.run_dir / "ctl.port").exists()


def test_without_wd_ctl_nothing_changes(game):
    """The same build without WD_CTL: no [ctl] line, no ctl.port, no listening
    socket, and the opens up to the intro movie are those of the run above."""
    plain = wdctl.start_game(
        tag=f"devtools-plain-{os.getpid()}", discs=True, headless=True, ctl=False, args=SOFTWARE
    )
    try:
        deadline = time.monotonic() + 30
        while "intro.hnm" not in plain.stderr_text:
            assert plain.process.poll() is None, plain.stderr_text
            assert time.monotonic() < deadline, plain.stderr_text
            time.sleep(0.1)
        time.sleep(0.5)
        assert listening_ports(plain.process.pid) in (None, set())
        text = plain.stderr_text
        assert "[ctl]" not in text
        assert not (plain.run_dir / "ctl.port").exists()
        opens = logged_opens(text)
        assert opens[: len(game.intro_opens)] == game.intro_opens
        assert opens[len(game.intro_opens) - 1][0].endswith("intro.hnm")
    finally:
        plain.close(remove=True)


# ---- release.py: a release executable must not hold the channel ----


def test_release_refuses_the_devtools(tmp_path):
    exe, cache = tmp_path / "game.exe", tmp_path / "CMakeCache.txt"
    exe.write_bytes(b"MZ plain host")
    cache.write_text("WD_RELEASE:BOOL=ON\nWD_DEVTOOLS:BOOL=OFF\n")
    assert release.devtools_problems(exe, cache) == []
    exe.write_bytes(b"MZ wd-devtools 1, host built")
    (marker,) = release.devtools_problems(exe, cache)
    assert "wd-devtools" in marker
    exe.write_bytes(b"MZ")
    for text in ("WD_DEVTOOLS:BOOL=ON\n", "WD_RELEASE:BOOL=ON\n"):
        cache.write_text(text)
        (option,) = release.devtools_problems(exe, cache)
        assert "WD_DEVTOOLS" in option


def test_the_development_build_holds_the_marker():
    """The check's own premise: the marker is in a devtools executable."""
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file() or "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text():
        pytest.skip("no devtools build")
    assert len(release.devtools_problems(exe, build / "CMakeCache.txt")) == 2
