"""Disc mode in play: the real game, driven through the development control
channel (recomp/windream/debug/wdctl.py), loading levels of both discs.

What is covered (spec 007): a new game into disc 2 projects, the one link that
crosses the discs (Project26 to Project116) and the way back, the saves the
game writes and loading them from the menus, and the CD music, located in the
images' track data (recomp/windream/debug/cd_audio_check.py).

How the tests reach a level: "New game" loads project 0, so a project bank
whose slot 0 is a copy of another record (recomp/windream/debug/bank_patch.py)
is put into the run's data directory as dreams.dat, where it overrides the
disc's. Game data stays under out/: each run has its own --tag directory,
removed afterwards.

Facts of the retail game the helpers rely on (WINDREAM.EXE, read-only Ghidra
decompilation; the addresses are guest addresses read through the channel):

- The game has no "save" command. MENU_HandleSystemPageInput (0x431603) steps
  over entry 1 of its list, so the in-game menu offers Load, Options and Quit;
  the save is written by GAME_StartLevel on entering a level (0x439341), into
  the slot named after the level ("Grotto of the Shaman").
- In-game "Quit" ends the process; there is no way back to the main menu.
- A LINK with an empty box fires when its conditions hold. The link from
  Project26 to Project116 has flags 0x11: enabled, and "the level's triggers
  are done" (SCENE_IsTriggerDone, the dword at 0x6155e4). The tests set that
  dword instead of playing the level's fight; everything after it is the game's
  own (SCENE_CheckExits, CD_PrepareLevel, the swap prompt).
- The CD track of a level is the low byte of the record's dword at +0x11c
  (GAME_Tick sends it with MGM 0x1e: CD_SetPlaylist takes up to three track
  numbers, one per byte). The field at +0x1f8 that project.py calls cd_track
  is not it: Project0 has 2 there and 9 at +0x11c, and plays track 9.

Skipped when the development build (with WD_DEVTOOLS) or the two disc images
are missing.
"""

import importlib.util
import os
import shutil
import struct
import sys
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative, register=False):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    if register:  # a module with dataclasses must be findable by name
        sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


wdctl = load("recomp/windream/debug/wdctl.py")
run = load("recomp/windream/run.py")
recomp_env = run.recomp_env
RATE = 44100

# ---- guest addresses (WINDREAM.EXE / GDIDREAM.EXE, the same in both) ----

TRIGGER_DONE = 0x6155E4  # SCENE_IsTriggerDone's flag: the LINK flag 0x10 condition
GAME_MENU_TICK = 0x4A1547  # MENU_RunGameMenu stores the time here every 40 ms while it is open
GAME_MENU_TAB = 0x4A1537  # 0 magic, 1 objects, 2 system, 3 close
GAME_MENU_PAGE = 0x4A153B  # 1: inside the tab's page
SYSTEM_ENTRY = 0x4A155B  # 0 Load, (1 Save: never selectable), 2 Options, 3 Quit
SYSTEM_OPEN = 0x4A155F  # 1: the entry's own page (the slot list) is open
MAIN_MENU_ITEM = 0x4A2EF9  # 0 new game, 1 load, 2 options, 3 quit
MAIN_MENU_PAGE = 0x4A2EED  # 1: the item's page (the slot list) is open
SLOT_SELECTED = 0x4A2F3D
SLOT_NAMES, SLOT_NAME_SIZE, SLOTS = 0x5DABF8, 22, 10
PROJECT_NAMES, LEVEL_TITLES, TITLE_SIZE, PROJECTS = 0x4A1599, 0x4A21E7, 21, 150
PLAYLIST_AT = 0x11C  # record: up to three CD track numbers, one per byte

SAVE_SIZE, ICON_SIZE, INDEX_SIZE = 11388, 8192, 340  # game<n>.dat, game<n>.ico, game.dat


def requirements():
    """Skip unless a devtools build and the two disc images are there; the cues."""
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    if "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text(errors="replace"):
        pytest.skip(f"{build} was built without WD_DEVTOOLS")
    try:
        return run.disc_sources(True, None, None)
    except Exception as error:  # no DREAMS_DISC1/2, or no single .cue beside them
        pytest.skip(f"no disc images: {error}")


@pytest.fixture(scope="module")
def cues():
    return requirements()


@pytest.fixture(scope="module")
def bank_patch(cues):
    return load("recomp/windream/debug/bank_patch.py")


@pytest.fixture(scope="module")
def audio(cues):
    module = load("recomp/windream/debug/cd_audio_check.py", register=True)
    if not module.disc_list_tool().is_file():
        pytest.skip("the disc listing tool is not built: uv run python recomp/disc/build.py")
    return module


def run_dir_of(tag):
    return recomp_env.out_dir("windream") / f"run-{tag}"


