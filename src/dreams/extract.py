"""Extract and decode every asset whose format we have actually solved.

Scope: **content only**. Engine code, level geometry and animation payloads are
still packed or still need reverse engineering, so they are not exported — but
their decoded headers are, as JSON, because that metadata is the useful part of
what we know.

Everything written here is lossless:

    audio   FLAC          (from raw CD-DA, and from the RIFF WAVE banks)
    video   FFV1 in MKV   (the archival standard; PNG frames would be ~40 GB)
    images  PNG
    data    JSON, UTF-8

The two discs overlap by 170 filenames but only 10 differ in content. Identical
files are exported once; genuinely different ones are exported twice with a
``_d1`` / ``_d2`` suffix, because picking a winner would discard evidence.

External tools, both optional — groups needing a missing tool are skipped, not
failed:

    ffmpeg          FLAC / FFV1 / TGA decoding
    na_game_tool    HNM4/5/6 video, patched for the HNS6 and UBS2 tags
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from collections import Counter
from collections.abc import Iterator
from dataclasses import asdict, dataclass, field
from datetime import UTC, datetime
from pathlib import Path

from dreams import paths, png
from dreams.formats import audio, cdaudio, image, model, scene, video

#: Group name -> subdirectory of the extraction root.
LAYOUT = {
    "music": "audio/music",
    "sfx": "audio/sfx",
    "voice": "audio/voice",
    "cutscenes": "video/cutscenes",
    "movies": "video/movies",
    "textures": "video/textures",
    "sprites": "images/sprites",
    "icons": "images/icons",
    "tiles": "images/3dm-blocks",
    "gallery": "images/gallery",
    "renders": "images/renders",
    "metadata": "metadata",
    "text": "text",
}

VIDEO_GROUPS = {"cutscenes", "movies", "textures"}

#: Where na_game_tool lives if it is not on PATH. Built from the NihAV tarball
#: with the HNS6/UBS2 tag patch applied - see docs/hnm-video.md.
NAGAME_FALLBACK = (
    Path(r"E:\dreams-work\websweep\na_game_tool-0.6.0") / "target" / "release" / "na_game_tool.exe"
)


@dataclass
class Item:
    group: str
    source: str
    outputs: list[str] = field(default_factory=list)
    status: str = "ok"
    note: str = ""


@dataclass
class Source:
    """One logical asset, resolved across both discs."""

    rel: str
    path: Path
    disc: int
    suffix: str = ""  # "_d1"/"_d2" when the two discs genuinely differ
    qualified: bool = False  # use the full-path slug because bare stems collide

    @property
    def slug(self) -> str:
        """Path-qualified name, unique across the whole disc tree.

        ``DATA/ICONE/OLD/ICONES.BAK`` -> ``icone_old_icones_bak``. The extension
        is kept: five generations of ``ICONES`` differ only by it.
        """
        s = re.sub(r"[^a-z0-9]+", "_", self.rel.lower()).strip("_")
        return s.removeprefix("data_") + self.suffix

    @property
    def stem(self) -> str:
        if self.qualified:
            return self.slug
        return Path(self.rel).stem.lower() + self.suffix


def disambiguate(sources: list[Source]) -> list[Source]:
    """Promote a whole group to path-qualified names if any bare stem repeats.

    ``ICONES.BF``, ``ICONES.BAK``, ``ICONES.OLI`` and ``OLD/ICONES.BAK`` all
    reduce to ``icones``, so without this they silently overwrite one another -
    and those five generations are the best format-diffing material on the discs.
    Applied per group so unambiguous groups keep short, readable names.
    """
    counts = Counter(s.stem for s in sources)
    if any(n > 1 for n in counts.values()):
        for s in sources:
            s.qualified = True
    return sources


def _sha256(p: Path, limit: int | None = None) -> str:
    h = hashlib.sha256()
    with p.open("rb") as fh:
        if limit:
            h.update(fh.read(limit))
        else:
            for chunk in iter(lambda: fh.read(1 << 22), b""):
                h.update(chunk)
    return h.hexdigest()


def _which(name: str) -> str | None:
    return shutil.which(name)


def find_nagame() -> Path | None:
    onpath = _which("na_game_tool")
    if onpath:
        return Path(onpath)
    return NAGAME_FALLBACK if NAGAME_FALLBACK.is_file() else None


# --------------------------------------------------------------- discovery ---


def merge_discs(pattern: str, subdir: str | None = None) -> list[Source]:
    """Resolve a glob across both discs, de-duplicating identical files.

    Files present on both discs with identical content are returned once.
    Files that differ are returned twice, tagged ``_d1`` and ``_d2``.
    """
    found: dict[str, dict[int, Path]] = {}
    for n in (1, 2):
        root = paths.disc(n)
        if not root.is_dir():
            continue
        base = root / subdir if subdir else root
        if not base.is_dir():
            continue
        for p in base.rglob(pattern):
            if p.is_file():
                found.setdefault(p.relative_to(root).as_posix().upper(), {})[n] = p

    out: list[Source] = []
    for rel, bydisc in sorted(found.items()):
        if len(bydisc) == 1:
            n, p = next(iter(bydisc.items()))
            out.append(Source(rel, p, n))
        else:
            p1, p2 = bydisc[1], bydisc[2]
            same = p1.stat().st_size == p2.stat().st_size and _sha256(p1) == _sha256(p2)
            if same:
                out.append(Source(rel, p1, 1))
            else:
                out.append(Source(rel, p1, 1, "_d1"))
                out.append(Source(rel, p2, 2, "_d2"))
    return out


# ------------------------------------------------------------------- audio ---


def _to_flac(ffmpeg: str, args_in: list[str], dest: Path) -> bool:
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", *args_in,
           "-c:a", "flac", "-compression_level", "8", str(dest)]
    return subprocess.run(cmd, capture_output=True).returncode == 0


def extract_music(root: Path, ffmpeg: str, force: bool) -> Iterator[Item]:
    """Redbook CD audio -> FLAC. The only assets that live outside the filesystem."""
    out = root / LAYOUT["music"]
    tracks: list[cdaudio.Track] = []
    for n in (1, 2):
        image_dir = paths.disc(n).parent
        tracks += cdaudio.find_tracks(image_dir, n)

    if not tracks:
        yield Item("music", "-", status="skipped", note="no per-track .bin files found")
        return

    unique, dupes = cdaudio.dedupe(tracks)
    dupe_note = {
        cdaudio.hash_track(v[0])[:12]: [f"d{t.disc}t{t.number:02d}" for t in v]
        for v in dupes.values()
    }

    for t in sorted(unique, key=lambda x: (x.disc, x.number)):
        name = f"d{t.disc}_track{t.number:02d}.flac"
        dest = out / name
        if dest.exists() and not force:
            yield Item("music", t.path.name, [name], "skipped", "exists")
            continue
        raw_in = ["-f", "s16le", "-ar", str(cdaudio.RATE),
                  "-ac", str(cdaudio.CHANNELS), "-i", str(t.path)]
        ok = _to_flac(ffmpeg, raw_in, dest)
        shared = next((v for v in dupe_note.values() if f"d{t.disc}t{t.number:02d}" in v), None)
        yield Item(
            "music",
            t.path.name,
            [name] if ok else [],
            "ok" if ok else "failed",
            f"{t.duration}" + (f"; also at {', '.join(x for x in shared)}" if shared else ""),
        )


def extract_bank(root: Path, group: str, src: Source, ffmpeg: str, force: bool) -> Iterator[Item]:
    """FSB.DAT / DIALOG.DRD -> one FLAC per clip."""
    out = root / LAYOUT[group]
    try:
        bank = audio.read_bank(src.path)
    except ValueError as exc:
        yield Item(group, src.rel, status="failed", note=str(exc))
        return

    with tempfile.TemporaryDirectory() as tmp:
        wavs, repaired = audio.extract(bank, tmp, prefix=group)
        written = []
        for wav in wavs:
            dest = out / (wav.stem + ".flac")
            if dest.exists() and not force:
                written.append(dest.name)
                continue
            if _to_flac(ffmpeg, ["-i", str(wav)], dest):
                written.append(dest.name)

    note = f"{len(written)}/{bank.declared_count} clips"
    if bank.clips:
        note += f"; {bank.clips[0].describe().split('  ')[0]}"
    if repaired:
        note += (
            f"; repaired wFormatTag on clip(s) {repaired} "
            "(declared IEEE float at 16-bit, actually PCM - defect in the game data)"
        )
    yield Item(group, src.rel, written, "ok" if written else "failed", note)


# ------------------------------------------------------------------- video ---


def extract_video(
    root: Path, group: str, src: Source, nagame: Path, ffmpeg: str, force: bool
) -> Iterator[Item]:
    """HNM4/5/6 -> FFV1 in MKV, via na_game_tool's raw AVI."""
    out = root / LAYOUT[group]
    try:
        info = video.read_header(src.path)
    except ValueError as exc:
        yield Item(group, src.rel, status="failed", note=str(exc))
        return

    plugin = video.PLUGIN.get(info.magic)
    if not plugin:
        yield Item(group, src.rel, status="skipped", note=f"no plugin for {info.magic}")
        return

    dest = out / f"{src.stem}.mkv"
    if dest.exists() and not force:
        yield Item(group, src.rel, [dest.name], "skipped", "exists")
        return

    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "raw.avi"
        r = subprocess.run(
            [str(nagame), "-ifmt", plugin, str(src.path), "-ofmt", "avi", str(raw)],
            capture_output=True,
            text=True,
        )
        if r.returncode != 0 or not raw.is_file():
            msg = (r.stderr or r.stdout or "").strip().splitlines()
            yield Item(group, src.rel, status="failed", note=msg[-1] if msg else "decode failed")
            return

        enc = subprocess.run(
            [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(raw),
             "-c:v", "ffv1", "-level", "3", "-g", "1", "-slices", "4", "-slicecrc", "1",
             str(dest)],
            capture_output=True,
            text=True,
        )
        if enc.returncode != 0:
            yield Item(group, src.rel, status="failed", note=(enc.stderr or "").strip()[:160])
            return

    note = f"{info.magic} {info.width}x{info.height} {info.frames}f"
    if info.magic == "HNM4":
        note += "; header implies 24 fps, decoder emits 15"
    yield Item(group, src.rel, [dest.name], "ok", note)


