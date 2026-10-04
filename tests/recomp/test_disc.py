"""The disc library (recomp/disc), through its disc_list tool.

Needs the tool built (uv run python recomp/disc/build.py). The tests on the real
discs need DREAMS_DISC1 and DREAMS_DISC2 (the extracted trees, with the .cue and
the (Track NN).bin files in each one's parent folder); the rest build tiny
images in a temporary directory. Whatever is missing is skipped.
"""

import hashlib
import importlib.util
import random
import re
import shutil
import subprocess
from functools import cache
from pathlib import Path

import pytest

from dreams import paths

ROOT = Path(__file__).resolve().parents[2]
FRAME = 2352
SECTOR = 2048
PREGAP = 150


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# ---- the tool ----


@pytest.fixture(scope="session")
def tool():
    exe = load("recomp/disc/build.py").tool_path()
    if not exe.is_file():
        pytest.skip(f"disc_list is not built ({exe}); run: uv run python recomp/disc/build.py")
    return exe


def run(tool, *args):
    p = subprocess.run([str(tool), *map(str, args)], capture_output=True)
    return p.returncode, p.stdout, p.stderr.decode("utf-8", "replace")


def parse(out):
    """The listing as {disc, source, tracks: {number: dict}, files: {path: (size, sha)}}."""
    info = {"tracks": {}, "files": {}}
    for line in out.decode("utf-8").splitlines():
        f = line.split("\t")
        if f[0] == "DISC":
            info["disc"] = int(f[1])
        elif f[0] == "SOURCE":
            info["source"] = f[1]
        elif f[0] == "TRACKS":
            info["ntracks"] = int(f[1])
        elif f[0] == "TRACK":
            info["tracks"][int(f[1])] = {
                "audio": f[2] == "AUDIO",
                "sector": int(f[3]),
                "offset": int(f[4]),
                "length": int(f[5]),
                "path": f[6],
            }
        elif f[0] == "FILES":
            info["nfiles"] = int(f[1])
        else:
            info["files"][f[2]] = (int(f[0]), f[1])
    return info


def listing(tool, path, *flags):
    rc, out, err = run(tool, *flags, path)
    assert rc == 0, err
    return parse(out)


# ---- the real discs ----


def disc_root(n):
    root = paths.configured(f"disc{n}")
    if root is None or not root.is_dir():
        pytest.skip(f"DREAMS_DISC{n} is not configured")
    return root


def cue_of(n):
    cues = sorted(disc_root(n).parent.glob("*.cue"))
    if len(cues) != 1:
        pytest.skip(f"no single .cue beside DREAMS_DISC{n}")
    return cues[0]


def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 20):
            h.update(chunk)
    return h.hexdigest()


@cache
def tree_listing(root):
    """{UPPER/PATH: (size, sha256)} of an extracted tree, hashed here in Python."""
    return {
        p.relative_to(root).as_posix().upper(): (p.stat().st_size, sha256_of(p))
        for p in sorted(root.rglob("*"))
        if p.is_file()
    }


@cache
def cue_listing(tool, n):
    return listing(tool, cue_of(n))


def cue_tracks(cue):
    """The cue's own track table, read independently: {number: (file, index01 frame)}."""
    tracks, current = {}, None
    for line in cue.read_text().splitlines():
        line = line.strip()
        if m := re.match(r'FILE "(.*)" BINARY', line):
            current = m.group(1)
        elif m := re.match(r"TRACK (\d+)", line):
            number = int(m.group(1))
        elif m := re.match(r"INDEX 01 (\d+):(\d+):(\d+)", line):
            mm, ss, ff = map(int, m.groups())
            tracks[number] = (current, (mm * 60 + ss) * 75 + ff)
    return tracks


