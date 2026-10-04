"""Cryo's Save page, the demo recorder and the save guard (spec 008 phase 7b):
host/sdl/dev_save_page.c, the recorder of host/sdl/dev_tools.c and
host/sdl/save_guard.c, driven through the host's own key path (the control
channel's `type` command) and the game's menus (game_nav).

One headless Develop game (software renderer) on a copy of the developer folder
(the saves, REPLAY.BIN and the Save page write into it; test_editor_save.py's copy:
large read-only directories hard-linked, DATA\\GAME copied), booted into project 0
with its opening dialogue ended; its tests run in file order. Then a Play edits game
on the same copy loads the Develop save (spec 008 phase M's open check) and meets
the save guard on two broken saves made from the copy's own; then a Play game, whose
save list is the install root's (ten foreign 10,364-byte files that crashed
GAME_LoadGame before the guard), refuses one. The Save page's screen and the main
menu's refusal are saved to DREAMS_OUT/recomp/editor-reference/, the software
reference phase D matches.
"""

import hashlib
import importlib.util
import os
import shutil
import struct
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
tools = load("tests/recomp/test_dev_tools.py")  # settle(), press(), shot()
recomp_env = save.recomp_env

HANDLER, GAME_TICK_FRAME, MENU_HANDLER = 0x626F74, 0x416D45, 0x40E75C
EDITOR = 0x4A477C
PLAYER = 0x4FBA78
VITALITY, MAGIC = 0x4FBAB0, 0x4FBAB4  # the player's +0x38, +0x3c; GAME_SaveGame writes both
INVENTORY = 0x61520C  # GAME_SaveGame's 0x3b8 bytes (the first dword is a pointer)
LOADING = 0x661E08
SLOT_NAMES, SLOT_SELECTED = 0x5DABF8, 0x4A2F3D
PAGE_STATUS = 0x4A1563
CAMERA_MODE = 0x52C874
DEMO_MODE, DEMO_INDEX, DEMO_RECORDING = 0x49D34A, 0x49D346, 0x49D5DC
INPUT_MODE = 0x49D2F8
MAIN_MENU_CODE = 0x4A2EF5
RING = 0x5E2B08  # the level-state ring GAME_LoadGame reads first
SAVE_SIZE, NAME_AT = 11388, 0x2884
TRACE = [(PLAYER, 24), (DEMO_INDEX, 4), (DEMO_MODE, 4), (0x49D2FE, 44)]
TITLE = "Dev Save 1"


def index(path):
    """game.dat: (title, protected, recency, file number) of each used slot."""
    data = path.read_bytes()
    rows = []
    for i in range(10):
        name = data[0x16 * i : 0x16 * i + 0x16].split(b"\0")[0].decode("latin-1")
        if name:
            rows.append(
                (
                    name,
                    struct.unpack_from("<i", data, 0xDC + 4 * i)[0],
                    struct.unpack_from("<i", data, 0x104 + 4 * i)[0],
                    struct.unpack_from("<i", data, 0x12C + 4 * i)[0],
                )
            )
    return rows


def type_text(ctl, text):
    for ch in text:
        key = "Space" if ch == " " else ch.upper()
        ctl.type(key, ch, "shift" if ch.isupper() else None, 80)
        ctl.wait(ms=250)


def back_in_game(ctl):
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=15000)
    ctl.wait(frames=3)


def stderr_lines(game, prefix):
    return [line for line in game.stderr_text.splitlines() if line.startswith(prefix)]


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
    target = recomp_env.out_dir("dev-save") / f"tree-{os.getpid()}"
    save.tree_copy(source, target)
    yield target
    shutil.rmtree(target, ignore_errors=True)


@pytest.fixture(scope="module")
def game_dir(tree):
    return tree / "DATA" / "GAME"


@pytest.fixture(scope="module")
def game(tree):
    args = ["--mode", "dev", "--tree", str(tree), "--renderer", "software", "--scale", "1"]
    game = wdctl.start_game(tag=f"dev-save-{os.getpid()}", args=args, wait=60.0)
    yield game
    game.close(remove=True)


