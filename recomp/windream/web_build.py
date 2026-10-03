"""Browser build of windream_recomp with Emscripten (build.py --web).

The tools come from the machine-local configuration recomp/web-env.ps1 reads
(out/recomp/web-tools/tools.json, or DREAMS_WEB_TOOLS): an Emscripten SDK, the
repository's CMake and Ninja, the pinned sokol-shdc and the pinned SDL3 built
for WebAssembly with pthreads. This module activates the SDK the way
emsdk_env does (without touching the caller's shell) and runs CMake with the
Emscripten toolchain file. Output: DREAMS_OUT/recomp/windream/build-web/
dreams.{js,wasm} (the contract is recomp/web/CONTRACT.md).
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import recomp_env


def tools_config() -> Path:
    env = os.environ.get("DREAMS_WEB_TOOLS")
    return Path(env) if env else recomp_env.out_dir("web-tools") / "tools.json"


def load_tools() -> dict:
    cfg = tools_config()
    if not cfg.is_file():
        sys.exit(f"browser tools configuration is missing: {cfg} (see recomp/web-env.ps1)")
    tools = json.loads(cfg.read_text())
    for key in ("emsdk", "cmake", "ninja", "shdc", "sdl3"):
        if not Path(tools[key]).exists():
            sys.exit(f"browser tool or dependency is missing: {tools[key]}")
    return tools


def web_env(tools: dict) -> dict[str, str]:
    """The environment emsdk_env sets up, plus CMake and Ninja on PATH."""
    emsdk = Path(tools["emsdk"])
    env = dict(os.environ)
    if sys.platform == "win32":
        # emsdk's construct_env prints a bash script when it sees a Git Bash
        # environment, so build the same variables directly.
        env = {("PATH" if k.upper() == "PATH" else k): v for k, v in env.items()}
    node = sorted(emsdk.glob("node/*/bin/node*")) + sorted(emsdk.glob("node/*/node.exe"))
    python = sorted(emsdk.glob("python/*/python.exe")) + sorted(emsdk.glob("python/*/bin/python3"))
    if node:
        env["EMSDK_NODE"] = str(node[0])
    if python:
        env["EMSDK_PYTHON"] = str(python[0])
    env["PATH"] = os.pathsep.join(
        [str(emsdk), str(emsdk / "upstream" / "emscripten"), env.get("PATH", "")]
    )
    sep = os.pathsep
    extra = [str(Path(tools[k]).parent) for k in ("cmake", "ninja") if k in tools]
    env["PATH"] = sep.join([*extra, env.get("PATH", "")])
    env["EMSDK"] = str(emsdk)
    env["DREAMS_EMSDK"] = str(emsdk)
    env["SDL3_DIR"] = str(Path(tools["sdl3"]) / "lib" / "cmake" / "SDL3")
    return env


def build_dir(out: Path, name: str = "build-web") -> Path:
    return out / name


def configure_and_build(
    gen: Path,
    *,
    optimize: str = "",
    quiet: bool = False,
    exe: str = "GDIDREAM.EXE",
    name: str = "build-web",
    capture: bool = True,
) -> tuple[int, Path]:
    """Configure and build; returns (exit code, build directory). optimize is
    a CMake build type ("" is the development build: -O1, see WD_WEB_OPT in CMakeLists.txt).
    capture is WD_WEB_CAPTURE, the verification tools' frame capture (off in a release)."""
    tools = load_tools()
    env = web_env(tools)
    cmake = shutil.which("cmake", path=env["PATH"]) or "cmake"
    out = recomp_env.out_dir("windream")
    build = build_dir(out, name)
    toolchain = (
        Path(tools["emsdk"])
        / "upstream"
        / "emscripten"
        / "cmake"
        / "Modules"
        / "Platform"
        / "Emscripten.cmake"
    )
    cache = build / "CMakeCache.txt"
    home = f"CMAKE_HOME_DIRECTORY:INTERNAL={recomp_env.WINDREAM.as_posix()}"
    same = cache.is_file() and home in cache.read_text(errors="replace")
    fresh = [] if same and (build / "build.ninja").is_file() else ["--fresh"]
    configure = subprocess.run(
        [cmake, *fresh, "-S", str(recomp_env.WINDREAM), "-B", str(build), "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}",
         f"-DCMAKE_BUILD_TYPE={optimize}",
         f"-DCMAKE_PREFIX_PATH={Path(tools['sdl3']).as_posix()}",
         f"-DSDL3_DIR={env['SDL3_DIR']}".replace("\\", "/"),
         f"-DOD_SHDC_EXECUTABLE={Path(tools['shdc']).as_posix()}",
         f"-DCMAKE_MAKE_PROGRAM={Path(tools['ninja']).as_posix()}",
         f"-DWD_GEN_DIR={gen.as_posix()}", "-DWD_LAUNCHER=OFF", "-DWD_DEVTOOLS=OFF",
         f"-DWD_WEB_EXE={exe}", f"-DWD_WEB_CAPTURE={'ON' if capture else 'OFF'}"],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
    )  # fmt: skip
    if configure.returncode:
        print(configure.stderr, file=sys.stderr)
        return configure.returncode, build
    p = subprocess.run([cmake, "--build", str(build)], env=env, capture_output=quiet, text=True)
    if quiet:
        for line in (p.stdout + p.stderr).splitlines():
            if " error" in line or "FAILED" in line:
                print(line)
    return p.returncode, build