@pytest.mark.parametrize("n, files, ntracks", [(1, 1219, 12), (2, 409, 14)])
def test_cue_lists_the_extracted_tree(tool, n, files, ntracks):
    root, cue = disc_root(n), cue_of(n)
    got = cue_listing(tool, n)
    assert got["disc"] == n
    assert got["source"] == "cue"
    assert got["ntracks"] == ntracks == len(got["tracks"])
    assert got["nfiles"] == files == len(got["files"])
    want = tree_listing(root)
    assert got["files"].keys() == want.keys()
    wrong = [k for k in want if got["files"][k] != want[k]]  # size and SHA-256 together
    assert not wrong, wrong[:5]
    # Track 1 is data, the rest audio; each starts at the cue's INDEX 01.
    for number, (name, frame) in cue_tracks(cue).items():
        t, size = got["tracks"][number], (cue.parent / name).stat().st_size
        assert t["audio"] == (number != 1)
        assert Path(t["path"]) == cue.parent / name
        assert t["offset"] == frame * FRAME
        assert t["length"] == size - frame * FRAME


@pytest.mark.parametrize("n", [1, 2])
def test_directory_source_lists_the_same(tool, n):
    root = disc_root(n)
    got = listing(tool, root)
    cue = cue_listing(tool, n)
    assert got["source"] == "dir"
    assert got["disc"] == n
    assert got["files"] == cue["files"]
    # Audio comes from the sibling track files, with INDEX 01 from the cue beside them
    # (disc 2 track 5 starts 1 frame later than the usual 2 seconds).
    assert got["tracks"] == cue["tracks"]


def test_iso_lists_the_same(tool):
    iso = disc_root(1).parent / "disc1.iso"
    if not iso.is_file():
        pytest.skip("no disc1.iso beside DREAMS_DISC1")
    got = listing(tool, iso, "--no-hash")
    assert got["source"] == "iso"
    assert got["ntracks"] == 1
    assert {k: v[0] for k, v in got["files"].items()} == {
        k: v[0] for k, v in cue_listing(tool, 1)["files"].items()
    }


def test_lookup_ignores_case_separators_and_version(tool):
    cue = cue_of(1)
    want = cue_listing(tool, 1)["files"]["DATA/1CD.ID"]
    for guest in [
        "DATA\\1CD.ID",
        "data/1cd.id",
        "\\Data\\1CD.ID;1",
        "/data/./../data/1cd.id",
        "DATA\\1CD.ID.",
    ]:
        rc, out, err = run(tool, "--find", guest, cue)
        assert rc == 0, (guest, err)
        kind, name, size, sha = out.decode().strip().split("\t")
        assert (kind, name, (int(size), sha)) == ("FILE", "1CD.ID", want)
    rc, out, _ = run(tool, "--find", "data", cue)
    assert rc == 0 and out.startswith(b"DIR\tDATA\t")
    for guest in ["DATA\\NOPE.ID", "..\\DATA", "DATA\\1CD.ID\\X"]:
        rc, _, err = run(tool, "--find", guest, cue)
        assert rc == 1 and "not found" in err


def test_reads_at_any_offset_match_the_files(tool):
    cue, root = cue_of(1), disc_root(1)
    rng = random.Random(7)
    for guest in ["DATA/1CD.ID", "DREAMS.DAT", "DATA/3DC/DIALOG.DRD"]:
        data = (root / guest).read_bytes()
        spans = [(0, 5), (0, len(data)), (len(data) - 3, 10), (len(data), 4), (len(data) + 100, 4)]
        if len(data) > 100000:  # cross sector and read-chunk boundaries
            spans += [(2040, 20), (2047, 2), (2048 * 16 - 3, 6000), (2048 * 15, 2048 * 40 + 5)]
            spans += [(rng.randrange(len(data)), rng.randrange(1, 90000)) for _ in range(6)]
        for offset, count in spans:
            rc, out, err = run(tool, "--cat", guest, offset, count, cue)
            assert rc == 0, err
            assert out == data[offset : offset + count], (guest, offset, count)


# ---- single-file cues ----


def msf(frames):
    return f"{frames // (60 * 75):02d}:{frames // 75 % 60:02d}:{frames % 75:02d}"