@pytest.fixture(scope="module")
def ctl(game):
    ctl = game.ctl
    nav.boot_into(ctl, "H18ANGKR.DSN")
    tools.settle(ctl)
    ctl.write32(EDITOR, 0)
    return ctl


# ---- the Save page (keypad 0) ----


def test_the_save_page_saves_in_an_empty_slot(ctl, game, game_dir, image, reference):
    before = index(game_dir / "game.dat")
    assert len(before) < 10, "the copy's index is full: no empty slot to check"
    tools.press(ctl, "Keypad 0")
    ctl.wait_until_mem(HANDLER, "==", MENU_HANDLER, timeout_ms=5000)
    assert ctl.read32(SLOT_SELECTED) == len(before)  # the first empty slot (the index is sorted)
    type_text(ctl, TITLE)
    ctl.wait(ms=500)
    assert ctl.read_cstr(SLOT_NAMES + 0x16 * len(before), 0x16).rstrip("_") == TITLE
    tools.shot(ctl, image, reference, "save-page")
    ctl.type("Return", None, None, 120)
    back_in_game(ctl)
    after = index(game_dir / "game.dat")
    assert after[: len(before)] == before
    title, protected, recency, number = after[len(before)]
    assert (title, protected) == (TITLE, 0) and recency == max(r[2] for r in after)
    assert (game_dir / f"game{number}.dat").stat().st_size == SAVE_SIZE
    assert (game_dir / f"game{number}.ico").stat().st_size == 8192
    saved = (game_dir / f"game{number}.dat").read_bytes()
    assert saved[NAME_AT : NAME_AT + 32].split(b"\0")[0] == b"Project0"
    assert any(f'Saved "{TITLE}"' in line for line in stderr_lines(game, "[tools]"))


def test_a_title_already_used_is_refused(ctl, game, game_dir):
    before = (game_dir / "game.dat").read_bytes()
    tools.press(ctl, "Keypad 0")
    ctl.wait_until_mem(HANDLER, "==", MENU_HANDLER, timeout_ms=5000)
    type_text(ctl, TITLE)
    ctl.wait(ms=500)
    ctl.type("Return", None, None, 120)
    back_in_game(ctl)
    assert [r[0] for r in index(game_dir / "game.dat")].count(TITLE) == 1
    assert (game_dir / "game.dat").read_bytes() == before
    assert any("another save is called" in line for line in stderr_lines(game, "[tools]"))


def test_esc_saves_nothing_and_developer_keys_are_text(ctl, game_dir):
    before = hashlib.sha256((game_dir / "game.dat").read_bytes()).hexdigest()
    files = sorted(p.name for p in game_dir.iterdir())
    camera = ctl.read8(CAMERA_MODE)
    tools.press(ctl, "Keypad 0")
    ctl.wait_until_mem(HANDLER, "==", MENU_HANDLER, timeout_ms=5000)
    slot = ctl.read32(SLOT_SELECTED)
    type_text(ctl, "r-9")
    ctl.wait(ms=500)
    assert ctl.read_cstr(SLOT_NAMES + 0x16 * slot, 0x16).rstrip("_") == "r-9"
    ctl.type("Escape", None, None, 120)
    back_in_game(ctl)
    assert hashlib.sha256((game_dir / "game.dat").read_bytes()).hexdigest() == before
    assert sorted(p.name for p in game_dir.iterdir()) == files
    assert ctl.read8(CAMERA_MODE) == camera  # '9' and '-' did not reach the cameras
    assert ctl.read32(DEMO_MODE) == 2  # 'r' did not start a recording
    assert TITLE in nav.slot_names(ctl) and "r-9" not in nav.slot_names(ctl)


