"""The mastering lists (spec 008 phase 6): recomp/windream/debug/mastering_lists.py.

On the unedited disc 1 bank the five lists are the shipped LISTL0..4.TXT byte
for byte and copyL0..4.bat have the SHA-256 values of the research brief
(out/research/phase6-brief); disc 2's bank gives the same bytes. Those skip
without DREAMS_DISC1 or DREAMS_DISC2. A synthetic bank checks the rules the
shipped data never exercises.
"""

import hashlib
import importlib.util
import struct
from pathlib import Path

import pytest

from dreams import paths

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ml = load("recomp/windream/debug/mastering_lists.py")
bank_patch = load("recomp/windream/debug/bank_patch.py")

LIST_LINES = [6, 241, 166, 222, 164]
COPY_LINES = [24, 271, 186, 247, 187]
COPY_SHA256 = [
    "ebbff95088633e4b7be354fd50f9215079ac73ce9ed95a03c3a30c2bb557400a",
    "5ecd8ca91d6a8118839458e0fc8af939af4232fa858f609b950c676cc7b5f97b",
    "d9005d93591351df7ad5e6d82dadb8bcdaf3e77437bf96a5ed8ab68dd3b1ed40",
    "953773abaec60e27cd31845a389803c18f59d07525f41f4478ad84708063be05",
    "f8a81a5a0ae602d93449f4a6a53430a798e54fcc35e2a53ea0115a96b1abf89b",
]


def disc_root(n):
    root = paths.configured(f"disc{n}")
    if root is None or not root.is_dir():
        pytest.skip(f"DREAMS_DISC{n} is not configured")
    return root


def disc_bank(n):
    return ml.find_bank(disc_root(n))


# ---- the shipped discs ----


def test_disc1_lists_are_the_shipped_ones(tmp_path):
    root = disc_root(1)
    assert ml.main(["--bank", str(disc_bank(1)), "--out", str(tmp_path)]) == 0
    for g in range(5):
        (shipped,) = ml.matching(root, f"LISTL{g}.TXT")
        mine = (tmp_path / f"listL{g}.txt").read_bytes()
        assert mine == shipped.read_bytes(), f"listL{g}.txt"
        assert mine.count(b"\r\n") == LIST_LINES[g]
        copy = (tmp_path / f"copyL{g}.bat").read_bytes()
        assert copy.count(b"\r\n") == COPY_LINES[g]
        assert hashlib.sha256(copy).hexdigest() == COPY_SHA256[g], f"copyL{g}.bat"


def test_disc2_bank_gives_the_same_files():
    one = ml.generate(ml.read_bank(disc_bank(1)))
    two = ml.generate(ml.read_bank(disc_bank(2)))
    assert one == two


# ---- a synthetic bank ----


def record(live=True, group=0):
    rec = bytearray(0x2200)
    rec[:8] = b"Project0"
    rec[ml.F_LIVE] = 1 if live else 0
    struct.pack_into("<i", rec, ml.F_GROUP, group)
    return rec


def put(rec, at, text):
    rec[at : at + len(text) + 1] = text + b"\0"


def objet(rec, j, name, live=True):
    o = 0x600 + 0xC0 * j
    rec[o + ml.OBJET_LIVE] = 1 if live else 0
    put(rec, o + ml.OBJET_FILE, name)


def advent(rec, j, movie, live=True):
    a = 0x1E00 + 0x40 * j
    rec[a + ml.ADVENT_LIVE] = 1 if live else 0
    put(rec, a + ml.ADVENT_HNM, movie)


def write_bank(path, records):
    records = records + [bytearray(0x2200) for _ in range(150 - len(records))]
    packed = [bank_patch.pack(bytes(r)) for r in records]
    offsets, at = [], 0
    for chunk in packed:
        offsets.append(at)
        at += len(chunk)
    offsets.append(at)
    head = struct.pack("<151I", *offsets)
    path.write_bytes(head + bytes(0x400 - len(head)) + b"".join(packed))
    return path


