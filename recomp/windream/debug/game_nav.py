"""Finding the way through the running game over the control channel.

Helpers for a `wdctl.Ctl`: boot to a level, open the game's menus, load a
save, and read back which files, disc and CD track the game used. Each key is
pressed until the game shows it took it (a file opened, or a guest variable
changed), never on a fixed schedule.

    import game_nav as nav                      # recomp/windream/debug on sys.path
    with nav.wdctl.start_game(tag="play", headless=True) as game:
        ctl = game.ctl
        nav.boot_into(ctl, "H18ANGKR.DSN")      # new game, first level loaded
        nav.open_load_list_in_game(ctl)
        print(nav.slot_names(ctl))

Take `wdctl` from here (`nav.wdctl`) rather than loading it a second time:
`CtlError` must be the same class for the retries below to catch it.

Facts of the retail game these rely on (WINDREAM.EXE and GDIDREAM.EXE, the
same addresses in both; read-only Ghidra decompilation and live reads,
2026-10-01, docs/research/install-and-discs.md "Seen in play"):

- The game has no "save" command. MENU_HandleSystemPageInput (0x431603) steps
  over entry 1 of its list, so the in-game menu offers Load, Options and Quit;
  the save is written by GAME_StartLevel on entering a level, into the slot
  named after the level.
- In-game "Quit" ends the process; there is no way back to the main menu.
- A LINK with an empty box fires when its conditions hold; flag 0x10 is "the
  level's triggers are done" (SCENE_IsTriggerDone, the dword at 0x6155e4).
- The menus redraw only on input, so waits inside them are in host time.

Development only, like the channel itself (recomp/windream/devtools).
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wdctl  # noqa: E402

# ---- guest addresses ----

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


class NavError(RuntimeError):
    """The game did not get where a helper was taking it."""


def _timed_out(error: Exception) -> None:
    """Let a wait's timeout pass; any other channel error is a real one."""
    if "timeout" not in str(error):
        raise error


# ---- keys: pressed until the game shows it took them ----


def press_until_opened(ctl, key, path, since, timeout_ms=3000, tries=6):
    """Tap a key until a file whose path contains `path` is opened after
    event `since`; the wait's answer."""
    for _ in range(tries):
        ctl.tap(key)
        try:
            return ctl.wait_until_opened(path, since=since, timeout_ms=timeout_ms)
        except wdctl.CtlError as error:
            _timed_out(error)
    raise NavError(f"{key} never led to an open of {path}")


def press_until(ctl, key, done, what, tries=10):
    """Tap a key until done() holds (checked first: nothing is pressed when it
    already does)."""
    for _ in range(tries):
        if done():
            return
        ctl.tap(key)
        ctl.wait(ms=250)
    if not done():
        raise NavError(f"{key} did not lead to: {what}")


# ---- from the start of the process to a level ----


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
            _timed_out(error)
            ctl.tap("ESC")
    raise NavError(f"the new game never opened {scene}")


def boot_into(ctl, scene):
    """From the start of the process to project 0's scene being loaded. With
    the disc's own bank the scene is "H18ANGKR.DSN"; with a bank from
    bank_patch.py it is the scene of whatever record is in slot 0."""
    return new_game(ctl, scene, to_main_menu(ctl)["event"])


def complete_level_triggers(ctl):
    """Set the flag SCENE_TickTriggers sets when a level's triggers are done
    (the fight is won): a LINK with flag 0x10 and no box then fires."""
    ctl.write32(TRIGGER_DONE, 1)


# ---- the in-game menu and the main menu ----


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
    raise NavError("ESC never opened the in-game menu")


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
    """The ten save slots' names, in the order the open list shows them (the
    game sorts them by recency, so pick a slot by name, not by position)."""
    table = ctl.read(SLOT_NAMES, SLOT_NAME_SIZE * SLOTS)
    cells = [table[i * SLOT_NAME_SIZE :][:SLOT_NAME_SIZE] for i in range(SLOTS)]
    return [cell.split(b"\x00", 1)[0].decode("latin-1") for cell in cells]


def load_slot(ctl, title):
    """In an open slot list (either menu): UP/DOWN to the slot with that name,
    RETURN loads it. The event number before the load, for wait_until_opened."""
    names = slot_names(ctl)
    if title not in names:
        raise NavError(f"no save named {title!r}; the slots are {names}")
    slot = names.index(title)
    for _ in range(SLOTS + 2):
        selected = ctl.read32(SLOT_SELECTED)
        if selected == slot:
            break
        ctl.tap("UP" if selected > slot else "DOWN")
        ctl.wait(ms=250)
    if ctl.read32(SLOT_SELECTED) != slot:
        raise NavError(f"could not select slot {slot} ({title!r})")
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


# ---- what the game did: the channel's event log ----


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
