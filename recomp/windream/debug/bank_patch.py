"""Write a modified project bank (DREAMS.DAT) for a test run.

"New game" loads project 0, so a bank whose slot 0 holds a copy of another
project's record starts the game in that project. The patched bank goes into a
run's data directory as `dreams.dat`, where it overrides the disc's (in disc
mode the data directory is <run dir>/sandbox). Game data: written under out/
only, never committed.

As a library:

    bank = Bank.from_disc(1)
    bank.copy(116, 0)                      # slot 0 = Project116's record
    bank.spawn_in_link(0, destination=116) # or: stand in the link to Project116
    bank.write(run_dir / "sandbox")

As a command line:

    uv run python recomp/windream/debug/bank_patch.py list [--disc 2] [--level 3]
    uv run python recomp/windream/debug/bank_patch.py show 26
    uv run python recomp/windream/debug/bank_patch.py write out/recomp/windream/run-T/sandbox \
        --copy 116:0 [--keep-name] [--spawn-in-link 0:116] [--link 0:0:26] [--set32 0:0x1fc:3]

The record layout is src/dreams/formats/project.py's.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "src"))

from dreams import paths  # noqa: E402
from dreams.formats import project  # noqa: E402

NAME_SIZE = 0x20
SPAWN_AT = 0xB4  # three int32
HEADING_AT = 0x10C
CD_TRACK_AT = 0x1F8
LEVEL_AT = 0x1FC
LINK_DEST_AT, LINK_DEST_SIZE = 12, 12
LINK_BOX_AT = 0x24  # lo, hi: six int32


def pack(record: bytes) -> bytes:
    """Zero-run pack one record: a zero run of N (up to 255) becomes 00 N."""
    out = bytearray()
    i = 0
    while i < len(record):
        if record[i]:
            out.append(record[i])
            i += 1
        else:
            j = i
            while j < len(record) and record[j] == 0 and j - i < 255:
                j += 1
            out += bytes([0, j - i])
            i = j
    return bytes(out)


class Bank:
    """The 150 unpacked records of a DREAMS.DAT, editable, and its writer."""

    def __init__(self, data: bytes):
        self.source = data
        self.padding = data[project.INDEX * 4 : project.RECORDS]
        offsets = struct.unpack_from(f"<{project.INDEX}I", data, 0)
        self.records = [
            bytearray(
                project.decompress(
                    data[project.RECORDS + offsets[i] : project.RECORDS + offsets[i + 1]]
                )
            )
            for i in range(project.INDEX - 1)
        ]
        for i, record in enumerate(self.records):
            if len(record) != project.RECORD_SIZE:
                raise ValueError(f"record {i}: {len(record):#x} bytes")

    @classmethod
    def from_disc(cls, disc: int = 1) -> Bank:
        return cls((paths.disc(disc) / "DREAMS.DAT").read_bytes())

    # ---- reading ----

    def parsed(self, slot: int) -> project.Project:
        return project.parse(slot, bytes(self.records[slot]))

    def level(self, slot: int) -> int:
        return struct.unpack_from("<i", self.records[slot], LEVEL_AT)[0]

    def link_slot(self, slot: int, destination: int) -> int:
        """The LINK slot of that record whose destination is Project<destination>."""
        wanted = f"Project{destination}"
        for index, cell in project.slots(bytes(self.records[slot]), "LINK"):
            if project._cstr(cell[LINK_DEST_AT : LINK_DEST_AT + LINK_DEST_SIZE]) == wanted:
                return index
        raise ValueError(f"record {slot} has no link to {wanted}")

    def link_box(self, slot: int, link: int) -> tuple[project.Vec, project.Vec]:
        at = project.LINK_AT + project.LINK_SIZE * link + LINK_BOX_AT
        v = struct.unpack_from("<6i", self.records[slot], at)
        return v[:3], v[3:]

    # ---- editing ----

    def copy(self, source: int, target: int, keep_name: bool = False) -> None:
        """Slot `target` becomes a copy of record `source` (keep_name: all but
        the "Project<n>" name at +0)."""
        name = bytes(self.records[target][:NAME_SIZE])
        self.records[target] = bytearray(self.records[source])
        if keep_name:
            self.records[target][:NAME_SIZE] = name

    def set32(self, slot: int, offset: int, value: int) -> None:
        struct.pack_into("<i", self.records[slot], offset, value)

    def set_spawn(self, slot: int, position: project.Vec, heading: int | None = None) -> None:
        struct.pack_into("<3i", self.records[slot], SPAWN_AT, *position)
        if heading is not None:
            self.set32(slot, HEADING_AT, heading)

    def spawn_in_link(self, slot: int, destination: int) -> project.Vec:
        """Move the record's spawn to the centre of its link to Project<destination>."""
        lo, hi = self.link_box(slot, self.link_slot(slot, destination))
        centre = tuple((lo[i] + hi[i]) // 2 for i in range(3))
        self.set_spawn(slot, centre)
        return centre

    def set_link(self, slot: int, link: int, destination: int) -> None:
        """Point an existing LINK slot at Project<destination>."""
        at = project.LINK_AT + project.LINK_SIZE * link + LINK_DEST_AT
        text = f"Project{destination}".encode()
        self.records[slot][at : at + LINK_DEST_SIZE] = text.ljust(LINK_DEST_SIZE, b"\x00")

    # ---- writing ----

    def data(self) -> bytes:
        packed = [pack(bytes(record)) for record in self.records]
        offsets, at = [], 0
        for chunk in packed:
            offsets.append(at)
            at += len(chunk)
        offsets.append(at)
        return struct.pack(f"<{project.INDEX}I", *offsets) + self.padding + b"".join(packed)

    def write(self, data_dir: str | Path) -> Path:
        """Write the bank as <data_dir>/dreams.dat (must be under the output root)."""
        target = Path(data_dir).resolve() / "dreams.dat"
        out = paths.out_dir().resolve()
        if out not in target.parents:
            raise ValueError(f"{target} is not under {out}: game data stays in the output tree")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(self.data())
        return target


def describe(bank: Bank, slot: int) -> str:
    p = bank.parsed(slot)
    links = ", ".join(f"{link.name}->{link.destination}" for link in p.links)
    return (
        f"{slot:3d} {p.name:<11} level {bank.level(slot)} cd {p.cd_track:2d} "
        f"{p.scene:<14} spawn {p.spawn_position} links [{links}]"
    )


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("command", choices=["list", "show", "write"])
    ap.add_argument("target", nargs="?", help="show: the slot; write: the data directory")
    ap.add_argument("--disc", type=int, default=1, help="the disc whose DREAMS.DAT is the base")
    ap.add_argument("--level", type=int, help="list: only this level number")
    ap.add_argument("--copy", action="append", default=[], metavar="SRC:DST")
    ap.add_argument("--keep-name", action="store_true", help="--copy keeps the target's name")
    ap.add_argument("--spawn-in-link", action="append", default=[], metavar="SLOT:DEST")
    ap.add_argument("--link", action="append", default=[], metavar="SLOT:LINK:DEST")
    ap.add_argument("--set32", action="append", default=[], metavar="SLOT:OFFSET:VALUE")
    options = ap.parse_args()
    bank = Bank.from_disc(options.disc)
    if options.command == "list":
        for slot in range(len(bank.records)):
            if options.level is None or bank.level(slot) == options.level:
                print(describe(bank, slot))
        return 0
    if options.command == "show":
        slot = int(options.target)
        print(describe(bank, slot))
        p = bank.parsed(slot)
        for link in p.links:
            print(f"  {link.name} -> {link.destination} lo {link.lo} hi {link.hi}")
        for objet in p.objets:
            print(f"  {objet.name} {objet.asset} at {objet.position} flags {objet.flags:#x}")
        return 0
    for spec in options.copy:
        source, target = (int(x, 0) for x in spec.split(":"))
        bank.copy(source, target, options.keep_name)
    for spec in options.spawn_in_link:
        slot, destination = (int(x, 0) for x in spec.split(":"))
        print("spawn", bank.spawn_in_link(slot, destination))
    for spec in options.link:
        slot, link, destination = (int(x, 0) for x in spec.split(":"))
        bank.set_link(slot, link, destination)
    for spec in options.set32:
        slot, offset, value = (int(x, 0) for x in spec.split(":"))
        bank.set32(slot, offset, value)
    print("wrote", bank.write(options.target))
    return 0


if __name__ == "__main__":
    sys.exit(main())
