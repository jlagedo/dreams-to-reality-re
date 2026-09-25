"""Bake the extract archive into the data root the web app reads.

The pipeline stages (``docs/pipeline.md``):

    extract  discs -> extract/     lossless, faithful to the disc
    bake     extract/ -> baked/    exactly what the app reads, laid out as served
    pack     baked/ -> releases/   a subset plus the app build, ready to deploy

Bake never reads the discs, and nothing reads ``extract/`` at runtime. The dev
server mounts ``baked/`` at ``/data`` unchanged, so what runs locally is what
a release ships.

Data groups (fast, no external tools):

    projects  DREAMS.DAT records -> projects/<n>.json + the raw .bin, raw engine units
    scenes    level glTF -> scenes/<stem>/scene.gltf, scene.bin, tex/*.png
    models    model glTF -> models/<id>/model.gltf, model.bin, tex*.png
    clips     animation JSON -> models/<id>/clips.json, clips/*.json, skin.json
    ui        menu sprites, icon banks, font glyphs -> ui/
    text      DREAMS.INI, dialogue, level manifests -> text/*.json

Media groups (ffmpeg):

    music / sfx / voice          -> audio/*: Opus (or MP3 / AAC)
    cutscenes / movies / textures -> video/*: faststart H.264 MP4

Every run finishes by rewriting ``index.json`` from what is on disk: static
hosting cannot list a folder, so the index is the app's only directory listing.
There is no format versioning. The app and bake change in the same commit, and
old baked data is re-baked rather than migrated.
"""

from __future__ import annotations

import json
import re
import shutil
import struct
import subprocess
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from rich.progress import Progress

DATA_GROUPS = ("projects", "scenes", "models", "clips", "ui", "text")
AUDIO_GROUPS = ("music", "sfx", "voice")
VIDEO_GROUPS = ("cutscenes", "movies", "textures")
ALL_GROUPS = (*DATA_GROUPS, *AUDIO_GROUPS, *VIDEO_GROUPS)

#: Group -> where its output lands under the baked root.
LAYOUT = {
    "projects": "projects",
    "scenes": "scenes",
    "models": "models",
    "clips": "models/<id>/clips",
    "ui": "ui",
    "text": "text",
    "music": "audio/music",
    "sfx": "audio/sfx",
    "voice": "audio/voice",
    "cutscenes": "video/cutscenes",
    "movies": "video/movies",
    "textures": "video/textures",
}

#: Media group -> (subdirectory, same in both roots; source pattern).
MEDIA = {
    "music": ("audio/music", "*.flac"),
    "sfx": ("audio/sfx", "*.flac"),
    "voice": ("audio/voice", "*.flac"),
    "cutscenes": ("video/cutscenes", "*.mkv"),
    "movies": ("video/movies", "*.mkv"),
    "textures": ("video/textures", "*.mkv"),
}

DESCRIPTIONS = {
    "projects": "DREAMS.DAT records -> per-project JSON (raw engine units) + raw .bin",
    "scenes": "Level glTF -> one folder per scene",
    "models": "Model glTF -> one folder per .DAN / .3DC",
    "clips": "Animation clips, rigs and skin bindings -> the model's folder",
    "ui": "Menu sprites, icon banks, font glyphs",
    "text": "DREAMS.INI, dialogue script, level manifests as JSON",
    "music": "Redbook CD audio -> Opus @ 96 kbps (or MP3/AAC)",
    "sfx": "FSB sound effects -> Opus @ 32 kbps (or MP3/AAC)",
    "voice": "DIALOG.DRD dialogue -> Opus @ 32 kbps (or MP3/AAC)",
    "cutscenes": "Cutscenes -> H.264 MP4 (faststart)",
    "movies": "UBB movies -> H.264 MP4 (faststart)",
    "textures": "HNM4 texture animations -> H.264 MP4 (faststart, muted)",
}

#: Boot assets named by ``WINDREAM.EXE`` itself - see docs/boot-sequence.md.
BOOT_VIDEOS = {"intro": "intro", "warp": "generic", "elder": "tete_e~1"}
MENU_TRACK = 13
START_PROJECT = 0

