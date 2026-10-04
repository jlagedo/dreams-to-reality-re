"""The bank written to the developer folder (spec 008 phase 5): host/sdl/editor_bank.c.

Live, on a copy of the developer folder under DREAMS_OUT/recomp/editor-save (never the
shared one: these games write its DREAMS.DAT and EDITOR.DAT). The copy hard-links the
large read-only directories and copies the rest, so the saves the game writes in place
stay the copy's own; the writer replaces DREAMS.DAT through a rename, which leaves the
source folder's file as it was.

One Develop game (software renderer), then one Play edits game on the same copy:

- at the main menu, the bank marked dirty with no change: New Game's DDAT_Load
  (BOOT_Run 0x436481) writes it first, byte for byte the folder's DREAMS.DAT;
- in project 0, its sky speed changed and saved with W: F10 writes DREAMS.DAT, in which
  only record 0's bytes and the later offsets differ, and EDITOR.DAT, the 150 records raw;
  F10 again writes nothing;
- another value and W, then the game's own quit (Alt+X, GAME_Shutdown's SaveDiskScene_
  0x44900a; the system page's Quit takes the same path): the file has it;
- Play edits, on the written folder: project 0 plays with that sky speed.
"""

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
run = load("recomp/windream/run.py")
recomp_env = run.recomp_env

RECORDS, RECORD, HEADER = 150, 0x2200, 0x400
HANDLER, GAME_TICK_FRAME, DIALOGUE = 0x626F74, 0x416D45, 0x40E75C
EDITOR = 0x4A477C
DIRTY = 0x661DA4  # set by WorksGetEditor_ after a project save; DDAT_Load clears it
WORK = 0x65FB04  # _CurrentSceneS
SEMA_SAVE = 0x4A46C0
SKY = 0x1E4  # project +0x1e4: SCENE_RotateSky's turn rate (bindings.tsv)
SCENE = "H18ANGKR.DSN"  # project 0's scene
# Read-only and large: hard-linked into the copy. The rest (the saves in DATA\GAME,
# DREAMS.DAT, the TGA captures) is copied.
LINKED = (
    "DATA/3DC",
    "DATA/HNM",
    "DATA/ANIM",
    "DEMOS2",
    "DIRECTX",
    "CRYOPLUS",
    "DEMOS1",
    "DEMO0",
    "3DFX",
)


def requirements():
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    if "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text(errors="replace"):
        pytest.skip(f"{build} was built without WD_DEVTOOLS")
    try:
        run.disc_sources(True, None, None)
    except Exception as error:  # no DREAMS_DISC1/2
        pytest.skip(f"no disc images: {error}")
    source = recomp_env.out_dir("windream") / "developer"
    if not (source / "DREAMS.DAT").is_file():
        pytest.skip(f"no developer folder {source}: run run.py --mode dev once")
    return source


def tree_copy(source: Path, target: Path) -> Path:
    if target.exists():
        shutil.rmtree(target)
    for dirpath, _dirs, files in os.walk(source):
        rel = Path(dirpath).relative_to(source)
        (target / rel).mkdir(parents=True, exist_ok=True)
        top = rel.as_posix()
        linked = any(top == d or top.startswith(d + "/") for d in LINKED)
        for name in files:
            (os.link if linked else shutil.copy2)(Path(dirpath) / name, target / rel / name)
    return target


def offsets(data):
    return struct.unpack_from(f"<{RECORDS + 1}I", data, 0)


def packed(data, i):
    o = offsets(data)
    return data[HEADER + o[i] : HEADER + o[i + 1]]


def records(data):
    from dreams.formats import project

    return [project.decompress(packed(data, i)) for i in range(RECORDS)]


def sky(record):
    return struct.unpack_from("<i", record, SKY)[0]


@pytest.fixture(scope="module")
def folder():
    source = requirements()
    target = recomp_env.out_dir("editor-save") / f"tree-{os.getpid()}"
    tree_copy(source, target)
    yield source, target
    shutil.rmtree(target, ignore_errors=True)


def start(tree, mode, tag):
    args = ["--mode", mode, "--tree", str(tree), "--renderer", "software", "--scale", "1"]
    return wdctl.start_game(tag=f"{tag}-{os.getpid()}", args=args, wait=60.0)


