"""Build the release executable and zip it: what a user downloads.

Build: DREAMS_OUT/recomp/windream/build-release, apart from the development
build. The executable is DreamsToReality (CMakeLists.txt WD_RELEASE): on
Windows a GUI-subsystem program (no console window) with the C runtime and
SDL3 linked statically, whose stderr and stdout go to log.txt in the user data
directory. It has the development build's compiler flags (unoptimized): an
optimized build of the lifted code has never been verified.

Version: `git describe --tags --always` (v0.1.0 on a tagged commit), or
--version. It is compiled in (WD_VERSION; the first line of log.txt), written
into README.txt and the file names.

Output: DREAMS_OUT/recomp/windream/release/DreamsToReality/ holding the
executable, README.txt, resources/editor-tree.tsv (the Dreams Editor menu of
Develop, extracted from the July 1997 demo: DREAMS_WIP_DIR must be set; spec
008 phase 1) and resources/fonts/ (the July editor fonts, from the same demo,
SHA-256 checked), and beside it DreamsToReality-<version>-<system>.zip
(what is published) and the build's .pdb under the same name (kept, not
published: it symbolizes the exe+0x... addresses of a user's crash report and
crash dump). The script fails if the folder holds anything else (delete what a
test run left there), if the executable imports a DLL that Windows does not
ship, or if it contains the development control channel
(recomp/windream/devtools) or the launcher's test-only script and environment
variables (recomp/launcher/testing.h).

usage: uv run --with pefile python recomp/windream/release.py [--optimize] [--version V]
"""

import argparse
import platform
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent / "editor"))
import editor_tree  # noqa: E402
import recomp_env  # noqa: E402

NAME = "DreamsToReality"
# Windows' own DLLs the executable may import: the Win32 API SDL3, the host and
# the D3D11 renderer use (dwmapi: its display timing, recomp/render/graphics_d3d11.cpp). The
# C and C++ runtimes (VCRUNTIME140, MSVCP140, api-ms-win-crt-*) are not on the
# list: the release links them statically.
SYSTEM_DLLS = frozenset(
    name + ".dll"
    for name in (
        "advapi32", "d3d11", "dbghelp", "dwmapi", "dxgi", "gdi32", "imm32", "kernel32", "ole32",
        "oleaut32", "setupapi", "shell32", "user32", "version", "winmm",
    )
)  # fmt: skip
SUBSYSTEM_WINDOWS_GUI = 2
# The development control channel embeds this text (devtools/devtools.c,
# MARKER); a release must not have it.
DEVTOOLS_MARKER = b"wd-devtools"
# The launcher's test-only code carries this text (launcher/testscript.cpp,
# kBuildMarker); CMake option DREAMS_LAUNCHER_TESTING, forced off under WD_RELEASE.
LAUNCHER_TESTING_MARKER = b"launcher-testing"

