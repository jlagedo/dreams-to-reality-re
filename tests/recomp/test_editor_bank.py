"""The project bank of Develop (spec 008 phase 4): host/sdl/editor_bank.c.

Offline: the codec (host/sdl/editor_bank_rle.c), built with the native harness
verify/native/editor_bank_tests.c, on disc 1's DREAMS.DAT: a byte-for-byte round
trip, one edit, the over-full and broken cases.

Live: one headless Develop game (software renderer) booted into project 0; the tests
run in file order on it. The project page (0x44991e, phase 3's host page) lists the
bank; a row is chosen with the mouse (game_nav.page_choose) and Space confirms. A level
exit is taken by writing a LINK into the working project that needs nothing (flags 1:
the player; no box, no item, no trigger), which SCENE_CheckExits (0x420b60) takes at
once with the editor off.
"""

import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

from dreams import paths

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

RECORDS, RECORD, HEADER, CAP = 150, 0x2200, 0x400, 0x25400
EDITOR = 0x4A477C
HANDLER, GAME_TICK_FRAME, DIALOGUE = 0x626F74, 0x416D45, 0x40E75C
RLE_SAVES = 0x633C14
WORK = 0x65FB04  # _CurrentSceneS
WORK_OBJET = 0x65F8C4  # _CurrentSceneObjetS: +0x0c the mesh LOAD MESH writes
SAVE_PTR = 0x661D94  # _LoadSaveSceneSPtr
LIST_NB, PAGE_SEL, PAGE_START = 0x661D5C, 0x661D78, 0x661D4C  # the project page's state
SEMA_CREATE, SEMA_LOAD, SEMA_SAVE, SEMA_DELETE = 0x4A46B8, 0x4A46BC, 0x4A46C0, 0x4A46C4
SKY = 0x1E4  # project +0x1e4: SCENE_RotateSky's turn rate (bindings.tsv)
LINKS, LINK_SIZE = 0x200, 0x80


def unpack(data):
    """The 150 records of a DREAMS.DAT image (src/dreams/formats/project.py's codec)."""
    from dreams.formats import project

    offsets = struct.unpack_from("<151I", data, 0)
    out = []
    for i in range(RECORDS):
        rec = project.decompress(data[HEADER + offsets[i] : HEADER + offsets[i + 1]])
        assert len(rec) == RECORD, i
        out.append(rec)
    return out


# ---- offline: the codec ----


def test_the_codec_round_trips_disc_1s_bank(tmp_path):
    try:
        bank = paths.disc(1) / "DREAMS.DAT"
    except Exception as error:
        pytest.skip(f"no disc 1: {error}")
    if not bank.is_file():
        pytest.skip(f"no {bank}")
    sources = [
        ROOT / "recomp/windream/host/sdl/editor_bank_rle.c",
        ROOT / "recomp/windream/verify/native/editor_bank_tests.c",
    ]
    include = ROOT / "recomp/windream/host/sdl"
    if sys.platform == "win32":
        env = recomp_env.build_env()
        compiler = shutil.which("clang-cl", path=env.get("PATH"))
        if not compiler:
            pytest.skip("needs clang-cl")
        exe = tmp_path / "editor_bank_tests.exe"
        cmd = [compiler, "/nologo", "/Od", "/W3", f"/I{include}", f"/Fe{exe}", f"/Fo{tmp_path}/"]
    else:
        env = dict(os.environ)
        compiler = shutil.which("cc") or shutil.which("gcc")
        if not compiler:
            pytest.skip("needs a C compiler")
        exe = tmp_path / "editor_bank_tests"
        cmd = [compiler, "-std=c99", "-O0", f"-I{include}", "-o", str(exe)]
    p = subprocess.run([*cmd, *map(str, sources)], env=env, capture_output=True, text=True)
    assert p.returncode == 0, p.stdout + p.stderr
    p = subprocess.run([str(exe), str(bank)], capture_output=True, text=True, timeout=60)
    assert p.returncode == 0, p.stdout + p.stderr
    assert "all passed" in p.stdout
    assert "ok round trip: 138879 bytes" in p.stdout  # disc 1's bank (spec 008 phase 4)


# ---- live ----


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


@pytest.fixture(scope="module")
def game():
    requirements()
    args = ["--mode", "dev", "--renderer", "software", "--scale", "1"]
    game = wdctl.start_game(tag=f"editor-bank-{os.getpid()}", args=args, wait=60.0)
    yield game
    game.close(remove=True)


@pytest.fixture(scope="module")
def ctl(game):
    ctl = game.ctl
    nav.boot_into(ctl, "H18ANGKR.DSN")
    try:  # the opening dialogue holds GAME_TickFrame back (AGENTS.md)
        ctl.wait_until_mem(HANDLER, "==", 0x40E75C, timeout_ms=20000)
    except wdctl.CtlError:
        pass
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)
    ctl.wait(frames=10)
    return ctl


