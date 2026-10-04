"""The DOS keys of Develop (spec 008 phase 2): keys typed through the host's own key path.

The control channel's `type` command pushes SDL key and text events, as a keyboard
would, so they go through host_pump and dev_keys.c (the `key` command sets the key
state directly and would bypass both). One headless Develop game (software renderer,
WD_KEYMAP's WASD preset, to check that the editor suspends it) is booted into project
0; the live tests run in file order on it. Addresses are retail's; the editor's
semaphores are WorksGetEditor_'s (0x44c625), set by sceneKeyboard_ (0x44c28c).

`A` on the full bank and the bank itself are tests/recomp/test_editor_bank.py (phase 4).
`Z` then a mesh pick is tests/recomp/test_editor_pickers.py (phase 3). The phase 7
tools behind `6 7` and keypad 6-9 are tests/recomp/test_dev_tools.py; `r R` and
keypad 0 are phase 7b.
"""

import codecs
import importlib.util
import os
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DEV_KEYS = ROOT / "recomp/windream/host/sdl/dev_keys.c"


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
HANDLER, GAME_TICK_FRAME = 0x626F74, 0x416D45
KEY_STATE = 0x6308D8  # + virtual key: bit 0 held (INPUT_PollKeyboard 0x440757)
CAMERA_MODE = 0x52C874  # byte: 7 free (CAM_ToggleFree 0x40b386), 6 overhead
READOUT, HUD_ON, OBJECT_HUD = 0x49D5C0, 0x49D5D4, 0x49D5D0
SEMA_PROJECT_CREATE = 0x4A46B8  # A
SEMA_OBJET_LOAD = 0x4A46CC  # S
SEMA_LOAD_MESH = 0x4A4718  # 2
WORK_LINK, WORK_OBJET = 0x65D644, 0x65F8C4  # _CurrentSceneLinkS, _CurrentSceneObjetS
CLIP_OBJET = 0x65D544  # PushCurrentSceneObjet_ (0x448aa5) copies the working objet here
PROJECT = 0x65FB04  # _CurrentSceneS: links at +0x200 (16 x 0x40), objets at +0x600 (16 x 0xc0)
SAVE_PTR = 0x661D94  # _LoadSaveSceneSPtr
SCRATCH = 0x65B344  # _CurrentScene2S, where retail's Q binds it


def test_the_cp850_table_is_cp850():
    """dev_keys.c maps typed Unicode to the DOS build's code page: its table is Python's cp850."""
    text = DEV_KEYS.read_text()
    body = text[text.index("g_cp850[128] = {") :]
    body = body[: body.index("};")]
    table = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4})", body)]
    assert len(table) == 128
    assert table == [ord(codecs.decode(bytes([0x80 + i]), "cp850")) for i in range(128)]
    for char, code in (("§", 0xF5), ("µ", 0xE6), ("ù", 0x97)):
        assert table[code - 0x80] == ord(char)


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
    game = wdctl.start_game(
        tag=f"editor-keys-{os.getpid()}",
        args=args,
        wait=60.0,
        extra_env={"WD_KEYMAP": "W=UP,A=LEFT,S=DOWN,D=RIGHT"},
    )
    yield game
    game.close(remove=True)


@pytest.fixture(scope="module")
def ctl(game):
    ctl = game.ctl
    nav.boot_into(ctl, "H18ANGKR.DSN")
    # The opening dialogue ("You are Duncan, the chosen one.") runs under CTRL_Dispatcher
    # a few seconds after the level starts, and keys typed then are not in a level.
    try:
        ctl.wait_until_mem(HANDLER, "==", 0x40E75C, timeout_ms=20000)
    except wdctl.CtlError:
        pass
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)
    ctl.wait(frames=10)
    return ctl


def held(ctl, vk):
    return ctl.read8(KEY_STATE + vk) & 1


def press(ctl, key, text=None, mods=None, ms=500):
    """Type a key and read the game's key state while it is held."""
    ctl.type(key, text, mods, ms)
    ctl.wait(ms=250)
    state = ctl.read(KEY_STATE, 256)
    ctl.wait(ms=ms)
    ctl.wait(frames=3)
    return state


def editor(ctl, on):
    ctl.write32(EDITOR, 1 if on else 0)
    ctl.wait(frames=2)