def start(name, bank=None, **options):
    """A headless disc-mode game in its own run directory, with `bank` (a
    bank_patch.Bank) as its dreams.dat. The caller closes it with
    close(remove=True)."""
    tag = f"discplay-{name}-{os.getpid()}"
    directory = run_dir_of(tag)
    shutil.rmtree(directory, ignore_errors=True)
    if bank is not None:
        bank.write(directory / "sandbox")
    return wdctl.start_game(tag=tag, headless=True, **options)


def assert_runs_clean(game):
    """Still running, and the log has no crash block and no message box."""
    assert game.process.poll() is None, game.stderr_text[-2000:]
    text = game.stderr_text
    assert "=== recomp: CRASH" not in text, text[text.index("=== recomp: CRASH") :][:3000]
    assert "MessageBox" not in text
    assert "unresolved" not in text


# ---- driving the game: each key is pressed until the game shows it took it ----


def press_until_opened(ctl, key, path, since, timeout_ms=3000, tries=6):
    """Tap a key until a file whose path contains `path` is opened after
    event `since`; the wait's answer."""
    for _ in range(tries):
        ctl.tap(key)
        try:
            return ctl.wait_until_opened(path, since=since, timeout_ms=timeout_ms)
        except wdctl.CtlError as error:
            assert "timeout" in str(error)
    pytest.fail(f"{key} never led to an open of {path}")


def press_until(ctl, key, done, what, tries=10):
    """Tap a key until done() holds (checked first: nothing is pressed when it
    already does). The menus redraw only on input, so waits are in host time."""
    for _ in range(tries):
        if done():
            return
        ctl.tap(key)
        ctl.wait(ms=250)
    assert done(), f"{key} did not lead to: {what}"


def to_main_menu(ctl):
    """The intro movie, ESC, the main menu (its background movie generic.hnm)."""
    intro = ctl.wait_until_opened("intro.hnm", since=0)
    return press_until_opened(ctl, "ESC", "generic.hnm", intro["event"])


def new_game(ctl, scene, since):
    """RETURN on the main menu's first item ("New game" re-reads dreams.dat),
    then ESC through project 0's movie if it has one, until its scene opens."""
    began = press_until_opened(ctl, "RETURN", "dreams.dat", since)
    for _ in range(6):
        try:
            return ctl.wait_until_opened(scene, since=began["event"], timeout_ms=2500)
        except wdctl.CtlError as error:
            assert "timeout" in str(error)
            ctl.tap("ESC")
    pytest.fail(f"the new game never opened {scene}")


def boot_into(ctl, scene):
    """From the start of the process to project 0's scene being loaded."""
    return new_game(ctl, scene, to_main_menu(ctl)["event"])


def complete_level_triggers(ctl):
    """Set the flag SCENE_TickTriggers sets when a level's triggers are done
    (the fight is won): a LINK with flag 0x10 and no box then fires."""
    ctl.write32(TRIGGER_DONE, 1)


def game_menu_is_open(ctl):
    before = ctl.read32(GAME_MENU_TICK)
    ctl.wait(ms=200)
    return ctl.read32(GAME_MENU_TICK) != before


def open_game_menu(ctl):
    """ESC opens the in-game menu; while a dialogue runs, ESC ends a line of it
    instead, so it is pressed until the menu's loop is seen running."""
    for _ in range(12):
        if game_menu_is_open(ctl):
            return
        ctl.tap("ESC")
        ctl.wait(ms=300)
    pytest.fail("ESC never opened the in-game menu")


def open_system_page(ctl):
    """In-game menu: RIGHT goes round the four tabs (0, 1, 3, 2), SPACE enters
    the system tab, whose list is Load, Options, Quit."""
    open_game_menu(ctl)
    press_until(ctl, "RIGHT", lambda: ctl.read32(GAME_MENU_TAB) == 2, "the system tab")
    press_until(ctl, "SPACE", lambda: ctl.read32(GAME_MENU_PAGE) == 1, "the system page")


def open_load_list_in_game(ctl):
    """The save list of the in-game menu: system page, "Load", SPACE."""
    open_system_page(ctl)
    press_until(ctl, "UP", lambda: ctl.read32(SYSTEM_ENTRY) == 0, "the Load entry")
    press_until(ctl, "SPACE", lambda: ctl.read32(SYSTEM_OPEN) == 1, "the slot list")


def open_load_list_from_main_menu(ctl):
    """Main menu: RIGHT moves from "New game" to "Load", RETURN opens its list."""
    press_until(ctl, "RIGHT", lambda: ctl.read32(MAIN_MENU_ITEM) == 1, "the Load item")
    press_until(ctl, "RETURN", lambda: ctl.read32(MAIN_MENU_PAGE) == 1, "the slot list")