def test_loading_the_save_restores_the_player(ctl, game_dir):
    number = next(r[3] for r in index(game_dir / "game.dat") if r[0] == TITLE)
    saved = (game_dir / f"game{number}.dat").read_bytes()
    # GAME_SaveGame's order: the ring, its index, the name, vitality, magic, inventory
    ring = 0x2880 + 4 + 32 + 4 + 4
    vitality, magic = saved[0x2880 + 36 : 0x2880 + 40], saved[0x2880 + 40 : 0x2880 + 44]
    tools.settle(ctl)  # a dialogue line would take the ESC that opens the menu
    nav.open_load_list_in_game(ctl)
    # changed while the menu holds the game, so nothing reacts before the load
    ctl.write(VITALITY, struct.pack("<f", 7.0))
    ctl.write(MAGIC, struct.pack("<f", 3.0))
    inventory = ctl.read(INVENTORY + 4, 0x3B4)
    ctl.write(INVENTORY + 4, bytes(16))  # the first entry's name gone
    nav.load_slot(ctl, TITLE)
    ctl.wait_until_mem(LOADING, "==", 0, timeout_ms=20000)
    back_in_game(ctl)
    assert ctl.read(VITALITY, 4) == vitality
    # magic moves every frame (it refills): within a unit of the saved value, far from 3.0
    now, then = (struct.unpack("<f", x)[0] for x in (ctl.read(MAGIC, 4), magic))
    assert abs(now - then) < 1.0
    assert ctl.read(INVENTORY + 4, 0x3B4) == saved[ring + 4 : ring + 0x3B8] == inventory
    assert ctl.current_project()["name"] == "Project0"


# ---- the recorder (r, R) ----


def test_the_shipped_dos_replay_is_refused(ctl, game, tree):
    shipped = tree / "DATA" / "REPLAY.BIN"
    if not shipped.is_file() or (shipped.stat().st_size - 4) % 112 == 0:
        pytest.skip("the folder has no DOS recording")
    tools.press(ctl, "R", "R", "shift")
    ctl.wait(frames=5)
    assert ctl.read32(DEMO_MODE) == 2
    assert any("REPLAY.BIN refused" in line for line in stderr_lines(game, "[tools]"))


def _trace(path):
    rows = []
    for line in path.read_text().splitlines():
        f = line.split()
        if len(f) != 6 or "-" in f[2:]:
            continue
        try:  # the trace may still be writing its last line
            b = [bytes.fromhex(x) for x in f[2:]]
        except ValueError:
            continue
        if [len(x) for x in b] != [size for _, size in TRACE]:
            continue
        rows.append(
            dict(
                pos=struct.unpack("<3d", b[0]),
                idx=struct.unpack("<i", b[1])[0],
                demo=struct.unpack("<i", b[2])[0],
                words=b[3],
            )
        )
    return rows


@pytest.fixture(scope="module")
def recording(ctl, tree):
    """r, a scripted run, r: DATA\\REPLAY.BIN and the record trace by index."""
    work = recomp_env.out_dir("dev-save") / f"trace-{os.getpid()}"
    work.mkdir(parents=True, exist_ok=True)
    since = ctl.status()["seq"]
    tools.press(ctl, "R", "r", None, frames=1)
    assert ctl.read32(DEMO_MODE) == 0 and ctl.read32(DEMO_RECORDING) == 1
    ctl.trace(work / "record.txt", 420, TRACE)
    ctl.wait_until_opened("H18ANGKR.DSN", since=since, timeout_ms=20000)
    ctl.wait_until_mem(LOADING, "==", 0, timeout_ms=20000)
    ctl.wait(frames=10)
    ctl.key_down("UP")
    ctl.wait(frames=45)
    ctl.tap("CTRL")
    ctl.wait(frames=20)
    ctl.key_down("LEFT")
    ctl.wait(frames=15)
    ctl.key_up("LEFT")
    ctl.wait(frames=30)
    ctl.key_up("UP")
    ctl.tap("ALT")
    ctl.wait(frames=40)
    since = ctl.status()["seq"]
    at_stop = ctl.read32(DEMO_INDEX)
    tools.press(ctl, "R", "r", None, frames=1)
    ctl.wait_until_opened("intro.hnm", since=since, timeout_ms=20000)
    data = (tree / "DATA" / "REPLAY.BIN").read_bytes()
    yield data, _trace(work / "record.txt"), work, at_stop
    shutil.rmtree(work, ignore_errors=True)