# ------------------------------------------------------------------ images ---


def extract_sprites(root: Path, src: Source, force: bool) -> Iterator[Item]:
    """Indexed .SPR bundles -> one RGBA PNG per record."""
    try:
        sheet = image.read_spritesheet(src.path)
    except ValueError as exc:
        yield Item("sprites", src.rel, status="skipped", note=str(exc))
        return

    out = root / LAYOUT["sprites"] / src.stem
    written = []
    for i, sp in enumerate(sheet.sprites):
        dest = out / f"{i:03d}_{sp.width}x{sp.height}.png"
        if dest.exists() and not force:
            written.append(dest.name)
            continue
        png.write(dest, sp.width, sp.height, image.sprite_rgba(sheet, sp), alpha=True)
        written.append(dest.name)
    yield Item(
        "sprites",
        src.rel,
        written,
        "ok" if written else "failed",
        f"{len(written)} sprites; index 0 treated as transparent [unverified]",
    )


def extract_icons(root: Path, src: Source, force: bool) -> Iterator[Item]:
    """ICONES.BF -> its members, then decode any that are indexed sprites."""
    try:
        bundle = image.read_bundle(src.path)
    except ValueError as exc:
        yield Item("icons", src.rel, status="failed", note=str(exc))
        return

    out = root / LAYOUT["icons"] / src.stem
    members = image.extract_bundle(bundle, out)
    written = [m.name for m in members]

    for m in members:
        try:
            sheet = image.read_spritesheet(m)
        except ValueError:
            continue  # .ALP members: pixel layout not established, keep raw
        for i, sp in enumerate(sheet.sprites):
            dest = out / f"{m.stem.lower()}_{i:03d}_{sp.width}x{sp.height}.png"
            if dest.exists() and not force:
                continue
            png.write(dest, sp.width, sp.height, image.sprite_rgba(sheet, sp), alpha=True)
            written.append(dest.name)

    yield Item(
        "icons",
        src.rel,
        written,
        "ok",
        f"{bundle.entry_count} members, chain exact={bundle.chain_is_exact}; "
        ".ALP members kept raw (layout unknown)",
    )


