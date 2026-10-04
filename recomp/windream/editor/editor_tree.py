"""The Dreams Editor menu of Develop (spec 008 phase 1): the July tree, bound to retail.

The retail executables keep the editor's code but only two menu nodes ("Dreams
Editor" and "Exit To DOS"). The July 1997 demo's DOS DREAMS.EXE keeps the 351
nodes as named data. This module

  extract   reads that executable (DREAMS_WIP_DIR, SHA-256 checked), walks the
            tree from _editorObjetMain and writes tree.json: labels,
            structure, value symbol + offset, slider range, mask and flags;
  resolve   applies the committed bindings table (bindings.tsv beside this
            file: each July value -> retail address, with the retail reader
            that proves it, and the rules of spec 008 phase 1) and writes
            editor-tree.tsv, the resource the host installs in guest memory
            (host/sdl/editor_menu.c), and menu.txt, the installed menu as text;
  build     both, into DREAMS_OUT/recomp/windream/editor/; build.py runs it and
            copies the resource beside the executable (stage), release.py
            bundles it (check_resource).

Everything written is game-derived (July labels): it stays under out/ and is
bundled with the release as resources/editor-tree.tsv, never committed.
Standard library only: build.py also runs under WSL without uv.

usage: python recomp/windream/editor/editor_tree.py build|show [--exe DREAMS.EXE]
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / "re" / "tools"))
from relocated_image import Image  # noqa: E402
from watcom_debug import read_debug  # noqa: E402

JULY_SHA256 = "45d00510f993d321d2288b0791a96297893de9e5e0dc24d36246e41e144008ce"
ROOT_SYMBOL = "_editorObjetMain"
NODE_SIZE = 0x40
LABEL_SIZE = 24
BINDINGS = HERE / "bindings.tsv"
RESOURCE_NAME = "editor-tree.tsv"
RESOURCE_VERSION = 1
# The July fonts, shipped as resources/fonts/ (owner, 2026-10-04): July's InitGame_ loads them
# into font slots 0-3 (0x10131...): the title, the rows and values, and the two sizes TEXT_PrintAt
# uses below 640 wide. The host puts them into Develop's data tree and gives them to the editor
# (host/sdl/editor_menu.c). Name -> SHA-256 of the July demo's DATA\FONT file.
FONT_DIR = "fonts"
FONTS = {
    "COURE.016": "0e0a4a484963d9b6ebe1d352b0d5aff1a924a9207e396192690f8784cc8dc249",
    "DOSAPP.008": "26c6946f71eaaad884826046030a6466cc4111e6c0b238e4d868469ce41573a9",
    "SMALLE.008": "4720e096dd30551f7203a81ccbb16f9937d94c31e114f8277a3bdd8f58d3fcef",
    "SMALLE.006": "bf60d757f8579be8a56277bec2c40defb2f790977b3c256ce40833bd9a1adcf0",
}


class TreeError(ValueError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# ---- extract -------------------------------------------------------------


def extract(exe: Path) -> dict:
    """The July menu tree as plain data, depth first from the root."""
    data = exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != JULY_SHA256:
        raise TreeError(
            f"{exe}: SHA-256 {digest}, not the July 1997 demo's DREAMS.EXE ({JULY_SHA256})"
        )
    image = Image(exe)
    _, symbols = read_debug(data)
    at = sorted((int(s["address"], 16), s["name"]) for s in symbols)
    starts = [a for a, _ in at]
    exact = {}
    for address, name in at:
        exact.setdefault(address, name)

    def name_of(value: int) -> dict:
        i = bisect.bisect_right(starts, value) - 1
        if i < 0:
            raise TreeError(f"value {value:#x} precedes every symbol")
        base, name = at[i]
        return {"symbol": name, "offset": value - base, "july": f"{value:#x}"}

    nodes, seen = [], {}

    def walk(address: int, parent: str | None) -> str:
        if address in seen:  # a node listed under two parents: one node, shared
            seen[address]["parents"].append(parent)
            return seen[address]["symbol"]
        raw = image.read(address, NODE_SIZE)
        if len(raw) != NODE_SIZE:
            raise TreeError(f"node {address:#x} is outside the initialized image")
        symbol = exact.get(address) or f"@{address:x}"
        label = raw[:LABEL_SIZE].split(b"\0")[0].decode("latin-1")
        children = [c for c in struct.unpack_from("<5I", raw, 0x18) if c]
        value, low, high, mask, flags = struct.unpack_from("<IiiII", raw, 0x2C)
        node = {
            "symbol": symbol,
            "address": f"{address:#x}",
            "parents": [parent] if parent else [],
            "label": label,
            "children": [],
            "value": name_of(value) if value else None,
            "min": low,
            "max": high,
            "mask": mask,
            "flags": flags,
        }
        nodes.append(node)
        seen[address] = node
        node["children"] = [walk(c, symbol) for c in children]
        return symbol

    root = next((a for a, n in at if n == ROOT_SYMBOL), None)
    if root is None:
        raise TreeError(f"{exe}: no {ROOT_SYMBOL} symbol")
    walk(root, None)
    return {"source": {"file": exe.name, "sha256": digest}, "root": ROOT_SYMBOL, "nodes": nodes}


# ---- show ----------------------------------------------------------------


def value_text(value: dict | None) -> str:
    if not value:
        return ""
    return value["symbol"] + (f"+{value['offset']:#x}" if value["offset"] else "")


def show(tree: dict) -> str:
    """The tree as indented text; a shared node is printed under each parent."""
    by = {n["symbol"]: n for n in tree["nodes"]}
    lines = []

    def walk(symbol: str, d: int) -> None:
        n = by[symbol]
        extra = []
        if len(n["parents"]) > 1:
            extra.append(f"shared by {len(n['parents'])}")
        if n["value"]:
            extra.append(value_text(n["value"]))
        if n["flags"]:
            extra.append(f"flags={n['flags']:#x}")
        if n["mask"]:
            extra.append(f"mask={n['mask']:#x}")
        if n["min"] or n["max"]:
            extra.append(f"{n['min']}..{n['max']}")
        lines.append(f"{'  ' * d}{n['label']}  [{n['symbol']}]  " + "  ".join(extra))
        for c in n["children"]:
            walk(c, d + 1)

    walk(tree["root"], 0)
    return "\n".join(lines) + "\n"


# ---- resolve -------------------------------------------------------------

# July working records -> retail, matched by code (spec 008 A3): (July base, retail base, size).
RECORDS = {
    "_CurrentSceneS": (0x2978B0, 0x65FB04, 0x2200),
    "_CurrentSceneObjetS": (0x297670, 0x65F8C4, 0xC0),
    "_CurrentSceneLinkS": (0x2953F0, 0x65D644, 0x80),
    "_CurrentSceneBoxS": (0x28DC30, 0x65B244, 0x100),
    "_CurrentSceneLinkAdventureS": (0x2953B0, 0x65D604, 0x40),
}
SEMA_OFFSET = 0x3DF34C  # WORKS.C initialized data, July -> retail (A3)
RETAIL_ROOT = 0x4A47C4  # "Dreams Editor"
RETAIL_EXIT = 0x4A4784  # "Exit To DOS", value 0x4a4780, flags 6
RETAIL_IMAGE = (0x401000, 0x6B1400)  # code through the end of .bss (the working records live there)
MAX_CHILDREN = 5
COLUMNS = [
    "node",
    "value",
    "rule",
    "retail",
    "min",
    "max",
    "mask",
    "flags",
    "label",
    "parent",
    "reader",
    "note",
]
LEAF_RULES = {"keep", "relabel", "widen", "no-effect", "hide", "not-wired", "retail-node"}
OUR_RULES = {"gap", "group"}


def read_bindings(path: Path = BINDINGS) -> list[dict]:
    """The bindings table: one dict per row, with its line number under "line"."""
    rows, header = [], None
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        cells = line.split("\t")
        if header is None:
            if cells != COLUMNS:
                raise TreeError(f"{path.name}:{number}: header must be {' '.join(COLUMNS)}")
            header = cells
            continue
        if len(cells) != len(COLUMNS):
            raise TreeError(f"{path.name}:{number}: {len(cells)} columns, expected {len(COLUMNS)}")
        row = dict(zip(COLUMNS, cells, strict=True))
        row["line"] = number
        rows.append(row)
    return rows


def parse_value(text: str) -> tuple[str, int]:
    symbol, _, offset = text.partition("+")
    return symbol, int(offset, 16) if offset else 0


def expected_retail(text: str, july: int | None) -> int | None:
    """The retail address a July value translates to by rule, or None for an engine global
    (bound one by one, from the pinned table)."""
    symbol, offset = parse_value(text)
    if symbol in RECORDS:
        _, retail, size = RECORDS[symbol]
        if not 0 <= offset < size:
            raise TreeError(f"{text}: outside {symbol} (size {size:#x})")
        return retail + offset
    if symbol.startswith("_Sema") and july is not None:
        return july + SEMA_OFFSET
    return None


def _int(text: str, base: int = 10) -> int | None:
    return int(text, base) if text else None


def resolve(tree: dict, rows: list[dict]) -> list[dict]:
    """Apply the bindings to the July tree. Returns the installed nodes depth first from the
    root (index 0), each {label, kind, address, min, max, mask, flags, children}: kind is
    root, branch, value (a retail address), cell (a host-only cell starting at the retail
    global's value) or retail (an existing retail node, not rewritten)."""
    july = {n["symbol"]: n for n in tree["nodes"]}
    out = {}
    for n in tree["nodes"]:
        out[n["symbol"]] = {
            "label": n["label"],
            "kind": "branch" if n["value"] is None else None,
            "address": 0,
            "min": n["min"],
            "max": n["max"],
            "mask": n["mask"],
            "flags": n["flags"],
            "children": list(n["children"]),
        }
    root = out[tree["root"]]
    root.update(kind="root", address=RETAIL_ROOT)
    seen = set()

    def fail(row, problem):
        raise TreeError(f"{BINDINGS.name}:{row['line']}: {row['node']}: {problem}")

    def detach(symbol):
        for node in out.values():
            node["children"] = [c for c in node["children"] if c != symbol]

    def attach(row, symbol):
        parent = out.get(row["parent"])
        if parent is None or parent["kind"] not in ("root", "branch"):
            fail(row, f"parent {row['parent']!r} is not a branch defined earlier")
        parent["children"].append(symbol)

    for row in rows:
        symbol, rule = row["node"], row["rule"]
        if symbol in seen:
            fail(row, "second row for this node")
        seen.add(symbol)
        overrides = {k: _int(row[k]) for k in ("min", "max")}
        overrides.update({k: _int(row[k], 16) for k in ("mask", "flags")})
        if symbol.startswith("+"):  # ours: a gap entry or a new group (data principle rule 4)
            if rule not in OUR_RULES or not row["label"] or not row["parent"]:
                fail(row, "our entries need rule gap or group, a label and a parent")
            node = {"label": row["label"], "kind": "branch", "address": 0, "children": []}
            node.update({k: v or 0 for k, v in overrides.items()})
            if rule == "gap":
                expected = expected_retail(row["value"], None)
                if expected is None or not row["retail"] or int(row["retail"], 16) != expected:
                    fail(
                        row,
                        f"a gap entry binds a working-record field ({row['value']})",
                    )
                node.update(kind="value", address=expected)
            out[symbol] = node
            attach(row, symbol)
            continue
        n = july.get(symbol)
        if n is None:
            fail(row, "not a node of the July tree")
        if rule not in LEAF_RULES:
            fail(row, f"rule {rule!r} is not one of {sorted(LEAF_RULES)}")
        if row["value"] != value_text(n["value"]):
            fail(row, f"value {row['value']!r}, the July node binds {value_text(n['value'])!r}")
        node = out[symbol]
        if rule == "hide":
            if any(row[k] for k in ("retail", "min", "max", "mask", "flags", "label", "parent")):
                fail(row, "a hidden node takes no retail address, range, label or parent")
            detach(symbol)
            continue
        if row["parent"]:  # moved: out of its July parent(s), last under the new one
            detach(symbol)
            attach(row, symbol)
        if row["label"]:
            if rule in ("keep", "not-wired", "retail-node", "widen"):
                fail(row, f"rule {rule} keeps the July label")
            node["label"] = row["label"]
        elif rule in ("relabel", "no-effect") and n["value"] is not None:
            fail(row, f"rule {rule} needs a label")
        if n["value"] is None:  # a July branch: relabelled or moved
            if rule not in ("keep", "relabel") or row["retail"]:
                fail(row, "a branch can only be kept (moved) or relabelled")
            continue
        if not row["retail"]:
            fail(row, "no retail address")
        address = int(row["retail"], 16)
        expected = expected_retail(row["value"], int(n["value"]["july"], 16))
        if rule == "retail-node":
            if address != RETAIL_EXIT:
                fail(row, f"the only retail node kept is the exit node {RETAIL_EXIT:#x}")
            node.update(kind="retail", address=address)
            continue
        if expected is not None and address != expected:
            fail(row, f"retail {address:#x}, the translation gives {expected:#x}")
        if rule == "widen":
            low, high = overrides["min"], overrides["max"]
            if low is None and high is None:
                fail(row, "widen without a new range")
            if (low if low is not None else n["min"]) > n["min"] or (
                high if high is not None else n["max"]
            ) < n["max"]:
                fail(row, "a widened range must contain the July one")
        for key, value in overrides.items():
            if value is not None:
                node[key] = value
        node.update(kind="cell" if rule == "not-wired" else "value", address=address)

    missing = [
        n["symbol"] for n in tree["nodes"] if n["value"] is not None and n["symbol"] not in seen
    ]
    if missing:
        raise TreeError(
            f"{len(missing)} July leaves have no row in {BINDINGS.name}: {', '.join(missing[:8])}"
        )

    order, index = [], {}

    def visit(symbol):
        if symbol in index:
            return
        node = out[symbol]
        index[symbol] = len(order)
        order.append((symbol, node))
        for c in node["children"]:
            visit(c)

    visit(tree["root"])
    result = []
    for symbol, node in order:
        if len(node["children"]) > MAX_CHILDREN:
            raise TreeError(f"{symbol}: {len(node['children'])} children, at most {MAX_CHILDREN}")
        # A label may fill all 24 bytes only when the first child pointer after it is 0, which
        # ends the string for TEXT_DrawString (five July leaves do this); a branch's label
        # would run into its child pointers.
        label, room = node["label"], LABEL_SIZE - (1 if node["children"] else 0)
        if len(label) > room or any(not 0x20 <= ord(ch) < 0x7F for ch in label):
            raise TreeError(
                f"{symbol}: label {label!r}: printable ASCII, at most {room} characters"
            )
        if node["kind"] in ("value", "cell"):
            if not RETAIL_IMAGE[0] <= node["address"] < RETAIL_IMAGE[1] or node["address"] & 3:
                raise TreeError(
                    f"{symbol}: {node['address']:#x} is not an aligned dword of the retail image"
                )
        result.append(dict(node, symbol=symbol, children=[index[c] for c in node["children"]]))
    return result


def resource_text(nodes: list[dict], source: str, bindings: str) -> str:
    """editor-tree.tsv, the host's input (host/sdl/editor_menu.c parses it)."""
    lines = [
        "# The Dreams Editor menu of Develop (spec 008 phase 1): the July 1997 DREAMS.EXE",
        "# tree bound to retail by recomp/windream/editor/bindings.tsv. Generated by",
        "# editor_tree.py; do not edit.",
        "# index\tkind\taddress\tmin\tmax\tmask\tflags\tchildren\tlabel",
        f"version\t{RESOURCE_VERSION}",
        f"source\t{source}",
        f"bindings\t{bindings}",
        f"nodes\t{len(nodes)}",
    ]
    for i, n in enumerate(nodes):
        children = ",".join(str(c) for c in n["children"]) or "-"
        lines.append(
            f"{i}\t{n['kind']}\t{n['address']:#x}\t{n['min']}\t{n['max']}\t{n['mask']:#x}\t{n['flags']:#x}\t{children}\t{n['label']}"
        )
    return "\n".join(lines) + "\n"


def menu_text(nodes: list[dict]) -> str:
    """The installed menu as indented text, for review."""
    lines = []

    def walk(i, depth):
        n = nodes[i]
        what = {
            "value": f"{n['address']:#x}",
            "cell": f"host cell from {n['address']:#x}",
            "retail": f"retail node {n['address']:#x}",
        }
        extra = [what.get(n["kind"], "")]
        if n["flags"]:
            extra.append(f"flags={n['flags']:#x}")
        if n["mask"]:
            extra.append(f"mask={n['mask']:#x}")
        if n["kind"] in ("value", "cell") and not n["flags"] & 0x46:
            extra.append(f"{n['min']}..{n['max']}")
        lines.append(f"{'  ' * depth}{n['label']}  " + "  ".join(e for e in extra if e))
        for c in n["children"]:
            walk(c, depth + 1)

    walk(0, 0)
    return "\n".join(lines) + "\n"


def copy_fonts(demo: Path, target: Path) -> list[Path]:
    """The July fonts from the demo's DATA\\FONT into target, each SHA-256 checked."""
    folder = next((d for d in (demo / "DATA" / "FONT", demo / "data" / "font") if d.is_dir()), None)
    if folder is None:
        raise TreeError(f"{demo}: no DATA\\FONT directory")
    on_disk = {p.name.upper(): p for p in folder.iterdir()}
    target.mkdir(parents=True, exist_ok=True)
    written = []
    for name, digest in FONTS.items():
        source = on_disk.get(name)
        if source is None:
            raise TreeError(f"{folder}: no {name}")
        if sha256(source) != digest:
            raise TreeError(f"{source}: SHA-256 {sha256(source)}, not the July demo's ({digest})")
        (target / name).write_bytes(source.read_bytes())
        written.append(target / name)
    return written


def build(exe: Path, target: Path) -> Path:
    """Extract, resolve and write tree.json, editor-tree.tsv and menu.txt into target, and the
    July fonts into target/fonts."""
    tree = extract(exe)
    write_json(target / "tree.json", tree)
    nodes = resolve(tree, read_bindings())
    resource = target / RESOURCE_NAME
    resource.write_text(
        resource_text(nodes, tree["source"]["sha256"], sha256(BINDINGS)),
        encoding="ascii",
        newline="\n",
    )
    (target / "menu.txt").write_text(menu_text(nodes), encoding="ascii", newline="\n")
    copy_fonts(exe.parent, target / FONT_DIR)
    return resource


def check_resources(folder: Path) -> list[str]:
    """What is wrong with a release's resources folder: the menu (check_resource) and the fonts."""
    problems = check_resource(folder / RESOURCE_NAME)
    for name, digest in FONTS.items():
        path = folder / FONT_DIR / name
        if not path.is_file():
            problems.append(f"{path} is missing")
        elif sha256(path) != digest:
            problems.append(f"{path}: not the July demo's {name}")
    return problems


def check_resource(path: Path) -> list[str]:
    """What is wrong with a bundled resource: missing, another source build, or stale bindings."""
    if not path.is_file():
        return [f"{path} is missing (set DREAMS_WIP_DIR to the July demo's DREAMS directory)"]
    head = dict(
        line.split("\t", 1)
        for line in path.read_text(encoding="ascii").splitlines()[:8]
        if "\t" in line and not line.startswith("#")
    )
    problems = []
    if head.get("version") != str(RESOURCE_VERSION):
        problems.append(f"{path}: version {head.get('version')}, expected {RESOURCE_VERSION}")
    if head.get("source") != JULY_SHA256:
        problems.append(
            f"{path}: source {head.get('source')}, not the July DREAMS.EXE {JULY_SHA256}"
        )
    if head.get("bindings") != sha256(BINDINGS):
        problems.append(f"{path}: made from another bindings.tsv than {BINDINGS}")
    return problems


def stage(build_dir: Path) -> Path | None:
    """build.py: write the resource and put it beside the executable as resources/editor-tree.tsv,
    with the July fonts in resources/fonts. Without the July demo, remove staged copies: the
    editor then keeps the retail two-node menu and retail's fonts."""
    staged = build_dir / "resources" / RESOURCE_NAME
    exe = july_exe()
    if not exe or not exe.is_file():
        staged.unlink(missing_ok=True)
        for name in FONTS:
            (staged.parent / FONT_DIR / name).unlink(missing_ok=True)
        print("editor menu: no July DREAMS.EXE (DREAMS_WIP_DIR); Develop keeps the retail menu")
        return None
    resource = build(exe, out_dir())
    (staged.parent / FONT_DIR).mkdir(parents=True, exist_ok=True)
    staged.write_bytes(resource.read_bytes())
    for name in FONTS:
        (staged.parent / FONT_DIR / name).write_bytes(
            (resource.parent / FONT_DIR / name).read_bytes()
        )
    return staged


# ---- main ----------------------------------------------------------------


def july_exe() -> Path | None:
    """DREAMS_WIP_DIR/DREAMS.EXE, or None when the setting is absent."""
    sys.path.insert(0, str(REPO / "src"))
    from dreams import paths

    folder = paths.configured("wip_dir")
    return folder / "DREAMS.EXE" if folder else None


def out_dir() -> Path:
    sys.path.insert(0, str(REPO / "src"))
    from dreams import paths

    path = paths.get("out") / "recomp" / "windream" / "editor"
    path.mkdir(parents=True, exist_ok=True)
    return path


def write_json(path: Path, value) -> None:
    path.write_text(json.dumps(value, indent=1) + "\n", encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "command",
        choices=("build", "show"),
        help="build: write the files; show: print the July tree",
    )
    ap.add_argument("--exe", type=Path, help="the July DREAMS.EXE (default: DREAMS_WIP_DIR)")
    args = ap.parse_args(argv)
    exe = args.exe or july_exe()
    if not exe or not exe.is_file():
        print("no July DREAMS.EXE: set DREAMS_WIP_DIR or pass --exe", file=sys.stderr)
        return 2
    try:
        if args.command == "show":
            sys.stdout.write(show(extract(exe)))
            return 0
        resource = build(exe, out_dir())
    except TreeError as e:
        print(f"editor menu: {e}", file=sys.stderr)
        return 1
    lines = resource.read_text(encoding="ascii").splitlines()
    count = next(line.split("\t")[1] for line in lines if line.startswith("nodes\t"))
    print(f"{resource}  {count} nodes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