def in_level(ctl):
    """Project 0's opening dialogue holds GAME_TickFrame back once it starts, some
    seconds after the level (AGENTS.md): wait for it, and end it line by line with ESC,
    as test_editor_bank.py's settle does."""
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)
    try:
        ctl.wait_until_mem(HANDLER, "==", DIALOGUE, timeout_ms=30000)
    except wdctl.CtlError:
        pass
    for _ in range(120):
        if ctl.read32(HANDLER) == GAME_TICK_FRAME:
            break
        ctl.tap("ESC")
        ctl.wait(ms=500)
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME
    ctl.wait(frames=10)


def type_key(ctl, key, text=None, mods=None):
    ctl.type(key, text, mods, 150)
    ctl.wait(ms=400)
    ctl.wait(frames=3)


def save_sky(ctl, value):
    """Project 0's sky speed set in the working copy and saved with W (editor on)."""
    ctl.write32(EDITOR, 1)
    ctl.wait(frames=3)
    ctl.write(WORK + SKY, struct.pack("<i", value))
    type_key(ctl, "W", "W", "shift")
    assert ctl.read32(SEMA_SAVE) == 0
    assert ctl.read32(DIRTY) == 1, "WorksGetEditor_ marks the bank dirty after a save"


@pytest.fixture(scope="module")
def develop(folder):
    """The Develop game's checks, in order, on one game; returns what Play edits checks."""
    source, tree = folder
    original = (source / "DREAMS.DAT").read_bytes()
    game = start(tree, "dev", "editor-save")
    ctl = game.ctl
    try:
        menu = nav.to_main_menu(ctl)
        # 1. New Game reads DREAMS.DAT again: a dirty bank, unchanged, is written first.
        ctl.write32(DIRTY, 1)
        nav.new_game(ctl, SCENE, menu["event"])
        assert "Bank written (DREAMS.DAT read again)" in game.stderr_text, game.stderr_text[-3000:]
        assert (tree / "DREAMS.DAT").read_bytes() == original, "no edit: not byte for byte"
        in_level(ctl)

        # 2. An edit of project 0 and F10.
        before = records(original)
        first = sky(before[0]) + 37
        save_sky(ctl, first)
        type_key(ctl, "F10")
        assert "Bank written (F10)" in game.stderr_text, game.stderr_text[-3000:]
        assert ctl.read32(DIRTY) == 0
        written = (tree / "DREAMS.DAT").read_bytes()
        after = records(written)
        assert sky(after[0]) == first
        assert [i for i in range(RECORDS) if after[i] != before[i]] == [0]
        assert offsets(written)[0] == offsets(original)[0]
        assert all(packed(written, i) == packed(original, i) for i in range(1, RECORDS))
        assert written[0x25C:HEADER] == original[0x25C:HEADER]
        assert (tree / "EDITOR.DAT").read_bytes() == b"".join(after), "EDITOR.DAT is the bank raw"
        assert (source / "DREAMS.DAT").read_bytes() == original, "the source folder changed"

        # 3. F10 with nothing new writes nothing.
        type_key(ctl, "F10")
        assert "nothing written" in game.stderr_text
        assert (tree / "DREAMS.DAT").read_bytes() == written

        # 4. Another edit, then the game's own quit: GAME_Shutdown's SaveDiskScene_.
        second = first + 11
        save_sky(ctl, second)
        ctl.write32(EDITOR, 0)
        ctl.wait(frames=3)
        try:
            ctl.type("X", None, "alt", 200)  # Alt+X, the game's quit key
            ctl.wait(ms=10000)
        except wdctl.CtlError:
            pass  # the game ended under the call
        game.process.wait(timeout=30)
        assert "Bank written (shutdown)" in game.stderr_text, game.stderr_text[-3000:]
        assert sky(records((tree / "DREAMS.DAT").read_bytes())[0]) == second
        assert not list(tree.glob("*.tmp")), "a temporary file was left"
        return second
    finally:
        game.close(remove=True)


def test_develop_writes_the_bank(develop):
    assert develop


def test_play_edits_plays_the_written_bank(folder, develop):
    _source, tree = folder
    game = start(tree, "edited", "editor-save-edited")
    try:
        ctl = game.ctl
        nav.boot_into(ctl, SCENE)
        in_level(ctl)
        assert struct.unpack("<i", ctl.read(WORK + SKY, 4))[0] == develop
        assert "[bank]" not in game.stderr_text, "the bank is Develop's alone"
    finally:
        game.close(remove=True)