def test_the_key_list_is_printed(game, ctl):
    out = (game.run_dir / "stdout.txt").read_text(errors="replace")
    assert "Develop: the keys" in out
    assert "open and close the editor" in out


def test_bang_toggles_the_editor_and_leaves_the_links(ctl):
    editor(ctl, False)
    links = ctl.read(WORK_LINK, 0x80) + ctl.read(PROJECT + 0x200, 0x400)
    state = press(ctl, "1", "!", "shift")
    assert ctl.read32(EDITOR) == 1
    assert not state[ord("1")] & 1, "Shift+1 reached the game as key 1 (item slot 1)"
    press(ctl, "1", "!", "shift")
    assert ctl.read32(EDITOR) == 0
    assert ctl.read(WORK_LINK, 0x80) + ctl.read(PROJECT + 0x200, 0x400) == links


def test_lowercase_a_is_no_editor_command(ctl):
    editor(ctl, True)
    state = press(ctl, "A", "a")
    assert ctl.read32(SEMA_PROJECT_CREATE) == 0
    assert state[ord("A")] & 1, "a reaches the game as key A (WD_KEYMAP suspended: not LEFT)"
    assert not state[0x25] & 1


def test_the_editor_letters_open_a_page_and_esc_closes_it(ctl):
    editor(ctl, True)
    state = press(ctl, "S", "S", "shift")
    assert ctl.read32(SEMA_OBJET_LOAD) == 1, "S opens the objet picker"
    assert not state[ord("S")] & 1 and not state[0x28] & 1  # hidden: neither S nor DOWN
    state = press(ctl, "Escape")
    assert ctl.read32(SEMA_OBJET_LOAD) == 0, "Esc cancels the page"
    assert not state[0x1B] & 1, "Esc on a page is hidden from the game"


def test_2_opens_load_mesh_and_not_item_slot_2(ctl, game):
    """However the page closes, WorksGetEditor_ (0x44c625) then sets the objet save and
    the quick reload unless the mesh is "EMPTY": the project save writes the working
    copy into the level's bank record (phase 4) and the level reloads from it, which
    starts its entry dialogue again. The mesh is set to "EMPTY" first so that the level
    goes on (the save and reload are tests/recomp/test_editor_bank.py's)."""
    editor(ctl, True)
    mesh = ctl.read(WORK_OBJET + 0x0C, 16)
    ctl.write(WORK_OBJET + 0x0C, b"EMPTY" + bytes(11))
    state = press(ctl, "2", "2")
    assert not state[ord("2")] & 1, "the item slot key 2 reached the game"
    assert ctl.read32(SEMA_LOAD_MESH) == 1
    press(ctl, "Escape")
    assert ctl.read32(SEMA_LOAD_MESH) == 0
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME
    ctl.write(WORK_OBJET + 0x0C, mesh)
    assert game.process.poll() is None


def test_keypad_digits_are_not_typed(ctl):
    editor(ctl, True)
    hud = ctl.read32(OBJECT_HUD)
    press(ctl, "Keypad 2", "2")
    assert ctl.read32(SEMA_LOAD_MESH) == 0, "keypad 2 typed a 2"
    assert ctl.read32(OBJECT_HUD) == (not hud), "keypad 2 is the object HUD toggle"
    press(ctl, "Keypad 2", "2")
    assert ctl.read32(OBJECT_HUD) == hud


def test_objet_copy_then_ctrl_shift_2_pastes_a_new_objet(ctl):
    """`/` copies the working objet; Ctrl+Shift+2 posts the paste (0xf5), which
    creates a new OBJET with the copied data and its own name (0x448ada). Retail's
    paste calls Create when no project is bound; the host bank (phase 4) binds the
    level's record before the editor runs, so it does not."""
    editor(ctl, True)
    bound = ctl.read32(SAVE_PTR)
    assert bound and bound != SCRATCH, "no bank record bound to the level (phase 4)"
    in_use = [ctl.read32(PROJECT + 0x600 + i * 0xC0 + 0x34) & 1 for i in range(16)]
    working = bytearray(ctl.read(PROJECT + 0x600, 0xC0))  # OBJET0, the scene object
    working[0x40:0x44] = b"P2KY"  # a marker in the record's data
    ctl.write(WORK_OBJET, bytes(working))
    press(ctl, "/", "/", "shift")
    assert ctl.read(CLIP_OBJET, 0xC0) == bytes(working)
    state = press(ctl, "2", None, "ctrl+shift")
    assert not state[ord("2")] & 1
    after = [ctl.read32(PROJECT + 0x600 + i * 0xC0 + 0x34) & 1 for i in range(16)]
    new = [i for i in range(16) if after[i] and not in_use[i]]
    assert len(new) == 1, (in_use, after)
    slot = ctl.read(PROJECT + 0x600 + new[0] * 0xC0, 0xC0)
    assert slot[:16].split(b"\0")[0] == f"OBJET{new[0]}".encode()
    assert slot[16:] == bytes(working)[16:]
    assert ctl.read32(SAVE_PTR) == bound and ctl.read_cstr(PROJECT, 16) == "Project0"


