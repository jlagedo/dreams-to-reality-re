"""The Dreams Editor's pickers in Develop (spec 008 phase 3): host/sdl/editor_pickers.c.

One headless Develop game (software renderer, --scale 1) booted into project 0; the
tests run in file order on it. Pages open by their editor keys (typed through the
host's key path, `Ctl.type`) and rows are chosen with the mouse (`Ctl.mouse`, through
the host's mouse path; game_nav.page_choose); the anim and map pages, which have no
key, open by their menu buttons' semaphores. Each page's screenshot goes to
out/recomp/editor-reference/ (phase D matches them).
"""

import importlib.util
import os
import re
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

EDITOR = 0x4A477C
HANDLER, GAME_TICK_FRAME, DIALOGUE = 0x626F74, 0x416D45, 0x40E75C
WORK = 0x65FB04  # _CurrentSceneS
WORK_OBJET, WORK_LINK, WORK_BOX, WORK_LADV = 0x65F8C4, 0x65D644, 0x65B244, 0x65D604
MESH_FIELD = WORK_OBJET + 0x0C  # what LOAD MESH writes from Objet Edit
ONE_BYTE_TABLES = (0x661E1E, 0x661E28)  # retail's ten tables: nothing may write there now
RES_GLOBALS = (0x661E2C, 0x661E3C, 0x661E44)  # heap top, resource list, handles
OBJETS, LINKS, BOXES, LADVS = 0x660104, 0x65FD04, 0x660D04, 0x661904
# page: (semaphore, Nb, selected, start)
MESH = (0x4A4718, 0x661D6C, 0x661D54, 0x661D50)
SYM = (0x4A4714, 0x661D60, 0x661D74, 0x661D3C)
HNM = (0x4A471C, 0x661D98, 0x661D40, 0x661D68)
ANIM = (0x4A474C, 0x661D64, 0x661D58, 0x661D80)
MAP = (0x4A4748, 0x661D7C, 0x661D48, 0x661D84)  # "Material Name": its own sema, then LOAD Map's
PROJECT = (0x4A46BC, 0x661D5C, 0x661D78, 0x661D4C)
OBJET = (0x4A46CC, 0x661D70, 0x661D1C, 0x661D44)
LINK = (0x4A46F0, 0x661D34, 0x661D28, 0x661D20)
LADV = (0x4A46DC, 0x661D30, 0x661D14, 0x661D2C)
BOX = (0x4A4700, 0x661D18, 0x661D38, 0x661D24)
LINK_TARGET = 0x4A470C  # key 3: the link's exit-target page (the project list)
FOUND_OBJET, FOUND_LINK, FOUND_BOX, FOUND_LADV = 0x661D90, 0x661D8C, 0x661D88, 0x661D9C


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
    game = wdctl.start_game(tag=f"editor-pickers-{os.getpid()}", args=args, wait=60.0)
    yield game
    game.close(remove=True)


@pytest.fixture(scope="module")
def ctl(game):
    ctl = game.ctl
    nav.boot_into(ctl, "H18ANGKR.DSN")
    try:  # the opening dialogue holds GAME_TickFrame back (AGENTS.md)
        ctl.wait_until_mem(HANDLER, "==", DIALOGUE, timeout_ms=20000)
    except wdctl.CtlError:
        pass
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)
    ctl.wait(frames=10)
    return ctl


@pytest.fixture(scope="module")
def tables(game, ctl):
    """The host tables' guest addresses, from the host's log."""
    line = next(x for x in game.stderr_text.splitlines() if "picker functions replaced" in x)
    assert "23 of 23" in line, line
    found = {k: int(v, 16) for k, v in re.findall(r" (\w+) 0x([0-9A-F]{8})/\d+", line)}
    found["objet meshes"] = int(re.search(r"objet meshes 0x([0-9A-F]{8})", line)[1], 16)
    found["project"] = int(re.search(r"list at 0x([0-9A-F]{8})", game.stderr_text)[1], 16)
    return found


