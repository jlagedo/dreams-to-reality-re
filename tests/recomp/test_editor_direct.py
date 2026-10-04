"""The editor and Cryo's tools under the direct renderer (spec 008 phase D), matched
against the software renderer: host/sdl/dev_overlay.c and the render classes' guard.

Two headless Develop games, one per renderer, each on its own copy of the developer
folder (TGA captures write into it), run the same scripted states in project 0: the
editor's menu, a slider page, the ten picker pages, the collision views and the
profiler. For each state the control channel's overlay_shot takes the game frame of
one gameplay frame just before Develop's overlays (the editor's call, the tools) and
just after them (the GPU frame read back under the direct renderer); their difference
is exactly what the overlays drew. The 3D scene differs between the renderers by
design (texture filtering, the 3dfx build's pipeline), so only that footprint is
compared: the direct one must cover the software one, add little to it and show the
software colours on it (opaque text, sliders, bars and dots match; the picker bands
halve what is under them, so their pixels follow the two renderers' scenes).

Then, in the direct game only: the TGA capture (SaveImage_ reads the frame the direct
renderer reads back into guest memory) with the editor off and on, and the render
classes, which the direct renderer does not draw, left alone with a message. The
software screens of the states are kept in DREAMS_OUT/recomp/editor-direct/ beside
the direct ones.
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
tools = load("tests/recomp/test_dev_tools.py")  # settle()
menu = load("tests/recomp/test_editor_menu.py")  # path() and set_open()
recomp_env = save.recomp_env

EDITOR = 0x4A477C
MESH_FIELD = 0x65F8C4 + 0x0C
# The picker pages (host/sdl/editor_pickers.c): name, key, modifiers, semaphore.
PAGES = [
    ("load-mesh", "2", None, 0x4A4718),
    ("load-hnm", "1", None, 0x4A471C),
    ("load-symbole", "6", None, 0x4A4714),
    ("load-anim", None, None, 0x4A474C),
    ("load-map", None, None, 0x4A4748),
    ("load-project", "Q", "shift", 0x4A46BC),
    ("load-objet", "S", "shift", 0x4A46CC),
    ("load-link", "D", "shift", 0x4A46F0),
    ("load-box", "F", "shift", 0x4A4700),
    ("load-linkadventure", "G", "shift", 0x4A46DC),
]
CAPTURE_ONE, CAPTURE_COUNTER = 0x4A475C, 0x4A4754
SCENE_HANDLE = 0x4FBDBC  # the level's model handle
NODES = 0x661EE0  # the 3D engine's group table
TGA_BYTES = 18 + 640 * 480 * 3
# state: how far (pixels) a drawn pixel may sit from its counterpart. The collision
# views are one-pixel lines projected from the colliders with the camera of the
# moment, which the two games reach a little differently.
STATES = {"menu": 0, "sliders": 0, **{page[0]: 0 for page in PAGES}, "collision": 2}
SAME = 40  # a channel step still counted as the same colour (RGB565 expansion, dimmed bands)


def press(ctl, key, text=None, mods=None, frames=3):
    ctl.type(key, text, mods, 150)
    ctl.wait(ms=400)
    ctl.wait(frames=frames)


def shot(ctl, folder, name):
    ctl.overlay_shot(folder / f"{name}-before.bmp", folder / f"{name}-after.bmp")


def scenario(ctl, folder):
    ctl.write32(EDITOR, 1)
    ctl.wait(frames=3)
    shot(ctl, folder, "menu")
    opened = menu.path(
        ctl, "Project", "Project Edit...", "Misc...", "Material Light...", "Light Base..."
    )
    menu.set_open(ctl, opened, True)
    ctl.wait(frames=2)
    shot(ctl, folder, "sliders")
    menu.set_open(ctl, opened, False)
    ctl.write(MESH_FIELD, b"EMPTY".ljust(13, b"\0"))  # closing on another mesh reloads
    for name, key, mods, sema in PAGES:
        if key:
            press(ctl, key, key, mods)
        else:  # no key: its menu button's semaphore
            ctl.write32(sema, 1)
            ctl.wait(frames=3)
        assert ctl.read32(sema) == 1, f"{key or hex(sema)} opens {name}"
        shot(ctl, folder, name)
        press(ctl, "Escape")
        assert ctl.read32(sema) == 0
    ctl.write32(EDITOR, 0)
    ctl.wait(frames=3)
    for name, key in [("collision", "Keypad 7"), ("profiler", "Keypad 8")]:
        press(ctl, key, frames=20)
        shot(ctl, folder, name)
        press(ctl, key)


def start(renderer, tree):
    args = ["--mode", "dev", "--tree", str(tree), "--renderer", renderer, "--scale", "1"]
    game = wdctl.start_game(tag=f"editor-direct-{renderer}-{os.getpid()}", args=args, wait=60.0)
    nav.boot_into(game.ctl, "H18ANGKR.DSN")
    tools.settle(game.ctl)
    return game


@pytest.fixture(scope="module")
def image():
    return pytest.importorskip("PIL.Image")


@pytest.fixture(scope="module")
def folder():
    out = recomp_env.out_dir("editor-direct")
    shutil.rmtree(out, ignore_errors=True)
    (out / "software").mkdir(parents=True)
    (out / "direct").mkdir(parents=True)
    return out


def tree_for(renderer):
    source = save.requirements()
    target = recomp_env.out_dir("editor-direct") / f"tree-{renderer}-{os.getpid()}"
    save.tree_copy(source, target)
    shutil.rmtree(target / "DATA" / "TGA", ignore_errors=True)  # made again at Develop start
    return target


@pytest.fixture(scope="module")
def software(folder, image):
    tree = tree_for("software")
    game = start("software", tree)
    try:
        scenario(game.ctl, folder / "software")
    finally:
        game.close(remove=True)
        shutil.rmtree(tree, ignore_errors=True)


@pytest.fixture(scope="module")
def direct(folder, image, software):
    tree = tree_for("direct")
    game = start("direct", tree)
    try:
        scenario(game.ctl, folder / "direct")
        yield game, tree
    finally:
        game.close(remove=True)
        shutil.rmtree(tree, ignore_errors=True)


def rgb(image, path):
    picture = image.open(path).convert("RGB")
    assert picture.size == (640, 480), f"{path}: {picture.size}"
    flat = getattr(picture, "get_flattened_data", None)
    return list(flat() if flat else picture.getdata())


def differs(a, b, step):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]), abs(a[2] - b[2])) > step


def footprint(before, after):
    return {i for i, (a, b) in enumerate(zip(before, after, strict=True)) if a != b}


def near(i, radius):
    y, x = divmod(i, 640)
    return [
        v * 640 + u
        for v in range(max(0, y - radius), min(480, y + radius + 1))
        for u in range(max(0, x - radius), min(640, x + radius + 1))
    ]


def frames(image, folder, state):
    return {
        (r, k): rgb(image, folder / r / f"{state}-{k}.bmp")
        for r in ("software", "direct")
        for k in ("before", "after")
    }


@pytest.mark.parametrize("state", STATES)
def test_the_overlay_matches_the_software_renderer(state, folder, image, software, direct):
    """Same pixels drawn, and the same colours wherever the two renderers' scenes
    agree under them (the bands that halve the scene follow the scene)."""
    radius = STATES[state]
    f = frames(image, folder, state)
    s_before, s_after = f["software", "before"], f["software", "after"]
    d_before, d_after = f["direct", "before"], f["direct", "after"]
    want, got = footprint(s_before, s_after), footprint(d_before, d_after)
    assert len(want) > 200, f"{state}: the software reference draws {len(want)} pixels"
    covered = sum(any(j in got for j in near(i, radius)) for i in want) / len(want)
    extra = sum(not any(j in want for j in near(i, radius)) for i in got) / len(want)
    agree = [i for i in want if not differs(s_before[i], d_before[i], SAME)]
    same = sum(
        any(not differs(s_after[i], d_after[j], SAME) for j in near(i, radius)) for i in agree
    ) / max(1, len(agree))
    report = (
        f"{state}: {len(want)} px, covered {covered:.3f}, extra {extra:.3f}, "
        f"scene agrees under {len(agree) / len(want):.3f}, same {same:.3f}"
    )
    print(report)
    assert covered >= 0.95 and extra <= 0.05, report
    assert len(agree) >= len(want) / 2 and same >= 0.95, report


def test_the_profiler_matches_the_software_renderer(folder, image, software, direct):
    """The bar's stage lengths are the measured times, which differ between the
    renderers by design; its layout does not: four full frame rows, four stage rows
    from x 0, and the frame rate text below them."""
    f = frames(image, folder, "profiler")
    for renderer in ("software", "direct"):
        before, after = f[renderer, "before"], f[renderer, "after"]
        drawn = footprint(before, after)
        rows = [sum(1 for x in range(640) if y * 640 + x in drawn) for y in range(8)]
        assert rows[0::2] == [640] * 4, f"{renderer}: frame rows {rows}"
        assert all(
            y * 640 in drawn and n > 0 for y, n in zip(range(1, 8, 2), rows[1::2], strict=True)
        ), f"{renderer}: stage rows {rows}"
        text = [
            after[y * 640 + x] for y in range(14, 32) for x in range(160) if y * 640 + x in drawn
        ]
        assert len(text) > 40 and all(min(p) >= 240 for p in text), f"{renderer}: frame rate text"


def test_no_direct_renderer_stop(direct):
    game, _ = direct
    fatal = [x for x in game.stderr_text.splitlines() if "FATAL" in x]
    assert not fatal, fatal


def tga_pixels(path):
    data = path.read_bytes()
    assert len(data) == TGA_BYTES
    width, height = struct.unpack_from("<HH", data, 12)
    kind, bpp, descriptor = data[2], data[16], data[17]
    assert (kind, width, height, bpp) == (2, 640, 480, 24)
    rows = [data[18 + y * 1920 : 18 + (y + 1) * 1920] for y in range(480)]
    if not descriptor & 0x20:
        rows.reverse()  # bottom-up
    return [(r[x * 3 + 2], r[x * 3 + 1], r[x * 3]) for r in rows for x in range(640)]


def capture(ctl, tree, image, folder, name, editor):
    ctl.write32(EDITOR, 1 if editor else 0)
    ctl.wait(frames=3)
    before = set((tree / "DATA" / "TGA").glob("*.tga"))
    counter = ctl.read32(CAPTURE_COUNTER)
    ctl.pause()
    try:
        ctl.write32(CAPTURE_ONE, 1)
        ctl.screenshot(folder / "direct" / f"{name}.png")  # the frame the capture is made in
    finally:
        ctl.resume()
    made = sorted(set((tree / "DATA" / "TGA").glob("*.tga")) - before)
    assert [f.name for f in made] == [f"H18_{counter:04d}.tga"], made
    tga = tga_pixels(made[0])
    shown = rgb(image, folder / "direct" / f"{name}.png")
    return tga, shown


def test_tga_capture_reads_the_direct_frame(direct, image, folder):
    """Editor off (dev_tools.c) and on (WorksEdit_): the file is the frame being made,
    without the overlays drawn after the capture, not the black guest memory."""
    game, tree = direct
    ctl = game.ctl
    for name, editor in [("capture-off", False), ("capture-on", True)]:
        tga, shown = capture(ctl, tree, image, folder, name, editor)
        mean = sum(sum(p) for p in tga) / (3 * len(tga))
        assert mean > 20, f"{name}: the capture is black (mean {mean:.1f})"
        # Below the editor's panel and above the HUD the file and the screen hold
        # the same 3D frame: compare the band y 330..400.
        band = range(330 * 640, 400 * 640)
        close = sum(not differs(tga[i], shown[i], SAME) for i in band) / len(band)
        assert close >= 0.9, f"{name}: {close:.3f} of the band matches the screen"
    ctl.write32(EDITOR, 0)
    assert "materialize surface=" in game.stderr_text


def test_render_classes_are_left_alone(direct):
    """Classes 6 and 0x1c are the software rasterizer's: the direct renderer follows
    the 3dfx build, whose Glide hook draws nothing for them (the level would vanish)."""
    game, _ = direct
    ctl = game.ctl
    handle = ctl.read32(SCENE_HANDLE)
    assert handle >> 16, "no level model"

    def classes():
        group = ctl.read32(ctl.read32(NODES) + 4 * (handle >> 16))
        found = Counter()
        for i in range(ctl.read32(group + 0x14)):
            node = ctl.read32(group + 0x18 + 4 * i)
            for head in (0xA4, 0xA8):
                entry = ctl.read32(node + head)
                while entry:
                    found[ctl.read32(entry + 4)] += 1
                    entry = ctl.read32(entry)
        return found

    ctl.write32(EDITOR, 0)
    start = classes()
    assert start[3]
    press(ctl, "L", "l", frames=5)
    press(ctl, "F", "f", frames=5)
    assert classes() == start
    assert "render classes: software renderer only" in game.stderr_text
