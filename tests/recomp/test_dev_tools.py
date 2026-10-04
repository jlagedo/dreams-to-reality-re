"""Cryo's developer tools in Develop (spec 008 phase 7a): host/sdl/dev_tools.c and the
developer keys of host/sdl/dev_keys.c, driven through the host's own key and mouse path
(the control channel's `type` and `mouse` commands).

One headless Develop game (software renderer) on a copy of the developer folder (the
TGA captures write into it; the copy is test_editor_save.py's: large read-only
directories hard-linked, the rest copied), booted into project 0 with its opening
dialogue ended; the live tests run in file order on it. Then one Play game checks that
the same keys change none of the tools' state. Each tool's screen is saved to
DREAMS_OUT/recomp/editor-reference/tool-*.png, the software reference phase D matches.

Not here: the Save page (keypad 0), the demo recorder (r, R) and the save guard
(phase 7b); the give-all exit check of Project 99 (spec 008 phase 7 trace) is not run.
"""

import importlib.util
import os
import shutil
import struct
from collections import Counter
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


nav = load("recomp/windream/debug/game_nav.py")
wdctl = nav.wdctl
save = load("tests/recomp/test_editor_save.py")  # requirements() and tree_copy()
run = save.run
recomp_env = save.recomp_env

HANDLER, GAME_TICK_FRAME, DIALOGUE = 0x626F74, 0x416D45, 0x40E75C
EDITOR = 0x4A477C
CAMERA_MODE = 0x52C874  # byte: 0 follow, 6 overhead, 7 free
NODES = 0x661EE0  # the 3D engine's group table; group 0 node 0 is the drawn camera
PLAYER = 0x4FBA78
READOUT, OBJECT_HUD, HUD_ON = 0x49D5C0, 0x49D5D0, 0x49D5D4
CAPTURE_EVERY, CAPTURE_ONE, CAPTURE_COUNTER = 0x4A4758, 0x4A475C, 0x4A4754
VIDEO_ON = 0x49D5B4
PROJECT_PTR = 0x661E04
GIVE_FLAG = 0x49D5E0
SCENE_HANDLE = 0x4FBDBC  # actor slot 2 +0x74: the level's model, 0xFE0000 in project 0
GIVE_NAMES = (
    "feu arc epee guerison bouclier connaiss temps spirit holo resurec invivib mine shaman vitesse"
).split()
TGA_BYTES = 18 + 640 * 480 * 3


@pytest.fixture(scope="module")
def image():
    return pytest.importorskip("PIL.Image")


@pytest.fixture(scope="module")
def reference():
    folder = recomp_env.out_dir("editor-reference")
    folder.mkdir(parents=True, exist_ok=True)
    return folder


@pytest.fixture(scope="module")
def tree():
    source = save.requirements()
    target = recomp_env.out_dir("dev-tools") / f"tree-{os.getpid()}"
    save.tree_copy(source, target)
    shutil.rmtree(target / "DATA" / "TGA", ignore_errors=True)  # made again at Develop start
    yield target
    shutil.rmtree(target, ignore_errors=True)


def settle(ctl):
    """In a level, with project 0's opening dialogue (which starts some seconds after
    the level) ended line by line."""
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)
    try:
        ctl.wait_until_mem(HANDLER, "==", DIALOGUE, timeout_ms=45000)
    except wdctl.CtlError:
        pass
    for _ in range(120):
        if ctl.read32(HANDLER) == GAME_TICK_FRAME:
            break
        ctl.tap("ESC")
        ctl.wait(ms=500)
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME
    for _ in range(60):  # the level's opening camera, then follow
        if ctl.read8(CAMERA_MODE) == 0:
            break
        ctl.wait(frames=10)
    ctl.wait(frames=10)


@pytest.fixture(scope="module")
def game(tree):
    args = ["--mode", "dev", "--tree", str(tree), "--renderer", "software", "--scale", "1"]
    game = wdctl.start_game(tag=f"dev-tools-{os.getpid()}", args=args, wait=60.0)
    yield game
    game.close(remove=True)


@pytest.fixture(scope="module")
def ctl(game):
    ctl = game.ctl
    nav.boot_into(ctl, "H18ANGKR.DSN")
    settle(ctl)
    ctl.write32(EDITOR, 0)
    return ctl