#: INTERF slots 0-7: the main menu's active and inactive corner markers.
CORNERS = {"uplf", "uprg", "dnlf", "dnrg", "uplfna", "uprgna", "dnlfna", "dnrgna"}
#: TITRES.SPR's twelve images, read off the contact sheet: four titles in three states.
TITLES = ("new_game", "load_game", "options", "quit")
TITLE_STATES = ("normal", "active", "pressed")

#: Unnamed OBJET fields, keyed by offset: their meaning is not verified yet.
OBJET_RAW = (0x3C, 0x64, 0x68, 0x6C, 0x70, 0x78, 0x88)
ADVENT_RAW = (0x14, 0x1C, 0x20, 0x24)


@dataclass
class BakeItem:
    group: str
    source: str
    output: str = ""
    src_bytes: int = 0
    dest_bytes: int = 0
    status: str = "ok"
    note: str = ""


def _which_ffmpeg() -> str | None:
    return shutil.which("ffmpeg")


def _audio_ext(audio_format: str) -> str:
    return {"opus": ".opus", "mp3": ".mp3"}.get(audio_format, ".m4a")


def _posix(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _stale(src: Path, dest: Path, force: bool) -> bool:
    """True when ``dest`` is missing or older than ``src``."""
    return force or not dest.exists() or dest.stat().st_mtime < src.stat().st_mtime


def _copy(src: Path, dest: Path, force: bool) -> bool:
    if not _stale(src, dest, force):
        return False
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest)
    return True


def _write_json(dest: Path, data, indent: int | None = None) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    separators = None if indent is not None else (",", ":")
    dest.write_text(json.dumps(data, indent=indent, separators=separators), encoding="utf-8")


def model_id(asset: str) -> str:
    """``DATA\\3DC\\XH_.DAN`` -> ``xh``: the stem ``extract`` names the model by."""
    return Path(asset.replace("\\", "/")).stem.lower().rstrip("_")


# ------------------------------------------------------------------ media ---


def _bake_audio(
    ffmpeg: str,
    src: Path,
    dest: Path,
    group: str,
    audio_format: str,
) -> tuple[bool, str]:
    dest.parent.mkdir(parents=True, exist_ok=True)
    bitrate = "96k" if group == "music" else "32k"

    if audio_format == "opus":
        codec_args = ["-c:a", "libopus", "-b:a", bitrate]
    elif audio_format == "mp3":
        quality = "4" if group == "music" else "6"
        codec_args = ["-c:a", "libmp3lame", "-q:a", quality]
    elif audio_format == "aac":
        codec_args = ["-c:a", "aac", "-b:a", bitrate]
    else:
        return False, f"unsupported audio format: {audio_format}"

    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-i",
        str(src),
        *codec_args,
        str(dest),
    ]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return False, (r.stderr or "").strip()[:160]
    return True, ""


def _bake_video(
    ffmpeg: str,
    src: Path,
    dest: Path,
    group: str,
) -> tuple[bool, str]:
    dest.parent.mkdir(parents=True, exist_ok=True)

    if group == "textures":
        # Animated textures (HNM4) are loop surfaces without audio
        cmd = [
            ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-i",
            str(src),
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-crf",
            "23",
            "-preset",
            "medium",
            "-movflags",
            "+faststart",
            "-an",
            str(dest),
        ]
    else:
        # Cutscenes and movies with optional audio mapping
        cmd = [
            ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-i",
            str(src),
            "-map",
            "0:v:0",
            "-map",
            "0:a?",
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-crf",
            "24",
            "-preset",
            "medium",
            "-movflags",
            "+faststart",
            "-c:a",
            "aac",
            "-b:a",
            "96k",
            str(dest),
        ]

    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        return False, (r.stderr or "").strip()[:160]
    return True, ""