def shot(ctl, name):
    ctl.screenshot(recomp_env.out_dir("editor-reference") / f"{name}.bmp")


def press(ctl, key, text=None, mods=None):
    ctl.type(key, text, mods, 150)
    ctl.wait(ms=400)
    ctl.wait(frames=3)


def editor(ctl, on):
    ctl.write32(EDITOR, 1 if on else 0)
    ctl.wait(frames=2)


def names(ctl, table, page, stride=16):
    return [ctl.read_cstr(table + stride * i, stride) for i in range(ctl.read32(page[1]))]


def choose(ctl, listed, page, wanted, confirm="space"):
    nav.page_choose(ctl, listed, page[3], page[2], wanted, confirm)


def guarded(ctl):
    return ctl.read(*_span(ONE_BYTE_TABLES)), [ctl.read32(a) for a in RES_GLOBALS]


def _span(r):
    return r[0], r[1] - r[0]


def settle(ctl):
    """After a level load: its entry dialogue, if any, holds GAME_TickFrame; ESC ends it."""
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


def folder_files(directory, *suffixes):
    """What the game's own find path lists: per pattern, sorted by upper-cased name
    (host/sdl/files.c), 8.3 names only."""
    root = recomp_env.out_dir("windream") / "developer"
    data = next((d for d in root.iterdir() if d.name.upper() == "DATA"), None)
    found = next((d for d in data.iterdir() if d.name.upper() == directory), None) if data else None
    files = [p.name for p in found.iterdir() if p.is_file()] if found else []
    out = []
    for suffix in suffixes:
        out += sorted((f for f in files if f.upper().endswith(suffix)), key=str.upper)
    return [f for f in out if len(f) <= 12]


def test_load_mesh_lists_every_mesh_file_of_the_folder(ctl, tables):
    editor(ctl, True)
    ctl.write(MESH_FIELD, b"EMPTY".ljust(13, b"\0"))  # closing on any other mesh saves and reloads
    before = guarded(ctl)
    press(ctl, "2", "2")
    assert ctl.read32(MESH[0]) == 1, "2 opens LOAD MESH"
    expected = folder_files("3DC", ".3DC", ".DAN", ".DSN")
    assert names(ctl, tables["mesh"], MESH, 13) == expected
    shot(ctl, "load-mesh")
    nav.page_click(ctl, nav.PAGE_DOWN_Y)
    assert ctl.read32(MESH[3]) == 8, "DOWN steps the mesh page by 8"
    shot(ctl, "load-mesh-scrolled")
    press(ctl, "Escape")
    assert ctl.read32(MESH[0]) == 0
    assert ctl.read_cstr(MESH_FIELD, 16) == "EMPTY", "Esc wrote the field"
    assert guarded(ctl) == before, "a picker wrote retail's tables or the resource heap's globals"


def test_choosing_a_mesh_by_mouse_writes_the_objet_field(ctl, tables):
    editor(ctl, True)
    ctl.write(MESH_FIELD, b"EMPTY".ljust(13, b"\0"))
    tables_before = ctl.read(*_span(ONE_BYTE_TABLES))
    since = ctl.status()["seq"]
    press(ctl, "2", "2")
    listed = names(ctl, tables["mesh"], MESH, 13)
    wanted = listed[10]
    choose(ctl, listed, MESH, wanted, confirm="click")
    assert ctl.read_cstr(MESH_FIELD, 16) == wanted
    assert ctl.read32(MESH[0]) == 0
    # not "EMPTY": WorksGetEditor_ saves the objet and the project and reloads the level
    ctl.wait_until_opened("H18ANGKR.DSN", ok=True, since=since, timeout_ms=60000)
    settle(ctl)
    assert ctl.read(*_span(ONE_BYTE_TABLES)) == tables_before