def slot_names(ctl):
    table = ctl.read(SLOT_NAMES, SLOT_NAME_SIZE * SLOTS)
    cells = [table[i * SLOT_NAME_SIZE :][:SLOT_NAME_SIZE] for i in range(SLOTS)]
    return [cell.split(b"\x00", 1)[0].decode("latin-1") for cell in cells]


def load_slot(ctl, title):
    """In an open slot list (either menu): UP/DOWN to the slot with that name,
    RETURN loads it. The event number before the load, for wait_until_opened."""
    slot = slot_names(ctl).index(title)
    for _ in range(SLOTS + 2):
        selected = ctl.read32(SLOT_SELECTED)
        if selected == slot:
            break
        ctl.tap("UP" if selected > slot else "DOWN")
        ctl.wait(ms=250)
    assert ctl.read32(SLOT_SELECTED) == slot
    since = ctl.status()["seq"]
    press_until_opened(ctl, "RETURN", "data\\game\\game", since)
    return since


def level_title(ctl, project_name):
    """The name the game saves a level under: its table of 150 titles, in the
    order of its table of project names."""
    names = ctl.read(PROJECT_NAMES, TITLE_SIZE * PROJECTS)
    for i in range(PROJECTS):
        if names[i * TITLE_SIZE :][:TITLE_SIZE].split(b"\x00", 1)[0] == project_name.encode():
            return ctl.read_cstr(LEVEL_TITLES + i * TITLE_SIZE)
    raise KeyError(project_name)


def events_after(ctl, since):
    return ctl.log(since=since, max=500)["events"]


def disc_changes(events):
    return [event["text"] for event in events if event["kind"] == "disc"]


def opens(events, text, ok=None):
    return [
        event
        for event in events
        if event["kind"] == "open"
        and text.lower() in event["path"].lower()
        and (ok is None or event["ok"] == ok)
    ]


def assert_level_files_come_from(events, disc):
    """After the last change of disc in `events`: every file read was found on
    that disc or in the data directory, except the game's own probes (a model
    is tried as .DSN, then .DAN; a scene's .DAN is optional)."""
    last = max((i for i, event in enumerate(events) if event["kind"] == "disc"), default=-1)
    reads = [e for e in events[last + 1 :] if e["kind"] == "open" and not e["write"]]
    found = {Path(e["path"].replace("\\", "/")).stem.upper() for e in reads if e["ok"]}
    for event in reads:
        stem = Path(event["path"].replace("\\", "/")).stem.upper()
        if event["ok"]:
            on_disc = event["host"].startswith("disc")
            assert not on_disc or event["host"].startswith(f"disc{disc}:"), event
        else:
            below_install = event["path"].upper().startswith("CRYO\\DREAMS\\")
            assert stem in found or below_install, f"not found on disc {disc}: {event}"


def playlist_of(bank, slot):
    return struct.unpack_from("<I", bank.records[slot], PLAYLIST_AT)[0]


def data_files(game):
    """What the game's data directory holds, as relative lower-case paths."""
    sandbox = game.run_dir / "sandbox"
    return {
        path.relative_to(sandbox).as_posix().lower(): path.stat().st_size
        for path in sandbox.rglob("*")
        if path.is_file()
    }


def colours(bmp_path):
    """How many different pixel values a 24-bit BMP has (sampled)."""
    data = bmp_path.read_bytes()
    start = struct.unpack_from("<I", data, 10)[0]
    return len({data[i : i + 3] for i in range(start, len(data) - 3, 3 * 7)})


# ---- the bank tool ----


def test_bank_tool_round_trips_and_patches(bank_patch, tmp_path):
    for disc in (1, 2):
        bank = bank_patch.Bank.from_disc(disc)
        assert bank.data() == bank.source  # the packer reproduces the retail file
    bank = bank_patch.Bank.from_disc(1)
    assert [bank.level(slot) for slot in (0, 26, 116, 84, 44, 113, 135)] == [1, 2, 3, 1, 2, 3, 4]
    original = bytes(bank.records[116])
    bank.copy(116, 0)
    assert bank.parsed(0).name == "Project116" and bytes(bank.records[0]) == original
    bank = bank_patch.Bank.from_disc(1)
    bank.copy(84, 0, keep_name=True)
    assert bank.parsed(0).name == "Project0" and bank.parsed(0).scene == "E25_RIDE.DSN"
    lo, hi = bank.link_box(0, bank.link_slot(0, 44))
    centre = bank.spawn_in_link(0, 44)
    assert all(lo[i] < centre[i] < hi[i] for i in range(3))
    assert bank.parsed(0).spawn_position == centre
    bank.set_link(0, 0, 26)
    assert bank.parsed(0).links[0].destination == "Project26"
    again = bank_patch.Bank(bank.data())
    assert [bytes(r) for r in again.records] == [bytes(r) for r in bank.records]
    with pytest.raises(ValueError, match="output tree"):
        bank.write(tmp_path)  # game data is written under out/ only


# ---- A. disc 2 levels load and run ----

