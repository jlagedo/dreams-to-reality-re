"""Transform extracted assets into lightweight, web-optimized formats.

The pipeline stages:
    extract -> disc images to canonical lossless archive (FLAC, FFV1 MKV, PNG)
    bake    -> transcode media to web-ready delivery formats (Opus / MP3, MP4 H.264)
    bundle  -> package minimal live demo distribution

Audio:
    music   -> Opus 96k (or MP3 / AAC)
    sfx     -> Opus 32k (or MP3 / AAC)
    voice   -> Opus 32k (or MP3 / AAC)

Video:
    cutscenes -> H.264 MP4 with faststart, AAC audio
    movies    -> H.264 MP4 with faststart, AAC audio
    textures  -> H.264 MP4 with faststart, muted (-an)
"""

from __future__ import annotations

import json
import shutil
import subprocess
from dataclasses import asdict, dataclass
from datetime import UTC, datetime
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from rich.progress import Progress

AUDIO_GROUPS = ("music", "sfx", "voice")
VIDEO_GROUPS = ("cutscenes", "movies", "textures")
ALL_GROUPS = (*AUDIO_GROUPS, *VIDEO_GROUPS)

LAYOUT = {
    "music": ("audio/music", "*.flac"),
    "sfx": ("audio/sfx", "*.flac"),
    "voice": ("audio/voice", "*.flac"),
    "cutscenes": ("video/cutscenes", "*.mkv"),
    "movies": ("video/movies", "*.mkv"),
    "textures": ("video/textures", "*.mkv"),
}

DESCRIPTIONS = {
    "music": "Redbook CD audio -> Opus @ 96 kbps (or MP3/AAC)",
    "sfx": "FSB sound effects -> Opus @ 32 kbps (or MP3/AAC)",
    "voice": "DIALOG.DRD dialogue -> Opus @ 32 kbps (or MP3/AAC)",
    "cutscenes": "Cutscenes -> H.264 MP4 (faststart)",
    "movies": "UBB movies -> H.264 MP4 (faststart)",
    "textures": "HNM4 texture animations -> H.264 MP4 (faststart, muted)",
}


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


def run(
    extract_root: Path,
    baked_root: Path,
    groups: list[str],
    audio_format: str = "opus",
    force: bool = False,
    progress: Progress | None = None,
) -> dict:
    """Bake media files from extract_root into web-ready formats in baked_root."""
    ffmpeg = _which_ffmpeg()
    if not ffmpeg:
        raise RuntimeError("ffmpeg executable not found in PATH; required for baking media")

    if not extract_root.is_dir():
        raise RuntimeError(f"extract root does not exist: {extract_root}")

    audio_ext = ".opus" if audio_format == "opus" else (".mp3" if audio_format == "mp3" else ".m4a")
    items: list[BakeItem] = []

    # Collect source files per group
    work: list[tuple[str, Path, Path]] = []
    for g in groups:
        if g not in LAYOUT:
            continue
        sub, pat = LAYOUT[g]
        src_dir = extract_root / sub
        dest_dir = baked_root / sub
        if not src_dir.is_dir():
            continue

        dest_ext = audio_ext if g in AUDIO_GROUPS else ".mp4"
        for src_file in sorted(src_dir.glob(pat)):
            dest_file = dest_dir / f"{src_file.stem}{dest_ext}"
            work.append((g, src_file, dest_file))

    task_id = progress.add_task("baking", total=len(work)) if progress else None

    for g, src, dest in work:
        src_size = src.stat().st_size
        rel_src = str(src.relative_to(extract_root))
        rel_dest = str(dest.relative_to(baked_root))

        if dest.exists() and not force:
            dest_size = dest.stat().st_size
            items.append(
                BakeItem(
                    group=g,
                    source=rel_src,
                    output=rel_dest,
                    src_bytes=src_size,
                    dest_bytes=dest_size,
                    status="skipped",
                    note="exists",
                )
            )
            if progress and task_id is not None:
                progress.advance(task_id)
            continue

        if g in AUDIO_GROUPS:
            ok, note = _bake_audio(ffmpeg, src, dest, g, audio_format)
        else:
            ok, note = _bake_video(ffmpeg, src, dest, g)

        dest_size = dest.stat().st_size if dest.exists() else 0
        ratio = (dest_size / src_size) if src_size and dest_size else 0.0
        pct = (1.0 - ratio) * 100

        items.append(
            BakeItem(
                group=g,
                source=rel_src,
                output=rel_dest if ok else "",
                src_bytes=src_size,
                dest_bytes=dest_size,
                status="ok" if ok else "failed",
                note=note or f"-{pct:.1f}% ({dest_size / 1024:.1f} KB)",
            )
        )

        if progress and task_id is not None:
            progress.advance(task_id)

    baked_root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "created_at": datetime.now(UTC).isoformat(),
        "extract_root": str(extract_root),
        "baked_root": str(baked_root),
        "audio_format": audio_format,
        "items": [asdict(i) for i in items],
        "totals": {
            "source_bytes": sum(i.src_bytes for i in items if i.status == "ok"),
            "baked_bytes": sum(i.dest_bytes for i in items if i.status == "ok"),
            "files_written": sum(1 for i in items if i.status == "ok"),
        },
    }

    (baked_root / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return manifest