def press(ctl, key, text=None, mods=None, frames=3):
    ctl.type(key, text, mods, 150)
    ctl.wait(ms=300)
    ctl.wait(frames=frames)


def shot(ctl, image, reference, name):
    path = reference / f"tool-{name}.bmp"
    ctl.screenshot(path)
    picture = image.open(path).convert("RGB")
    picture.save(path.with_suffix(".png"))
    path.unlink()
    return picture


def pixels(picture):
    flat = getattr(picture, "get_flattened_data", None)
    return list(flat() if flat else picture.getdata())


def colours(picture):
    return Counter(pixels(picture))


def last_seq(ctl):
    events = nav.events_after(ctl, 0)
    return events[-1]["seq"] if events else 0


def camera(ctl):
    """The drawn camera: group 0 node 0's matrix (+0x58) and position (+0x1c)."""
    node = ctl.read32(ctl.read32(ctl.read32(NODES)) + 0x18)
    matrix = struct.unpack("<9i", ctl.read(node + 0x58, 36))
    return matrix, struct.unpack("<3i", ctl.read(node + 0x1C, 12))


def test_overhead_camera(ctl, image, reference):
    press(ctl, "9", "9", frames=20)
    assert ctl.read8(CAMERA_MODE) == 6
    shot(ctl, image, reference, "overhead")
    press(ctl, "9", "9")
    assert ctl.read8(CAMERA_MODE) != 6


def test_free_camera_turns_with_the_mouse_and_stops_with_it(ctl, image, reference):
    press(ctl, "-", "-")
    assert ctl.read8(CAMERA_MODE) == 7
    still = camera(ctl)[0]
    ctl.wait(frames=3)
    assert camera(ctl)[0] == still
    ctl.mouse(320, 240, "move", dx=40, dy=0)
    turned = None
    for _ in range(10):
        ctl.wait(frames=1)
        if camera(ctl)[0] != still:
            turned = camera(ctl)[0]
            break
    assert turned, "the mouse did not turn the free camera"
    ctl.wait(frames=2)
    after = camera(ctl)[0]
    ctl.wait(frames=10)
    assert camera(ctl)[0] == after, "the camera kept turning after the mouse stopped"
    shot(ctl, image, reference, "free-camera")
    press(ctl, "-", "-")
    assert ctl.read8(CAMERA_MODE) != 7


def test_hud_on_and_off(ctl, image, reference):
    assert ctl.read32(HUD_ON) == 1
    press(ctl, "A", "A", "shift")
    assert ctl.read32(HUD_ON) == 0
    shot(ctl, image, reference, "hud-off")
    press(ctl, "A", "A", "shift")
    assert ctl.read32(HUD_ON) == 1


def test_the_readouts_still_work(ctl, image, reference):
    press(ctl, "8", "8")
    assert ctl.read32(READOUT) == 1
    press(ctl, "Keypad 2")
    assert ctl.read32(OBJECT_HUD) == 1
    shot(ctl, image, reference, "readouts")
    press(ctl, "8", "8")
    press(ctl, "Keypad 2")
    assert ctl.read32(READOUT) == 0 and ctl.read32(OBJECT_HUD) == 0


def tga_files(tree):
    return sorted((tree / "DATA" / "TGA").glob("*.tga"))


def test_capture_one_frame(ctl, tree):
    assert (tree / "DATA" / "TGA").is_dir(), "DATA\\TGA was not made at Develop start"
    before = tga_files(tree)
    counter = ctl.read32(CAPTURE_COUNTER)
    press(ctl, "7", "7")
    files = [f for f in tga_files(tree) if f not in before]
    assert [f.name for f in files] == [f"H18_{counter:04d}.tga"]
    data = files[0].read_bytes()
    assert len(data) == TGA_BYTES
    kind, width, height, bpp = data[2], *struct.unpack_from("<HH", data, 12), data[16]
    assert (kind, width, height, bpp) == (2, 640, 480, 24)
    assert sum(data[18::97]) / len(data[18::97]) > 20, "the capture is black"
    assert ctl.read32(CAPTURE_ONE) == 0