# (project, frames to keep it running). Both level numbers of disc 2 (3 and
# 4), nine different scenes between this and the other tests.
DISC2_PROJECTS = [(116, 300), (4, 80), (29, 80), (57, 80), (79, 80)]


@pytest.mark.parametrize("source, frames", DISC2_PROJECTS)
def test_new_game_into_a_disc2_project(bank_patch, tmp_path, source, frames):
    bank = bank_patch.Bank.from_disc(1)
    level, scene, track = bank.level(source), bank.parsed(source).scene, playlist_of(bank, source)
    assert level in (3, 4)
    if source == 116:
        assert level == 3 and scene == "H15ARENE.DSN" and track == 2
    bank.copy(source, 0)
    game = start(f"a{source}", bank)
    try:
        ctl = game.ctl
        opened = boot_into(ctl, scene)
        assert opened["open_ok"] and opened["host"].lower() == f"disc2:data/3dc/{scene}".lower()
        events = events_after(ctl, 0)
        assert disc_changes(events) == ["active disc 1 -> 2"]
        # CD_PrepareLevel: which disc is in (1CD.ID), then the prompt's two polls of 2CD.ID
        markers = [(e["path"], e["host"][:6], e["ok"]) for e in opens(events, "CD.ID")[1:]]
        assert markers == [
            ("DATA\\1CD.ID", "disc1:", True),
            ("DATA\\2CD.ID", "disc1:", False),
            ("DATA\\2CD.ID", "disc2:", True),
        ]
        if track:
            ctl.wait_until_cd_track(track & 0xFF, timeout_ms=20000)
        ctl.wait(frames=frames, ms=60000)
        project = ctl.current_project()
        assert (project["name"], project["level"]) == (f"Project{source}", level)
        assert project["objet0_asset"] == scene
        status = ctl.status()
        assert status["disc"] == 2
        if track:
            assert (status["cd_track"], status["cd_disc"]) == (track & 0xFF, 2)
            assert f"[cd] play track {track & 0xFF} (disc 2)" in game.stderr_text
        assert_level_files_come_from(events_after(ctl, 0), 2)
        shot = ctl.screenshot(tmp_path / "level.bmp")
        assert (shot["width"], shot["height"]) == (640, 480)
        assert colours(tmp_path / "level.bmp") > 200  # a rendered scene, not a flat screen
        assert_runs_clean(game)
    finally:
        game.close(remove=True)
    assert not game.run_dir.exists()


# ---- B, C, D, E: the link from disc 1 to disc 2, and what follows from it ----


class LinkSession:
    """One game that starts in a copy of Project26 (disc 1); the tests below
    run in file order and each leaves it where the next one starts."""

    def __init__(self, game):
        self.game = game
        self.titles = {}
        self.dump = None  # the mixer's output across the link
        self.dump_started = None
        self.link_events = []


@pytest.fixture(scope="module")
def link(bank_patch):
    bank = bank_patch.Bank.from_disc(1)
    assert bank.parsed(26).scene == "E01GROTT.DSN" and bank.level(26) == 2
    assert bank.parsed(26).links[1].destination == "Project116"
    assert bank.link_box(26, bank.link_slot(26, 116)) == ((0, 0, 0), (0, 0, 0))  # no box
    bank.copy(26, 0)
    session = LinkSession(start("link", bank))
    try:
        yield session
    finally:
        session.game.close(remove=True)
    assert not session.game.run_dir.exists()


def test_link_from_disc1_to_disc2(link):
    """Project26 loads from disc 1; when its triggers are done its link asks
    for Project116, the prompt changes the disc, Project116 loads from disc 2."""
    game, ctl = link.game, link.game.ctl
    boot_into(ctl, "E01GROTT.DSN")
    ctl.wait_until_cd_track(5, timeout_ms=20000)
    before = ctl.current_project()
    assert (before["name"], before["level"], before["objet0_asset"]) == (
        "Project26", 2, "E01GROTT.DSN",
    )  # fmt: skip
    status = ctl.status()
    assert (status["disc"], status["cd_track"], status["cd_disc"]) == (1, 5, 1)
    assert disc_changes(events_after(ctl, 0)) == []
    link.titles = {name: level_title(ctl, name) for name in ("Project26", "Project116")}

    link.dump = game.run_dir / "link.wav"
    link.dump_started = ctl.audio_dump(link.dump)
    since = link.dump_started["seq"]
    ctl.wait(frames=10)
    assert ctl.current_project()["name"] == "Project26"  # the link waits for its condition
    complete_level_triggers(ctl)
    opened = ctl.wait_until_opened("H15ARENE.DSN", since=since, timeout_ms=30000)
    assert opened["open_ok"] and opened["host"] == "disc2:data/3dc/H15ARENE.DSN"
    ctl.wait_until_cd_track(2, timeout_ms=20000)
    ctl.wait(ms=5000)
    ctl.audio_dump_stop()

    after = ctl.current_project()
    assert (after["name"], after["level"], after["objet0_asset"]) == (
        "Project116", 3, "H15ARENE.DSN",
    )  # fmt: skip
    status = ctl.status()
    assert (status["disc"], status["cd_track"], status["cd_disc"]) == (2, 2, 2)
    link.link_events = events = events_after(ctl, since)
    assert disc_changes(events) == ["active disc 1 -> 2"]
    order = [
        (e["kind"], e.get("path") or e.get("text"), e.get("ok"))
        for e in events
        if e["kind"] in ("disc", "cd") or "CD.ID" in e.get("path", "")
    ]
    assert order == [
        ("cd", "stop", None),
        ("open", "DATA\\1CD.ID", True),
        ("open", "DATA\\2CD.ID", False),
        ("disc", "active disc 1 -> 2", None),
        ("open", "DATA\\2CD.ID", True),
        ("cd", "play track 2 (disc 2)", None),
    ]
    assert_level_files_come_from(events, 2)
    assert_runs_clean(game)


