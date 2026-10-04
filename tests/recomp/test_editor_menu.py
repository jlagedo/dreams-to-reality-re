"""The July Dreams Editor menu in the running game (spec 008 phase 1 acceptance).

One headless Develop game (software renderer, the developer folder) is started
with the control channel and booted into project 0; the tests run in file
order on it. The editor flag is set through the channel; menu branches are
opened by setting the walker's open bit (node +0x3c bit 0) on the path, as a
click would; `0` is a typed key (the channel's `type`); the slider drag is a scripted mouse
(run.py --mouse), repeated every 2.5 s from 30 s so that it lands whenever the
row is on screen.

Skipped when the development build (with WD_DEVTOOLS), the disc images or the
menu resource beside the executable (DREAMS_WIP_DIR at build time) are missing.
"""

import importlib.util
import os
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

EDITOR = 0x4A477C  # the editor flag (keypad 5 in Develop)
ROOT_NODE = 0x4A47C4  # "Dreams Editor"
HANDLER = 0x626F74  # the message manager's handler: GAME_TickFrame (0x416d45) while a level plays
PLAYER_POS = 0x4FBA78  # actor slot 1 position, three doubles (GetObjetReference_ 0x41c9dd)
SCENE_POS = 0x4FBD48  # actor slot 2, OBJET0, the scene object
INIT_POS = 0x65FBB8  # working project +0xb4..+0xbc
LIGHT_MED_R = 0x65FB1C  # +0x18, copied to the palette base 0x4a0f88 by WorksGetEditor_ (0x44cc98)
LIGHT_BASE_R = 0x65FB34  # +0x30, copied to the actors' ambient R 0x62630c (0x44ccd4)
AMBIENT_R = 0x62630C
PALETTE_BASE_R = 0x4A0F88
# Light Base R with Project > Project Edit... > Misc... > Material Light... > Light Base...
# open (rows 10 px apart from y 40, each open level 50 px lower and 10 px right): the
# row at y 290, its slider at x 250..378 for 0..256. The editor adds 6 to the mouse y.
DRAG_Y, DRAG_FROM, DRAG_TO, DRAGGED = 289, 260, 314, (314 - 250) * 256 // 128
MOUSE = ",".join(
    f"{t}:move:{DRAG_FROM}:{DRAG_Y},{t + 200}:left-down:{DRAG_FROM}:{DRAG_Y},"
    f"{t + 600}:move:{DRAG_TO}:{DRAG_Y},{t + 1000}:left-up:{DRAG_TO}:{DRAG_Y}"
    for t in range(30000, 110000, 2500)
)


def requirements():
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    if "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text(errors="replace"):
        pytest.skip(f"{build} was built without WD_DEVTOOLS")
    resource = build / "resources" / "editor-tree.tsv"
    if not resource.is_file():
        pytest.skip(f"no {resource}: build with DREAMS_WIP_DIR set")
    try:
        run.disc_sources(True, None, None)
    except Exception as error:  # no DREAMS_DISC1/2
        pytest.skip(f"no disc images: {error}")
    return resource


@pytest.fixture(scope="module")
def resource():
    rows = []
    for line in requirements().read_text(encoding="ascii").splitlines():
        if line[:1].isdigit():
            _, kind, address, low, high, mask, flags, children, label = line.split("\t")
            rows.append(
                {"kind": kind, "address": int(address, 16), "min": int(low), "max": int(high),
                 "mask": int(mask, 16), "flags": int(flags, 16), "label": label,
                 "children": [] if children == "-" else [int(c) for c in children.split(",")]}
            )  # fmt: skip
    return rows


@pytest.fixture(scope="module")
def game(resource):
    args = ["--mode", "dev", "--renderer", "software", "--scale", "1", "--mouse", MOUSE]
    game = wdctl.start_game(tag=f"editor-menu-{os.getpid()}", args=args, wait=60.0)
    yield game
    game.close(remove=True)


@pytest.fixture(scope="module")
def ctl(game):
    return game.ctl


def node(ctl, va):
    raw = ctl.read(va, 0x40)
    value, low, high, mask, flags = struct.unpack_from("<IiiII", raw, 0x2C)
    return {
        "label": raw[:24].split(b"\0")[0].decode("latin-1"),
        "kids": [k for k in struct.unpack_from("<5I", raw, 0x18) if k],
        "value": value, "min": low, "max": high, "mask": mask, "flags": flags,
    }  # fmt: skip