def test_synthetic_rules(tmp_path):
    one = record(group=1)
    objet(one, 0, b"x.3dc")  # strupr of the extension only, then the .DAN twin
    objet(one, 1, b"")  # live with an empty name: still printed
    objet(one, 2, b"SKIP.3DC", live=False)
    put(one, ml.F_ANIM, b"lowanim.ubb")  # stays as stored
    zero = record(group=0)
    objet(zero, 0, b"A.3DC")
    put(zero, ml.F_HNM, b"INTRO.HNM")
    advent(zero, 0, b"NO.UBB", live=False)  # bit 0 clear: skipped
    advent(zero, 1, b"YES.UBB")
    idle = record(live=False, group=7)  # not in use: its group is never read
    objet(idle, 0, b"IDLE.3DC")
    bank = ml.read_bank(write_bank(tmp_path / "DREAMS.DAT", [one, zero, idle]))

    lists = ml.listl(bank)
    assert lists[1] == [
        b"DATA\\3DC\\x.3DC",
        b"DATA\\3DC\\x.DAN",
        b"DATA\\3DC\\",
        b"DATA\\ANIM\\lowanim.ubb",
    ]
    assert lists[0] == [
        b"DATA\\3DC\\A.3DC",
        b"DATA\\3DC\\A.DAN",
        b"DATA\\3DC\\XH_.dan",
        b"DATA\\3DC\\MHE.dan",
    ]
    assert lists[2] == lists[3] == lists[4] == []

    copies = ml.copyl(bank)
    assert copies[0][: -len(ml.FIXED_COPYL0)] == [
        b"COPY DATA\\3DC\\A.3DC D:\\CD1\\DATA\\3DC",
        b"COPY DATA\\3DC\\A.3DC D:\\CD2\\DATA\\3DC",
        b"COPY DATA\\3DC\\A.DAN D:\\CD1\\DATA\\3DC",
        b"COPY DATA\\3DC\\A.DAN D:\\CD2\\DATA\\3DC",
        b"COPY DATA\\HNM\\INTRO.HNM D:\\CD1\\DATA\\HNM",
        b"COPY DATA\\HNM\\INTRO.HNM D:\\CD2\\DATA\\HNM",
        b"COPY DATA\\HNM\\YES.UBB D:\\CD1\\DATA\\HNM",
        b"COPY DATA\\HNM\\YES.UBB D:\\CD2\\DATA\\HNM",
    ]
    assert copies[1] == [
        b"COPY DATA\\3DC\\x.3DC D:\\CD1\\DATA\\3DC",
        b"COPY DATA\\3DC\\x.DAN D:\\CD1\\DATA\\3DC",
        b"COPY DATA\\3DC\\ D:\\CD1\\DATA\\3DC",
        b"COPY DATA\\ANIM\\lowanim.ubb D:\\CD1\\DATA\\ANIM",
    ]
    assert ml.render([b"a", b"b"]) == b"a\r\nb\r\n"


def test_group_outside_0_to_4_is_an_error(tmp_path):
    bank = ml.read_bank(write_bank(tmp_path / "DREAMS.DAT", [record(group=5)]))
    with pytest.raises(ValueError, match="outside 0..4"):
        ml.listl(bank)


def test_existing_list_is_written_over_case_insensitively(tmp_path):
    (tmp_path / "LISTL0.TXT").write_bytes(b"old\r\n")
    ml.write({"listL0.txt": b"new\r\n"}, tmp_path)
    entries = [p for p in tmp_path.iterdir() if p.name.upper() == "LISTL0.TXT"]
    assert [p.name for p in entries] == ["LISTL0.TXT"]
    assert entries[0].read_bytes() == b"new\r\n"


def test_tree_default_reads_the_folder_bank(tmp_path):
    write_bank(tmp_path / "dreams.dat", [record(group=0)])
    assert ml.main(["--tree", str(tmp_path)]) == 0
    assert (tmp_path / "listL0.txt").read_bytes() == b"DATA\\3DC\\XH_.dan\r\nDATA\\3DC\\MHE.dan\r\n"
    assert (tmp_path / "copyL0.bat").read_bytes() == ml.render(ml.FIXED_COPYL0)