def test_a_recording_is_written_and_returns_to_the_title(recording):
    data, _, _, at_stop = recording
    count = struct.unpack_from("<i", data, 0)[0]
    assert len(data) == 4 + 112 * count
    assert at_stop <= count <= at_stop + 10  # the frames between the read and the key
    assert count > 150


def test_the_recording_plays_back_and_ends_at_the_title(ctl, recording):
    data, record, work, _ = recording
    count = struct.unpack_from("<i", data, 0)[0]
    since = ctl.status()["seq"]
    main = nav.press_until_opened(ctl, "ESC", "generic.hnm", since)
    nav.new_game(ctl, "H18ANGKR.DSN", main["event"])
    tools.settle(ctl)
    ctl.write32(EDITOR, 0)
    mode = ctl.read8(INPUT_MODE)
    since = ctl.status()["seq"]
    tools.press(ctl, "R", "R", "shift", frames=1)
    assert ctl.read32(DEMO_MODE) == 1
    ctl.trace(work / "play.txt", count + 120, TRACE)
    ctl.wait_until_mem(DEMO_MODE, "==", 2, timeout_ms=60000)
    ctl.wait_until_opened("intro.hnm", since=since, timeout_ms=20000)
    assert ctl.read8(INPUT_MODE) == mode
    by_index = {r["idx"]: r for r in record if r["demo"] == 0}
    pairs = zip(record, record[1:], strict=False)
    edges = [r["idx"] for a, r in pairs if a["demo"] == r["demo"] == 0 and r["words"] != a["words"]]
    first_edge = edges[0] if edges else None
    # The first indexes run before the level load places the player: they hold the
    # position the game had before, which the recording's start (after this module's
    # save load) and the playback's (a New Game) do not share. Compare from the
    # placement, the first jump of more than 100 units.
    jumps = [
        r["idx"]
        for a, r in zip(record, record[1:], strict=False)
        if a["demo"] == r["demo"] == 0
        and max(abs(x - y) for x, y in zip(a["pos"], r["pos"], strict=True)) > 100
    ]
    placed = jumps[0] if jumps else 0
    played = [p for p in _trace(work / "play.txt") if p["demo"] == 1 and p["idx"] in by_index]
    assert len(played) > count // 2
    worst = 0.0
    for p in played:
        if p["idx"] < placed:
            continue
        d = max(abs(a - b) for a, b in zip(p["pos"], by_index[p["idx"]]["pos"], strict=True))
        if first_edge is not None and p["idx"] < first_edge:
            assert d < 0.01, f"index {p['idx']} before the first key edge is {d} units off"
        worst = max(worst, d)
    assert worst <= 50.0, f"playback strayed {worst:.1f} units from the recording"


def test_a_menu_during_playback_is_stopped_by_the_host(ctl, game):
    since = ctl.status()["seq"]
    main = nav.press_until_opened(ctl, "ESC", "generic.hnm", since)
    nav.new_game(ctl, "H18ANGKR.DSN", main["event"])
    tools.settle(ctl)
    ctl.write32(EDITOR, 0)
    mode = ctl.read8(INPUT_MODE)
    tools.press(ctl, "R", "R", "shift", frames=1)
    assert ctl.read32(DEMO_MODE) == 1
    ctl.wait_until_mem(LOADING, "==", 0, timeout_ms=20000)
    ctl.wait(frames=30)
    ctl.write(VITALITY, struct.pack("<f", 0.0))  # the death path opens a game menu
    ctl.write(PLAYER + 0x38, struct.pack("<f", 0.0))
    ctl.wait_until_mem(DEMO_MODE, "==", 2, timeout_ms=30000)
    assert ctl.read8(INPUT_MODE) == mode
    lines = stderr_lines(game, "[tools]")
    assert any("a game menu opened during playback" in line for line in lines)


