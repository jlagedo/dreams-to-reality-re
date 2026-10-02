"""Shared helpers of the demo-pack tools: paths, the source trees the game's
files come from, hard-link trees for native runs, and the DREAMS.DAT and
DIALOG.DRD readers/writers the cut needs.

Everything written goes under out/recomp/web/ (game-derived data, never
committed). The game's two path spaces, as the retail recomp sees them:

  C:\\ (the CD root; the host also searches the EXE's directory):
        dreams.dat, DATA\\FONT, DATA\\OBJET, DATA\\ICONE, DATA\\HNM, DATA\\SOUND,
        DATA\\1CD.ID
  C:\\CRYO\\DREAMS\\ (the install root):
        DATA\\HD.ID, DATA\\FULL.ID, DATA\\LEVEL.ID, DATA\\3DC, DATA\\ANIM,
        DATA\\GAME (saves)

The demo root holds the union of both under one tree (the CD-root files at
their own relative path), which is what /dreams is in the browser: the host
must resolve a guest path with or without the CRYO\\DREAMS prefix in it.
"""

from __future__ import annotations

import os
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))
sys.path.insert(0, str(ROOT / "recomp" / "windream" / "debug"))

from dreams import paths  # noqa: E402
from dreams.formats import project  # noqa: E402

WEB_OUT = paths.out_dir() / "recomp" / "web"
DEMO_OUT = WEB_OUT / "demo"  # the pack: manifest.json and chunks
DEMO_ROOT = WEB_OUT / "demo-root"  # the same files, loose: what /dreams holds
NATIVE_TREE = WEB_OUT / "demo-native"  # CRYO/DREAMS hard links of demo-root, for WD_READ_ROOTS
REC_OUT = WEB_OUT / "demo-rec"  # recordings (opens json, screenshots)
REC_ROOT = WEB_OUT / "rec-root"  # hard-link copy of disc 1 with a patched bank (recording runs)
REC_NATIVE = WEB_OUT / "rec-native"


# ---- finding the retail files ----


def _ci_find(base: Path, rel: str) -> Path | None:
    """`rel` below `base`, matching each segment without regard to case."""
    cur = base
    for part in rel.replace("\\", "/").split("/"):
        if not part:
            continue
        nxt = cur / part
        if not nxt.exists():
            try:
                names = {n.lower(): n for n in os.listdir(cur)}
            except OSError:
                return None
            hit = names.get(part.lower())
            if hit is None:
                return None
            nxt = cur / hit
        cur = nxt
    return cur if cur.exists() else None


def source_roots() -> list[Path]:
    """Where retail files are looked up, in order: disc 1, the install root, disc 2.
    The shared data files are byte-identical in all of them."""
    roots = []
    for key in ("disc1", "install_root", "disc2"):
        p = paths.configured(key)
        if p is not None and p.is_dir():
            roots.append(p)
    return roots


def find_source(rel: str) -> Path | None:
    """A retail file by its path relative to the CD root (DATA/3DC/X.DAN). The
    install root is searched as CRYO/DREAMS-less too (its tree is the CD's)."""
    for base in source_roots():
        hit = _ci_find(base, rel)
        if hit is not None and hit.is_file():
            return hit
    return None


def disc1() -> Path:
    p = paths.configured("disc1")
    if p is None:
        raise SystemExit("set DREAMS_DISC1 (the extracted disc 1) in .dreams.local.env")
    return p


def have_data() -> bool:
    p = paths.configured("disc1")
    return bool(p and (p / "GDIDREAM.EXE").is_file() and (p / "DREAMS.DAT").is_file())


# ---- trees ----


