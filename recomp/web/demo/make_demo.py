"""Build the minimal browser demo pack from the retail game files.

    uv run python recomp/web/demo/make_demo.py                    # profile wip, each level's movie
    uv run python recomp/web/demo/make_demo.py --profile first    # the first level only
    uv run python recomp/web/demo/make_demo.py --movies none      # no movies (experiments)

Reads disc 1 (`DREAMS_DISC1`; the install root `DREAMS_INSTALL_ROOT` and disc 2
are searched too, their shared files are byte-identical) through
src/dreams/paths.py and writes only under out/recomp/web/:

  demo-root/        the files loose, as /dreams holds them in the browser
                    (also the EXE directory of a native run: see demo_common.py)
  demo/             the pack: manifest.json and chunk files of at most 20 MiB
  demo/files.json   what each file is for and its size (not read by the page)

What is in it, and why (every claim is checked by tests/recomp/test_web_demo.py,
which boots the cut with the native recomp):

* GDIDREAM.EXE: the lifted program's image (the host loads it for the
  data/resource sections), at the root as in the retail install.
* DREAMS.DAT: the retail bank with only the profile's projects reachable:
  links to projects outside the profile are cleared, and a project left with
  none exits to Project0. Records of other projects stay (they are never loaded).
* The profile's levels: each project's OBJET assets (scene .DSN, models .DAN/
  .3DC) and, with --movies, its movie (anim_video) and animated texture.
* The files every level loads (player model XH_.DAN, ground and shadow
  models, particles, fonts, UI icons, the sound-effect bank).
* DIALOG.DRD cut to the dialogue entries the profile's levels can trigger.
* The install markers (HD.ID, FULL.ID, LEVEL.ID = 1, DATA\\1CD.ID): the game
  runs as a full install on disc 1, so it never asks for a CD or copies files.

Left out, with what the game does without it (see NOTES-demo.md):
the intro movie, the menu background movie, the talking-head movie (all
optional), CD music (MCI fails; the game goes on), 3D levels and models of
other levels, the full dialogue bank, disc 2, DOS and installer files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

import demo_common as dc

DRD_ENTRIES = 178
CHUNK = 20 * 1024 * 1024  # contract: at most 20 MiB per asset (Cloudflare Pages: 25 MiB)

# Projects by profile (retail record numbers, found by scene name against the
# July 1997 demo: see NOTES-demo.md). The July demo's playable graph is the
# Angkor island: its scenes H18ANGKR, E13_ANGK, E11_ANGK, E12_ANGK, F36ANGK,
# L07_TUN2, F07_GROT, E10_PIEC, F34COUL1, E20_RIDE.
PROFILES = {
    "first": [0],
    "wip": [0, 62, 39, 30, 108, 46, 31, 99, 48, 76],
}

# Files every level loads (recorded: record_opens.py over every project of the
# wip profile). (path below the CD root / install root, why)
CORE = [
    ("DATA/1CD.ID", "disc-1 marker the game checks at start (CD_FindDrive)"),
    ("DATA/HD.ID", "install marker (CD_CheckInstall)"),
    (
        "DATA/FULL.ID",
        "full-install marker for a native run of the root; the browser host hides it (files.c)",
    ),
    ("DATA/LEVEL.ID", "cached level number (4-byte 1, written here)"),
    ("DATA/FONT/HI640.SPR", "font"),
    ("DATA/FONT/HI480.SPR", "font"),
    ("DATA/FONT/HI320.SPR", "font"),
    ("DATA/OBJET/PARTICLE.SPR", "particle sprite"),
    ("DATA/OBJET/SOUR.ALP", "UI sprite bank"),
    ("DATA/ICONE/ICONES.BF", "icons and menu sprites"),
    ("DATA/SOUND/FSB.DAT", "sound-effect bank"),
    ("DATA/3DC/XH_.DAN", "the player's model"),
    ("DATA/3DC/OMBRE.3DC", "shadow model"),
    ("DATA/3DC/OMBRE2.3DC", "shadow model"),
    ("DATA/3DC/OMBRE2.3DM", "shadow material"),
    ("DATA/3DC/GRILLE.3DM", "material"),
    ("DATA/3DC/ESSAI.3DM", "material"),
    ("DATA/3DC/PARTICL2.3DC", "particle model"),
    ("DATA/3DC/GR00.3DC", "ground tile"),
    ("DATA/3DC/GR01.3DC", "ground tile"),
    ("DATA/3DC/GR02.3DC", "ground tile"),
    ("DATA/3DC/BOULE.3DC", "mana ball"),
    ("DATA/3DC/X01SOL.3DC", "floor model"),
    ("DATA/3DC/X01SOL1.3DC", "floor model"),
    ("DATA/3DC/EPEE.3DC", "sword"),
    ("DATA/3DC/ARC.3DC", "bow"),
    # not opened in the recordings; kept because they are tiny and the game
    # names them next to the above (pick-ups and the spell effects)
    ("DATA/3DC/GUN.3DC", "weapon model (named by the game, not opened in the recordings)"),
    ("DATA/3DC/MANA.3DC", "mana model (named by the game, not opened in the recordings)"),
]


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


class Plan:
    """The files of the demo: path -> (bytes source, reason)."""

    def __init__(self):
        self.files: dict[str, tuple[Path | bytes, str]] = {}

    def add(self, rel: str, why: str, data: bytes | None = None, required: bool = True) -> bool:
        rel = rel.replace("\\", "/")
        if rel in self.files:
            return True
        if data is not None:
            self.files[rel] = (data, why)
            return True
        src = dc.find_source(rel)
        if src is None:
            if required:
                raise SystemExit(f"retail file not found: {rel}")
            return False
        self.files[rel] = (src, why)
        return True


def build_bank(projects: list[int]):
    """The retail bank, reduced to the projects: (Bank, notes)."""
    bank = dc.load_bank()
    keep = set(projects)
    notes = []
    for slot in projects:
        parsed = bank.parsed(slot)
        live = {i: k for (i, _), k in zip(project_links(bank, slot), parsed.links, strict=True)}
        outside = [i for i, k in live.items() if k.project is not None and k.project not in keep]
        inside = [i for i in live if i not in outside]
        if not outside:
            continue
        # a project that would be left without an exit sends the player to Project0
        redirect = None
        if not inside:
            redirect = next((i for i in outside if live[i].project == 62), outside[0])
        for i in outside:
            if i == redirect:
                bank.set_link(slot, i, 0)
                notes.append(f"Project{slot} LINK{i}: Project{live[i].project} -> Project0")
            else:
                dc.clear_link(bank, slot, i)
                notes.append(f"Project{slot} LINK{i}: Project{live[i].project} removed")
    return bank, notes


def project_links(bank, slot):
    from dreams.formats import project  # noqa: PLC0415

    return project.slots(bytes(bank.records[slot]), "LINK")


def project_files(bank, projects: list[int], movies: str, plan: Plan) -> set[int]:
    """Add each project's files; returns the DIALOG.DRD entries (zero-based) its
    events can play."""
    drd: set[int] = set()
    for slot in projects:
        p = bank.parsed(slot)
        for o in p.objets:
            plan.add(f"DATA/3DC/{o.asset}", f"Project{slot} {o.name}")
        if movies != "none":
            # A level's movie (anim_video) plays when the level is entered, and a
            # missing one stalls the game until a key is pressed (the same wait as
            # a missing intro): keep those and the events' cutscenes.
            if p.anim_video:
                plan.add(f"DATA/HNM/{p.anim_video}", f"Project{slot} level movie", required=False)
            for a in p.advents:
                if a.cutscene_video:
                    plan.add(
                        f"DATA/HNM/{a.cutscene_video}", f"Project{slot} cutscene", required=False
                    )
        if movies == "all" and p.anim_video2:
            # animated texture of the level: the game runs without it
            plan.add(
                f"DATA/ANIM/{p.anim_video2}", f"Project{slot} animated texture", required=False
            )
        for a in p.advents:
            # +0x1C is the dialogue entry (1-based) for opcode 0x40 and also
            # for opcode 16 (seen: Project0's stage 170); keep every plausible one
            if 0 < a.condition_stage <= DRD_ENTRIES:
                drd.add(a.condition_stage - 1)
    return drd


def build_plan(profile: str, movies: str) -> tuple[Plan, list[str]]:
    projects = PROFILES[profile]
    plan = Plan()
    exe = dc.disc1() / "GDIDREAM.EXE"
    plan.files["GDIDREAM.EXE"] = (exe, "the lifted program's image (retail, disc 1)")
    for rel, why in CORE:
        if rel == "DATA/LEVEL.ID":
            plan.add(rel, why, data=(1).to_bytes(4, "little"))
        else:
            plan.add(rel, why)
    bank, notes = build_bank(projects)
    plan.add("DREAMS.DAT", f"project bank: {len(projects)} projects reachable", data=bank.data())
    drd_ids = project_files(bank, projects, movies, plan)
    drd_src = dc.find_source("DATA/3DC/DIALOG.DRD")
    plan.add(
        "DATA/3DC/DIALOG.DRD",
        f"dialogue bank cut to entries {sorted(i + 1 for i in drd_ids)} (1-based)",
        data=dc.drd_trim(drd_src.read_bytes(), drd_ids),
    )
    return plan, notes


def write_root(plan: Plan, root: Path) -> None:
    if root.exists():
        shutil.rmtree(root)
    for rel, (src, _why) in sorted(plan.files.items()):
        target = root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(src, bytes):
            target.write_bytes(src)
        else:
            shutil.copyfile(src, target)


def write_pack(root: Path, out: Path, plan: Plan, meta: dict) -> dict:
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    files = []
    info = []
    for rel in sorted(plan.files):
        path = root / rel
        size = path.stat().st_size
        digest = sha256(path)
        chunks = []
        with open(path, "rb") as f:
            for n in range(max(1, -(-size // CHUNK))):
                data = f.read(CHUNK)
                name = f"{digest[:10]}-{Path(rel).name}.{n:03d}"
                (out / name).write_bytes(data)
                chunks.append({"url": name, "size": len(data)})
        files.append({"path": rel, "size": size, "sha256": digest, "chunks": chunks})
        info.append({"path": rel, "size": size, "why": plan.files[rel][1]})
    manifest = {
        "version": 1,
        "name": "demo",
        "total": sum(f["size"] for f in files),
        "files": files,
        **meta,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))
    (out / "files.json").write_text(json.dumps(info, indent=1))
    return manifest


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("--profile", choices=sorted(PROFILES), default="wip")
    ap.add_argument(
        "--movies",
        choices=["none", "project", "all"],
        default="project",
        help="project (default): each level's movie and cutscenes; all: also the animated "
        "textures; none: no movies (entering a level that has one then waits for a key)",
    )
    ap.add_argument("--exclude", action="append", default=[], help="drop a path (experiments)")
    a = ap.parse_args()
    if not dc.have_data():
        print("DREAMS_DISC1 is not set or does not hold the extracted disc 1", file=sys.stderr)
        return 2
    plan, notes = build_plan(a.profile, a.movies)
    for ex in a.exclude:
        plan.files.pop(ex.replace("\\", "/"), None)
    write_root(plan, dc.DEMO_ROOT)
    manifest = write_pack(
        dc.DEMO_ROOT,
        dc.DEMO_OUT,
        plan,
        {"profile": a.profile, "projects": PROFILES[a.profile], "movies": a.movies},
    )
    total = manifest["total"]
    print(f"profile {a.profile}, movies {a.movies}: {len(plan.files)} files, {total / 1e6:.2f} MB")
    for n in notes:
        print("  bank:", n)
    for rel, (src, _) in sorted(
        plan.files.items(),
        key=lambda kv: -len(kv[1][0]) if isinstance(kv[1][0], bytes) else -kv[1][0].stat().st_size,
    )[:12]:
        size = len(src) if isinstance(src, bytes) else src.stat().st_size
        print(f"  {size:>10,d}  {rel}")
    print(f"root {dc.DEMO_ROOT}\npack {dc.DEMO_OUT / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