def extract_tiles(root: Path, src: Source, force: bool) -> Iterator[Item]:
    """.3DM -> three 128x128 PNGs. The images are NOT textures.

    Each .3DM is exactly 28 + 3*0x8000 bytes and 128*128*2 == 0x8000, and the
    RGB555 unused high bit is clear across whole blocks - which is why a hi-colour
    texture was the obvious first guess. **Rendering them disproves it**: the
    output is structured noise, not imagery. **[verified]**

    What the numbers actually say (ESSAI.3DM): blocks 1 and 2 hold 15-bit values
    (max 32124 / 32639, both < 0x8000), only 107 distinct byte values, and a
    median consecutive delta of ~512 - a smooth ramp. Block 0 differs (full
    16-bit range, 232 distinct bytes, long equal runs).

    That is the shape of a **lookup table**, not a picture, and the four .3DM
    names - ESSAI, GRILLE, OMBRE2, SPRITE - are all .3DC *material* names, with
    OMBRE meaning shadow. Shading/lighting LUTs for the software rasteriser fit
    the evidence far better. **[unverified]**

    The PNGs are still written, cheaply, as visual evidence for the next person.
    """
    data = src.path.read_bytes()
    if len(data) != 28 + 3 * 0x8000:
        yield Item("tiles", src.rel, status="skipped", note=f"unexpected size {len(data)}")
        return

    out = root / LAYOUT["tiles"]
    written = []
    for b in range(3):
        blk = data[28 + b * 0x8000 : 28 + (b + 1) * 0x8000]
        dest = out / f"{src.stem}_block{b}_128x128.png"
        if dest.exists() and not force:
            written.append(dest.name)
            continue
        png.write(dest, 128, 128, png.rgb555_to_rgb(blk, 128 * 128))
        written.append(dest.name)
    yield Item(
        "tiles", src.rel, written, "ok",
        "rendered as 128x128 RGB555; output is noise, so NOT a texture - "
        "measurements point to shading lookup tables [unverified]",
    )


