"""Write a project bank's mastering lists: listL0..4.txt and copyL0..4.bat.

Spec 008 phase 6. Retail GDIDREAM.EXE and WINDREAM.EXE keep the October
editor's two mastering generators, with no caller:

    0x447b72  listL<g>.txt  the files level group g needs (what CD_PrepareLevel
                            0x427d64 copies to the hard disk, as ListL<g>.txt)
    0x447e7c  copyL<g>.bat  COPY lines that stage those files, the movies too,
                            into D:\\CD1\\DATA (groups 0-2) and D:\\CD2\\DATA (0, 3, 4)

This replays both over a DREAMS.DAT with the same C semantics: the flat bank of
150 records of 0x2200 bytes (as at 0x659044), strcpy up to the NUL, strupr of
the last three bytes of a 3DC-folder name only, its .DAN twin when those are
"3DC", no empty-name test for a live OBJET, no sorting or de-duplication, and
fopen "wt" line ends (CRLF). On disc 1's bank the lists are the shipped
LISTL0..4.TXT byte for byte (tests/recomp/test_mastering_lists.py).

By default it reads the developer folder's DREAMS.DAT (--tree, else WD_TREE,
else DREAMS_OUT/recomp/windream/developer, the folder run.py --mode dev uses)
and writes the ten files into the folder's root. An existing file whose name
matches case-insensitively (the folder's LISTL0.TXT, copied from disc 1) is
written over under its own name, so a case-sensitive file system does not get a
second file. --check lists, as information, the listed files that are on no
disc, on the wrong disc for their group, or not in the folder, and writes
nothing unless --out is given.

    python recomp/windream/debug/mastering_lists.py [--tree DIR] [--bank DAT] [--out DIR] [--check]

Standard library only (it runs under WSL python3 without uv). Game data: the
lists are generated from the bank, so they stay under out/.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))

from dreams import paths  # noqa: E402
from dreams.formats import project  # noqa: E402

GROUPS = 5  # FILE*[5], indexed by the record's group with no range check

# Record fields (offset in one 0x2200 record; retail address 0x659044 + i*0x2200 + offset)
F_LIVE = 0x14  # bit 0: in use (0x659058)
F_HNM = 0x3C  # level intro movie, DATA\HNM (copyL only)
F_ANIM = 0x5C  # HNM4 animated texture, DATA\ANIM
F_PLAYER = 0x8C  # player model replacement, DATA\3DC
F_GROUP = 0x1FC  # level group 0..4 (SCENE_GetLevelNumber)
OBJET_LIVE, OBJET_FILE = 0x34, 0x0C
ADVENT_LIVE, ADVENT_HNM = 0x28, 0x2C  # LINKADVENT movie (copyL only)

CRLF = b"\r\n"  # fopen_(name, "wt") at 0x447bcb / 0x447ec2: Watcom text mode
CD1, CD2 = (0, 1, 2), (0, 3, 4)  # the groups copyL stages to each disc


def read_bank(path: str | Path) -> bytes:
    """A DREAMS.DAT as the flat bank at 0x659044."""
    records = project.records(path)
    for i, record in enumerate(records):
        if len(record) != project.RECORD_SIZE:
            raise ValueError(f"{path}: record {i} decodes to {len(record):#x} bytes, not 0x2200")
    return b"".join(records)


def cstr(bank: bytes, at: int) -> bytes:
    """strcpy_: up to the first NUL, wherever it is in the flat bank."""
    end = bank.find(b"\0", at)
    return bank[at : end if end >= 0 else len(bank)]


def upper_ascii(name: bytes) -> bytes:
    return bytes(c - 32 if 0x61 <= c <= 0x7A else c for c in name)


def up3(name: bytes) -> bytes:
    """strupr_(s + strlen(s) - 3). A name under three bytes makes retail start
    before the buffer; the visible string is then upper-cased whole."""
    if len(name) < 3:
        return upper_ascii(name)
    return name[:-3] + upper_ascii(name[-3:])


def dan_twin(name: bytes) -> bytes | None:
    """After up3: strcmp_(ext, "3DC") == 0, then strcpy_ "DAN" over it."""
    if len(name) >= 3 and name[-3:] == b"3DC":
        return name[:-3] + b"DAN"
    return None


def live_records(bank: bytes):
    """(index, record offset, group) of every record in use, in bank order."""
    for i in range(len(bank) // project.RECORD_SIZE):
        r = i * project.RECORD_SIZE
        if bank[r + F_LIVE] & 1:
            group = struct.unpack_from("<i", bank, r + F_GROUP)[0]
            if not 0 <= group < GROUPS:
                # Retail would index FILE*[5] with it: undefined behaviour.
                raise ValueError(f"record {i}: group {group} is outside 0..4")
            yield i, r, group


def model_names(bank: bytes, r: int):
    """The 3DC-folder names of one record in retail order, live OBJETs 0..15 then
    the player model, each as (name after up3, its .DAN twin or None)."""
    for j in range(project.OBJETS):
        o = r + project.OBJET_AT + j * project.OBJET_SIZE
        if bank[o + OBJET_LIVE] & 1:  # no empty-name test (0x447c51)
            name = up3(cstr(bank, o + OBJET_FILE))
            yield name, dan_twin(name)
    if bank[r + F_PLAYER]:
        name = up3(cstr(bank, r + F_PLAYER))
        yield name, dan_twin(name)


def listl(bank: bytes) -> list[list[bytes]]:
    """0x447b72: the lines of listL0..4.txt."""
    out: list[list[bytes]] = [[] for _ in range(GROUPS)]
    for _i, r, group in live_records(bank):
        lines = out[group]
        for name, twin in model_names(bank, r):
            lines.append(b"DATA\\3DC\\" + name)
            if twin is not None:
                lines.append(b"DATA\\3DC\\" + twin)
        if bank[r + F_ANIM]:
            lines.append(b"DATA\\ANIM\\" + cstr(bank, r + F_ANIM))  # case as stored
    out[0] += [b"DATA\\3DC\\XH_.dan", b"DATA\\3DC\\MHE.dan"]  # 0x447e35, 0x447e47
    return out


def copy_lines(group: int, folder: bytes, name: bytes) -> list[bytes]:
    source = b"COPY DATA\\" + folder + b"\\" + name
    lines = []
    if group in CD1:
        lines.append(source + b" D:\\CD1\\DATA\\" + folder)
    if group in CD2:
        lines.append(source + b" D:\\CD2\\DATA\\" + folder)
    return lines


def copyl(bank: bytes) -> list[list[bytes]]:
    """0x447e7c: the lines of copyL0..4.bat."""
    out: list[list[bytes]] = [[] for _ in range(GROUPS)]
    for _i, r, group in live_records(bank):
        lines = out[group]
        for name, twin in model_names(bank, r):
            lines += copy_lines(group, b"3DC", name)
            if twin is not None:
                lines += copy_lines(group, b"3DC", twin)
        if bank[r + F_ANIM]:
            lines += copy_lines(group, b"ANIM", cstr(bank, r + F_ANIM))
        if bank[r + F_HNM]:
            lines += copy_lines(group, b"HNM", cstr(bank, r + F_HNM))
        for j in range(project.ADVENTS):
            a = r + project.ADVENT_AT + j * project.ADVENT_SIZE
            if bank[a + ADVENT_LIVE] & 1 and bank[a + ADVENT_HNM]:
                lines += copy_lines(group, b"HNM", cstr(bank, a + ADVENT_HNM))
    out[0] += FIXED_COPYL0
    return out


# The fixed lines 0x447e7c appends to copyL0 (0x448597-0x448667), in that order.
FIXED_COPYL0 = [
    b"COPY DATA\\3DC\\XH_.dan D:\\CD1\\DATA\\3DC",
    b"COPY DATA\\3DC\\MHE.dan D:\\CD1\\DATA\\3DC",
    b"COPY DATA\\3DC\\XH_.dan D:\\CD2\\DATA\\3DC",
    b"COPY DATA\\3DC\\MHE.dan D:\\CD2\\DATA\\3DC",
    b"COPY DATA\\3DC\\DIALOG.DRD D:\\CD1\\DATA\\3DC",
    b"COPY DATA\\3DC\\DIALOG.DRD D:\\CD2\\DATA\\3DC",
    b"COPY DATA\\3DC\\*.3DC D:\\CD1\\DATA\\3DC",
    b"COPY DATA\\3DC\\*.3DM D:\\CD1\\DATA\\3DC",
    b"COPY DATA\\3DC\\*.3DC D:\\CD2\\DATA\\3DC",
    b"COPY DATA\\3DC\\*.3DM D:\\CD2\\DATA\\3DC",
    b"COPY DATA\\HNM\\GENERIC.* D:\\CD1\\DATA\\HNM",
    b"COPY DATA\\HNM\\GENERIC.* D:\\CD2\\DATA\\HNM",
]


def render(lines: list[bytes]) -> bytes:
    return b"".join(line + CRLF for line in lines)


def generate(bank: bytes) -> dict[str, bytes]:
    """The ten files, by the names retail's sprintf gives them."""
    files = {}
    for g, lines in enumerate(listl(bank)):
        files[f"listL{g}.txt"] = render(lines)
    for g, lines in enumerate(copyl(bank)):
        files[f"copyL{g}.bat"] = render(lines)
    return files