def bake_media(
    extract_root: Path,
    baked_root: Path,
    groups: list[str],
    audio_format: str,
    force: bool,
    progress: Progress | None = None,
) -> list[BakeItem]:
    work: list[tuple[str, Path, Path]] = []
    for g in groups:
        sub, pat = MEDIA[g]
        src_dir = extract_root / sub
        if not src_dir.is_dir():
            continue
        dest_ext = _audio_ext(audio_format) if g in AUDIO_GROUPS else ".mp4"
        for src_file in sorted(src_dir.glob(pat)):
            work.append((g, src_file, baked_root / sub / f"{src_file.stem}{dest_ext}"))
    if not work:
        return []

    ffmpeg = _which_ffmpeg()
    if not ffmpeg:
        return [BakeItem(g, "-", status="skipped", note="ffmpeg not on PATH") for g in groups]

    items: list[BakeItem] = []
    task_id = progress.add_task("media", total=len(work)) if progress else None
    for g, src, dest in work:
        src_size = src.stat().st_size
        rel_src = str(src.relative_to(extract_root))
        rel_dest = str(dest.relative_to(baked_root))

        if dest.exists() and not force:
            items.append(
                BakeItem(g, rel_src, rel_dest, src_size, dest.stat().st_size, "skipped", "exists")
            )
        else:
            if g in AUDIO_GROUPS:
                ok, note = _bake_audio(ffmpeg, src, dest, g, audio_format)
            else:
                ok, note = _bake_video(ffmpeg, src, dest, g)
            dest_size = dest.stat().st_size if dest.exists() else 0
            ratio = (dest_size / src_size) if src_size and dest_size else 0.0
            items.append(
                BakeItem(
                    g,
                    rel_src,
                    rel_dest if ok else "",
                    src_size,
                    dest_size,
                    "ok" if ok else "failed",
                    note or f"-{(1.0 - ratio) * 100:.1f}% ({dest_size / 1024:.1f} KB)",
                )
            )
        if progress and task_id is not None:
            progress.advance(task_id)
    return items


# ------------------------------------------------------------------- glTF ---


def _bake_gltf(src: Path, dest: Path, prefix: str, textures: str, force: bool) -> int:
    """Copy one glTF with its buffer and images into a folder of its own.

    ``extract`` writes flat names (``h18angkr.bin``, ``h18angkr_h18_sol.png``);
    here the buffer takes the glTF's own name and each image drops the stem
    prefix, so a folder reads ``scene.gltf  scene.bin  tex/h18_sol.png``.
    Returns the number of files written.
    """
    doc = json.loads(src.read_text(encoding="utf-8"))
    written = 0
    buffers = [b for b in doc.get("buffers", []) if not b.get("uri", "data:").startswith("data:")]
    for i, buf in enumerate(buffers):
        name = dest.with_suffix(".bin").name if len(buffers) == 1 else f"{dest.stem}{i}.bin"
        written += _copy(src.parent / buf["uri"], dest.parent / name, force)
        buf["uri"] = name
    for img in doc.get("images", []):
        uri = img.get("uri", "")
        if not uri or uri.startswith("data:"):
            continue
        name = textures + uri.removeprefix(prefix)
        written += _copy(src.parent / uri, dest.parent / name, force)
        img["uri"] = name
    if _stale(src, dest, force):
        _write_json(dest, doc)
        written += 1
    return written


def bake_scenes(extract_root: Path, baked_root: Path, force: bool) -> list[BakeItem]:
    src_dir = extract_root / "scenes"
    if not src_dir.is_dir():
        return [
            BakeItem(
                "scenes", "scenes/", status="skipped", note="run: dreams extract --only scenes"
            )
        ]
    items = []
    for src in sorted(src_dir.glob("*.gltf")):
        dest = baked_root / "scenes" / src.stem / "scene.gltf"
        n = _bake_gltf(src, dest, f"{src.stem}_", "tex/", force)
        status = "ok" if n else "skipped"
        items.append(
            BakeItem(
                "scenes",
                _posix(src, extract_root),
                _posix(dest, baked_root),
                status=status,
                note=f"{n} files",
            )
        )
    return items