@pytest.fixture(scope="module")
def bank(game, ctl):
    """The guest addresses of the bank's records and list, from the host's log."""
    m = re.search(r"records at 0x([0-9A-F]{8}), list at 0x([0-9A-F]{8})", game.stderr_text)
    assert m, game.stderr_text[-2000:]
    return int(m[1], 16), int(m[2], 16)


def record(bank, slot):
    return bank[0] + slot * RECORD


def name_at(ctl, va):
    return ctl.read_cstr(va, 32)


def press(ctl, key, text=None, mods=None):
    ctl.type(key, text, mods, 150)
    ctl.wait(ms=400)
    ctl.wait(frames=3)


def editor(ctl, on):
    ctl.write32(EDITOR, 1 if on else 0)
    ctl.wait(frames=2)


def read_big(ctl, addr, size):
    """The channel reads at most 64 KB at a time."""
    return b"".join(ctl.read(addr + at, min(0x10000, size - at)) for at in range(0, size, 0x10000))


def guest_bank(ctl):
    """DREAMS.DAT as the game holds it now (_RLE_SAVES), unpacked."""
    return unpack(read_big(ctl, RLE_SAVES, CAP))


def choose(ctl, bank, wanted):
    """On the open project page, scroll to the row named `wanted`, click it, Space."""
    nb = ctl.read32(LIST_NB)
    names = [name_at(ctl, bank[1] + 16 * row) for row in range(nb)]
    nav.page_choose(ctl, names, PAGE_START, PAGE_SEL, wanted)


def settle(ctl):
    """After a level load: its entry dialogue, if any, starts a few seconds in and holds
    GAME_TickFrame (keys typed then are not in a level); ESC ends it line by line."""
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)
    try:
        ctl.wait_until_mem(HANDLER, "==", DIALOGUE, timeout_ms=12000)
    except wdctl.CtlError:
        return
    for _ in range(120):
        if ctl.read32(HANDLER) == GAME_TICK_FRAME:
            break
        ctl.tap("ESC")
        ctl.wait(ms=500)
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME
    ctl.wait(frames=10)


def take_exit(ctl, game, destination, opens):
    """Leave the level through a LINK that needs nothing, written into a free slot."""
    editor(ctl, False)
    links = ctl.read(WORK + LINKS, 8 * LINK_SIZE)
    slot = next(i for i in range(8) if not links[i * LINK_SIZE + 0x18] & 1)
    link = bytearray(LINK_SIZE)
    link[0:12] = f"LINK{slot}".encode().ljust(12, b"\0")
    link[0x0C:0x18] = destination.encode().ljust(12, b"\0")
    link[0x18] = 1
    since = ctl.status()["seq"]
    ctl.write(WORK + LINKS + slot * LINK_SIZE, bytes(link))
    ctl.wait_until_opened(opens, ok=True, since=since, timeout_ms=60000)
    settle(ctl)
    assert game.process.poll() is None


def test_the_bank_is_dreams_dat_in_memory(game, ctl, bank):
    """At DDAT_Load the host unpacked the folder's DREAMS.DAT, which the game read whole."""
    held = guest_bank(ctl)
    for slot in (0, 12, 89, 149):
        assert ctl.read(record(bank, slot), RECORD) == held[slot], slot
    data = (recomp_env.out_dir("windream") / "developer" / "DREAMS.DAT").read_bytes()
    assert read_big(ctl, RLE_SAVES, len(data)) == data, "not the developer folder's DREAMS.DAT"


def test_the_save_pointer_is_bound_to_the_level(ctl, bank):
    editor(ctl, True)
    ctl.wait(frames=3)
    assert name_at(ctl, WORK) == "Project0"
    assert ctl.read32(SAVE_PTR) == record(bank, 0)


def test_create_refuses_on_the_full_bank(game, ctl, bank):
    editor(ctl, True)
    before = ctl.read(WORK, 0x40)
    press(ctl, "A", "A", "shift")
    assert ctl.read32(SEMA_CREATE) == 0
    assert "Project Create refused: all 150 projects are in use" in game.stderr_text
    # the message, for the record
    ctl.screenshot(recomp_env.out_dir("editor-bank") / "create-refused.bmp")
    assert ctl.read(WORK, 0x40) == before, "the live level's record changed"
    assert ctl.read32(SAVE_PTR) == record(bank, 0)
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME and game.process.poll() is None


