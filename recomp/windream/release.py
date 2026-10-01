"""Build the release executable and zip it: what a user downloads.

Build: DREAMS_OUT/recomp/windream/build-release, apart from the development
build. The executable is DreamsToReality (CMakeLists.txt WD_RELEASE): on
Windows a GUI-subsystem program (no console window) with the C runtime and
SDL3 linked statically, whose stderr and stdout go to log.txt in the user data
directory. It has the development build's compiler flags (unoptimized): an
optimized build of the lifted code has never been verified.

Output: DREAMS_OUT/recomp/windream/release/DreamsToReality/ holding the
executable and README.txt, and beside it DreamsToReality-<system>.zip. The
script fails if the folder holds anything else (delete what a test run left
there), if the executable imports a DLL that Windows does not ship, or if it
contains the development control channel (recomp/windream/devtools) or the
launcher's test-only script and environment variables (recomp/launcher/testing.h).

usage: uv run --with pefile python recomp/windream/release.py [--optimize]
"""

import argparse
import platform
import re
import shutil
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomp_env  # noqa: E402

NAME = "DreamsToReality"
# Windows' own DLLs the executable may import: the Win32 API SDL3, the host and
# the D3D11 renderer use. The C and C++ runtimes (VCRUNTIME140, MSVCP140,
# api-ms-win-crt-*) are not on the list: the release links them statically.
SYSTEM_DLLS = frozenset(
    name + ".dll"
    for name in (
        "advapi32", "d3d11", "dbghelp", "dxgi", "gdi32", "imm32", "kernel32", "ole32", "oleaut32",
        "setupapi", "shell32", "user32", "version", "winmm",
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

This is the 1997 Cryo game "Dreams to Reality" (European English edition for
Windows), recompiled to run on a modern PC. It contains no game data. You need
your own two game discs as disc images:

    one .cue file with its .bin files for disc 1, and the same for disc 2

The music is CD audio and exists only in such an image. An .iso file also
works, but then the game has no music.

First start
-----------
1. Unpack this folder anywhere you can write to.
2. Start DreamsToReality.exe. The launcher window opens.
3. Under "Discs", choose the .cue file of each disc (in either order). Each
   row says whether the image was found and is the supported edition.
4. Change the port settings if you like (renderer, window, keyboard, gamepad).
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

If that folder cannot be written to, both are kept in
%APPDATA%\\DreamsToReality instead; the launcher shows which is in use.
Nothing is written anywhere else, and the disc images are only read.

In the game
-----------
Arrows move, Ctrl jumps or kicks, Alt punches, Space switches to combat,
1 to 3 select magic, Esc opens the menu, holding F10 shows the controls.
F11 toggles fullscreen. With a gamepad in "game" mode, press J in the game to
use it and K to return to the keyboard.
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--optimize", action="store_true",
        help="build with CMake's Release type (optimized) instead of the development build's "
        "flags. UNVERIFIED: this project has only ever built and tested the lifted code "
        "unoptimized, and the build takes much longer",
    )  # fmt: skip
    args = ap.parse_args()
    out = recomp_env.out_dir("windream")
    build = recomp_env.build_dir(out, release=True)
    rc = recomp_env.configure_and_build(build, out / "gen", release=True, optimize=args.optimize)
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
    (stage / "README.txt").write_text(README, encoding="utf-8", newline="\r\n")
    extra = unexpected_files(stage, {exe_name, "README.txt"})
    if extra:
        print(
            f"{stage} holds files that are not part of the release; delete them and run again:\n  "
            + "\n  ".join(extra),
            file=sys.stderr,
        )
        return 1
    archive = release / f"{NAME}-{system_name()}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in (exe_name, "README.txt"):
            z.write(stage / name, f"{NAME}/{name}")
    print(f"{stage / exe_name}  {(stage / exe_name).stat().st_size:,} bytes")
    print(f"{archive}  {archive.stat().st_size:,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