def test_capture_every_frame(ctl, tree):
    before = len(tga_files(tree))
    press(ctl, "6", "6", frames=0)
    assert ctl.read32(CAPTURE_EVERY) == 1
    ctl.wait(frames=5)
    press(ctl, "6", "6", frames=0)
    assert ctl.read32(CAPTURE_EVERY) == 0
    made = len(tga_files(tree)) - before
    assert made >= 5
    ctl.wait(frames=5)
    assert len(tga_files(tree)) - before == made, "captures went on after 6 again"


def test_render_classes_change_and_change_back(ctl, image, reference):
    """MDL_ReplaceMaterial on [0x4fbdbc]: in the Windows build group 0 is the camera
    (one node, no faces), so July's handle 0 would change nothing."""
    handle = ctl.read32(SCENE_HANDLE)
    assert handle >> 16, "the scene is group 0 here: July's handle 0 would do"

    def classes(index):
        group = ctl.read32(ctl.read32(NODES) + 4 * index)
        found = Counter()
        for i in range(ctl.read32(group + 0x14)):
            node = ctl.read32(group + 0x18 + 4 * i)
            for head in (0xA4, 0xA8):
                entry = ctl.read32(node + head)
                while entry:
                    found[ctl.read32(entry + 4)] += 1
                    entry = ctl.read32(entry)
        return found

    assert classes(0) == Counter()
    start = classes(handle >> 16)
    assert start[3]
    base = shot(ctl, image, reference, "class-base")
    press(ctl, "F", "f", frames=5)
    assert classes(handle >> 16)[0x1C] == start[3]
    flat = shot(ctl, image, reference, "class-f")
    press(ctl, "V", "v")
    assert classes(handle >> 16) == start
    press(ctl, "L", "l", frames=5)
    assert classes(handle >> 16)[6] == start[3]
    shot(ctl, image, reference, "class-l")
    press(ctl, "E", "e")
    assert classes(handle >> 16) == start
    changed = sum(
        1
        for a, b in zip(pixels(base), pixels(flat), strict=True)
        if sum(abs(x - y) for x, y in zip(a, b, strict=True)) > 60
    )
    assert changed > 20000, "class 0x1c left the picture as it was"


def test_give_all_items(ctl, image, reference):
    inv = ctl.read32(PLAYER + 0x30)

    def entries():
        raw = ctl.read(inv, 0x3B8)
        names = [raw[4 + 16 * i : 20 + 16 * i].split(b"\0")[0].decode("latin-1") for i in range(32)]
        return names, struct.unpack_from("<32i", raw, 0x314), struct.unpack_from("<32i", raw, 0x294)

    names, _, _ = entries()
    assert not any(names), "project 0 starts with an empty inventory"
    ctl.type("Keypad 6", None, None, 150)
    for _ in range(30):  # served at the next frame
        if ctl.read32(GIVE_FLAG):
            break
        ctl.wait(frames=1)
    frames = 0
    while ctl.read32(GIVE_FLAG) and frames < 60:
        ctl.wait(frames=1)
        frames += 1
    names, counts, levels = entries()
    assert sorted(n for n in names if n) == sorted(GIVE_NAMES)
    # One name per GAME_TickFrame (0x4170e4); a presented frame may hold several.
    assert frames < 60, f"{frames} frames"
    hot = struct.unpack("<3i", ctl.read(inv + 0x208, 12))
    assert all(h >= 0 for h in hot), f"hotkeys {hot}"
    shot(ctl, image, reference, "give-all")
    # A held entry keeps its spelling and count (the pick-up's upper case).
    k = names.index("vitesse")
    ctl.write(inv + 4 + 16 * k, b"VITESSE\0")
    before = entries()
    press(ctl, "Keypad 6", frames=0)
    ctl.wait_until_mem(GIVE_FLAG, "==", 0, timeout_ms=10000)
    ctl.wait(frames=3)
    assert entries() == before, "a second give-all changed the held entries"


def test_collision_views(ctl, image, reference):
    red = (255, 0, 0)
    off = colours(shot(ctl, image, reference, "collision-off"))[red]
    press(ctl, "Keypad 7", frames=5)
    on = colours(shot(ctl, image, reference, "collision"))
    press(ctl, "Keypad 7", frames=5)
    assert on[red] > off + 100, f"wall boxes: {on[red]} red pixels, {off} without"
    assert on[(255, 255, 0)], "no centre dot"