README = """\
Dreams to Reality - port for modern Windows
===========================================
Version {version}

This is the 1997 Cryo game "Dreams to Reality" (European English edition for
Windows), recompiled to run on a modern PC: the game's program, GDIDREAM.EXE,
translated to run natively. No game data, music or video is included. You
need your own two game discs as disc images:

    one .cue file with its .bin files for disc 1, and the same for disc 2

The music is CD audio and exists only in such an image. An .iso file also
works, but then the game has no music.

This is a pre-release: see "If something goes wrong" below.

What you need
-------------
- Both discs of the European English Windows release. The launcher checks
  disc 1 by its program, GDIDREAM.EXE, whose SHA-256 must be
      b2f053bd26627eb618f034481fbeb49c2287bec834351787385a69d74db05001
  Other editions (Dutch, Spanish, Turkish, ...) have another program and are
  refused with "wrong edition".
- Disc images: a .cue with its .bin files (for example a dump named
  "Dreams to Reality (Europe) (Disc 1).cue" with one .bin per track), or an
  .iso without music. CHD, MDF/MDS, NRG, CCD/IMG and CDI images and zip, 7z
  or rar archives are not read: convert or unpack them to .cue/.bin first.
  An installed copy (C:\\CRYO\\DREAMS) or a CD in a drive cannot be used.
- 64-bit Windows 10 or 11. The default renderer, "New (GPU)", needs a
  Direct3D 11 graphics card; "Original (software)" does not. Nothing else to
  install.

The program is not signed, so Windows may warn about an unknown publisher:
choose "More info", then "Run anyway".

First start
-----------
1. Unpack this folder anywhere you can write to.
2. Start DreamsToReality.exe. The launcher window opens.
3. Under "Discs", choose the .cue file of each disc (in either order), or drop
   it on the disc's row (not on DreamsToReality.exe). Each row says whether
   the image was found and is the supported edition.
4. Change the port settings if you like (renderer, window, keyboard, gamepad).
   "Smooth motion" is on unless you turn it off: the game runs a steady 30
   steps a second and, with the GPU renderer, shows a frame at every refresh
   of your display, and "Camera smoothing" (60 ms) evens out the camera
   (0 turns it off; more is smoother but the camera trails further).
   Turn Smooth motion off to play with the original timing.
5. Press Play.

The launcher opens on every start with your settings remembered. To skip it,
start "DreamsToReality.exe --play" (for example from a shortcut): the game
starts at once, or a message says what is wrong with the discs.

    --play           start the game without the launcher window
    --disc1 PATH     disc 1 for this run only (also --disc2)
    --data DIR       user data directory for this run only

Where your files are
--------------------
Beside DreamsToReality.exe:

    dreams.ini       the launcher's settings (disc paths, port settings)
    userdata\\        everything the game writes
        CRYO\\DREAMS\\data\\game\\   your saved games
        log.txt                   the log of the last run
        crash-<number>.dmp        written if the game crashes (can be large)
        direct-fatal.txt          written if the GPU renderer stops

If that folder cannot be written to, both are kept in
%APPDATA%\\DreamsToReality instead; the launcher shows which is in use.
Nothing is written anywhere else, and the disc images are only read.

In the game
-----------
Arrows move, Ctrl jumps or kicks, Alt punches, Space switches to combat,
1 to 3 select magic, Esc opens the menu, holding F10 shows the controls.
F11 toggles fullscreen. With the gamepad mode "Game joystick", press J in the
game to use the pad and K to return to the keyboard; connect the pad before
you press Play and leave its stick alone while the game starts.

The game saves by itself on every level entry; it has no Save command.

If something goes wrong
-----------------------
- The game stops with "The GPU renderer stopped on a case it does not
  support": choose Renderer: Original (software) in the launcher and play
  on. In this pre-release the GPU renderer loads every level in automated
  tests but has seen little real play.
- Keypad 1 to 5 toggle the original game's hidden debug switches; keypad 5
  turns off collision and level exits. Press the key again to undo.
- With Smooth motion off, a frame cap above 30 breaks the original game's
  physics.
- To report a problem, open an issue at
  https://github.com/jlagedo/dreams-to-reality-re/issues and attach
  userdata\\log.txt (its first line names this version, {version}).
"""


def foreign_dlls(imported: list[str]) -> list[str]:
    """The imported DLL names Windows does not ship (case ignored), sorted."""
    return sorted({name for name in imported if name.lower() not in SYSTEM_DLLS}, key=str.lower)


def unexpected_files(folder: Path, expected: set[str]) -> list[str]:
    """Paths below folder, relative, that are not one of the expected files."""
    found = (p.relative_to(folder).as_posix() for p in sorted(folder.rglob("*")))
    return [name for name in found if name not in expected]


def check_executable(exe: Path) -> list[str]:
    """What is wrong with a Windows release executable: imports of DLLs that
    are not Windows' own, or a console subsystem. Needs pefile."""
    try:
        import pefile
    except ImportError:
        sys.exit("release.py needs pefile to check the executable: uv run --with pefile python ...")
    pe = pefile.PE(str(exe), fast_load=True)
    names = ("IMAGE_DIRECTORY_ENTRY_IMPORT", "IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT")
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY[name] for name in names])
    tables = ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT")
    imported = [e.dll.decode() for table in tables for e in getattr(pe, table, [])]
    problems = [f"imports {name}, which Windows does not ship" for name in foreign_dlls(imported)]
    if pe.OPTIONAL_HEADER.Subsystem != SUBSYSTEM_WINDOWS_GUI:
        problems.append(f"subsystem is {pe.OPTIONAL_HEADER.Subsystem}, not the Windows GUI one (2)")
    pe.close()
    return problems


def option_problems(
    exe: Path, cache: Path, marker: bytes, option_name: str, what: str
) -> list[str]:
    """What shows that a development-only feature is in a release build: its
    marker text in the executable, or its CMake option not off in the build's
    cache (CMakeLists.txt forces it off under WD_RELEASE)."""
    problems = []
    if marker in exe.read_bytes():
        problems.append(f"contains {what} (the text {marker.decode()!r})")
    found = re.search(rf"^{option_name}:\w+=(.*)$", cache.read_text(errors="replace"), re.MULTILINE)
    value = found[1].strip().upper() if found else "missing"
    if value not in ("OFF", "0", "FALSE", "NO"):
        problems.append(f"was built with {option_name} {value} ({cache}); a release has it OFF")
    return problems


