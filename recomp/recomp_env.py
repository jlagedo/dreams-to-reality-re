"""Shared paths and tool environment for the recomp scripts.

The hand-written sources live in recomp/; everything they produce (generated C,
builds, runs, dumps) goes under DREAMS_OUT/recomp, by default the repository's
out/recomp. Run the scripts from the project environment (uv run ...) so that
dreams.paths resolves the local settings.
"""

from __future__ import annotations

import functools
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
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
    env = (
        {key.upper(): value for key, value in os.environ.items()}
        if sys.platform == "win32"
        else dict(os.environ)
    )
    if sys.platform != "win32" or all(shutil.which(tool) for tool in ("cl.exe", "cmake", "ninja")):
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
            env[name.upper()] = value
    return env


def _cmake(env: dict[str, str]) -> str:
    # Windows resolves the program on the parent's PATH, not the child's.
    return shutil.which("cmake", path=env.get("PATH")) or "cmake"


def _compilers(*, cxx: bool = False) -> list[str]:
    if sys.platform != "win32":
        return []
    return ["-DCMAKE_C_COMPILER=clang-cl"] + (["-DCMAKE_CXX_COMPILER=clang-cl"] if cxx else [])


def ensure_sdl3() -> Path:
    """Install prefix of a static SDL3 release build, built on first use.

    Same pinned source as OpenDreams (the SDL3 URL in
    opendreams/cmake/Dependencies.cmake), built once under
    DREAMS_OUT/recomp/sdl3/<commit> and shared by every recomp and difftest
    build.
    """
    deps = RECOMP.parent / "opendreams" / "cmake" / "Dependencies.cmake"
    m = re.search(r"FetchContent_Declare\(SDL3\s+URL\s+(\S+)", deps.read_text())
    if not m:
        sys.exit(f"no SDL3 URL in {deps}")
    url = m.group(1)
    root = out_dir("sdl3", url.rstrip("/").rsplit("/", 1)[-1][:12])
    install = root / "install"
    stamp = install / "built-from.txt"
    if stamp.is_file() and stamp.read_text().strip() == url:
        return install
    print(f"building SDL3 from {url} into {root} (once)")
    archive = root / "sdl3.tar.gz"
    if not archive.is_file():
        urllib.request.urlretrieve(url, archive)
    src = root / "src"
    if src.exists():
        shutil.rmtree(src)
    with tarfile.open(archive) as tar:
        tar.extractall(src, filter="data")
    (top,) = [p for p in src.iterdir() if p.is_dir()]
    env = build_env()
    cmake = _cmake(env)
    subprocess.run(
        [cmake, "--fresh", "-S", str(top), "-B", str(root / "build"), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", *_compilers(cxx=True), f"-DCMAKE_INSTALL_PREFIX={install}",
         "-DSDL_STATIC=ON", "-DSDL_SHARED=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    subprocess.run([cmake, "--build", str(root / "build")], env=env, check=True)
    install_cmd = [cmake, "--install", str(root / "build")]
    subprocess.run(install_cmd, env=env, check=True, stdout=subprocess.DEVNULL)
    stamp.write_text(url + "\n")
    return install


def configure_and_build(
    build: Path, gen: Path, *, trace: bool = False, quiet: bool = False, render_audit: bool = False
) -> int:
    """Configure (from scratch when the build dir belongs to another source
    tree) and build windream_recomp with clang-cl and Ninja. Returns the exit
    code of the build."""
    sdl3 = ensure_sdl3()
    env = build_env()
    cmake = _cmake(env)
    cache = build / "CMakeCache.txt"
    home = f"CMAKE_HOME_DIRECTORY:INTERNAL={WINDREAM.as_posix()}"
    same = cache.is_file() and home in cache.read_text(errors="replace")
    fresh = [] if same and (build / "build.ninja").is_file() else ["--fresh"]
    subprocess.run(
        [cmake, *fresh, "-S", str(WINDREAM), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=", *_compilers(cxx=True), f"-DCMAKE_PREFIX_PATH={sdl3}",
         f"-DWD_GEN_DIR={gen}",
         f"-DWD_TRACE={'ON' if trace else 'OFF'}",
         f"-DWD_RENDER_AUDIT={'ON' if render_audit else 'OFF'}"],
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