def matching(directory: Path, name: str) -> list[Path]:
    """The entries of directory whose name equals name case-insensitively."""
    if not directory.is_dir():
        return []
    want = name.upper()
    return sorted(p for p in directory.iterdir() if p.name.upper() == want)


def write(files: dict[str, bytes], directory: Path) -> list[Path]:
    """Write each file into directory, over an existing case-insensitive match
    under that match's name; further matches (a case-sensitive file system with
    both LISTL0.TXT and listL0.txt) are removed, so one file is left."""
    directory.mkdir(parents=True, exist_ok=True)
    written = []
    for name, data in files.items():
        found = matching(directory, name)
        target = found[0] if found else directory / name
        target.write_bytes(data)
        for extra in found[1:]:
            extra.unlink()
        written.append(target)
    return written


def default_tree() -> Path:
    tree = os.environ.get("WD_TREE", "").strip()
    if tree:
        return Path(tree)
    return paths.get("out") / "recomp" / "windream" / "developer"


def find_bank(tree: Path) -> Path:
    found = [p for p in matching(tree, "DREAMS.DAT") if p.is_file()]
    if not found:
        raise FileNotFoundError(f"{tree} has no DREAMS.DAT")
    return found[0]


# ---- --check ----


def sources(files: dict[str, bytes]) -> dict[str, dict[str, set[str]]]:
    """Every named (non-wildcard) source, as "DIR\\NAME" upper-cased, with the
    files that name it and the discs a copyL line stages it to."""
    found: dict[str, dict[str, set[str]]] = {}
    for file, data in files.items():
        for line in data.decode("latin-1").split("\r\n"):
            if not line:
                continue
            if file.startswith("copyL"):
                _, source, target = line.split(" ")
                disc = target.split("\\")[1]  # CD1 or CD2
            else:
                source, disc = line, None
            if "*" in source:
                continue
            key = source.split("\\", 1)[1].upper()
            entry = found.setdefault(key, {"files": set(), "discs": set()})
            entry["files"].add(file)
            if disc:
                entry["discs"].add(disc)
    return found