def path(ctl, *labels):
    va, out = ROOT_NODE, []
    for label in labels:
        va = next(k for k in node(ctl, va)["kids"] if node(ctl, k)["label"].strip() == label)
        out.append(va)
    return out


def set_open(ctl, vas, on):
    for va in vas:
        flags = ctl.read32(va + 0x3C)
        ctl.write32(va + 0x3C, flags | 1 if on else flags & ~1)


def red_mean(bmp):
    """Mean red of the 3D view right of the menu (x 400..640, y 100..400), 24-bit BMP."""
    data = Path(bmp).read_bytes()
    reds = [
        data[54 + (479 - y) * 1920 + x * 3 + 2]
        for y in range(100, 400, 4)
        for x in range(400, 640, 4)
    ]
    return sum(reds) / len(reds)


def test_the_installed_nodes_match_the_resource(ctl, game, resource):
    seen, problems = {}, []

    def walk(i, va):
        if i in seen:
            assert seen[i] == va, f"row {i} installed twice"
            return
        seen[i] = va
        row, n = resource[i], node(ctl, va)
        if row["kind"] in ("branch", "value", "cell"):
            for key in ("label", "min", "max", "mask", "flags"):
                if n[key] != row[key]:
                    problems.append(
                        f"row {i} {row['label']!r}: {key} {n[key]!r}, resource {row[key]!r}"
                    )
        if row["kind"] == "value" and n["value"] != row["address"]:
            problems.append(f"row {i} {row['label']!r}: value {n['value']:#x}")
        if row["kind"] == "cell" and (
            n["value"] == row["address"] or ctl.read32(n["value"]) != ctl.read32(row["address"])
        ):
            problems.append(
                f"row {i} {row['label']!r}: cell {n['value']:#x} not from {row['address']:#x}"
            )
        assert len(n["kids"]) == len(row["children"]), row["label"]
        for c, kid in zip(row["children"], n["kids"], strict=True):
            walk(c, kid)

    walk(0, ROOT_NODE)
    assert not problems, "\n".join(problems)
    assert len(seen) == len(resource)
    assert "[editor] menu: " in game.stderr_text


def test_the_editor_draws_the_july_root(ctl, game):
    nav.boot_into(ctl, "H18ANGKR.DSN")
    ctl.wait(frames=30)
    ctl.write32(EDITOR, 1)
    ctl.wait(frames=10)
    shot = ctl.screenshot(game.run_dir / "editor-root")
    assert shot["width"] == 640
    labels = [node(ctl, k)["label"] for k in node(ctl, ROOT_NODE)["kids"]]
    assert labels == ["Project", "Scene Particle", "Option", "Debug", "Exit To DOS"]
    assert game.process.poll() is None


def test_key_0_captures_the_player_position_into_init_pos(ctl):
    vas = path(
        ctl,
        "Project",
        "Project Edit...",
        "Misc...",
        "Misc Player &Scene...",
        "Misc Player...",
        "Init Pos...",
    )
    set_open(ctl, vas, True)
    ctl.write(INIT_POS, struct.pack("<3i", 123456, 123456, 123456))
    ctl.wait(frames=3)
    ctl.type("0", "0")  # a typed key: the editor reads DOS characters (dev_keys.c)
    ctl.wait_until_mem(INIT_POS, "!=", 123456, timeout_ms=10000)
    ctl.pause()
    try:
        got = struct.unpack("<3i", ctl.read(INIT_POS, 12))
        player = struct.unpack("<3d", ctl.read(PLAYER_POS, 24))
        scene = struct.unpack("<3d", ctl.read(SCENE_POS, 24))
    finally:
        ctl.resume()
    assert got == tuple(int(p) - int(s) for p, s in zip(player, scene, strict=True))
    set_open(ctl, vas, False)


def bmp_pixel(data, x, y):
    at = 54 + (479 - y) * 1920 + x * 3  # bottom-up 24-bit rows
    return data[at + 2], data[at + 1], data[at]