def extract_gallery(root: Path, src: Source, ffmpeg: str, force: bool) -> Iterator[Item]:
    """CRYOPLUS 16-bit TGA -> PNG."""
    out = root / LAYOUT["gallery"]
    dest = out / f"{src.stem}.png"
    if dest.exists() and not force:
        yield Item("gallery", src.rel, [dest.name], "skipped", "exists")
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(src.path), str(dest)],
        capture_output=True,
    )
    ok = r.returncode == 0
    yield Item("gallery", src.rel, [dest.name] if ok else [], "ok" if ok else "failed")


def copy_verbatim(root: Path, group: str, src: Source, force: bool) -> Iterator[Item]:
    """Assets already in a modern format. Re-encoding could only lose data."""
    out = root / LAYOUT[group]
    dest = out / Path(src.rel).name.lower()
    if dest.exists() and not force:
        yield Item(group, src.rel, [dest.name], "skipped", "exists")
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src.path, dest)
    yield Item(group, src.rel, [dest.name], "ok", "copied verbatim")


# --------------------------------------------------------------- metadata ----


def _projects() -> dict:
    """DREAMS.DAT: u32[151] index, 420 zero bytes, 150 ProjectN records."""
    src = next((s for s in merge_discs("DREAMS.DAT") if not s.suffix or s.suffix == "_d1"), None)
    if src is None:
        return {}
    data = src.path.read_bytes()
    offsets = list(struct.unpack_from("<151I", data, 0))
    records = []
    for i in range(150):
        start, end = 0x400 + offsets[i], 0x400 + offsets[i + 1]
        blob = data[start:end]
        name = blob.split(b"\x00")[0].decode("latin-1", "replace")
        refs = sorted({
            s.decode("latin-1")
            for s in __import__("re").findall(rb"[A-Za-z0-9_~\-]{1,12}\.[A-Za-z0-9]{2,3}", blob)
        })
        records.append({"index": i, "name": name, "offset": start, "size": end - start,
                        "asset_refs": refs})
    return {
        "source": src.rel,
        "disc": src.disc,
        "size": len(data),
        "index_entries": len(offsets),
        "tail_matches_filesize": 0x400 + offsets[150] == len(data),
        "padding_all_zero": data[0x25C:0x400] == bytes(420),
        "records": records,
    }