def devtools_problems(exe: Path, cache: Path) -> list[str]:
    """The development control channel in a release build (WD_DEVTOOLS)."""
    return option_problems(
        exe, cache, DEVTOOLS_MARKER, "WD_DEVTOOLS", "the development control channel"
    )


def launcher_testing_problems(exe: Path, cache: Path) -> list[str]:
    """The launcher's test script and test-only variables in a release build
    (DREAMS_LAUNCHER_TESTING)."""
    return option_problems(
        exe, cache, LAUNCHER_TESTING_MARKER, "DREAMS_LAUNCHER_TESTING",
        "the launcher's test-only script and environment variables",
    )  # fmt: skip


def system_name() -> str:
    if sys.platform == "win32":
        return "windows-x64"
    return f"{sys.platform}-{platform.machine().lower()}"


def git_version() -> str:
    """`git describe --tags --always` of this tree: v0.1.0 on the tagged commit,
    v0.1.0-3-gabc1234 three commits later. Without --dirty: an edited document
    does not change the build."""
    p = subprocess.run(
        ["git", "describe", "--tags", "--always"],
        cwd=Path(__file__).resolve().parent, capture_output=True, text=True,
    )  # fmt: skip
    if p.returncode or not p.stdout.strip():
        sys.exit(f"git describe failed ({p.stderr.strip()}); name the build with --version")
    return p.stdout.strip()


def release_stem(version: str) -> str:
    """The published name without extension: DreamsToReality-v0.1.0-windows-x64."""
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]*", version):
        raise ValueError(f"version {version!r}: letters, digits and . _ + - only")
    return f"{NAME}-{version}-{system_name()}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--optimize", action="store_true",
        help="build with CMake's Release type (optimized) instead of the development build's "
        "flags. UNVERIFIED: this project has only ever built and tested the lifted code "
        "unoptimized, and the build takes much longer",
    )  # fmt: skip
    ap.add_argument("--version", help="the build's name (default: git describe --tags --always)")
    args = ap.parse_args()
    version = args.version or git_version()
    stem = release_stem(version)
    out = recomp_env.out_dir("windream")
    july = editor_tree.july_exe()
    if not july or not july.is_file():
        sys.exit("release.py needs the July 1997 demo for the editor menu: set DREAMS_WIP_DIR")
    try:
        menu = editor_tree.build(july, editor_tree.out_dir())
    except editor_tree.TreeError as e:
        sys.exit(f"editor menu: {e}")
    build = recomp_env.build_dir(out, release=True)
    rc = recomp_env.configure_and_build(
        build, out / "gen", release=True, optimize=args.optimize, version=version
    )
    if rc:
        return rc
    exe_name = recomp_env.exe_name(NAME)
    built = build / exe_name
    problems = check_executable(built) if sys.platform == "win32" else []
    problems += devtools_problems(built, build / "CMakeCache.txt")
    problems += launcher_testing_problems(built, build / "CMakeCache.txt")
    if problems:
        print("\n".join(f"{built}: {problem}" for problem in problems), file=sys.stderr)
        return 1

    release = out / "release"
    stage = release / NAME
    stage.mkdir(parents=True, exist_ok=True)
    shutil.copy2(built, stage / exe_name)
    readme = README.format(version=version)
    (stage / "README.txt").write_text(readme, encoding="utf-8", newline="\r\n")
    resource = f"resources/{editor_tree.RESOURCE_NAME}"
    fonts = [f"resources/{editor_tree.FONT_DIR}/{name}" for name in editor_tree.FONTS]
    (stage / "resources" / editor_tree.FONT_DIR).mkdir(parents=True, exist_ok=True)
    shutil.copy2(menu, stage / resource)
    for font in fonts:
        shutil.copy2(menu.parent / font.removeprefix("resources/"), stage / font)
    problems = editor_tree.check_resources(stage / "resources")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 1
    files = (exe_name, "README.txt", resource, *fonts)
    extra = unexpected_files(stage, {*files, "resources", f"resources/{editor_tree.FONT_DIR}"})
    if extra:
        print(
            f"{stage} holds files that are not part of the release; delete them and run again:\n  "
            + "\n  ".join(extra),
            file=sys.stderr,
        )
        return 1
    archive = release / f"{stem}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in files:
            z.write(stage / name, f"{NAME}/{name}")
    print(f"{stage / exe_name}  {(stage / exe_name).stat().st_size:,} bytes")
    print(f"{archive}  {archive.stat().st_size:,} bytes")
    pdb = built.with_suffix(".pdb")
    if pdb.is_file():  # Windows; keep it to symbolize this version's crash reports
        shutil.copy2(pdb, release / f"{stem}.pdb")
        print(f"{release / f'{stem}.pdb'}  kept, not for publishing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
