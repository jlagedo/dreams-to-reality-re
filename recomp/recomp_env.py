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
LIFT = WINDREAM / "lift"
# Host runtime, by the API each part stands on: core (guest runtime, trace,
# crash report), sdl (KERNEL32 files, process and threads, USER32, GDI32, WinMM
# and DirectSound on SDL3), vm (the guest's virtual memory: the win32, ledger
# and shadow implementations and the vm_os_* layer under the ledger), render
# (GPU renderer adapters) and hooks (diagnostics).
HOST = WINDREAM / "host"
HOST_DIRS = [HOST / name for name in ("core", "sdl", "vm", "render", "hooks")]
VM_CHOICES = ("win32", "ledger", "shadow")
VM_DEFAULT = "ledger"  # the implementation plain build.py / run.py use (CMakeLists.txt WD_VM)


def platform_suffix() -> str:
    """Empty on Windows, otherwise -<sys.platform> (-linux, -darwin): what
    keeps the builds of two hosts apart when they share one DREAMS_OUT (WSL on
    the Windows checkout). The Windows names are the original ones."""
    return "" if sys.platform == "win32" else f"-{sys.platform}"


def vm_sources(host: Path, vm: str) -> list[tuple[Path, list[str]]]:
    """The host/vm sources of one implementation, each with its compile
    definitions (the shadow build compiles the two it compares under their own
    prefixes). A tree from before host/vm has its single win32/vm.c.

    The ledger stands on vm_os_win32.c on Windows and vm_os_posix.c elsewhere;
    win32 and shadow need vm_win32.c and exist on Windows only."""
    legacy = host / "win32" / "vm.c"
    if legacy.is_file():
        return [(legacy, [])]
    d = host / "vm"
    front = [(d / "vm_front.c", [])]
    windows = sys.platform == "win32"
    if vm in ("win32", "shadow") and not windows:
        raise ValueError(f"vm implementation {vm!r} is Windows only; use 'ledger'")
    if vm == "win32":
        return front + [(d / "vm_win32.c", [])]
    if vm == "ledger":
        return front + [
            (d / "vm_ledger.c", []),
            (d / ("vm_os_win32.c" if windows else "vm_os_posix.c"), []),
        ]
    if vm == "shadow":
        return front + [
            (d / "vm_shadow.c", []),
            (d / "vm_win32.c", ["VM_PREFIX=vm_win32_"]),
            (d / "vm_ledger.c", ["VM_PREFIX=vm_ledger_", "VM_SECONDARY"]),
            (d / "vm_os_null.c", []),
        ]
    raise ValueError(f"unknown vm implementation {vm!r}")


def host_includes() -> list[str]:
    """One /I option per host source directory, in the clang-cl and cl
    spelling: only the Windows-only verify scripts use it (CMake has its own
    list)."""
    return [f"/I{d}" for d in HOST_DIRS]


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

    The pinned source (the SDL3 URL in
    recomp/render/cmake/Dependencies.cmake), built once under
    DREAMS_OUT/recomp/sdl3/<commit> and shared by every recomp and difftest
    build. The archive and the extracted src are shared by every host; build
    and install are <commit>/build and <commit>/install on Windows and
    <commit>/<sys.platform>/build and .../install elsewhere, so that Windows
    and WSL can use one DREAMS_OUT.
    """
    deps = RECOMP / "render" / "cmake" / "Dependencies.cmake"
    m = re.search(r"FetchContent_Declare\(SDL3\s+URL\s+(\S+)", deps.read_text())
    if not m:
        sys.exit(f"no SDL3 URL in {deps}")
    url = m.group(1)
    root = out_dir("sdl3", url.rstrip("/").rsplit("/", 1)[-1][:12])
    host = root if sys.platform == "win32" else root / sys.platform
    build, install = host / "build", host / "install"
    stamp = install / "built-from.txt"
    if stamp.is_file() and stamp.read_text().strip() == url:
        return install
    print(f"building SDL3 from {url} into {root} (once)")
    archive = root / "sdl3.tar.gz"
    if not archive.is_file():
        urllib.request.urlretrieve(url, archive)
    # src may be in use by another host's build of the same commit: extract
    # beside it and rename, and never remove a complete one.
    src = root / "src"
    if not (src.is_dir() and any(p.is_dir() for p in src.iterdir())):
        partial = root / f"src.partial{platform_suffix()}"
        for stale in (partial, src):
            if stale.exists():
                shutil.rmtree(stale)
        with tarfile.open(archive) as tar:
            tar.extractall(partial, filter="data")
        try:
            partial.rename(src)
        except OSError:
            if not src.is_dir():
                raise
            shutil.rmtree(partial)  # another host extracted it first
    (top,) = [p for p in src.iterdir() if p.is_dir()]
    env = build_env()
    cmake = _cmake(env)
    subprocess.run(
        [cmake, "--fresh", "-S", str(top), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", *_compilers(cxx=True), f"-DCMAKE_INSTALL_PREFIX={install}",
         "-DSDL_STATIC=ON", "-DSDL_SHARED=OFF", "-DSDL_TESTS=OFF", "-DSDL_EXAMPLES=OFF"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    subprocess.run([cmake, "--build", str(build)], env=env, check=True)
    install_cmd = [cmake, "--install", str(build)]
    subprocess.run(install_cmd, env=env, check=True, stdout=subprocess.DEVNULL)
    stamp.write_text(url + "\n")
    return install


def configure_and_build(
    build: Path,
    gen: Path,
    *,
    trace: bool = False,
    quiet: bool = False,
    render_audit: bool = False,
    vm: str = VM_DEFAULT,
) -> int:
    """Configure (from scratch when the build dir belongs to another source
    tree) and build windream_recomp with Ninja: clang-cl on Windows, the
    default compiler (cc, c++) elsewhere. Returns the exit code of the build."""
    if vm != "ledger" and sys.platform != "win32":
        sys.exit(f"--vm {vm} is Windows only (host/vm/vm_win32.c); use ledger")
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
         f"-DWD_RENDER_AUDIT={'ON' if render_audit else 'OFF'}",
         f"-DWD_VM={vm}"],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    p = subprocess.run([cmake, "--build", str(build)], env=env, capture_output=quiet, text=True)
    if quiet:
        for line in (p.stdout + p.stderr).splitlines():
            if " error" in line or "FAILED" in line:
                print(line)
    return p.returncode


def build_dir(
    out: Path, *, trace: bool = False, render_audit: bool = False, vm: str = VM_DEFAULT
) -> Path:
    """The windream build directory of one configuration: build, build-trace,
    build-audit, and -vm-<impl> appended for an implementation other than the
    default (build.py writes it, run.py picks the executable from it). Off
    Windows the host comes last (build-linux, build-trace-darwin)."""
    name = "build" + ("-trace" if trace else "") + ("-audit" if render_audit else "")
    if vm != VM_DEFAULT:
        name += f"-vm-{vm}"
    return out / (name + platform_suffix())


def exe_name(stem: str) -> str:
    return stem + (".exe" if sys.platform == "win32" else "")
