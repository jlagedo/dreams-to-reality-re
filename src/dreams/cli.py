"""``dreams`` command line interface.

An exploration toolkit, not a finished converter. Commands fall into three groups:

  survey   paths, census, identify, probe, strings, tags
  decode   audio, scene, model, video, res, pe
  experiment  render, stride

Run ``dreams --help`` or ``dreams <group> --help``.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Annotated

import typer
from rich.console import Console
from rich.progress import BarColumn, Progress, SpinnerColumn, TextColumn, TimeElapsedColumn
from rich.table import Table

from dreams import binio, paths, pe, probe
from dreams import extract as extractor
from dreams.formats import audio, disc, image, model, resource, scene, video

app = typer.Typer(
    add_completion=False,
    no_args_is_help=True,
    help="Reverse-engineering toolkit for Dreams to Reality (Cryo, 1997).",
)
console = Console()

audio_app = typer.Typer(no_args_is_help=True, help="FSB / DRD audio banks.")
disc_app = typer.Typer(no_args_is_help=True, help="CD image handling.")
app.add_typer(audio_app, name="audio")
app.add_typer(disc_app, name="disc")


def _files(target: Path, pattern: str | None) -> list[Path]:
    if target.is_file():
        return [target]
    return sorted(p for p in target.rglob(pattern or "*") if p.is_file())


# ---------------------------------------------------------------- survey ---


@app.command()
def config() -> None:
    """Show where the toolkit expects the discs to be."""
    table = Table("key", "path", "exists")
    for key, path, exists in paths.describe():
        table.add_row(key, str(path), "[green]yes" if exists else "[red]no")
    console.print(table)
    console.print(
        "\nOverride with [cyan]DREAMS_DISC1[/] / [cyan]DREAMS_DISC2[/] env vars, "
        "or a gitignored [cyan]dreams.local.toml[/]:"
    )
    console.print("  [dim]\\[paths][/]")
    console.print('  [dim]disc1 = "D:/path/to/extracted"[/]')


@app.command()
def census(
    target: Annotated[Path, typer.Argument(help="Directory to walk")],
    exclude: Annotated[
        str, typer.Option(help="Comma-separated dir names to skip")
    ] = "DIRECTX,DEMO0,DEMOS1,DEMOS2",
) -> None:
    """Count files and bytes by extension."""
    skip = {s.strip().upper() for s in exclude.split(",") if s.strip()}
    counts: dict[str, list[int]] = {}
    for p in target.rglob("*"):
        if not p.is_file() or {part.upper() for part in p.parts} & skip:
            continue
        ext = (p.suffix.lstrip(".") or "(none)").upper()
        row = counts.setdefault(ext, [0, 0])
        row[0] += 1
        row[1] += p.stat().st_size

    table = Table("ext", "files", "MB", title=f"census of {target}")
    for ext, (n, size) in sorted(counts.items(), key=lambda kv: -kv[1][1]):
        table.add_row(ext, str(n), f"{size / 1048576:.1f}")
    console.print(table)


@app.command()
def identify(
    target: Annotated[Path, typer.Argument(help="File or directory")],
    pattern: Annotated[str | None, typer.Option(help="Glob when target is a directory")] = None,
    limit: Annotated[int, typer.Option(help="Max rows")] = 60,
) -> None:
    """Identify files by magic bytes."""
    table = Table("file", "size", "kind", "magic", "what")
    for p in _files(target, pattern)[:limit]:
        ident = probe.identify(p)
        table.add_row(p.name, f"{ident.size:,}", ident.kind, ident.magic, ident.label)
    console.print(table)


@app.command()
def stats(
    target: Annotated[Path, typer.Argument(help="File or directory")],
    pattern: Annotated[str | None, typer.Option(help="Glob when target is a directory")] = None,
    limit_bytes: Annotated[int, typer.Option(help="Bytes to sample per file")] = 600_000,
) -> None:
    """Entropy and compressibility - tells raw data from packed data."""
    table = Table("file", "size", "entropy", "zlib%", "zero%", "verdict")
    for p in _files(target, pattern)[:40]:
        st = probe.stats(p.read_bytes()[:limit_bytes])
        table.add_row(
            p.name,
            f"{st.size:,}",
            f"{st.entropy:.2f}",
            f"{st.zlib_ratio}%",
            f"{st.zero_pct}%",
            st.verdict,
        )
    console.print(table)
    console.print(
        "[dim]Calibration on these discs: raw .TGA/.3DC/.SPR = 18-31%, "
        "INTRO.HNM (compressed video) = 69%, .DSN body = 72%.[/]"
    )


@app.command()
def regions(
    file: Path,
    bucket: Annotated[int, typer.Option(help="Bucket size in bytes")] = 65536,
) -> None:
    """Per-bucket zero/ascii map. Flat output means homogeneous (packed) data."""
    rows = probe.region_map(file.read_bytes(), bucket)
    cells = [f"{i:3}:z{z:02}a{a:02}" for i, z, a in rows]
    for i in range(0, len(cells), 6):
        console.print("  " + "  ".join(cells[i : i + 6]))


@app.command()
def tags(file: Path) -> None:
    """Count embedded four-character format tags anywhere in a file."""
    hits = probe.find_tags(file.read_bytes())
    if not hits:
        console.print("[yellow]no known tags found")
        return
    table = Table("tag", "count")
    for tag, n in hits.most_common():
        table.add_row(tag, str(n))
    console.print(table)


@app.command()
def strings(
    file: Path,
    min_len: Annotated[int, typer.Option("--min", help="Minimum run length")] = 6,
    grep: Annotated[str | None, typer.Option(help="Only lines matching this substring")] = None,
    limit: int = 80,
) -> None:
    """Printable strings in a binary."""
    found = probe.strings(file.read_bytes(), min_len)
    if grep:
        found = [s for s in found if grep.lower() in s.lower()]
    for s in list(dict.fromkeys(found))[:limit]:
        console.print(s)


@app.command()
def dump(
    file: Path,
    offset: Annotated[int, typer.Option(help="Start offset")] = 0,
    length: Annotated[int, typer.Option(help="Bytes to show")] = 256,
) -> None:
    """Hex dump."""
    console.print(binio.hexdump(file.read_bytes(), offset, length))


# ---------------------------------------------------------------- decode ---


@audio_app.command("info")
def audio_info(file: Path) -> None:
    """List the clips inside an FSB or DRD bank."""
    bank = audio.read_bank(file)
    console.print(
        f"[bold]{bank.path.name}[/]  kind={bank.kind}  "
        f"declared={bank.declared_count}  found={len(bank.clips)}"
    )
    table = Table("#", "offset", "size", "format")
    for clip in bank.clips[:30]:
        table.add_row(str(clip.index), f"0x{clip.offset:x}", f"{clip.size:,}", clip.describe())
    console.print(table)
    if len(bank.clips) > 30:
        console.print(f"[dim]... and {len(bank.clips) - 30} more")


@audio_app.command("unpack")
def audio_unpack(
    file: Path,
    out: Annotated[Path | None, typer.Option(help="Output directory")] = None,
) -> None:
    """Extract every clip from a bank as a standalone .wav."""
    bank = audio.read_bank(file)
    target = out or paths.out_dir("audio", bank.path.stem.lower())
    written, repaired = audio.extract(bank, target)
    console.print(f"[green]wrote {len(written)} wav files[/] to {target}")
    if repaired:
        console.print(
            f"[yellow]repaired wFormatTag on clip(s) {repaired}[/] — declared IEEE float "
            "at 16-bit, which cannot exist; payload is PCM. Sample data untouched."
        )


@app.command("scene")
def scene_info(
    file: Path,
    names: Annotated[bool, typer.Option(help="List the object name table")] = True,
) -> None:
    """Parse a .DSN scene header and object table."""
    sc = scene.read_dsn(file)
    ok = "[green]exact" if sc.size_ok else "[red]MISMATCH"
    console.print(
        f"[bold]{sc.path.name}[/]  {sc.actual_size:,} bytes\n"
        f"  declared size {sc.declared_size:,} {ok}\n"
        f"  count_a={sc.count_a}  name_count={sc.name_count}  "
        f"names found={len(sc.names)}\n"
        f"  body starts at 0x{sc.body_offset:x}"
    )
    if names:
        table = Table("name", "interpretation")
        for n in sc.names:
            table.add_row(n, scene.classify_name(n))
        console.print(table)


@app.command("anim")
def anim_info(file: Path) -> None:
    """Parse a .DAN animation header."""
    an = scene.read_dan(file)
    ok = "[green]exact" if an.size_ok else "[red]MISMATCH"
    console.print(
        f"[bold]{an.path.name}[/]  {an.actual_size:,} bytes\n"
        f"  declared size {an.declared_size:,} {ok}\n"
        f"  object name: {an.name}\n"
        f"  frame refs ({len(an.frame_refs)}): {', '.join(an.frame_refs) or '-'}\n"
        f"  body starts at 0x{an.body_offset:x}\n"
        "[dim]  .3DA files are not on disc - frames are embedded, names are labels[/]"
    )


@app.command("model")
def model_info(file: Path) -> None:
    """Parse an F3DC model or a PAK0 archive."""
    head = file.open("rb").read(4)
    if head == b"PAK0":
        pk = model.read_pak(file)
        console.print(
            f"[bold]{pk.path.name}[/]  {pk.actual_size:,} bytes  "
            f"declared={pk.declared_size:,}  unknown={pk.unknown}\n"
            f"  embedded F3DC chunks: {len(pk.entries)} at "
            + ", ".join(f"0x{e.offset:x}" for e in pk.entries)
        )
        return
    md = model.read_model(file)
    console.print(
        f"[bold]{md.path.name}[/]  {md.size:,} bytes  version={md.version}  "
        f"zeros={md.zero_pct}%\n"
        f"  material/name slots: {', '.join(md.materials) or '-'}\n"
        f"  first 16.16 values: {', '.join(f'{v:.3f}' for v in md.fixed_values)}"
    )


@app.command("video")
def video_info(
    target: Annotated[Path, typer.Argument(help="File or directory")],
    pattern: Annotated[str, typer.Option()] = "*.HNM",
) -> None:
    """Parse HNM video headers."""
    table = Table("file", "magic", "WxH", "bpp", "frames", "fps", "length", "size")
    for p in _files(target, pattern)[:60]:
        try:
            v = video.read_header(p)
        except ValueError:
            continue
        table.add_row(
            p.name,
            v.magic,
            f"{v.width}x{v.height}",
            str(v.bpp),
            str(v.frames),
            str(v.fps),
            f"{v.seconds:.1f}s",
            f"{v.size:,}",
        )
    console.print(table)


@app.command("res")
def res_parse(
    file: Path,
    section: Annotated[str | None, typer.Option(help="OBJECT, PROJECT, DIALOG, SYSTEM")] = None,
) -> None:
    """Parse DREAMS.INI - inventory items and the level list."""
    res = resource.read(file)
    if section is None:
        table = Table("section", "records")
        for name, records in res.sections.items():
            table.add_row(name, str(len(records)))
        console.print(table)
        return

    key = section.upper()
    if key == "OBJECT":
        table = Table("#", "item", "description")
        for i, (name, desc) in enumerate(res.items):
            table.add_row(str(i), name, desc)
        console.print(table)
    elif key == "PROJECT":
        table = Table("id", "level name")
        for pid, name in res.levels:
            table.add_row(pid, name)
        console.print(table)
    else:
        for rec in res.sections.get(key, []):
            console.print(" / ".join(rec))


@app.command("bundle")
def bundle_info(file: Path) -> None:
    """Parse a UBIK bundle header (.BF). Format is UNSOLVED past the header."""
    b = image.read_bundle(file)
    st = probe.stats(file.read_bytes())
    console.print(
        f"[bold]{b.path.name}[/]  {b.size:,} bytes\n"
        f"  version={b.version}  declared={b.declared:,}  entries={b.entry_count}\n"
        f"  data starts at 0x{b.data_offset:x}\n"
        f"  entropy={st.entropy:.2f}  zlib={st.zlib_ratio}%  -> {st.verdict}\n"
        "[dim]  Per-entry sub-headers hold the real dimensions. Parse them "
        "rather than guessing strides.[/]"
    )


@app.command("pe")
def pe_info(
    file: Path,
    exports: Annotated[bool, typer.Option(help="List exports")] = False,
    grep: Annotated[str | None, typer.Option(help="Filter exports")] = None,
) -> None:
    """Inspect a PE/LE binary: sections, toolchain, imports, exports."""
    b = pe.read(file)
    console.print(
        f"[bold]{b.path.name}[/]  {b.size:,} bytes  format={b.format}  "
        f"dll={b.is_dll}  subsystem={b.subsystem_version}\n"
        f"  toolchain: [cyan]{b.toolchain}[/]  "
        f"(sections: {', '.join(s.name for s in b.sections)})"
    )
    for marker in pe.fingerprints(file):
        console.print(f"  [dim]{marker}")
    if b.imports:
        table = Table("dll", "count", "functions")
        for dll, funcs in b.imports.items():
            shown = ", ".join(funcs[:6]) + (f" ... +{len(funcs) - 6}" if len(funcs) > 6 else "")
            table.add_row(dll, str(len(funcs)), shown)
        console.print(table)
    if b.exports:
        console.print(f"\n[bold]{len(b.exports)} exports[/]")
        if exports:
            names = [e for e in b.exports if not grep or grep.lower() in e.lower()]
            for i in range(0, len(names), 3):
                console.print("  " + "".join(f"{n:<34}" for n in names[i : i + 3]))


# ------------------------------------------------------------ experiment ---


@app.command()
def render(
    file: Path,
    out: Annotated[Path | None, typer.Option(help="Output PNG")] = None,
    offset: Annotated[int, typer.Option(help="Byte offset to start at")] = 0,
    width: Annotated[int, typer.Option(help="Width in pixels")] = 256,
    height: Annotated[int, typer.Option(help="Height in pixels")] = 256,
    fmt: Annotated[str, typer.Option(help="|".join(binio.PIXEL_FORMATS))] = "rgb555",
) -> None:
    """Render raw bytes as an image, to test a pixel-layout hypothesis."""
    if fmt not in binio.PIXEL_FORMATS:
        raise typer.BadParameter(f"fmt must be one of {binio.PIXEL_FORMATS}")
    bpp = binio.bytes_per_pixel(fmt)
    need = width * height * bpp
    blob = file.read_bytes()[offset : offset + need].ljust(need, b"\0")
    rgb = binio.unpack_pixels(blob, width * height, fmt)
    target = out or paths.out_dir("render") / f"{file.stem}_{offset:x}_{width}x{height}_{fmt}.png"
    binio.write_png(target, width, height, rgb)
    console.print(f"[green]wrote[/] {target}")


@app.command()
def stride(
    file: Path,
    lo: Annotated[int, typer.Option(help="Smallest stride to test")] = 16,
    hi: Annotated[int, typer.Option(help="Largest stride to test")] = 1400,
) -> None:
    """Guess image row pitch by autocorrelation. Low score = strong periodicity."""
    results = probe.best_strides(file.read_bytes(), lo, hi)
    table = Table("stride (bytes)", "px @16bpp", "px @8bpp", "score")
    for score, s in results:
        table.add_row(str(s), f"{s / 2:g}", str(s), f"{score:.2f}")
    console.print(table)


@disc_app.command("iso")
def disc_iso(
    track: Annotated[Path, typer.Argument(help="MODE1/2352 .bin data track")],
    out: Annotated[Path, typer.Argument(help="Destination .iso")],
) -> None:
    """Convert a raw 2352 B/sector data track to a mountable ISO."""
    written = disc.to_iso(track, out)
    console.print(f"[green]wrote[/] {out}  ({written:,} bytes)")


@disc_app.command("cue")
def disc_cue(file: Path) -> None:
    """List the tracks in a cue sheet."""
    table = Table("track", "mode", "file")
    for num, mode, name in disc.parse_cue(file):
        table.add_row(str(num), mode, name)
    console.print(table)
    console.print("[dim]Audio tracks are the game's music. Mount the .cue, never the .iso.[/]")


@app.command()
def extract(
    out: Annotated[
        Path | None, typer.Option(help="Destination root. Defaults to the 'extract' path.")
    ] = None,
    only: Annotated[
        str | None, typer.Option(help="Comma-separated groups to run. Default: all.")
    ] = None,
    skip: Annotated[str | None, typer.Option(help="Comma-separated groups to skip.")] = None,
    force: Annotated[bool, typer.Option(help="Re-encode files that already exist.")] = False,
    list_groups: Annotated[bool, typer.Option("--list", help="List groups and exit.")] = False,
) -> None:
    """Decode every solved asset format to a folder, losslessly.

    Audio becomes FLAC, video FFV1-in-MKV, images PNG, headers JSON. Game
    content only - the engine and the still-packed level bodies are not here.
    """
    if list_groups:
        table = Table("group", "output", "contents")
        for g in extractor.ALL_GROUPS:
            table.add_row(g, extractor.LAYOUT[g], extractor.DESCRIPTIONS.get(g, ""))
        console.print(table)
        return

    groups = [g.strip() for g in only.split(",")] if only else list(extractor.ALL_GROUPS)
    if skip:
        drop = {g.strip() for g in skip.split(",")}
        groups = [g for g in groups if g not in drop]

    unknown = [g for g in groups if g not in extractor.LAYOUT]
    if unknown:
        console.print(f"[red]unknown group(s):[/] {', '.join(unknown)}")
        console.print(f"[dim]valid: {', '.join(extractor.ALL_GROUPS)}[/]")
        raise typer.Exit(1)

    root = out or paths.get("extract")
    console.print(f"extracting [cyan]{', '.join(groups)}[/] -> [cyan]{root}[/]\n")

    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description:<12}"),
        BarColumn(),
        TextColumn("{task.completed}/{task.total}"),
        TimeElapsedColumn(),
        console=console,
    ) as progress:
        manifest = extractor.run(root, groups, force=force, progress=progress)

    t = manifest["totals"]
    table = Table("group", "ok", "skip", "fail", "files", title="extraction")
    for g in groups:
        rows = [i for i in manifest["items"] if i["group"] == g]
        table.add_row(
            g,
            str(sum(1 for r in rows if r["status"] == "ok")),
            str(sum(1 for r in rows if r["status"] == "skipped")),
            str(sum(1 for r in rows if r["status"] == "failed")),
            str(sum(len(r["outputs"]) for r in rows)),
        )
    console.print()
    console.print(table)
    console.print(
        f"\n[green]{t['files_written']} files[/], {t['bytes'] / 1048576:,.0f} MB -> {root}"
    )
    console.print(f"[dim]manifest.json and README.md written to {root}[/]")

    for item in manifest["items"]:
        if item["status"] == "failed":
            console.print(f"[red]failed[/] {item['group']}: {item['source']} — {item['note']}")


if __name__ == "__main__":
    app()


@app.command("mesh")
def mesh_cmd(
    name: Annotated[
        str | None,
        typer.Argument(help="Scene stem, e.g. E01GROTT. Omit to list every scene."),
    ] = None,
    out: Annotated[
        Path | None, typer.Option("--gltf", help="Write glTF here")
    ] = None,
    force: Annotated[bool, typer.Option("--force", help="Export unclean scenes too")] = False,
    textures: Annotated[
        bool, typer.Option("--textures/--no-textures", help="Attach textures")
    ] = True,
    preview: Annotated[
        Path | None,
        typer.Option("--preview", help="Write three-view PNG previews here"),
    ] = None,
) -> None:
    """Decode scene geometry from `.DSN` tags 1 and 2, and export glTF.

    A tag 1 decode is accepted only when **every** face it produces is also a
    triangle in tag 2, which is known-correct geometry. Five scenes pass, and
    those carry object names, materials and UVs. The rest fall back to tag 2's
    own triangle array - correct geometry, no materials. Every scene exports.
    """
    from dreams import gltf
    from dreams.formats import mesh as meshmod

    sources = list(extractor.merge_discs("*.DSN"))
    if name:
        sources = [s for s in sources if s.path.stem.upper() == name.upper()]
        if not sources:
            console.print(f"[red]no scene named {name}")
            raise typer.Exit(1)

    table = Table("scene", "objects", "verts", "faces", "source")
    exported = 0
    for s in sources:
        try:
            m = meshmod.read_mesh(s.path)
        except (ValueError, struct.error) as exc:
            table.add_row(s.path.stem, "-", "-", "-", f"[red]{exc}")
            continue
        hit, total = meshmod.verify_against_tag2(s.path)
        if total and hit == total:
            state = f"[green]tag1 + uv ({total})"
        else:
            try:
                m = meshmod.read_tri_mesh(s.path)
                state = "[cyan]tag2"
            except (ValueError, struct.error) as exc:
                table.add_row(s.path.stem, "-", "-", "-", f"[red]{exc}")
                continue
        table.add_row(
            s.path.stem, str(len(m.objects)), str(len(m.vertices)),
            str(m.face_count), state,
        )
        if preview:
            from dreams import preview as pv
            preview.mkdir(parents=True, exist_ok=True)
            pv.render(m, preview / f"{s.path.stem.lower()}.png")
        if out:
            gltf.from_scene(s.path, out, textures=textures)
            exported += 1
    console.print(table)
    if out:
        console.print(f"\nexported [green]{exported}[/] scene(s) to {out}")
