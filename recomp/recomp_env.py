"""Shared paths and tool environment for the recomp scripts.

The hand-written sources live in recomp/; everything they produce (generated C,
builds, runs, dumps) goes under DREAMS_OUT/recomp, by default the repository's
out/recomp. Run the scripts from the project environment (uv run ...) so that
dreams.paths resolves the local settings.
"""

from __future__ import annotations

import functools
import os
import shutil
import subprocess
import sys
from pathlib import Path

from dreams import paths

RECOMP = Path(__file__).resolve().parent
WINDREAM = RECOMP / "windream"


def out_dir(*parts: str) -> Path:
    """DREAMS_OUT/recomp/<parts>, created."""
    return paths.out_dir("recomp", *parts)


def pcrecomp() -> Path:
    """The pcrecomp clone (lift32, pe_analyze, DumpBounds.java)."""
    return paths.get("pcrecomp")


@functools.cache
def build_env() -> dict[str, str]:
    """Environment for cmake, Ninja and clang-cl.

    On Windows outside a developer prompt, the x64 environment of the newest
    Visual Studio (vcvarsall.bat, which also puts its CMake and Ninja on PATH).
    Elsewhere, or with cl.exe already on PATH, the current environment.
    """
    env = dict(os.environ)
    if sys.platform != "win32" or shutil.which("cl.exe"):
        return env
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    vswhere = vswhere / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    vs = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    vcvars = Path(vs) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat"
    out = subprocess.run(
        f'"{vcvars}" x64 >nul && set', shell=True, capture_output=True, text=True, check=True
    ).stdout
    for line in out.splitlines():
        name, eq, value = line.partition("=")
        if eq:
            env[name] = value
    return env


def configure_and_build(build: Path, gen: Path, *, trace: bool = False, quiet: bool = False) -> int:
    """Configure (from scratch when the build dir belongs to another source
    tree) and build windream_recomp with clang-cl and Ninja. Returns the exit
    code of the build."""
    env = build_env()
    # Windows resolves the program on the parent's PATH, not the child's.
    cmake = shutil.which("cmake", path=env.get("PATH")) or "cmake"
    cache = build / "CMakeCache.txt"
    home = f"CMAKE_HOME_DIRECTORY:INTERNAL={WINDREAM.as_posix()}"
    same = cache.is_file() and home in cache.read_text(errors="replace")
    fresh = [] if same and (build / "build.ninja").is_file() else ["--fresh"]
    subprocess.run(
        [cmake, *fresh, "-S", str(WINDREAM), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=", "-DCMAKE_C_COMPILER=clang-cl", f"-DWD_GEN_DIR={gen}",
         f"-DWD_TRACE={'ON' if trace else 'OFF'}"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    p = subprocess.run([cmake, "--build", str(build)], env=env, capture_output=quiet, text=True)
    if quiet:
        for line in (p.stdout + p.stderr).splitlines():
            if " error" in line or "FAILED" in line:
                print(line)
    return p.returncode


def exe_name(stem: str) -> str:
    return stem + (".exe" if sys.platform == "win32" else "")