def test_profiler(ctl, image, reference):
    press(ctl, "Keypad 8", frames=20)
    picture = shot(ctl, image, reference, "profiler")
    press(ctl, "Keypad 8", frames=3)
    assert picture.getpixel((320, 0)) == (123, 125, 123), "no frame bar on row 0"
    assert picture.getpixel((0, 1)) == (255, 0, 0), "the render stage does not start row 1"
    text = [picture.getpixel((x, y)) for x in range(10, 60) for y in range(14, 30)]
    assert (255, 255, 255) in text, "no frame rate text"
    after = shot(ctl, image, reference, "profiler-off")
    assert after.getpixel((320, 0)) == (0, 0, 0)


def test_dialogue_test(ctl, image, reference):
    """D posts message 0x40 with entry 0 to the HUD queue: MENJ_Dispatcher plays it under
    CTRL_Dispatcher, as the opening dialogue. The caption is saved; the voice is not
    checked here."""
    ctl.type("D", "D", "shift", 150)
    started = False
    for _ in range(60):
        ctl.wait(ms=50)
        if ctl.read32(HANDLER) != GAME_TICK_FRAME:
            started = True
            break
    assert started, "no dialogue started"
    ctl.wait(ms=1500)
    shot(ctl, image, reference, "dialogue")
    settle(ctl)


def test_level_movie(ctl, image, reference):
    """Project 0 has no movie after New Game (MENU_Tick clears +0x3c): one is written in."""
    record = ctl.read32(PROJECT_PTR)
    old = ctl.read(record + 0x3C, 16)
    ctl.write(record + 0x3C, b"ANGKOR.HNM\0")
    since = last_seq(ctl)
    try:
        press(ctl, "H", "H", "shift", frames=10)
        assert ctl.read32(VIDEO_ON) == 1
        assert nav.opens(nav.events_after(ctl, since), "hnm\\angkor.hnm", ok=True)
        shot(ctl, image, reference, "movie")
        for _ in range(20):
            if not ctl.read32(VIDEO_ON):
                break
            ctl.type("Space", " ", None, 150)
            ctl.wait(ms=400)
        assert ctl.read32(VIDEO_ON) == 0
    finally:
        ctl.write(record + 0x3C, old)
    settle(ctl)


def test_console_window_prints_cryos_dumps(ctl, game):
    """Keypad 9 opens a console window (a new one: run.py's game has none) and Cryo's
    Scan_Mem_ and PrintMisEntry_ print into the game's output, teed into it."""
    stdout = game.run_dir / "stdout.txt"
    size = stdout.stat().st_size
    press(ctl, "Keypad 9", frames=5)
    log = game.stderr_text
    assert "[tools] keypad 9 console window: " in log
    assert "not opened" not in log
    printed = stdout.read_bytes()[size:]
    assert sum(chr(c).isdigit() for c in printed) > 100, "Scan_Mem_ printed nothing"
    press(ctl, "Keypad 9", frames=3)
    assert "[tools] keypad 9 console window: closed" in game.stderr_text
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME


def test_play_changes_none_of_it():
    """Phase M: in Play the developer keys are ordinary keys."""
    save.requirements()
    args = ["--renderer", "software", "--scale", "1"]
    game = wdctl.start_game(tag=f"dev-tools-play-{os.getpid()}", args=args, wait=60.0)
    try:
        ctl = game.ctl
        nav.boot_into(ctl, "H18ANGKR.DSN")
        settle(ctl)
        cells = (GIVE_FLAG, CAPTURE_EVERY, CAPTURE_ONE, HUD_ON, READOUT, EDITOR)
        before = [ctl.read32(c) for c in cells] + [ctl.read8(CAMERA_MODE)]
        for key, text, mods in (
            ("Keypad 6", None, None),
            ("Keypad 7", None, None),
            ("Keypad 8", None, None),
            ("6", "6", None),
            ("7", "7", None),
            ("9", "9", None),
            ("-", "-", None),
            ("8", "8", None),
        ):
            press(ctl, key, text, mods)
        after = [ctl.read32(c) for c in cells] + [ctl.read8(CAMERA_MODE)]
        assert after == before
        assert "[tools]" not in game.stderr_text
    finally:
        game.close(remove=True)