def link_or_copy(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def link_tree(src: Path, dst: Path) -> int:
    """Hard-link (or copy) every file of `src` into `dst`; returns the count."""
    n = 0
    for p in src.rglob("*"):
        if p.is_file():
            link_or_copy(p, dst / p.relative_to(src))
            n += 1
    return n


def native_tree(demo_root: Path = DEMO_ROOT, dest: Path = NATIVE_TREE) -> Path:
    """dest/CRYO/DREAMS holds hard links of the demo root's files: the
    WD_READ_ROOTS directory of a native run. (The demo root itself is the EXE's
    directory, so it serves the CD-root paths.) Returns `dest`."""
    inner = dest / "CRYO" / "DREAMS"
    if inner.exists():
        shutil.rmtree(inner)
    link_tree(demo_root, inner)
    return dest


# ---- DREAMS.DAT ----


def load_bank():
    from bank_patch import Bank  # noqa: PLC0415

    return Bank((disc1() / "DREAMS.DAT").read_bytes())


def clear_link(bank, slot: int, link: int) -> None:
    """Make a LINK slot unused (all zero), as an unfilled slot of the bank is."""
    at = project.LINK_AT + project.LINK_SIZE * link
    bank.records[slot][at : at + project.LINK_SIZE] = bytes(project.LINK_SIZE)


# ---- DIALOG.DRD ----


def _silent_entry() -> bytes:
    """A valid DIALOG.DRD entry with 50 ms of silence and no caption lines."""
    samples = 551
    wave = (
        b"RIFF"
        + struct.pack("<I", 36 + samples)
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, 1, 11025, 11025, 1, 8)
        + b"data"
        + struct.pack("<I", samples)
        + bytes([128]) * samples
    )
    body = bytes([2]) + struct.pack("<I", 5 + len(wave)) + wave + bytes([3]) + struct.pack("<I", 5)
    return bytes([1]) + struct.pack("<II", 9 + len(body), 0) + body


def drd_trim(data: bytes, keep: set[int]) -> bytes:
    """A DIALOG.DRD holding only the entries in `keep` (zero-based).

    Layout (DRD_Open, DRD_LoadEntry): 16-byte header ("DRDF", file size, entry
    count, largest entry), a table block (tag 0, size 5+4N) with N absolute u32
    entry offsets, then the entries; each entry is a 9-byte header (tag 1, its
    own size, its caption line count) and blocks (tag 2 WAVE, tag 3 lines,
    tag 4 portrait), and is read on demand by seek. An entry not kept points at
    one shared silent entry with no captions: the game crashes in DSOUND_LoadWav
    when an entry fails to load (seen with an entry cut to end-of-file), and
    the levels of the pack should never ask for one, but a wrong request then
    costs a silent line instead of the process."""
    assert data[:4] == b"DRDF"
    _, count, _ = struct.unpack_from("<III", data, 4)
    table_at = 0x15
    offsets = struct.unpack_from(f"<{count}I", data, table_at)
    body_at = table_at + 4 * count
    out = bytearray(data[:body_at])
    new_offsets = []
    body = bytearray()
    for i, off in enumerate(offsets):
        if i in keep:
            size = struct.unpack_from("<I", data, off + 1)[0]
            new_offsets.append(body_at + len(body))
            body += data[off : off + size]
        else:
            new_offsets.append(None)
    stub = _silent_entry()
    stub_at = body_at + len(body)
    body += stub
    new_offsets = [stub_at if o is None else o for o in new_offsets]
    struct.pack_into(f"<{count}I", out, table_at, *new_offsets)
    out += body
    struct.pack_into("<I", out, 4, len(out))
    biggest = max([struct.unpack_from("<I", data, offsets[i] + 1)[0] for i in keep] + [len(stub)])
    struct.pack_into("<I", out, 12, biggest)
    return bytes(out)


# ---- running the cut from another project (tests, recordings) ----

CUT_ROOT = WEB_OUT / "cut-root"
CUT_NATIVE = WEB_OUT / "cut-native"


def cut_variant(
    project_slot: int, spawn_in_link: int | None = None, demo_root: Path = DEMO_ROOT
) -> tuple[Path, Path]:
    """The demo root with slot 0 of its bank replaced by a copy of `project_slot`
    (and, with `spawn_in_link`, the spawn moved into that project's link to
    Project<spawn_in_link>): (exe directory, WD_READ_ROOTS). Hard links of the
    demo root's files, so nothing is copied. Slot 0 is where a new game starts."""
    from bank_patch import Bank  # noqa: PLC0415

    if CUT_ROOT.exists():
        shutil.rmtree(CUT_ROOT)
    link_tree(demo_root, CUT_ROOT)
    bank = Bank((demo_root / "DREAMS.DAT").read_bytes())
    if project_slot != 0:
        bank.copy(project_slot, 0)
    if spawn_in_link is not None:
        bank.spawn_in_link(0, spawn_in_link)
    target = CUT_ROOT / "DREAMS.DAT"
    target.unlink()
    target.write_bytes(bank.data())
    return CUT_ROOT, native_tree(CUT_ROOT, CUT_NATIVE)
