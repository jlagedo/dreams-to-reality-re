"""Build the standalone renderer checks natively (D3D11 on Windows) for comparison.

    uv run python recomp/render/tests/web/native_build.py

Output in out/recomp/render-web/build-native: od_web_frame, od_gpu_tests.
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "recomp"))
import recomp_env  # noqa: E402

env = recomp_env.build_env()
cmake = recomp_env._cmake(env)
source = Path(__file__).resolve().parent
build = REPO / "out" / "recomp" / "render-web" / "build-native"
subprocess.run(
    [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja", *recomp_env._compilers(cxx=True),
     f"-DCMAKE_PREFIX_PATH={recomp_env.ensure_sdl3()}", "-DCMAKE_BUILD_TYPE=Release"],
    env=env, check=True,
)  # fmt: skip
subprocess.run([cmake, "--build", str(build)], env=env, check=True)