def test_slider_rows_draw_the_dos_track_and_knob(ctl, game):
    """The DOS slider, ported (editor_menu.c sprite_blit): sprite set 3 (alphabe2.spr)
    loaded by retail's LoadFileSpr_, each track segment a blue row over a white one.
    "Player Speed Move" (Init Pos's fifth row, y 380, value 0): track x 260..388."""
    assert ctl.read32(0x62BDB8) == 0x4A4684  # _TableObjetIdent[3] = the alphabe2.spr descriptor
    assert "[editor] sprite set 3 (alphabe2.spr): loaded" in game.stderr_text
    vas = path(
        ctl,
        "Project",
        "Project Edit...",
        "Misc...",
        "Misc Player &Scene...",
        "Misc Player...",
        "Init Pos...",
    )
    set_open(ctl, vas, True)
    ctl.wait(frames=3)
    shot = game.run_dir / "init-pos-sliders"
    ctl.screenshot(shot)
    set_open(ctl, vas, False)
    data = shot.read_bytes()
    white = [bmp_pixel(data, x, 380) for x in range(270, 387)]
    blue = [bmp_pixel(data, x, 379) for x in range(270, 387) if (x - 260) % 16]
    assert all(min(p) >= 230 for p in white), white
    assert all(b >= 180 and r <= 40 for r, _, b in blue), blue


FONT_SLOTS, FONT_SLOT_SIZE = 0x5EA4BC, 0x40C  # TEXT_LoadFont's records; +0x408 = sprite set


def test_the_july_fonts_are_loaded_beside_retails(ctl, game):
    """The July fonts (resources/fonts) are in the data tree's DATA\\FONT and in slots 4-7;
    outside the editor frame slots 0-3 are retail's again (their own sprite sets)."""
    assert "[editor] July fonts loaded into slots 4-7" in game.stderr_text
    for slot in range(8):
        record = FONT_SLOTS + slot * FONT_SLOT_SIZE
        assert ctl.read32(record) == 1, slot
        assert ctl.read32(record + 0x408) == slot, slot
    assert ctl.read32(FONT_SLOTS + 5 * FONT_SLOT_SIZE + 4) == 1  # DOSAPP.008: July's 1-px spacing
    assert ctl.read32(FONT_SLOTS + 1 * FONT_SLOT_SIZE + 4) == 0  # HI480: retail's none


def test_light_fields_act_live(ctl, game):
    """WorksGetEditor_ copies the working project's light fields into the live globals every
    editor frame: Light Base into the actors' ambient, Light Medium into the palette base,
    which recolours the whole view. Measured before the scripted drags begin (30 s)."""
    # The opening dialogue holds GAME_TickFrame (and so the palette) once it starts, some
    # seconds after the level: wait for it and end it line by line (AGENTS.md).
    try:
        ctl.wait_until_mem(HANDLER, "==", 0x40E75C, timeout_ms=30000)
    except wdctl.CtlError:
        pass
    for _ in range(120):
        if ctl.read32(HANDLER) == 0x416D45:
            break
        ctl.tap("ESC")
        ctl.wait(ms=500)
    assert ctl.read32(HANDLER) == 0x416D45
    base, medium = ctl.read32(LIGHT_BASE_R), ctl.read32(LIGHT_MED_R)
    ctl.pause()
    try:
        ctl.write32(LIGHT_BASE_R, 200)
        ctl.step(3)
        assert ctl.read32(AMBIENT_R) == 200
        reds = {}
        for value in (-127, 127):
            ctl.write32(LIGHT_MED_R, value & 0xFFFFFFFF)
            ctl.step(10)
            assert ctl.read32(PALETTE_BASE_R) == value & 0xFFFFFFFF
            ctl.screenshot(game.run_dir / f"medium-{value}")
            reds[value] = red_mean(game.run_dir / f"medium-{value}")
    finally:
        ctl.write32(LIGHT_BASE_R, base)
        ctl.write32(LIGHT_MED_R, medium)
        ctl.resume()
    assert reds[127] > reds[-127] + 100, reds


def test_dragging_light_base_r(ctl):
    vas = path(ctl, "Project", "Project Edit...", "Misc...", "Material Light...", "Light Base...")
    set_open(ctl, vas, True)
    ctl.wait_until_mem(LIGHT_BASE_R, "==", DRAGGED, timeout_ms=60000)
    ctl.wait_until_mem(AMBIENT_R, "==", DRAGGED, timeout_ms=5000)
