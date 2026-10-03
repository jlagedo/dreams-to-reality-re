"""Build/test the shared renderer without ODShared, ImGui or game loaders.

  uv run python recomp/windream/verify/direct_render_validate.py --gpu
Add --fixtures out/recomp/render-2d-smoke to replay the original CPU checkpoints.

Each fixture directory must contain the six streams from render_2d_smoke.py.
All generated shaders, binaries and reports live under DREAMS_OUT/recomp.
"""

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

FIXTURES = ("format-0", "format-1", "pitch-124", "movie", "movie-captured", "captured-hud")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gpu", action="store_true", help="hardware D3D11 checks (Windows)")
    ap.add_argument("--fixtures", type=Path, action="append", default=[])
    args = ap.parse_args()
    if args.fixtures and not args.gpu:
        ap.error("--fixtures requires --gpu")
    if args.gpu and sys.platform != "win32":
        ap.error("GPU readback oracle currently requires D3D11; other backend execution is pending")
    files = [directory / (name + ".bin") for directory in args.fixtures for name in FIXTURES]
    for file in files:
        if not file.is_file():
            ap.error(f"missing fixture: {file}; run render_2d_smoke.py first")
    out = recomp_env.out_dir("direct-render")
    build = out / "build"
    env = recomp_env.build_env()
    cmake = recomp_env._cmake(env)
    source = Path(__file__).parent / "native" / "direct_renderer"
    report = {"checks": [], "inputs": {}}
    for file in files:
        report["inputs"][str(file.resolve())] = hashlib.sha256(file.read_bytes()).hexdigest()
    with (out / "build.log").open("w", encoding="utf-8") as log:
        subprocess.run(
            [cmake, "-S", str(source), "-B", str(build), "-G", "Ninja",
             *recomp_env._compilers(cxx=True),
             f"-DCMAKE_PREFIX_PATH={recomp_env.ensure_sdl3()}"],
            env=env, check=True, stdout=log, stderr=subprocess.STDOUT,
        )  # fmt: skip
        subprocess.run(
            [cmake, "--build", str(build)],
            env=env,
            check=True,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
    commands = [
        [str(build / recomp_env.exe_name(name))]
        for name in ("ODDirectMathTests", "WDRenderBoundaryTests", "WDRenderInterpTests")
    ]
    if args.gpu:
        commands.append([str(build / "WDRenderMetricsTests.exe")])
        commands.append([str(build / "WDSceneModeTests.exe"), str(out / "fog-roundtrip.wds")])
        commands.append([str(build / "ODDirectExportTests.exe")])
        commands.append([str(build / "ODDirectGpuTests.exe"), *(str(p.resolve()) for p in files)])
    for command in commands:
        result = subprocess.run(command, env=env, capture_output=True, text=True, errors="replace")
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        report["checks"].append(
            {
                "command": command,
                "returncode": result.returncode,
                "stdout": result.stdout,
                "stderr": result.stderr,
            }
        )
        (out / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        if result.returncode:
            return result.returncode
    print(f"PASS: {out / 'results.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