def test_objet_load_lists_the_level_and_loads_the_chosen_slot(ctl, tables):
    editor(ctl, True)
    press(ctl, "S", "S", "shift")
    assert ctl.read32(OBJET[0]) == 1, "S opens the objet page"
    used = [i for i in range(16) if ctl.read(OBJETS + 0xC0 * i + 0x34, 1)[0] & 1]
    slot_names = [ctl.read_cstr(OBJETS + 0xC0 * i, 12) for i in used]
    listed = names(ctl, tables["objet"], OBJET)
    assert listed == slot_names
    meshes = [ctl.read_cstr(tables["objet meshes"] + 16 * k, 16) for k in range(len(used))]
    assert meshes == [ctl.read_cstr(OBJETS + 0xC0 * i + 0x0C, 16) for i in used]
    shot(ctl, "load-objet")
    k = min(3, len(used) - 1)
    choose(ctl, listed, OBJET, listed[k])
    slot = OBJETS + 0xC0 * used[k]
    assert ctl.read32(OBJET[0]) == 0
    assert ctl.read32(FOUND_OBJET) == slot
    assert ctl.read(WORK_OBJET, 0xC0) == ctl.read(slot, 0xC0)


def test_confirm_with_no_row_and_esc_write_nothing(ctl, tables):
    editor(ctl, True)
    working = ctl.read(WORK_OBJET, 0xC0)
    press(ctl, "S", "S", "shift")
    count = ctl.read32(OBJET[1])
    nav.page_click(ctl, nav.PAGE_DOWN_Y)  # start 1: the last row is past the list
    start = ctl.read32(OBJET[3])
    nav.page_click(ctl, nav.PAGE_ROW_Y + 70)
    assert ctl.read32(OBJET[2]) == start + 7 >= count, "a row past the end is selected"
    press(ctl, "Space", " ")
    assert ctl.read32(OBJET[0]) == 0, "Space closes the page"
    assert ctl.read32(FOUND_OBJET) == 0, "the page wrote a name"
    assert ctl.read(WORK_OBJET, 0xC0) == working
    press(ctl, "S", "S", "shift")
    press(ctl, "Escape")
    assert ctl.read32(OBJET[0]) == 0
    assert ctl.read(WORK_OBJET, 0xC0) == working


@pytest.mark.parametrize(
    "key,page,base,stride,used,working,size,found,shot_name",
    [
        ("D", LINK, LINKS, 0x80, 0x18, WORK_LINK, 0x80, FOUND_LINK, "load-link"),
        ("F", BOX, BOXES, 0x100, 0xEC, WORK_BOX, 0x100, FOUND_BOX, "load-box"),
        ("G", LADV, LADVS, 0x40, 0x28, WORK_LADV, 0x40, FOUND_LADV, "load-linkadventure"),
    ],
)
def test_link_box_and_link_adventure_pages(
    ctl, tables, key, page, base, stride, used, working, size, found, shot_name
):
    editor(ctl, True)
    press(ctl, key, key, "shift")
    assert ctl.read32(page[0]) == 1, f"{key} opens its page"
    count = 8 if stride == 0x80 else 16
    slots = [i for i in range(count) if ctl.read(base + stride * i + used, 1)[0] & 1]
    table = tables[{"load-link": "link", "load-box": "box"}.get(shot_name, "linkadventure")]
    listed = names(ctl, table, page)
    assert listed == [ctl.read_cstr(base + stride * i, 12) for i in slots]
    shot(ctl, shot_name)
    k = len(slots) - 1
    choose(ctl, listed, page, listed[k])
    assert ctl.read32(page[0]) == 0
    assert ctl.read32(found) == base + stride * slots[k]
    assert ctl.read(working, size) == ctl.read(base + stride * slots[k], size)


