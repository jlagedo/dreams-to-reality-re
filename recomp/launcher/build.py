"""Configure and build the launcher library and its launcher_demo executable with
Ninja (clang-cl on Windows, the default compiler elsewhere).

Sources: this directory. Build: DREAMS_OUT/recomp/launcher/build (build-<platform>
off Windows). SDL3 is the shared static build (recomp_env.ensure_sdl3); Dear ImGui is
fetched at the commit pinned in CMakeLists.txt. It does not touch the windream build.

usage: uv run python recomp/launcher/build.py
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomp_env  # noqa: E402

LAUNCHER = Path(__file__).resolve().parent


def build_dir() -> Path:
    return recomp_env.out_dir("launcher") / ("build" + recomp_env.platform_suffix())


def demo_path() -> Path:
    return build_dir() / recomp_env.exe_name("launcher_demo")


def main() -> int:
    build = build_dir()
    sdl3 = recomp_env.ensure_sdl3()
    env = recomp_env.build_env()
    cmake = recomp_env._cmake(env)
    cache = build / "CMakeCache.txt"
    same = (
        cache.is_file()
        and f"CMAKE_HOME_DIRECTORY:INTERNAL={LAUNCHER.as_posix()}"
        in cache.read_text(errors="replace")
    )
    fresh = [] if same and (build / "build.ninja").is_file() else ["--fresh"]
    subprocess.run(
        [cmake, *fresh, "-S", str(LAUNCHER), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={sdl3}",
         *recomp_env._compilers(cxx=True)],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    rc = subprocess.run([cmake, "--build", str(build)], env=env).returncode
    if rc == 0:
        exe = demo_path()
        print(f"{exe}  {exe.stat().st_size:,} bytes")
    return rc


if __name__ == "__main__":
    sys.exit(main())
