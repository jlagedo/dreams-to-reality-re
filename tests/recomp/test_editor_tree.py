"""The Dreams Editor menu of Develop (spec 008 phase 1): recomp/windream/editor.

Offline checks: the bindings table's shape and rules, each binding's retail
reader against the lifted code (spec 008 B4 "Binding table proof"), and the
extraction and resolution of the July tree into the resource the host installs
(host/sdl/editor_menu.c). Tests that read the July demo skip without
DREAMS_WIP_DIR; the reader check skips without the lifted code (lift.py).
"""

import importlib.util
import re
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


editor_tree = load("recomp/windream/editor/editor_tree.py")
ROWS = editor_tree.read_bindings()

# Where the engine reads each working record's fields besides the working copy
# itself: the sub-record's slot inside a project record, and for LINKADVENT the
# 0x38-byte task SCENE_InitTriggers copies it into (task = record - 0x0c, for the
# fields from +0x0c to +0x28; tasks at 0x614e8c + i * 0x38).
FAMILY_AT = {
    "_CurrentSceneS": (),
    "_CurrentSceneLinkS": (0x200,),
    "_CurrentSceneObjetS": (0x600,),
    "_CurrentSceneBoxS": (0x1200,),
    "_CurrentSceneLinkAdventureS": (0x1E00,),
}
TASKS = 0x614E8C
COMMENT = re.compile(r"/\* 0x([0-9A-Fa-f]{8}): ([^*]*?) \*/")
DISPLACEMENT = re.compile(r"[+-] 0x([0-9a-f]+)\]|\[0x([0-9a-f]+)\]|[+-] (\d+)\]")


def july_exe():
    exe = editor_tree.july_exe()
    if not exe or not exe.is_file():
        pytest.skip("no July DREAMS.EXE: set DREAMS_WIP_DIR to the July demo's DREAMS directory")
    return exe


@pytest.fixture(scope="module")
def tree():
    return editor_tree.extract(july_exe())


@pytest.fixture(scope="module")
def instructions():
    gen = editor_tree.out_dir().parent / "gen"
    files = sorted(gen.glob("recomp_0*.c"))
    if not files:
        pytest.skip(f"no lifted code in {gen}: run lift.py")
    found = {}
    for f in files:
        for m in COMMENT.finditer(f.read_text(errors="replace")):
            found[int(m[1], 16)] = m[2]
    return found


def readers(row):
    return [r for r in row["reader"].split(";") if r]


def test_rows_are_well_formed():
    nodes = [row["node"] for row in ROWS]
    assert len(nodes) == len(set(nodes))
    for row in ROWS:
        where = f"bindings.tsv:{row['line']} {row['node']}"
        assert row["rule"] in editor_tree.LEAF_RULES | editor_tree.OUR_RULES, where
        if row["retail"]:
            assert re.fullmatch(r"0x[0-9a-f]+", row["retail"]), where
        for reader in readers(row):
            assert re.fullmatch(r"(DREAMSFX\.EXE:)?0x[0-9a-f]+", reader), where
        bound = row["value"] and row["rule"] != "hide"
        if bound and row["rule"] in ("keep", "relabel", "widen", "not-wired", "retail-node", "gap"):
            assert readers(row), f"{where}: rule {row['rule']} needs a retail reader"
        if row["rule"] in ("hide", "no-effect"):
            assert row["note"], f"{where}: say why"


def displacements(text):
    for m in DISPLACEMENT.finditer(text):
        if m[1] or m[2]:
            yield int(m[1] or m[2], 16)
        else:
            yield int(m[3])


def names_field(row, text):
    """Whether one lifted instruction addresses the row's value."""
    symbol, offset = editor_tree.parse_value(row["value"])
    if symbol in FAMILY_AT:
        width = 4
        starts = {offset} | {at + offset for at in FAMILY_AT[symbol]}
        if symbol == "_CurrentSceneLinkAdventureS" and 0x0C <= offset <= 0x28:
            starts |= {offset - 0x0C, TASKS + offset - 0x0C}
        return any(s <= d < s + width for d in displacements(text) for s in starts)
    address = 0x4A4780 if row["rule"] == "retail-node" else int(row["retail"], 16)
    return re.search(rf"\b0x{address:x}\b", text) is not None


def test_each_reader_addresses_its_field(instructions):
    """spec 008 B4: each binding's proof is a retail instruction that reads (or, for an
    engine global, writes) the very field."""
    problems = []
    for row in ROWS:
        for reader in readers(row):
            if reader.startswith("DREAMSFX.EXE:"):
                continue  # the DOS Glide build: not in the lifted code
            text = instructions.get(int(reader, 16))
            if text is None:
                problems.append(f"{row['node']}: no lifted instruction at {reader}")
            elif not names_field(row, text):
                problems.append(f"{row['node']} {row['value']}: {reader} is '{text}'")
    assert not problems, "\n".join(problems)