@pytest.fixture
def single_bin(tmp_path):
    """Disc 2's data track and its first audio tracks as one .bin, with a cue to match."""
    cue = cue_of(2)
    members = [1, 2, 3, 4, 5]
    files = cue_tracks(cue)
    lines, start = ['FILE "single.bin" BINARY'], 0
    with open(tmp_path / "single.bin", "wb") as out:
        for number in members:
            src = cue.parent / files[number][0]
            size = src.stat().st_size
            assert size % FRAME == 0
            lines.append(f"  TRACK {number:02d} {'MODE1/2352' if number == 1 else 'AUDIO'}")
            if number > 1:
                lines.append(f"    INDEX 00 {msf(start // FRAME)}")
            lines.append(f"    INDEX 01 {msf((start + files[number][1] * FRAME) // FRAME)}")
            with open(src, "rb") as f:
                shutil.copyfileobj(f, out, 1 << 22)
            start += size
    (tmp_path / "single.cue").write_text("\n".join(lines) + "\n")
    yield tmp_path / "single.cue", members
    shutil.rmtree(tmp_path, ignore_errors=True)


def test_single_bin_cue_gives_the_same_disc(tool, single_bin):
    cue, members = single_bin
    got = listing(tool, cue)
    ref = cue_listing(tool, 2)
    assert got["files"] == ref["files"]
    assert got["disc"] == 2
    assert got["ntracks"] == len(members)
    big = cue.parent / "single.bin"
    for number in members:
        a, b = got["tracks"][number], ref["tracks"][number]
        assert (a["audio"], a["sector"], a["length"]) == (b["audio"], b["sector"], b["length"])
        assert Path(a["path"]) == big
        if number > 1:  # the same audio bytes, at another offset in another file
            with open(a["path"], "rb") as f1, open(b["path"], "rb") as f2:
                f1.seek(a["offset"])
                f2.seek(b["offset"])
                assert (
                    hashlib.sha256(f1.read(a["length"])).digest()
                    == hashlib.sha256(f2.read(b["length"])).digest()
                )


# ---- tiny images ----


def le(v, n):
    return v.to_bytes(n, "little")


def be(v, n):
    return v.to_bytes(n, "big")


def record(extent, size, name, directory=False):
    n = 33 + len(name)
    rec = bytearray(n + (n & 1))
    rec[0] = len(rec)
    rec[2:10] = le(extent, 4) + be(extent, 4)
    rec[10:18] = le(size, 4) + be(size, 4)
    rec[25] = 2 if directory else 0
    rec[28:32] = le(1, 2) + be(1, 2)
    rec[32] = len(name)
    rec[33 : 33 + len(name)] = name
    return bytes(rec)


README = bytes(i * 7 % 251 for i in range(3000))
FILES = {
    "DATA/1CD.ID": b"DISC1",
    "README.TXT": README,
    "NOEXT": b"x" * 2049,
}


def tiny_iso():
    """A 25-sector ISO 9660 image.

    Sectors: root directory 18, DATA 19, 1CD.ID 20, README.TXT 21-22, NOEXT 23-24."""
    dot, dotdot = bytes([0]), bytes([1])

    def directory(extent, entries):
        data = (
            record(extent, SECTOR, dot, True) + record(18, SECTOR, dotdot, True) + b"".join(entries)
        )
        return data.ljust(SECTOR, b"\0")

    sectors = [bytes(SECTOR)] * 16
    pvd = bytearray(SECTOR)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[80:88] = le(25, 4) + be(25, 4)
    pvd[128:132] = le(SECTOR, 2) + be(SECTOR, 2)
    pvd[156:190] = record(18, SECTOR, dot, True)[:34]
    end = bytearray(SECTOR)
    end[0:7] = b"\xffCD001\x01"
    root = directory(
        18,
        [
            record(19, SECTOR, b"DATA", True),
            record(21, len(README), b"README.TXT;1"),
            record(23, 2049, b"NOEXT."),
        ],
    )
    data = directory(19, [record(20, 5, b"1CD.ID;1")])
    body = (
        [b"DISC1".ljust(SECTOR, b"\0")]
        + [README.ljust(2 * SECTOR, b"\0")]
        + [b"x" * 2049 + bytes(SECTOR * 2 - 2049)]
    )
    sectors += [bytes(pvd), bytes(end), root, data] + body
    return b"".join(sectors)


