"""Configure and build the disc library and its disc_list tool with Ninja (clang-cl
on Windows, the default compiler elsewhere).

Sources: this directory. Build: DREAMS_OUT/recomp/disc/build (build-<platform>
off Windows, so Windows and WSL can share one DREAMS_OUT). It does not touch the
windream build.

usage: uv run python recomp/disc/build.py
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomp_env  # noqa: E402

DISC = Path(__file__).resolve().parent


def build_dir() -> Path:
    return recomp_env.out_dir("disc") / ("build" + recomp_env.platform_suffix())


def tool_path() -> Path:
    return recomp_env.disc_tool()


def main() -> int:
    build = build_dir()
    env = recomp_env.build_env()
    cmake = recomp_env._cmake(env)
    cache = build / "CMakeCache.txt"
    same = (
        cache.is_file()
        and f"CMAKE_HOME_DIRECTORY:INTERNAL={DISC.as_posix()}" in cache.read_text(errors="replace")
    )
    fresh = [] if same and (build / "build.ninja").is_file() else ["--fresh"]
    subprocess.run(
        [cmake, *fresh, "-S", str(DISC), "-B", str(build), "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", "-DDREAMS_DISC_TOOLS=ON", *recomp_env._compilers()],
        env=env, check=True, stdout=subprocess.DEVNULL,
    )  # fmt: skip
    rc = subprocess.run([cmake, "--build", str(build)], env=env).returncode
    if rc == 0:
        exe = tool_path()
        print(f"{exe}  {exe.stat().st_size:,} bytes")
    return rc


if __name__ == "__main__":
    sys.exit(main())