@pytest.mark.xfail(
    strict=True,
    reason="retail order, not a host fault: SCENE_CheckExits opens the destination's movie "
    "(Project116: data\\hnm\\BIENMAL.HNM, on disc 2 only) before CD_PrepareLevel asks for the "
    "disc, so it is looked for on disc 1 and skipped; the same happens from a save or new game",
)
def test_the_movie_of_the_link_plays(link):
    movie = opens(link.link_events, "BIENMAL.HNM")
    assert movie, "the link did not ask for its movie"
    assert movie[0]["ok"], movie[0]


def test_music_after_the_link_is_disc2s_track(link, cues, audio):
    """The dump across the link holds disc 2's track 2 from its INDEX 01,
    starting when the game played it, and not disc 1's track of that number."""
    play = [e for e in link.link_events if e.get("text") == "play track 2 (disc 2)"][0]
    expected = round((play["ms"] - link.dump_started["ms"]) * RATE / 1000)
    dump = audio.read_wav(link.dump)
    ours = audio.track_table(cues[1])[2]
    best = best_window(audio, dump, ours, expected)
    assert best.score > 0.9, best
    assert abs(best.lag - expected) < ONSET_TOLERANCE, (best, expected)
    for cue, number in ((cues[0], 2), (cues[1], 3)):  # disc 1's track 2, and the next one
        other = audio.track_pcm(audio.track_table(cue)[number], 0, 6 * RATE)
        wrong = audio.align(dump, other, expected, at=best.track_frame)
        assert wrong.score < 0.5, (cue, number, wrong)


def test_the_game_saves_on_entering_a_level(link):
    """One save per level entered, in the data directory, at the retail sizes;
    nothing else is written there (dreams.dat is the test's own)."""
    files = data_files(link.game)
    assert files == {
        "dreams.dat": files["dreams.dat"],
        "cryo/dreams/data/game/game.dat": INDEX_SIZE,
        "cryo/dreams/data/game/game0.dat": SAVE_SIZE,
        "cryo/dreams/data/game/game0.ico": ICON_SIZE,
        "cryo/dreams/data/game/game1.dat": SAVE_SIZE,
        "cryo/dreams/data/game/game1.ico": ICON_SIZE,
    }
    index = (link.game.run_dir / "sandbox/CRYO/DREAMS/data/game/game.dat").read_bytes()
    names = [index[i * 22 :][:22].split(b"\x00", 1)[0].decode("latin-1") for i in range(10)]
    assert names[:3] == [link.titles["Project26"], link.titles["Project116"], ""]
    for number, project in ((0, "Project26"), (1, "Project116")):
        save = (link.game.run_dir / f"sandbox/CRYO/DREAMS/data/game/game{number}.dat").read_bytes()
        assert save[0x2884:][:32].split(b"\x00", 1)[0] == project.encode()  # the level it reloads


def test_the_game_menu_has_no_save_entry(link):
    """The system page steps from Load to Options and back: entry 1, the save
    the retail texts describe, cannot be selected."""
    ctl = link.game.ctl
    open_system_page(ctl)
    press_until(ctl, "UP", lambda: ctl.read32(SYSTEM_ENTRY) == 0, "the Load entry")
    seen = [ctl.read32(SYSTEM_ENTRY)]
    for key in ("DOWN", "DOWN", "DOWN", "UP", "UP", "UP"):
        ctl.tap(key)
        ctl.wait(ms=250)
        seen.append(ctl.read32(SYSTEM_ENTRY))
    assert 1 not in seen and {0, 2, 3} <= set(seen), seen
    assert seen[-1] == 0