def raw_sectors(iso):
    """2048-byte sectors as MODE1/2352: sync, header, user data, 288 bytes of EDC/ECC (zero)."""
    sync = b"\0" + b"\xff" * 10 + b"\0"
    return b"".join(
        sync + bytes([0, 2, i % 75, 1]) + iso[i * SECTOR : (i + 1) * SECTOR] + bytes(288)
        for i in range(len(iso) // SECTOR)
    )


def check_tiny(tool, source):
    got = listing(tool, source)
    assert {k: v[0] for k, v in got["files"].items()} == {k: len(v) for k, v in FILES.items()}
    for k, v in FILES.items():
        assert got["files"][k][1] == hashlib.sha256(v).hexdigest()
    assert got["disc"] == 1
    return got


def test_tiny_iso(tool, tmp_path):
    (tmp_path / "t.iso").write_bytes(tiny_iso())
    got = check_tiny(tool, tmp_path / "t.iso")
    assert got["source"] == "iso" and got["ntracks"] == 1


def test_tiny_iso_in_a_non_ascii_directory(tool, tmp_path):
    d = tmp_path / "Dréams ü ひ"
    d.mkdir()
    (d / "díçc.iso").write_bytes(tiny_iso())
    check_tiny(tool, d / "díçc.iso")


def test_tiny_raw_bin_cue(tool, tmp_path):
    (tmp_path / "t.bin").write_bytes(raw_sectors(tiny_iso()))
    (tmp_path / "t.cue").write_text(
        'FILE "t.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    )
    got = check_tiny(tool, tmp_path / "t.cue")
    assert got["tracks"][1]["sector"] == 2352 and not got["tracks"][1]["audio"]


def test_tiny_cooked_bin_cue_with_unusual_cue_formatting(tool, tmp_path):
    (tmp_path / "t.bin").write_bytes(tiny_iso())
    cue = (
        "REM comment\r\nCATALOG 0000000000000\r\nfile t.bin BINARY\r\n"
        "  track 01 mode1/2048\r\n    index 01 00:00:00\r\n"
    )
    (tmp_path / "t.cue").write_bytes(b"\xef\xbb\xbf" + cue.encode())  # with a UTF-8 BOM
    check_tiny(tool, tmp_path / "t.cue")


def test_tiny_reads_cross_sectors(tool, tmp_path):
    (tmp_path / "t.bin").write_bytes(raw_sectors(tiny_iso()))
    (tmp_path / "t.cue").write_text(
        'FILE "t.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    )
    for offset, count in [(0, 3000), (2040, 20), (2047, 2), (2999, 5), (3000, 5), (5, 0)]:
        rc, out, err = run(tool, "--cat", "readme.txt", offset, count, tmp_path / "t.cue")
        assert rc == 0, err
        assert out == README[offset : offset + count]


def test_single_bin_with_index_00_and_with_pregap(tool, tmp_path):
    """Audio after the data track in one file: INDEX 00 stored, or PREGAP not stored."""
    data = raw_sectors(tiny_iso())
    n = len(data) // FRAME
    rng = random.Random(3)
    t2 = rng.randbytes((PREGAP + 100) * FRAME)  # 2 s of pregap, then 100 frames
    t3 = rng.randbytes(60 * FRAME)  # no pregap in the file
    (tmp_path / "t.bin").write_bytes(data + t2 + t3)
    cue = [
        'FILE "t.bin" BINARY',
        "  TRACK 01 MODE1/2352",
        "    INDEX 01 00:00:00",
        "  TRACK 02 AUDIO",
        f"    INDEX 00 {msf(n)}",
        f"    INDEX 01 {msf(n + PREGAP)}",
        "  TRACK 03 AUDIO",
        "    PREGAP 00:02:00",
        f"    INDEX 01 {msf(n + PREGAP + 100)}",
    ]
    (tmp_path / "t.cue").write_text("\n".join(cue) + "\n")
    got = listing(tool, tmp_path / "t.cue")
    assert got["tracks"][1]["length"] == len(data)
    assert (got["tracks"][2]["offset"], got["tracks"][2]["length"]) == (
        (n + PREGAP) * FRAME,
        100 * FRAME,
    )
    assert (got["tracks"][3]["offset"], got["tracks"][3]["length"]) == (
        (n + PREGAP + 100) * FRAME,
        60 * FRAME,
    )


# ---- damaged and unusable input ----


def fails(tool, source):
    rc, out, err = run(tool, "--no-hash", source)
    assert rc == 1, (rc, err)  # an error exit, not a crash (which is a signal or a large status)
    assert err.startswith("disc_list: ") and len(err.strip()) > len("disc_list: "), err
    assert out == b""
    return err


def test_truncated_data_track(tool, tmp_path):
    src = cue_tracks(cue_of(2))[1][0]
    head = (cue_of(2).parent / src).open("rb").read(40 << 20)
    (tmp_path / "t.bin").write_bytes(head)
    (tmp_path / "t.cue").write_text(
        'FILE "t.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    )
    assert "truncated" in fails(tool, tmp_path / "t.cue")


@pytest.mark.parametrize("keep", [0, 1000, 16 * SECTOR, 20 * SECTOR, 24 * SECTOR])
def test_truncated_tiny_iso(tool, tmp_path, keep):
    (tmp_path / "t.iso").write_bytes(tiny_iso()[:keep])
    fails(tool, tmp_path / "t.iso")


def test_cue_naming_a_missing_file(tool, tmp_path):
    (tmp_path / "t.cue").write_text(
        'FILE "gone.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
    )
    assert "missing file" in fails(tool, tmp_path / "t.cue")


@pytest.mark.parametrize(
    "cue, word",
    [
        ("", "no TRACK"),
        ('FILE "t.bin" WAVE\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n', "FILE type"),
        ('FILE "t.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n', "mode"),
        ('FILE "t.bin" BINARY\n  TRACK 01 MODE1/2352\n', "INDEX 01"),
        ('FILE "t.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:99:00\n', "INDEX"),
        ('FILE "t.bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 99:00:00\n', "past the end"),
        (
            'FILE "t.bin" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n  TRACK 01 AUDIO\n',
            "increase",
        ),
        ('FILE "t.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n', "no data track"),
        ("TRACK 01 MODE1/2352\n", "before any FILE"),
    ],
)
def test_bad_cue_sheets(tool, tmp_path, cue, word):
    (tmp_path / "t.bin").write_bytes(raw_sectors(tiny_iso()))
    (tmp_path / "t.cue").write_text(cue)
    assert word in fails(tool, tmp_path / "t.cue")


@pytest.mark.parametrize("suffix", [".iso", ".cue"])
def test_a_file_of_zeros(tool, tmp_path, suffix):
    (tmp_path / f"z{suffix}").write_bytes(bytes(1 << 20))
    fails(tool, tmp_path / f"z{suffix}")


@pytest.mark.parametrize("suffix", [".iso", ".cue"])
def test_a_file_of_random_bytes(tool, tmp_path, suffix):
    (tmp_path / f"r{suffix}").write_bytes(random.Random(11).randbytes(1 << 20))
    fails(tool, tmp_path / f"r{suffix}")


def test_missing_and_unsupported_paths(tool, tmp_path):
    assert "no such file" in fails(tool, tmp_path / "nothing.cue")
    (tmp_path / "t.zip").write_bytes(b"PK")
    assert "unpack or convert" in fails(tool, tmp_path / "t.zip")
    (tmp_path / "t.bin").write_bytes(b"")
    assert ".cue" in fails(tool, tmp_path / "t.bin")


def patched(iso, offset, data):
    return iso[:offset] + data + iso[offset + len(data) :]


ROOT_DIR = 18 * SECTOR
DATA_DIR = 19 * SECTOR
# the root's "README.TXT;1" record starts after ".", ".." (34 bytes each) and "DATA" (38)
README_REC = ROOT_DIR + 34 + 34 + 38


@pytest.mark.parametrize(
    "name, damage, word",
    [
        ("not iso", lambda i: patched(i, 16 * SECTOR + 1, b"XXXXX"), "not an ISO 9660"),
        ("block size", lambda i: patched(i, 16 * SECTOR + 128, le(1024, 2)), "block size"),
        ("volume too big", lambda i: patched(i, 16 * SECTOR + 80, le(5000, 4)), "truncated"),
        ("extent past the end", lambda i: patched(i, README_REC + 2, le(900, 4)), "truncated"),
        ("size past the end", lambda i: patched(i, README_REC + 10, le(1 << 30, 4)), "truncated"),
        (
            "record runs off the directory",
            lambda i: patched(i, 16 * SECTOR + 156 + 10, le(100, 4)),
            "bad directory record",
        ),
        ("record too short", lambda i: patched(i, README_REC, b"\x10"), "bad directory record"),
        ("name too long", lambda i: patched(i, README_REC + 32, b"\xfe"), "bad name length"),
        ("empty name", lambda i: patched(i, README_REC + 32, b"\x00"), "bad file name"),
        ("name with a slash", lambda i: patched(i, README_REC + 33, b"A/B"), "bad character"),
        ("root size zero", lambda i: patched(i, 16 * SECTOR + 156 + 10, le(0, 4)), "bad size"),
        (
            "root size huge",
            lambda i: patched(i, 16 * SECTOR + 156 + 10, le(0xFFFFFFFF, 4)),
            "bad size",
        ),
        (
            "root extent past the end",
            lambda i: patched(i, 16 * SECTOR + 156 + 2, le(5000, 4)),
            "truncated",
        ),
        ("directory loop", lambda i: patched(i, ROOT_DIR + 68 + 2, le(18, 4)), "contains itself"),
        ("interleaved", lambda i: patched(i, README_REC + 26, b"\x01"), "interleaved"),
        ("multi-extent", lambda i: patched(i, README_REC + 25, b"\x80"), "multi-extent"),
    ],
)
def test_damaged_iso(tool, tmp_path, name, damage, word):
    (tmp_path / "t.iso").write_bytes(damage(tiny_iso()))
    assert word in fails(tool, tmp_path / "t.iso"), name


def test_random_damage_never_crashes(tool, tmp_path):
    """Flip bytes in the volume descriptor and directories: a listing or an error, no crash."""
    rng = random.Random(2026)
    good = tiny_iso()
    for _ in range(150):
        iso = bytearray(good)
        for _ in range(rng.choice([1, 1, 2, 4])):
            at = rng.choice([16 * SECTOR, 18 * SECTOR, 19 * SECTOR]) + rng.randrange(0, 260)
            iso[at] = rng.randrange(256)
        (tmp_path / "t.iso").write_bytes(iso)
        rc, out, err = run(tool, tmp_path / "t.iso")
        assert rc in (0, 1), (rc, err)
        if rc:
            assert err.startswith("disc_list: ")


# ---- SHA-256 ----


def test_sha256_standard_vectors(tool):
    rc, out, err = run(tool, "--selftest")
    assert rc == 0, out
    assert out.decode().count("ok  ") == 4 and "FAIL" not in out.decode()


def test_sha256_matches_hashlib_on_block_boundaries(tool, tmp_path):
    """Files of sizes around the 64-byte hash block and the 2048-byte sector, from a .iso."""
    rng = random.Random(5)
    sizes = [0, 1, 55, 56, 63, 64, 65, 119, 120, 2047, 2048, 2049, 65535, 65536, 65537, 200000]
    blobs = {f"F{i:02d}.BIN": rng.randbytes(s) for i, s in enumerate(sizes)}
    sectors, records, extent = [], [], 19
    for name, blob in blobs.items():
        records.append(record(extent, len(blob), name.encode() + b";1"))
        count = max(1, -(-len(blob) // SECTOR)) if blob else 0
        sectors.append(blob.ljust(count * SECTOR, b"\0"))
        extent += count
    root = (
        record(18, SECTOR, b"\0", True) + record(18, SECTOR, b"\1", True) + b"".join(records)
    ).ljust(SECTOR, b"\0")
    assert len(root) == SECTOR
    pvd = bytearray(SECTOR)
    pvd[0:7] = b"\x01CD001\x01"
    pvd[80:84] = le(extent, 4)
    pvd[128:130] = le(SECTOR, 2)
    pvd[156:190] = record(18, SECTOR, b"\0", True)[:34]
    end = bytearray(SECTOR)
    end[0:7] = b"\xffCD001\x01"
    iso = bytes(16 * SECTOR) + bytes(pvd) + bytes(end) + root + b"".join(sectors)
    (tmp_path / "h.iso").write_bytes(iso)
    got = listing(tool, tmp_path / "h.iso")
    assert got["files"] == {n: (len(b), hashlib.sha256(b).hexdigest()) for n, b in blobs.items()}


def write_tree(root, files):
    for rel, data in files.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def merged(root):
    """dest's files as {relative path with '/': bytes}, the spelling kept."""
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file()}


def test_copy_merged_follows_the_developer_folder_rules(tool, tmp_path):
    """disc_copy_merged (spec 008 phase M): disc 1 wins a shared path but
    UVCONFIG.EXE, FULL.ID and disc 2's saves stay out, a directory keeps disc
    1's spelling, and a second run copies nothing."""
    disc1, disc2, dest = tmp_path / "disc1", tmp_path / "disc2", tmp_path / "out" / "developer"
    write_tree(
        disc1,
        {
            "DATA/1CD.ID": b"",
            "DATA/FULL.ID": b"full",
            "DATA/HD.ID": b"toto\r\n",
            "DREAMS.DAT": b"bank from disc 1",
            "DATA/UNIVBE/UVCONFIG.EXE": b"old",
            "DATA/3DC/A.DSN": b"a" * 70000,
        },
    )
    write_tree(
        disc2,
        {
            "DATA/2CD.ID": b"",
            "DATA/HD.ID": b"\x01\x00\x00\x00",
            "DREAMS.DAT": b"bank from disc 2",
            "DATA/UNIVBE/UVCONFIG.EXE": b"newer",
            "data/3dc/B.DSN": b"b" * 3,
            "DATA/GAME/GAME0.DAT": b"foreign save",
            "DATA/GAME/GAME.DAT": b"foreign index",
        },
    )
    rc, out, err = run(tool, "--copy-merged", disc1, disc2, dest)
    assert rc == 0, err
    assert merged(dest) == {
        "DATA/1CD.ID": b"",
        "DATA/2CD.ID": b"",
        "DATA/3DC/A.DSN": b"a" * 70000,
        "DATA/3DC/B.DSN": b"bbb",
        "DATA/HD.ID": b"toto\r\n",
        "DATA/UNIVBE/UVCONFIG.EXE": b"newer",
        "DREAMS.DAT": b"bank from disc 1",
    }
    assert "COPIED\t7" in out.decode() and "SKIPPED\t0" in out.decode()

    # A resumed copy: complete files are skipped, a missing one is copied again.
    (dest / "DREAMS.DAT").unlink()
    rc, out, err = run(tool, "--copy-merged", disc1, disc2, dest)
    assert rc == 0, err
    assert "COPIED\t1" in out.decode() and "SKIPPED\t6" in out.decode()
    assert (dest / "DREAMS.DAT").read_bytes() == b"bank from disc 1"
    assert not list(dest.rglob("*.part"))