def test_developer_keys_with_the_editor_off(ctl):
    editor(ctl, False)
    readout = ctl.read32(READOUT)
    press(ctl, "8", "8")
    assert ctl.read32(READOUT) == (not readout)
    press(ctl, "8", "8")
    assert ctl.read32(READOUT) == readout
    hud = ctl.read32(HUD_ON)
    state = press(ctl, "A", "A", "shift")
    assert ctl.read32(HUD_ON) == (not hud)
    assert not state[0x25] & 1 and not state[ord("A")] & 1  # neither LEFT (WASD) nor A
    press(ctl, "A", "A", "shift")
    assert ctl.read32(HUD_ON) == hud


def test_minus_is_the_free_camera_only_with_the_editor_off(ctl):
    editor(ctl, True)
    mode = ctl.read8(CAMERA_MODE)
    assert mode != 7
    press(ctl, "-", "-")
    assert ctl.read8(CAMERA_MODE) == mode, "- acted with the editor on"
    editor(ctl, False)
    press(ctl, "-", "-")
    assert ctl.read8(CAMERA_MODE) == 7
    press(ctl, "-", "-")
    assert ctl.read8(CAMERA_MODE) != 7


def test_9_is_the_overhead_camera(ctl):
    editor(ctl, False)
    press(ctl, "9", "9")
    assert ctl.read8(CAMERA_MODE) == 6
    press(ctl, "9", "9")
    assert ctl.read8(CAMERA_MODE) != 6


def wait_handler_change(ctl, frames=60):
    for _ in range(frames):
        ctl.wait(frames=1)
        if ctl.read32(HANDLER) != GAME_TICK_FRAME:
            return True
    return False


def test_shift_d_plays_the_dialogue_test(ctl):
    """D posts message 0x40 to the HUD queue: MENJ_Dispatcher plays dialogue entry 0,
    under CTRL_Dispatcher as the opening dialogue does."""
    editor(ctl, False)
    ctl.type("D", "D", "shift", 150)
    assert wait_handler_change(ctl), "no dialogue started"
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=60000)


def test_u_grave_opens_the_object_page(ctl):
    """The AZERTY u grave types CP850 0x97, which GAME_HandleHotkeys reads as the
    object page (MENU_OpenObjectPage); no virtual key reaches it in Play."""
    editor(ctl, False)
    ctl.type("'", "ù", None, 150)
    assert wait_handler_change(ctl, 30), "no page opened"
    ctl.wait(frames=10)
    ctl.type("Escape", None, None, 150)
    ctl.wait_until_mem(HANDLER, "==", GAME_TICK_FRAME, timeout_ms=10000)


def test_h_and_the_render_classes_are_served(ctl, game):
    """H plays the level's movie (none in project 0); e f l v call MDL_ReplaceMaterial.
    Their visible effect is phase 7's acceptance."""
    editor(ctl, False)
    for key, text in (("H", "H"), ("L", "l"), ("E", "e")):
        press(ctl, key, text, "shift" if text.isupper() else None, ms=150)
    log = game.stderr_text
    assert "[keys] H: no movie to play" in log
    assert "[keys] l render class 3 -> 6" in log and "[keys] e render class 6 -> 3" in log
    assert ctl.read32(HANDLER) == GAME_TICK_FRAME


def test_wasd_is_back_with_the_editor_off(ctl, game):
    editor(ctl, False)
    state = press(ctl, "W", "w")
    assert state[0x26] & 1 and not state[ord("W")] & 1  # UP, as WD_KEYMAP says
    assert game.process.poll() is None