def bake_models(extract_root: Path, baked_root: Path, force: bool) -> list[BakeItem]:
    src_dir = extract_root / "models"
    if not src_dir.is_dir():
        return [
            BakeItem(
                "models", "models/", status="skipped", note="run: dreams extract --only models"
            )
        ]
    items = []
    for src in sorted(src_dir.glob("*.gltf")):
        dest = baked_root / "models" / src.stem / "model.gltf"
        n = _bake_gltf(src, dest, f"{src.stem}_", "", force)
        status = "ok" if n else "skipped"
        items.append(
            BakeItem(
                "models",
                _posix(src, extract_root),
                _posix(dest, baked_root),
                status=status,
                note=f"{n} files",
            )
        )
    return items


def bake_clips(extract_root: Path, baked_root: Path, force: bool) -> list[BakeItem]:
    """Clips, rig and skin bindings land beside the model they animate."""
    src_dir = extract_root / "animations"
    if not (src_dir / "catalog.json").is_file():
        return [
            BakeItem(
                "clips",
                "animations/",
                status="skipped",
                note="run: dreams extract --only animations",
            )
        ]
    catalog = json.loads((src_dir / "catalog.json").read_text(encoding="utf-8"))
    listing = json.loads((src_dir / "manifest.json").read_text(encoding="utf-8"))
    items = []
    for entry in catalog:
        folder = baked_root / "models" / entry["assetStem"]
        clips = listing.get(entry["model"], [])
        n = sum(
            _copy(
                src_dir / entry["model"] / f"{c['id']}.json",
                folder / "clips" / f"{c['id']}.json",
                force,
            )
            for c in clips
        )
        n += _copy(src_dir / f"{entry['assetStem']}_skin.json", folder / "skin.json", force)
        _write_json(folder / "clips.json", {**entry, "clips": clips}, indent=1)
        items.append(
            BakeItem(
                "clips",
                f"animations/{entry['model']}/",
                _posix(folder, baked_root),
                status="ok" if n else "skipped",
                note=f"{len(clips)} clips",
            )
        )
    return items


# --------------------------------------------------------------- projects ---


def _i32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<i", buf, off)[0]


def _video_index(extract_root: Path) -> dict[str, list[tuple[str, str]]]:
    """Bare stem -> [(group, extracted name)]; names carry ``_d1``/``_d2`` when discs differ."""
    found: dict[str, list[tuple[str, str]]] = {}
    for group in VIDEO_GROUPS:
        for p in sorted((extract_root / MEDIA[group][0]).glob("*.mkv")):
            base = re.sub(r"_d[12]$", "", p.stem)
            found.setdefault(base, []).append((group, p.stem))
    return found


def _pick(names: list[tuple[str, str]], base: str, disc: int) -> tuple[str, str] | None:
    """The disc-neutral copy if there is one, else the copy on ``disc``, else any."""
    for want in (base, f"{base}_d{disc}"):
        for group, name in names:
            if name == want:
                return group, name
    return names[0] if names else None


class _Resolver:
    """Turns names in a project record into data-root paths, or reports them missing."""

    def __init__(self, extract_root: Path, audio_ext: str) -> None:
        self.ext = audio_ext
        self.scenes = {p.stem for p in (extract_root / "scenes").glob("*.gltf")}
        self.models = {p.stem for p in (extract_root / "models").glob("*.gltf")}
        self.videos = _video_index(extract_root)
        self.music = {p.stem for p in (extract_root / "audio" / "music").glob("*.flac")}
        self.scene_disc: dict[str, int] = {}
        meta = extract_root / "metadata" / "scenes.json"
        if meta.is_file():
            for row in json.loads(meta.read_text(encoding="utf-8")):
                if "disc" in row:
                    self.scene_disc[Path(row["file"]).stem.lower()] = row["disc"]

    def video(self, name: str, disc: int) -> str | None:
        base = Path(name.replace("\\", "/")).stem.lower()
        hit = _pick(self.videos.get(base, []), base, disc)
        return f"video/{hit[0]}/{hit[1]}.mp4" if hit else None

    def track(self, number: int, disc: int) -> str | None:
        # MCI plays the track number off whichever disc is in the drive: the
        # one holding the level. The level's disc is inferred from where its
        # scene file lives. [unverified: the game's own disc bookkeeping]
        for d in (disc, 3 - disc):
            stem = f"d{d}_track{number:02d}"
            if stem in self.music:
                return f"audio/music/{stem}{self.ext}"
        return None