# ---- Play edits: the Develop save, and the save guard ----


@pytest.fixture(scope="module")
def edited(tree, game_dir, game):
    game.close(remove=True)  # the Develop game is done; one game at a time
    rows = index(game_dir / "game.dat")
    others = [r for r in rows if r[0] != TITLE]
    if len(others) < 2:
        pytest.skip("the copy has fewer than two other saves to break")
    (foreign, *_), (renamed, *_) = others[0], others[1]
    number = {r[0]: r[3] for r in rows}
    (game_dir / f"game{number[foreign]}.dat").write_bytes(bytes(10364))
    path = game_dir / f"game{number[renamed]}.dat"
    data = bytearray(path.read_bytes())
    data[NAME_AT : NAME_AT + 32] = b"NoSuchProject".ljust(32, b"\0")
    path.write_bytes(bytes(data))
    args = ["--mode", "edited", "--tree", str(tree), "--renderer", "software", "--scale", "1"]
    play = wdctl.start_game(tag=f"dev-save-edited-{os.getpid()}", args=args, wait=60.0)
    nav.to_main_menu(play.ctl)
    nav.open_load_list_from_main_menu(play.ctl)
    yield play, foreign, renamed
    play.close(remove=True)


def refused(play, title, image=None, reference=None, name=None):
    ctl = play.ctl
    ring = ctl.read(RING, 64)
    nav.load_slot(ctl, title)
    ctl.wait(ms=1500)
    assert ctl.read32(MAIN_MENU_CODE) == 8
    assert ctl.read(RING, 64) == ring
    if image:
        tools.shot(ctl, image, reference, name)
    return ctl


def test_the_guard_refuses_a_foreign_save(edited, image, reference):
    play, foreign, _ = edited
    refused(play, foreign, image, reference, "save-guard-main-menu")
    assert any("not 11,388" in line for line in stderr_lines(play, "[save]"))


def test_the_guard_refuses_a_save_of_a_level_not_in_the_bank(edited):
    play, _, renamed = edited
    refused(play, renamed)
    assert any("NoSuchProject" in line for line in stderr_lines(play, "[save]"))


def test_the_develop_save_loads_in_play_edits(edited):
    play, _, _ = edited
    ctl = play.ctl
    since = nav.load_slot(ctl, TITLE)
    ctl.wait_until_opened("H18ANGKR.DSN", since=since, timeout_ms=30000)
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=30000)
    assert ctl.current_project()["name"] == "Project0"


# ---- Play: the install root's foreign saves ----


def test_the_guard_refuses_a_foreign_save_in_play():
    from dreams import paths

    install = paths.configured("install_root")
    saves = install / "DATA" / "GAME" if install else None
    pattern = "[Gg][Aa][Mm][Ee][0-9].[Dd][Aa][Tt]"
    files = sorted(saves.glob(pattern)) if saves and saves.is_dir() else []
    if not files or any(p.stat().st_size == SAVE_SIZE for p in files):
        pytest.skip("the install root has no foreign saves to refuse")
    # Play keeps its saves in the run's sandbox: seed it with the install root's
    # (copies; the install root is only read)
    tag = f"dev-save-play-{os.getpid()}"
    sandbox = recomp_env.out_dir("windream") / f"run-{tag}" / "sandbox" / "CRYO" / "DREAMS"
    shutil.rmtree(sandbox.parent.parent, ignore_errors=True)
    shutil.copytree(saves, sandbox / "DATA" / "GAME")
    play = wdctl.start_game(tag=tag, args=["--renderer", "software"], wait=60.0)
    try:
        ctl = play.ctl
        nav.to_main_menu(ctl)
        nav.open_load_list_from_main_menu(ctl)
        title = next(n for n in nav.slot_names(ctl) if n)
        refused(play, title)
        assert any("not 11,388" in line for line in stderr_lines(play, "[save]"))
        assert play.process.poll() is None
    finally:
        play.close(remove=True)