def test_loading_a_disc1_save_changes_back_to_disc1(link):
    """In play on disc 2: the menu's Load, the save made in Project26."""
    game, ctl = link.game, link.game.ctl
    open_load_list_in_game(ctl)
    assert set(slot_names(ctl)) == {link.titles["Project26"], link.titles["Project116"], ""}
    since = load_slot(ctl, link.titles["Project26"])
    opened = ctl.wait_until_opened("E01GROTT.DSN", since=since, timeout_ms=30000)
    assert opened["host"] == "disc1:data/3dc/E01GROTT.DSN"
    ctl.wait_until_cd_track(5, timeout_ms=20000)
    project = ctl.current_project()
    assert (project["name"], project["level"]) == ("Project26", 2)
    status = ctl.status()
    assert (status["disc"], status["cd_track"], status["cd_disc"]) == (1, 5, 1)
    events = events_after(ctl, since)
    assert disc_changes(events) == ["active disc 2 -> 1"]
    # the question "is disc 1 in?" is already the first miss: one poll changes the disc
    assert [(e["path"], e["ok"]) for e in opens(events, "CD.ID")] == [
        ("DATA\\1CD.ID", False),
        ("DATA\\1CD.ID", True),
    ]
    assert_level_files_come_from(events, 1)
    assert_runs_clean(game)


def test_loading_a_disc2_save_changes_to_disc2(link):
    """And on disc 1 again: the save made in Project116."""
    game, ctl = link.game, link.game.ctl
    open_load_list_in_game(ctl)
    since = load_slot(ctl, link.titles["Project116"])
    opened = ctl.wait_until_opened("H15ARENE.DSN", since=since, timeout_ms=30000)
    assert opened["host"] == "disc2:data/3dc/H15ARENE.DSN"
    ctl.wait_until_cd_track(2, timeout_ms=20000)
    project = ctl.current_project()
    assert (project["name"], project["level"]) == ("Project116", 3)
    assert ctl.status()["disc"] == 2
    events = events_after(ctl, since)
    assert disc_changes(events) == ["active disc 1 -> 2"]
    assert_level_files_come_from(events, 2)
    assert_runs_clean(game)


def test_a_fresh_process_loads_the_saves_from_the_main_menu(link):
    """A new process on the same data directory, with the disc's own project
    bank: the main menu's Load lists both saves; the disc 2 one changes the
    disc while it loads, and the disc 1 one then changes it back."""
    link.game.close()
    titles = link.titles
    saves = {name: size for name, size in data_files(link.game).items() if "/game/" in name}
    (link.game.run_dir / "sandbox" / "dreams.dat").unlink()
    tag = link.game.run_dir.name.removeprefix("run-")
    game = link.game = wdctl.start_game(tag=tag, headless=True)
    ctl = game.ctl
    assert {name: size for name, size in data_files(game).items()} == saves
    to_main_menu(ctl)
    assert ctl.status()["disc"] == 1
    open_load_list_from_main_menu(ctl)
    assert set(slot_names(ctl)) == {titles["Project26"], titles["Project116"], ""}
    since = load_slot(ctl, titles["Project116"])
    opened = ctl.wait_until_opened("H15ARENE.DSN", since=since, timeout_ms=30000)
    assert opened["host"] == "disc2:data/3dc/H15ARENE.DSN"
    ctl.wait_until_cd_track(2, timeout_ms=20000)
    project = ctl.current_project()
    assert (project["name"], project["level"]) == ("Project116", 3)
    assert disc_changes(events_after(ctl, since)) == ["active disc 1 -> 2"]

    open_load_list_in_game(ctl)
    since = load_slot(ctl, titles["Project26"])
    opened = ctl.wait_until_opened("E01GROTT.DSN", since=since, timeout_ms=30000)
    assert opened["host"] == "disc1:data/3dc/E01GROTT.DSN"
    assert ctl.current_project()["name"] == "Project26"
    assert disc_changes(events_after(ctl, since)) == ["active disc 2 -> 1"]
    assert all("/game/" in name for name in data_files(game))  # only saves were written
    assert_runs_clean(game)


# ---- C. a link from disc 2 back to disc 1 ----


def test_link_from_disc2_back_to_disc1(bank_patch):
    """No retail link leads back; here Project116's own link (no box, flags
    0x11, as the one that led to it) points at Project26 instead."""
    bank = bank_patch.Bank.from_disc(1)
    bank.copy(116, 0)
    bank.set_link(0, bank.link_slot(0, 140), 26)
    game = start("back", bank)
    try:
        ctl = game.ctl
        boot_into(ctl, "H15ARENE.DSN")
        ctl.wait_until_cd_track(2, timeout_ms=20000)
        assert ctl.status()["disc"] == 2 and ctl.current_project()["level"] == 3
        since = ctl.status()["seq"]
        complete_level_triggers(ctl)
        opened = ctl.wait_until_opened("E01GROTT.DSN", since=since, timeout_ms=30000)
        assert opened["host"] == "disc1:data/3dc/E01GROTT.DSN"
        ctl.wait_until_cd_track(5, timeout_ms=20000)
        project = ctl.current_project()
        assert (project["name"], project["level"]) == ("Project26", 2)
        status = ctl.status()
        assert (status["disc"], status["cd_track"], status["cd_disc"]) == (1, 5, 1)
        assert disc_changes(events_after(ctl, 0)) == ["active disc 1 -> 2", "active disc 2 -> 1"]
        assert_level_files_come_from(events_after(ctl, since), 1)
        ctl.wait(frames=50, ms=30000)
        assert_runs_clean(game)
    finally:
        game.close(remove=True)