def _project_json(n: int, rec: bytes, names: dict[str, str], res: _Resolver) -> dict:
    from dreams.formats import project

    pj = project.parse(n, rec)
    scene = model_id(pj.scene) if pj.scene else ""
    disc = res.scene_disc.get(scene, 1)
    needs = [f"projects/{n}.json", f"projects/{n}.bin"]
    missing: list[str] = []

    def need(path: str | None, name: str) -> str | None:
        if path:
            needs.append(path)
        elif name:
            missing.append(name)
        return path

    need(f"scenes/{scene}/" if scene in res.scenes else None, pj.scene)

    # The engine reads a video name at +0x3C and +0x5C as a C string. The
    # rest of the area is open: names sit in six 16-byte slots from +0x3C to
    # +0x9C, not the documented char[32] fields, and across 150 records 36 of
    # them begin one byte late behind a zero - P0's is "\0ETE_E~1.HNM". So the
    # whole area goes out as found, keyed by offset, and only videos are read.
    videos = []
    for off in (0x3C, 0x5C):
        file = rec[off : off + 0x20].split(b"\x00", 1)[0].decode("latin-1")
        if file:
            path = need(res.video(file, disc), file)
            videos.append({"at": f"x{off:x}", "file": file, "path": path})
    strings = {
        f"x{m.start() + 0x3C:x}": m.group().decode("latin-1")
        for m in re.finditer(rb"[\x20-\x7e]+", rec[0x3C:0x9C])  # +0x9C on is numeric
    }

    objects = []
    for o, (slot, cell) in zip(pj.objets, project.slots(rec, "OBJET"), strict=True):
        model = None
        if slot != 0 and o.asset:  # OBJET0 is the scene itself, in 150 of 150
            mid = model_id(o.asset)
            if need(f"models/{mid}/" if mid in res.models else None, o.asset):
                model = mid
        objects.append(
            {
                "slot": slot,
                "name": o.name,
                "asset": o.asset,
                "model": model,
                "pos": list(o.position),
                "heading": o.heading,
                "flags": o.flags,
                **{f"x{off:x}": _i32(cell, off) for off in OBJET_RAW},
            }
        )

    advents = []
    for a, (slot, cell) in zip(pj.advents, project.slots(rec, "LINKADVENT"), strict=True):
        path = (
            need(res.video(a.cutscene_video, disc), a.cutscene_video) if a.cutscene_video else None
        )
        advents.append(
            {
                "slot": slot,
                "name": a.name,
                **{f"x{off:x}": _i32(cell, off) for off in ADVENT_RAW},
                "video": a.cutscene_video,
                "videoPath": path,
            }
        )

    music = None
    if pj.cd_track > 0:
        music = need(res.track(pj.cd_track, disc), f"CD track {pj.cd_track}")

    return {
        "index": n,
        "id": pj.name,
        "name": names.get(pj.name, ""),
        "scene": scene,
        "disc": disc,
        "spawn": list(pj.spawn_position),
        "heading": pj.spawn_heading,
        "ambient": list(pj.ambient_rgb),
        "light1": list(pj.dir_light1),
        "light2": list(pj.dir_light2),
        "fog": list(pj.fog),
        "sky": list(pj.sky_rgb),
        "fov": pj.camera_fov,
        "cdTrack": pj.cd_track,
        "music": music,
        "aiSchedule": pj.ai_schedule_selector,
        "x138": _i32(rec, 0x138),  # documented as day/night 0/1; P0 holds 16
        "videos": videos,
        "headerStrings": strings,
        "links": [
            {
                "slot": slot,
                "name": ln.name,
                "destination": ln.destination,
                "to": ln.project,
                "min": list(ln.lo),
                "max": list(ln.hi),
            }
            for ln, (slot, _) in zip(pj.links, project.slots(rec, "LINK"), strict=True)
        ],
        "objects": objects,
        "boxes": [
            {"slot": slot, "name": b.name, "xf0": b.kind, "points": [list(q) for q in b.points]}
            for b, (slot, _) in zip(pj.boxes, project.slots(rec, "BOX"), strict=True)
        ],
        "advents": advents,
        "needs": list(dict.fromkeys(needs)),
        "missing": sorted(set(missing)),
    }