def extract_metadata(root: Path) -> Iterator[Item]:
    out = root / LAYOUT["metadata"]
    out.mkdir(parents=True, exist_ok=True)

    scenes = []
    for s in merge_discs("*.DSN"):
        try:
            sc = scene.read_dsn(s.path)
        except (ValueError, struct.error) as exc:
            scenes.append({"file": s.rel, "error": str(exc)})
            continue
        scenes.append({
            "file": s.rel, "disc": s.disc, "size": sc.actual_size,
            "size_ok": sc.size_ok, "span_ok": sc.span_ok,
            "count_a": sc.count_a, "name_count": sc.name_count,
            "body_offset": sc.body_offset, "body_size": sc.body_size,
            "names": sc.names,
        })
    (out / "scenes.json").write_text(json.dumps(scenes, indent=1), encoding="utf-8")

    anims = []
    for s in merge_discs("*.DAN"):
        try:
            an = scene.read_dan(s.path)
        except (ValueError, struct.error) as exc:
            anims.append({"file": s.rel, "error": str(exc)})
            continue
        anims.append({
            "file": s.rel, "disc": s.disc, "size": an.actual_size,
            "size_ok": an.size_ok, "span_ok": an.span_ok,
            "names": an.names, "frame_refs": an.frame_refs,
            "body_offset": an.body_offset, "body_size": an.body_size,
        })
    (out / "animation.json").write_text(json.dumps(anims, indent=1), encoding="utf-8")

    models = []
    for pattern in ("*.3DC", "*.3DM"):
        for s in merge_discs(pattern):
            try:
                m = model.read_model(s.path)
                models.append({"file": s.rel, "disc": s.disc,
                               "size": s.path.stat().st_size, **_model_fields(m)})
            except (ValueError, struct.error) as exc:
                models.append({"file": s.rel, "error": str(exc)})
    (out / "models.json").write_text(json.dumps(models, indent=1), encoding="utf-8")

    vids = []
    for pattern in ("*.HNM", "*.UBB"):
        for s in merge_discs(pattern):
            try:
                v = video.read_header(s.path)
            except ValueError:
                continue
            vids.append({"file": s.rel, "disc": s.disc, "magic": v.magic,
                         "width": v.width, "height": v.height, "bpp": v.bpp,
                         "frames": v.frames, "size": v.size})
    (out / "video.json").write_text(json.dumps(vids, indent=1), encoding="utf-8")

    projects = _projects()
    (out / "projects.json").write_text(json.dumps(projects, indent=1), encoding="utf-8")

    yield Item(
        "metadata", "-",
        ["scenes.json", "animation.json", "models.json", "video.json", "projects.json"],
        "ok",
        f"{len(scenes)} scenes, {len(anims)} animations, {len(models)} models, "
        f"{len(vids)} videos, {len(projects.get('records', []))} projects",
    )


def _model_fields(m: model.Model) -> dict:
    return {
        "version": m.version,
        "materials": m.materials,
        "fixed_values": m.fixed_values,
        "zero_pct": m.zero_pct,
    }


def extract_text(root: Path, force: bool) -> Iterator[Item]:
    """Config and manifests, transcoded CP1252 -> UTF-8 so the French reads right."""
    out = root / LAYOUT["text"]
    out.mkdir(parents=True, exist_ok=True)
    written = []
    for pattern in ("*.INI", "LISTL*.TXT", "README.TXT", "INIT.TXT"):
        for s in merge_discs(pattern):
            if "DEMO" in s.rel or "DIRECTX" in s.rel:
                continue
            dest = out / (Path(s.rel).name.lower().replace(".", f"{s.suffix}.", 1)
                          if s.suffix else Path(s.rel).name.lower())
            if dest.exists() and not force:
                written.append(dest.name)
                continue
            raw = s.path.read_bytes()
            dest.write_text(raw.decode("cp1252", "replace"), encoding="utf-8", newline="\n")
            written.append(dest.name)
    yield Item("text", "-", written, "ok", f"{len(written)} files, CP1252 -> UTF-8")


# ------------------------------------------------------------- orchestration --

ALL_GROUPS = list(LAYOUT)