# ---- B. the two links that cross a level number on one disc ----


def test_level_1_to_2_link_stays_on_disc1(bank_patch):
    """Project84 to Project44: a link with a box; the player is put inside it."""
    bank = bank_patch.Bank.from_disc(1)
    assert (bank.level(84), bank.level(44)) == (1, 2)
    bank.copy(84, 0)
    bank.spawn_in_link(0, 44)
    game = start("l12", bank)
    try:
        ctl = game.ctl
        boot_into(ctl, "E25_RIDE.DSN")
        opened = ctl.wait_until_opened("H15ARENE.DSN", since=0, timeout_ms=30000)
        assert opened["host"] == "disc1:data/3dc/H15ARENE.DSN"
        ctl.wait_until_cd_track(2, timeout_ms=20000)
        project = ctl.current_project()
        assert (project["name"], project["level"]) == ("Project44", 2)
        events = events_after(ctl, 0)
        assert disc_changes(events) == [] and ctl.status()["disc"] == 1
        assert not opens(events, "2CD.ID")  # the other disc was never asked for
        assert ctl.status()["cd_disc"] == 1
        assert_runs_clean(game)
    finally:
        game.close(remove=True)


def test_level_3_to_4_link_stays_on_disc2(bank_patch):
    """Project113 to Project135: no box, the trigger condition; its movie
    (RONALD.HNM) is on the disc that is in, and plays."""
    bank = bank_patch.Bank.from_disc(1)
    assert (bank.level(113), bank.level(135)) == (3, 4)
    bank.copy(113, 0)
    game = start("l34", bank)
    try:
        ctl = game.ctl
        boot_into(ctl, "F38ARENE.DSN")
        ctl.wait(frames=30)
        assert ctl.current_project()["name"] == "Project113"
        since = ctl.status()["seq"]
        complete_level_triggers(ctl)
        movie = ctl.wait_until_opened("RONALD.HNM", since=since, timeout_ms=20000)
        assert movie["open_ok"] and movie["host"].startswith("disc2:")
        opened = press_until_opened(ctl, "ESC", "L12_TORN.DSN", since, timeout_ms=4000)
        assert opened["host"] == "disc2:data/3dc/L12_TORN.DSN"
        ctl.wait_until_cd_track(6, timeout_ms=20000)
        project = ctl.current_project()
        assert (project["name"], project["level"]) == ("Project135", 4)
        assert disc_changes(events_after(ctl, 0)) == ["active disc 1 -> 2"]  # the new game's
        assert not opens(events_after(ctl, since), "CD.ID", ok=False)[1:]
        assert ctl.status()["cd_disc"] == 2
        assert_runs_clean(game)
    finally:
        game.close(remove=True)


# ---- E. the music, located in the images ----

# The dump and the "[cd] play" event are timed by different clocks: the event
# by the host's millisecond clock at the game's MCI_PLAY, the dump by the
# mixer's 1024-frame blocks (23 ms) on its own thread. A track that started at
# its pregap (INDEX 00) would be 2 s late, a wrong start by a sector 13 ms.
ONSET_TOLERANCE = RATE // 5


def best_window(audio, dump, track, expected):
    """The best alignment among windows 1 to 4 s into the track: a voice line
    or an effect can cover the music in one of them."""
    pcm = audio.track_pcm(track, 0, 6 * RATE)
    matches = [audio.align(dump, pcm, expected, at=at * RATE) for at in (1, 2, 3, 4)]
    return max(matches, key=lambda match: match.score)