def test_the_july_tree(tree):
    nodes = tree["nodes"]
    assert len(nodes) == 351
    assert nodes[0]["label"] == "Dreams Editor"
    assert [n["label"] for n in nodes if n["parents"] == [tree["root"]]] == [
        "Project", "Scene Particle", "Option", "Debug", "Exit To DOS",
    ]  # fmt: skip
    shared = sorted(n["label"] for n in nodes if len(n["parents"]) > 1)
    assert shared == ["Flags Action...", "Flags Condition..."]


def test_every_july_leaf_has_one_row(tree):
    leaves = {n["symbol"] for n in tree["nodes"] if n["value"]}
    july_rows = {row["node"] for row in ROWS if not row["node"].startswith("+")}
    assert leaves - july_rows == set()
    assert july_rows - {n["symbol"] for n in tree["nodes"]} == set()


@pytest.fixture(scope="module")
def menu(tree):
    return editor_tree.resolve(tree, ROWS)


def find(menu, *labels):
    node = menu[0]
    for label in labels:
        matches = [menu[c] for c in node["children"] if menu[c]["label"].strip() == label]
        assert matches, f"no {label!r} under {node['label']!r}"
        node = matches[0]
    return node


def test_the_installed_menu(menu):
    root = menu[0]
    assert root["kind"] == "root" and root["address"] == editor_tree.RETAIL_ROOT
    tops = [menu[c] for c in root["children"]]
    assert [n["label"] for n in tops] == [
        "Project",
        "Scene Particle",
        "Option",
        "Debug",
        "Exit To DOS",
    ]
    assert tops[-1]["kind"] == "retail" and tops[-1]["address"] == editor_tree.RETAIL_EXIT
    for n in menu:
        assert len(n["children"]) <= editor_tree.MAX_CHILDREN
        if n["kind"] in ("value", "cell"):
            assert editor_tree.RETAIL_IMAGE[0] <= n["address"] < editor_tree.RETAIL_IMAGE[1]
    pos = find(
        menu,
        "Project",
        "Project Edit...",
        "Misc...",
        "Misc Player &Scene...",
        "Misc Player...",
        "Init Pos...",
    )
    xyz = [menu[c] for c in pos["children"]][:3]
    assert [(n["address"], n["flags"]) for n in xyz] == [
        (0x65FBB8, 8),
        (0x65FBBC, 0x10),
        (0x65FBC0, 0x20),
    ]
    light = find(
        menu,
        "Project",
        "Project Edit...",
        "Misc...",
        "Material Light...",
        "Light Base...",
        "Light Base R",
    )
    assert light["address"] == 0x65FB34
    cells = [n for n in menu if n["kind"] == "cell"]
    assert sorted(n["address"] for n in cells) == [
        0x49D1CC,
        0x49D1D0,
        0x49D1D8,
        0x49D1DC,
        0x49D1E4,
        0x49D1E8,
    ]


def test_the_resource_round_trip(tree, menu, tmp_path):
    text = editor_tree.resource_text(
        menu, tree["source"]["sha256"], editor_tree.sha256(editor_tree.BINDINGS)
    )
    path = tmp_path / editor_tree.RESOURCE_NAME
    path.write_text(text, encoding="ascii", newline="\n")
    assert editor_tree.check_resource(path) == []
    rows = [line.split("\t") for line in text.splitlines() if line[:1].isdigit()]
    assert len(rows) == len(menu)
    assert all(len(r) == 9 for r in rows)
    stale = text.replace(editor_tree.sha256(editor_tree.BINDINGS), "0" * 64)
    path.write_text(stale, encoding="ascii", newline="\n")
    assert editor_tree.check_resource(path)


def test_the_july_fonts_are_copied_and_checked(tmp_path, menu, tree):
    exe = july_exe()
    fonts = editor_tree.copy_fonts(exe.parent, tmp_path / editor_tree.FONT_DIR)
    assert sorted(p.name for p in fonts) == sorted(editor_tree.FONTS)
    text = editor_tree.resource_text(
        menu, tree["source"]["sha256"], editor_tree.sha256(editor_tree.BINDINGS)
    )
    (tmp_path / editor_tree.RESOURCE_NAME).write_text(text, encoding="ascii", newline="\n")
    assert editor_tree.check_resources(tmp_path) == []
    (tmp_path / editor_tree.FONT_DIR / "DOSAPP.008").write_bytes(b"not a font")
    assert editor_tree.check_resources(tmp_path) == [
        f"{tmp_path / editor_tree.FONT_DIR / 'DOSAPP.008'}: not the July demo's DOSAPP.008"
    ]


def test_a_wrong_executable_is_refused(tmp_path):
    exe = tmp_path / "DREAMS.EXE"
    exe.write_bytes(b"MZ" + bytes(100))
    with pytest.raises(editor_tree.TreeError, match="SHA-256"):
        editor_tree.extract(exe)