#: Group -> one-line description, used in the generated README.
DESCRIPTIONS = {
    "music": "Redbook CD audio, de-duplicated across discs",
    "sfx": "FSB.DAT sound effects",
    "voice": "DIALOG.DRD speech",
    "cutscenes": "HNS6/HNM6 640x304 full-motion video",
    "movies": "UBB2/UBS2 640x304 video (HNM generation 5)",
    "textures": "HNM4 256x256 animated textures",
    "sprites": "Indexed .SPR bundles, one PNG per record",
    "icons": "ICONES.BF members, decoded where the format is known",
    "tiles": ".3DM blocks rendered as 128x128 RGB555 -- NOT a texture, see note",
    "gallery": "CRYOPLUS bonus gallery, 16-bit TGA",
    "renders": "Developer reference renders, copied verbatim",
    "metadata": "Decoded headers for formats whose bodies stay packed",
    "text": "Config and manifests, CP1252 -> UTF-8",
}


def _video_sources() -> dict[str, list[Source]]:
    """Route every HNM/UBB file to a group by its magic, not by its directory."""
    buckets: dict[str, list[Source]] = {"cutscenes": [], "movies": [], "textures": []}
    for pattern in ("*.HNM", "*.UBB"):
        for s in merge_discs(pattern):
            if "DEMOS" in s.rel:
                continue
            try:
                magic = video.read_header(s.path).magic
            except ValueError:
                continue
            if magic == "HNM4":
                buckets["textures"].append(s)
            elif magic in ("UBB2", "UBS2"):
                buckets["movies"].append(s)
            else:
                buckets["cutscenes"].append(s)
    return buckets


def plan(groups: list[str]) -> dict[str, list[Source]]:
    """Resolve the source files for each requested group."""
    out: dict[str, list[Source]] = {}
    vids = _video_sources() if VIDEO_GROUPS & set(groups) else {}

    for g in groups:
        if g in VIDEO_GROUPS:
            out[g] = vids.get(g, [])
        elif g == "sfx":
            out[g] = [s for s in merge_discs("FSB.DAT") if "SOUND" in s.rel]
        elif g == "voice":
            out[g] = merge_discs("DIALOG.DRD")
        elif g == "sprites":
            out[g] = [s for s in merge_discs("*.SPR") if "DEMOS" not in s.rel]
        elif g == "icons":
            out[g] = [
                s for s in merge_discs("ICONES.*")
                if s.path.open("rb").read(4) == image.UBIK_MAGIC
            ]
        elif g == "tiles":
            out[g] = merge_discs("*.3DM")
        elif g == "gallery":
            out[g] = [s for s in merge_discs("*.TGA") if "CRYOPLUS" in s.rel]
        elif g == "renders":
            out[g] = [s for s in merge_discs("*.JPG") if "TEMP" in s.rel]
        else:
            out[g] = []  # music / metadata / text discover their own sources

    for g, srcs in out.items():
        out[g] = disambiguate(srcs)
    return out


def run(
    root: Path,
    groups: list[str] | None = None,
    force: bool = False,
    progress=None,
) -> dict:
    """Extract the requested groups. Returns the manifest dict."""
    groups = groups or ALL_GROUPS
    root.mkdir(parents=True, exist_ok=True)

    ffmpeg = _which("ffmpeg")
    nagame = find_nagame()
    sources = plan(groups)

    items: list[Item] = []
    for g in groups:
        srcs = sources.get(g, [])
        needs_ffmpeg = g in {"music", "sfx", "voice", "gallery"} | VIDEO_GROUPS
        if needs_ffmpeg and not ffmpeg:
            items.append(Item(g, "-", status="skipped", note="ffmpeg not on PATH"))
            continue
        if g in VIDEO_GROUPS and not nagame:
            items.append(Item(g, "-", status="skipped", note="na_game_tool not found"))
            continue

        total = len(srcs) or 1
        task = progress.add_task(f"{g}", total=total) if progress else None

        if g == "music":
            items += list(extract_music(root, ffmpeg, force))
        elif g == "metadata":
            items += list(extract_metadata(root))
        elif g == "text":
            items += list(extract_text(root, force))
        else:
            for s in srcs:
                if g in {"sfx", "voice"}:
                    items += list(extract_bank(root, g, s, ffmpeg, force))
                elif g in VIDEO_GROUPS:
                    items += list(extract_video(root, g, s, nagame, ffmpeg, force))
                elif g == "sprites":
                    items += list(extract_sprites(root, s, force))
                elif g == "icons":
                    items += list(extract_icons(root, s, force))
                elif g == "tiles":
                    items += list(extract_tiles(root, s, force))
                elif g == "gallery":
                    items += list(extract_gallery(root, s, ffmpeg, force))
                elif g == "renders":
                    items += list(copy_verbatim(root, g, s, force))
                if progress and task is not None:
                    progress.advance(task)
        if progress and task is not None:
            progress.update(task, completed=total)

    manifest = _manifest(root, groups, items, ffmpeg, nagame)
    (root / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    (root / "README.md").write_text(_readme(manifest), encoding="utf-8", newline="\n")
    return manifest


def _tool_version(exe: str | Path | None, args: list[str]) -> str:
    if not exe:
        return "not found"
    try:
        r = subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=20)
        return (r.stdout or r.stderr).strip().splitlines()[0][:120]
    except (OSError, subprocess.SubprocessError, IndexError):
        return "unknown"