def bake_projects(extract_root: Path, baked_root: Path, audio_ext: str) -> list[BakeItem]:
    """One JSON per project in raw engine units, beside the raw record it came from.

    Always rewritten: 150 small files, and their content depends on what else
    was extracted (a model that now exists moves from ``missing`` to ``needs``).
    """
    from dreams.formats import resource

    src_dir = extract_root / "projects"
    records = sorted(src_dir.glob("*.bin"), key=lambda p: int(p.stem)) if src_dir.is_dir() else []
    if not records:
        return [
            BakeItem(
                "projects",
                "projects/",
                status="skipped",
                note="run: dreams extract --only projects",
            )
        ]
    names: dict[str, str] = {}
    ini = extract_root / "text" / "dreams.ini"
    if ini.is_file():
        names = dict(resource.read(ini, encoding="utf-8").levels)
    res = _Resolver(extract_root, audio_ext)
    out = baked_root / "projects"
    missing = 0
    for path in records:
        n = int(path.stem)
        data = _project_json(n, path.read_bytes(), names, res)
        missing += len(data["missing"])
        _write_json(out / f"{n}.json", data, indent=1)
        _copy(path, out / f"{n}.bin", force=False)
    return [
        BakeItem(
            "projects",
            "projects/",
            "projects/",
            status="ok",
            note=f"{len(records)} projects; {missing} referenced assets absent or not extracted",
        )
    ]


# --------------------------------------------------------------------- ui ---


def _icon_dir(extract_root: Path) -> Path | None:
    """The retail ``ICONES.BF``; disc 2's copy adds TITRES.SPR, so prefer it."""
    root = extract_root / "images" / "icons"
    dirs = sorted(d for d in root.glob("icone_icones_bf*") if d.is_dir()) if root.is_dir() else []
    with_titles = [d for d in dirs if any(d.glob("titres_*.png"))]
    return (with_titles or dirs or [None])[-1]


_SPRITE = re.compile(r"^(?P<bank>[a-z0-9]+)_(?P<label>.+)_(?P<w>\d+)x(?P<h>\d+)$", re.IGNORECASE)


def bake_ui(extract_root: Path, baked_root: Path, force: bool) -> list[BakeItem]:
    items = []
    out = baked_root / "ui"
    icons = _icon_dir(extract_root)
    if icons is None:
        items.append(
            BakeItem(
                "ui", "images/icons/", status="skipped", note="run: dreams extract --only icons"
            )
        )
    else:
        menu = banks = 0
        for png in sorted(icons.glob("*.png")):
            m = _SPRITE.match(png.stem)
            if not m:
                continue
            bank, label = m["bank"].lower(), m["label"]
            banks += _copy(png, out / "icons" / bank / f"{label}_{m['w']}x{m['h']}.png", force)
            if bank == "interf" and label.lower() in CORNERS:
                menu += _copy(png, out / "menu" / f"bracket_{label.lower()}.png", force)
            elif bank == "titres" and label.isdigit() and int(label) < 12:
                i = int(label)
                name = f"title_{TITLES[i % 4]}_{TITLE_STATES[i // 4]}.png"
                menu += _copy(png, out / "menu" / name, force)
        items.append(
            BakeItem(
                "ui",
                _posix(icons, extract_root),
                "ui/",
                note=f"{menu} menu, {banks} icon files written",
            )
        )
    cursor = [d for d in (extract_root / "images" / "icons").glob("*sour*") if d.is_dir()]
    for d in cursor:
        n = sum(
            _copy(p, out / "icons" / "sour" / p.name.removeprefix("sour_"), force)
            for p in d.glob("sour_*.png")
        )
        items.append(
            BakeItem("ui", _posix(d, extract_root), "ui/icons/sour/", note=f"{n} files written")
        )
    fonts = extract_root / "images" / "sprites"
    for d in sorted(fonts.glob("hi*")) if fonts.is_dir() else []:
        n = sum(_copy(p, out / "fonts" / d.name / p.name, force) for p in d.glob("*.png"))
        items.append(
            BakeItem(
                "ui", _posix(d, extract_root), f"ui/fonts/{d.name}/", note=f"{n} glyphs written"
            )
        )
    return items