def index(root: Path) -> set[str]:
    """ "DIR\\NAME" upper-cased for every file in root's DATA\\3DC, ANIM and HNM."""
    names: set[str] = set()
    for data in matching(root, "DATA"):
        for folder in data.iterdir():
            if folder.is_dir() and folder.name.upper() in ("3DC", "ANIM", "HNM"):
                names |= {f"{folder.name.upper()}\\{p.name.upper()}" for p in folder.iterdir()}
    return names


def check(files: dict[str, bytes], tree: Path | None) -> None:
    """Print which listed files are on no disc, on the wrong disc for the lines
    that stage them, or missing from the folder. Information only: retail lists
    and skips such names (CD_CopyFileList 0x428452 treats a missing source as
    copied), and the shipped lists name 22 of them."""
    named = sources(files)
    roots: dict[str, set[str]] = {}
    for n in (1, 2):
        root = paths.configured(f"disc{n}")
        if root is not None and root.is_dir():
            roots[f"CD{n}"] = index(root)
        else:
            print(f"disc {n}: DREAMS_DISC{n} is not an extracted disc here, not checked")
    print(f"{len(named)} named sources in the ten files")
    if roots:
        none = sorted(k for k in named if not any(k in s for s in roots.values()))
        print(f"on no disc: {len(none)}")
        for k in none:
            print(f"    {k}  ({', '.join(sorted(named[k]['files']))})")
        wrong = [
            f"    {k} -> {disc}"
            for k in sorted(named)
            if k not in none
            for disc in sorted(named[k]["discs"])
            if disc in roots and k not in roots[disc]
        ]
        print(f"on a disc, but not on the disc a copyL line stages it to: {len(wrong)}")
        for line in wrong:
            print(line)
    if tree is not None and tree.is_dir():
        have = index(tree)
        absent = sorted(k for k in named if k not in have)
        print(f"not in {tree}: {len(absent)}")
        for k in absent:
            print(f"    {k}")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument(
        "--tree",
        type=Path,
        help="the developer folder (default WD_TREE, else DREAMS_OUT/recomp/windream/developer)",
    )
    ap.add_argument("--bank", type=Path, help="the DREAMS.DAT to read (default the tree's)")
    ap.add_argument("--out", type=Path, help="where to write (default the tree's root)")
    ap.add_argument(
        "--check",
        action="store_true",
        help="report listed files on no disc or not in the tree; writes nothing without --out",
    )
    a = ap.parse_args(argv)
    tree = a.tree or (None if a.bank and a.out else default_tree())
    bank_path = a.bank or find_bank(tree)
    out = a.out or tree
    bank = read_bank(bank_path)
    files = generate(bank)
    written = [] if a.check and not a.out else write(files, out)
    print(f"{bank_path}: {sum(1 for _ in live_records(bank))} records in use")
    for path in written:
        lines = path.read_bytes().count(CRLF)
        print(f"wrote {path} ({lines} lines)")
    if a.check:
        check(files, tree)
    return 0


if __name__ == "__main__":
    sys.exit(main())