def _manifest(root: Path, groups: list[str], items: list[Item], ffmpeg, nagame) -> dict:
    files = sum(len(i.outputs) for i in items)
    total_bytes = sum(p.stat().st_size for p in root.rglob("*") if p.is_file())
    return {
        "generated": datetime.now(UTC).isoformat(timespec="seconds"),
        "root": str(root),
        "groups": groups,
        "tools": {
            "ffmpeg": _tool_version(ffmpeg, ["-version"]),
            "na_game_tool": str(nagame) if nagame else "not found",
        },
        "discs": {f"disc{n}": str(paths.disc(n)) for n in (1, 2)},
        "totals": {
            "items": len(items),
            "files_written": files,
            "bytes": total_bytes,
            "ok": sum(1 for i in items if i.status == "ok"),
            "skipped": sum(1 for i in items if i.status == "skipped"),
            "failed": sum(1 for i in items if i.status == "failed"),
        },
        "items": [asdict(i) for i in items],
    }


def _readme(m: dict) -> str:
    by_group: dict[str, list[dict]] = {}
    for i in m["items"]:
        by_group.setdefault(i["group"], []).append(i)

    lines = [
        "# Dreams to Reality — extracted assets",
        "",
        f"Generated {m['generated']} by `dreams extract`.",
        "",
        "Everything here is **lossless**: FLAC for audio, FFV1-in-MKV for video,",
        "PNG for stills, JSON for metadata. Files already in a modern format are",
        "copied byte-for-byte rather than re-encoded.",
        "",
        "This directory holds **game content only**. The engine, the packed `.DSN`",
        "level bodies and the `.DAN` animation payloads are not here — those are",
        "still being reverse engineered. What is known about them is in",
        "`metadata/` as decoded headers.",
        "",
        "## Contents",
        "",
        "| Directory | Contents | Items | Files |",
        "|---|---|--:|--:|",
    ]
    for g in m["groups"]:
        rows = by_group.get(g, [])
        files = sum(len(r["outputs"]) for r in rows)
        lines.append(
            f"| `{LAYOUT[g]}/` | {DESCRIPTIONS.get(g, '')} | {len(rows)} | {files} |"
        )

    t = m["totals"]
    lines += [
        "",
        f"**{t['files_written']} files, {t['bytes'] / 1048576:.0f} MB.** "
        f"{t['ok']} ok, {t['skipped']} skipped, {t['failed']} failed.",
        "",
        "## Caveats",
        "",
        "- `images/3dm-blocks/` are **not textures**. They are the right size for",
        "  128x128 RGB555 and the unused high bit behaves, but rendering them gives",
        "  noise. The value distributions look like shading lookup tables instead.",
        "  The PNGs are kept as visual evidence, not as usable art.",
        "- Sprite transparency assumes palette index 0. Not checked against the engine.",
        "- HNM4 headers imply 24 fps; the decoder emits 15. Timing may be wrong.",
        "- HNM audio (`SD` chunks) is **not** extracted — the codec is undecoded.",
        "- Files that differ between the two discs are exported twice, `_d1` / `_d2`.",
        "",
        "Full provenance, per item, in `manifest.json`.",
    ]
    return "\n".join(lines) + "\n"