def test_first_level_music_is_track_9_from_its_index_01(cues, audio):
    """Disc 1, the first level: the dump holds track 9's samples (not track 8's
    or 10's), from the first one after the pregap, starting at the play event."""
    game = start("music")
    try:
        ctl = game.ctl
        menu = to_main_menu(ctl)
        movie = press_until_opened(ctl, "RETURN", "tete_e", menu["event"])
        wav = game.run_dir / "music.wav"
        started = ctl.audio_dump(wav)
        press_until_opened(ctl, "ESC", "H18ANGKR.DSN", movie["event"])
        ctl.wait_until_cd_track(9, timeout_ms=20000)
        ctl.wait(ms=5500)
        ctl.audio_dump_stop()
        play = [e for e in events_after(ctl, 0) if e.get("text") == "play track 9 (disc 1)"]
        assert len(play) == 1
        expected = round((play[0]["ms"] - started["ms"]) * RATE / 1000)
        dump = audio.read_wav(wav)
        table = audio.track_table(cues[0])
        assert table[9].offset == 2 * RATE * 4  # the file starts with a 2 s pregap (INDEX 00)
        assert audio.loudness(audio.file_pcm(table[9], 0, 2 * RATE)) == 0  # which is silence
        pcm = audio.track_pcm(table[9], 0, 6 * RATE)
        first, second = (audio.align(dump, pcm, expected, at=at * RATE) for at in (1, 2))
        assert first.score > 0.98 and second.score > 0.98, (first, second)
        assert first.lag == second.lag  # one start, sample for sample
        assert abs(first.lag - expected) < ONSET_TOLERANCE, (first, expected)
        assert first.exact > 0.9  # the mixer adds the track at its own level
        for number in (8, 10):
            other = audio.track_pcm(table[number], 0, 6 * RATE)
            assert audio.align(dump, other, expected, at=RATE).score < 0.5
        # nothing of the track was output before its start
        assert audio.loudness(dump[: 2 * first.lag]) < 1
        assert_runs_clean(game)
    finally:
        game.close(remove=True)


def test_a_track_ends_where_the_next_begins_in_the_same_file(cues, audio, bank_patch):
    """A cue whose second file holds two tracks: disc 2's track 2 cut to 3 s,
    and a track 3 that is the rest of the same music. The game plays track 2
    (Project116); playback must stop at 3 s, not run on into "track 3", and
    the game's playlist then starts the track again. (3 s: the level's first
    voice line, which would cover the music, comes a second later.)"""
    seconds = 3
    table = audio.track_table(cues[1])
    data, music = table[1], table[2]
    game = None
    work = recomp_env.out_dir("windream") / f"discplay-cue-{os.getpid()}"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    try:
        try:
            names = [
                os.path.relpath(track.path, work).replace("\\", "/") for track in (data, music)
            ]
        except ValueError:  # another drive: the cue cannot name the files
            pytest.skip("the disc image is on another drive than the output tree")
        cue = work / "two-in-one.cue"
        cue.write_text(
            f'FILE "{names[0]}" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
            f'FILE "{names[1]}" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n'
            f"    INDEX 01 00:02:00\n  TRACK 03 AUDIO\n    INDEX 01 00:{2 + seconds:02d}:00\n",
            encoding="utf-8",
        )
        cut = audio.track_table(cue)
        assert (cut[2].offset, cut[2].length) == (music.offset, seconds * RATE * 4)
        assert cut[3].offset == music.offset + seconds * RATE * 4

        bank = bank_patch.Bank.from_disc(1)
        bank.copy(116, 0)
        game = start("cue", bank, discs=False, args=["--disc1", cues[0], "--disc2", str(cue)])
        ctl = game.ctl
        menu = to_main_menu(ctl)
        wav = game.run_dir / "end.wav"
        started = ctl.audio_dump(wav)
        new_game(ctl, "H15ARENE.DSN", menu["event"])
        ctl.wait_until_cd_track(2, timeout_ms=20000)
        deadline = time.monotonic() + 30
        while True:
            plays = [e for e in events_after(ctl, 0) if e.get("text") == "play track 2 (disc 2)"]
            if len(plays) >= 2:
                break
            assert time.monotonic() < deadline, "the track never ended"
            ctl.wait(ms=500)
        ctl.wait(ms=1500)
        ctl.audio_dump_stop()
        assert_runs_clean(game)

        dump = audio.read_wav(wav)
        pcm = audio.track_pcm(music, 0, (seconds + 4) * RATE)  # the real track, past the cut
        first, again = (round((e["ms"] - started["ms"]) * RATE / 1000) for e in plays[:2])
        start_match = audio.align(dump, pcm, first, at=RATE)
        assert start_match.score > 0.98, start_match
        assert abs(start_match.lag - first) < ONSET_TOLERANCE
        lag = start_match.lag
        # the last second before the cut is the track's; what follows the cut is not
        assert audio.score_at(dump, pcm, lag, (seconds - 1) * RATE) > 0.98
        assert audio.score_at(dump, pcm, lag, seconds * RATE, RATE // 2) < 0.5
        # the game saw the track end and played it again from its start
        assert again - lag >= seconds * RATE
        restart = audio.align(dump, pcm, again, at=RATE // 4, length=RATE // 2)
        assert restart.score > 0.9 and abs(restart.lag - again) < ONSET_TOLERANCE, restart
        assert restart.lag - lag >= seconds * RATE
    finally:
        if game is not None:
            game.close(remove=True)
        shutil.rmtree(work, ignore_errors=True)