def test_q_loads_project_12_from_the_bank(ctl, bank):
    editor(ctl, True)
    since = ctl.status()["seq"]
    press(ctl, "Q", "Q", "shift")
    assert ctl.read32(SEMA_LOAD) == 1, "Q opens the project page"
    assert ctl.read32(LIST_NB) == 150
    choose(ctl, bank, "Project12")
    ctl.wait_until_opened("M01TORN.DSN", ok=True, since=since, timeout_ms=60000)
    settle(ctl)
    assert ctl.read32(SEMA_LOAD) == 0
    assert name_at(ctl, WORK) == "Project12"
    assert ctl.read32(SAVE_PTR) == record(bank, 12)


def test_q_then_esc_keeps_the_level_and_the_pointer(ctl, bank):
    editor(ctl, True)
    since = ctl.status()["seq"]
    press(ctl, "Q", "Q", "shift")
    assert ctl.read32(SEMA_LOAD) == 1
    press(ctl, "Escape")
    ctl.wait(frames=10)
    assert ctl.read32(SEMA_LOAD) == 0
    assert not nav.opens(nav.events_after(ctl, since), ".DSN"), "Esc reloaded the level"
    assert ctl.read32(SAVE_PTR) == record(bank, 12)


def test_closing_load_mesh_saves_and_reloads_from_the_bank(game, ctl, bank):
    """WorksGetEditor_ (0x44c89c): once LOAD MESH closes on a mesh other than "EMPTY"
    (here by Esc), the objet save (no objet slot loaded: nothing), the project save
    and the quick reload run, so the level reloads from its bank record."""
    editor(ctl, True)
    saves = game.stderr_text.count("Project Save: Project12 into slot 12")
    ctl.write(WORK_OBJET + 0x0C, b"P4MESH.3DC".ljust(13, b"\0"))
    since = ctl.status()["seq"]
    press(ctl, "2", "2")
    press(ctl, "Escape")
    ctl.wait_until_opened("M01TORN.DSN", ok=True, since=since, timeout_ms=60000)
    settle(ctl)
    assert game.stderr_text.count("Project Save: Project12 into slot 12") == saves + 1
    assert name_at(ctl, WORK) == "Project12"
    assert ctl.read32(SAVE_PTR) == record(bank, 12)


def test_a_saved_edit_is_served_at_the_next_transition(game, ctl, bank):
    editor(ctl, True)
    old = guest_bank(ctl)
    sky = struct.unpack_from("<i", old[12], SKY)[0]
    new = sky + 37
    ctl.write(WORK + SKY, struct.pack("<i", new))
    press(ctl, "W", "W", "shift")
    assert ctl.read32(SEMA_SAVE) == 0
    assert struct.unpack("<i", ctl.read(record(bank, 12) + SKY, 4))[0] == new
    held = guest_bank(ctl)
    assert struct.unpack_from("<i", held[12], SKY)[0] == new, "DREAMS.DAT in memory has the edit"
    assert [i for i in range(RECORDS) if held[i] != old[i]] == [12]
    assert "Project Save: Project12 into slot 12" in game.stderr_text

    take_exit(ctl, game, "Project89", "F05CAB.DSN")
    assert name_at(ctl, WORK) == "Project89"
    take_exit(ctl, game, "Project12", "M01TORN.DSN")
    assert name_at(ctl, WORK) == "Project12"
    assert struct.unpack("<i", ctl.read(WORK + SKY, 4))[0] == new, "the edit did not persist"
    editor(ctl, True)
    ctl.wait(frames=3)
    assert ctl.read32(SAVE_PTR) == record(bank, 12), "not bound again after the transitions"


def test_delete_frees_the_record_in_the_bank_only(ctl, bank):
    editor(ctl, True)
    before = guest_bank(ctl)
    ctl.write32(SEMA_DELETE, 1)  # the "Project Delete" button
    ctl.wait(frames=3)
    choose(ctl, bank, "Project149")
    ctl.wait(frames=3)
    assert ctl.read32(SEMA_DELETE) == 0
    assert ctl.read32(record(bank, 149) + 0x14) == 0
    assert guest_bank(ctl) == before, "DREAMS.DAT in memory changed before a save"
    assert ctl.read32(SAVE_PTR) == record(bank, 12)


def test_create_takes_the_freed_slot(game, ctl, bank):
    """Retail Create starts an empty project in the working copy (a level of nothing)."""
    editor(ctl, True)
    press(ctl, "A", "A", "shift")
    assert ctl.read32(SEMA_CREATE) == 0
    assert ctl.read32(SAVE_PTR) == record(bank, 149)
    assert name_at(ctl, WORK) == "Project149"
    assert ctl.read32(WORK + 0x14) & 1
    ctl.wait(frames=30)
    assert game.process.poll() is None