# ------------------------------------------------------------------- text ---


def bake_text(extract_root: Path, baked_root: Path, audio_ext: str) -> list[BakeItem]:
    from dreams.formats import resource

    src = extract_root / "text"
    out = baked_root / "text"
    items = []
    ini = src / "dreams.ini"
    if ini.is_file():
        _write_json(
            out / "dreams-ini.json", resource.read(ini, encoding="utf-8").sections, indent=1
        )
        items.append(BakeItem("text", "text/dreams.ini", "text/dreams-ini.json"))
    script = src / "dialogue.json"
    if script.is_file():
        entries = json.loads(script.read_text(encoding="utf-8"))
        voices = sorted((extract_root / "audio" / "voice").glob("voice_*.flac"))
        # The voice group numbers the bank's clips in file order; the entries
        # are in the same order and each carries one clip, so they pair 1:1
        # when the counts agree.
        paired = len(voices) == sum(1 for e in entries if e.get("hasWave"))
        clip = 0
        for e in entries:
            e["voice"] = None
            if paired and e.get("hasWave"):
                e["voice"] = f"audio/voice/{voices[clip].stem}{audio_ext}"
                clip += 1
        _write_json(out / "dialogue.json", entries, indent=1)
        note = f"{len(entries)} entries" + (
            "" if paired else "; voice clips not paired (counts differ)"
        )
        items.append(BakeItem("text", "text/dialogue.json", "text/dialogue.json", note=note))
    manifests = {
        p.stem: [ln.strip() for ln in p.read_text(encoding="utf-8").splitlines() if ln.strip()]
        for p in sorted(src.glob("listl*.txt"))
    }
    if manifests:
        _write_json(out / "manifests.json", manifests, indent=1)
        items.append(
            BakeItem(
                "text",
                "text/listl*.txt",
                "text/manifests.json",
                note=f"{len(manifests)} level manifests",
            )
        )
    return items or [
        BakeItem("text", "text/", status="skipped", note="run: dreams extract --only text,dialogue")
    ]


# ------------------------------------------------------------------ index ---


def _files(root: Path, sub: str) -> list[str]:
    folder = root / sub
    return (
        sorted(_posix(p, root) for p in folder.iterdir() if p.is_file()) if folder.is_dir() else []
    )


def _find(files: list[str], stem: str) -> str | None:
    by_stem = {Path(f).stem: f for f in files}
    return next((by_stem[s] for s in (stem, f"{stem}_d1", f"{stem}_d2") if s in by_stem), None)