def test_asset_pages_list_the_folder(ctl, tables):
    editor(ctl, True)
    before = guarded(ctl)
    for key, page, table, expected, name in [
        ("6", SYM, "symbol", folder_files("SYM", ".SYM"), "load-symbole"),
        ("1", HNM, "hnm", folder_files("HNM", ".UBB", ".HNM"), "load-hnm"),
    ]:
        press(ctl, key, key)
        assert ctl.read32(page[0]) == 1, f"{key} opens {name}"
        assert names(ctl, tables[table], page, 13) == expected, name
        shot(ctl, name)
        press(ctl, "Escape")
        assert ctl.read32(page[0]) == 0
    ctl.write32(ANIM[0], 1)  # "Hnm Name" (Material Light): no key
    ctl.wait(frames=3)
    assert names(ctl, tables["anim"], ANIM, 13) == folder_files("ANIM", ".HNM")
    shot(ctl, "load-anim")
    press(ctl, "Escape")
    assert ctl.read32(ANIM[0]) == 0
    ctl.write32(MAP[0], 1)  # "Material Name": GetAllMap_ every frame, then LOAD Map
    ctl.wait(frames=3)
    materials = [
        ctl.read_cstr(0x615AD8 + 0x420 * i + 8, 16)
        for i in range(64)
        if ctl.read32(0x615AD8 + 0x420 * i + 0x18)
    ]
    assert names(ctl, tables["map"], MAP) == materials and materials
    shot(ctl, "load-map")
    press(ctl, "Escape")
    assert ctl.read32(MAP[0]) == 0
    assert guarded(ctl) == before


def test_the_link_target_page_lists_the_bank(ctl, tables):
    editor(ctl, True)
    press(ctl, "D", "D", "shift")  # a link into the working copy, so the target has a home
    nav.page_click(ctl, nav.PAGE_ROW_Y)
    press(ctl, "Space", " ")
    target = ctl.read(0x65D650, 12)
    press(ctl, "3", "3")
    assert ctl.read32(LINK_TARGET) == 1, "3 opens the link's target page"
    assert ctl.read32(PROJECT[1]) == 150
    shot(ctl, "link-scene")
    press(ctl, "Escape")
    assert ctl.read32(LINK_TARGET) == 0
    assert ctl.read(0x65D650, 12) == target


def test_q_lists_the_bank_and_loads_the_chosen_project(ctl, tables):
    editor(ctl, True)
    since = ctl.status()["seq"]
    press(ctl, "Q", "Q", "shift")
    assert ctl.read32(PROJECT[0]) == 1
    listed = names(ctl, tables["project"], PROJECT)
    assert len(listed) == 150 and listed[12] == "Project12"
    shot(ctl, "load-project")
    choose(ctl, listed, PROJECT, "Project12")
    ctl.wait_until_opened("M01TORN.DSN", ok=True, since=since, timeout_ms=60000)
    settle(ctl)
    assert ctl.read_cstr(WORK, 16) == "Project12"


def test_z_then_a_mesh_pick_creates_an_objet(ctl, tables):
    """Objet Create (0x44a517) takes the first free slot, names it OBJET<n> and opens
    LOAD MESH; the pick saves the objet into the live record and reloads the level."""
    editor(ctl, True)
    free = next(i for i in range(16) if not ctl.read(OBJETS + 0xC0 * i + 0x34, 1)[0] & 1)
    since = ctl.status()["seq"]
    press(ctl, "Z", "Z", "shift")
    assert ctl.read32(MESH[0]) == 1, "Z opens LOAD MESH"
    assert ctl.read_cstr(WORK_OBJET, 12) == f"OBJET{free}"
    listed = names(ctl, tables["mesh"], MESH, 13)
    choose(ctl, listed, MESH, "BOULE.3DC")
    ctl.wait_until_opened("M01TORN.DSN", ok=True, since=since, timeout_ms=60000)
    settle(ctl)
    slot = OBJETS + 0xC0 * free
    assert ctl.read_cstr(slot, 12) == f"OBJET{free}"
    assert ctl.read_cstr(slot + 0x0C, 16) == "BOULE.3DC"
    assert ctl.read(slot + 0x34, 1)[0] & 1, "the new objet is not in use"