def write_index(root: Path) -> dict:
    """The data root's only listing, rebuilt from whatever is on disk.

    ``pack`` calls this on a release's copy, so a subset gets an index of
    exactly what it holds.
    """
    projects = []
    for p in sorted((root / "projects").glob("*.json"), key=lambda p: int(p.stem)):
        pj = json.loads(p.read_text(encoding="utf-8"))
        projects.append({k: pj.get(k) for k in ("index", "id", "name", "scene")})

    scenes = {}
    for gltf in sorted((root / "scenes").glob("*/scene.gltf")):
        scenes[gltf.parent.name] = {"textured": '"textures"' in gltf.read_text(encoding="utf-8")}

    models = {}
    for folder in sorted(d for d in (root / "models").glob("*") if d.is_dir()):
        gltf = folder / "model.gltf"
        if not gltf.is_file():
            continue
        entry: dict = {"textured": '"textures"' in gltf.read_text(encoding="utf-8")}
        clips = folder / "clips.json"
        if clips.is_file():
            data = json.loads(clips.read_text(encoding="utf-8"))
            entry["animation"] = {k: v for k, v in data.items() if k != "clips"}
        models[folder.name] = entry

    audio = {g: _files(root, MEDIA[g][0]) for g in AUDIO_GROUPS}
    video = {g: _files(root, MEDIA[g][0]) for g in VIDEO_GROUPS}

    boot: dict = {key: _find(video["cutscenes"], stem) for key, stem in BOOT_VIDEOS.items()}
    boot["menuMusic"] = _find(audio["music"], f"d1_track{MENU_TRACK:02d}") or _find(
        audio["music"], f"d2_track{MENU_TRACK:02d}"
    )
    boot["startProject"] = START_PROJECT

    resident: dict = {"scenes": [], "models": []}
    manifests = root / "text" / "manifests.json"
    if manifests.is_file():
        for line in json.loads(manifests.read_text(encoding="utf-8")).get("listl0", []):
            stem, ext = model_id(line), Path(line.replace("\\", "/")).suffix.lower()
            if ext == ".dsn" and stem in scenes:
                resident["scenes"].append(stem)
            elif ext in (".dan", ".3dc") and stem in models:
                resident["models"].append(stem)

    index = {
        "generated": datetime.now(UTC).isoformat(timespec="seconds"),
        "projects": projects,
        "scenes": scenes,
        "models": models,
        "audio": audio,
        "video": video,
        "boot": boot,
        "resident": resident,
    }
    _write_json(root / "index.json", index, indent=1)
    return index


# -------------------------------------------------------------------- run ---


def run(
    extract_root: Path,
    baked_root: Path,
    groups: list[str],
    audio_format: str = "opus",
    force: bool = False,
    progress: Progress | None = None,
) -> dict:
    """Bake the requested groups from ``extract_root`` into ``baked_root``."""
    if not extract_root.is_dir():
        raise RuntimeError(f"extract root does not exist: {extract_root}")
    baked_root.mkdir(parents=True, exist_ok=True)
    ext = _audio_ext(audio_format)

    steps = {
        "projects": lambda: bake_projects(extract_root, baked_root, ext),
        "scenes": lambda: bake_scenes(extract_root, baked_root, force),
        "models": lambda: bake_models(extract_root, baked_root, force),
        "clips": lambda: bake_clips(extract_root, baked_root, force),
        "ui": lambda: bake_ui(extract_root, baked_root, force),
        "text": lambda: bake_text(extract_root, baked_root, ext),
    }
    items: list[BakeItem] = []
    for g in groups:
        if g in steps:
            task = progress.add_task(g, total=1) if progress else None
            items += steps[g]()
            if progress and task is not None:
                progress.update(task, completed=1)
    media = [g for g in groups if g in MEDIA]
    if media:
        items += bake_media(extract_root, baked_root, media, audio_format, force, progress)

    write_index(baked_root)

    # Merge with the previous run, so `--only projects` does not forget the video.
    path = baked_root / "manifest.json"
    previous = (
        json.loads(path.read_text(encoding="utf-8")).get("items", []) if path.is_file() else []
    )
    kept = [i for i in previous if i.get("group") not in groups]
    written = [i for i in items if i.status == "ok"]
    manifest = {
        "created_at": datetime.now(UTC).isoformat(),
        "extract_root": str(extract_root),
        "baked_root": str(baked_root),
        "audio_format": audio_format,
        "groups": groups,
        "items": kept + [asdict(i) for i in items],
        "totals": {
            "source_bytes": sum(i.src_bytes for i in written),
            "baked_bytes": sum(i.dest_bytes for i in written),
            "files_written": len(written),
        },
    }
    _write_json(path, manifest, indent=2)
    manifest["items"] = [asdict(i) for i in items]  # the caller reports this run only
    return manifest
