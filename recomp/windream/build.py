"""Configure and build windream_recomp with Ninja (clang-cl on Windows, the
default compiler elsewhere).

Sources: this directory. Generated C: DREAMS_OUT/recomp/windream/gen (lift.py,
gen_imports.py). Build: DREAMS_OUT/recomp/windream/build (build-trace with
--trace, build-audit with --render-audit, build-vm-<impl> with --vm other
than the default ledger; off Windows each name ends in -linux or -darwin, and
only the ledger exists); one unoptimized development build, see CMakeLists.txt.

--web builds the browser version with Emscripten instead (web_build.py):
out/recomp/windream/build-web/dreams.{js,wasm}; --web-opt picks a CMake build
type for it (Release, MinSizeRel, ...). --web-release is the build to deploy:
Release, without the verification tools' frame capture (WD_WEB_CAPTURE), in
out/recomp/windream/build-web-release.

usage: uv run python recomp/windream/build.py [--trace] [--render-audit] [--vm win32|ledger|shadow]
       uv run python recomp/windream/build.py --web [--web-opt Release]
       uv run python recomp/windream/build.py --web-release
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomp_env  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--trace", action="store_true", help="function-entry trace ring")
    ap.add_argument("--render-audit", action="store_true", help="registered-surface memory probes")
    ap.add_argument(
        "--vm",
        choices=recomp_env.VM_CHOICES,
        default=recomp_env.VM_DEFAULT,
        help="virtual memory implementation (host/vm/vm_impl.h)",
    )
    ap.add_argument(
        "--web", action="store_true", help="browser build with Emscripten (build-web/dreams.js)"
    )
    ap.add_argument(
        "--web-opt",
        default="",
        help="CMake build type of the --web build (default: development, -O1; Release is -O3)",
    )
    ap.add_argument(
        "--web-gen",
        help="lifted sources of another program for --web (default: out/recomp/windream/gen)",
    )
    ap.add_argument(
        "--web-exe", default="GDIDREAM.EXE", help="the guest EXE's name in /dreams (--web)"
    )
    ap.add_argument(
        "--web-name",
        help="build directory name under out/recomp/windream "
        "(--web: build-web, --web-release: build-web-release)",
    )
    ap.add_argument(
        "--web-release",
        action="store_true",
        help="the browser build to deploy: Release, no frame capture (build-web-release)",
    )
    args = ap.parse_args()
    out = recomp_env.out_dir("windream")
    if args.web or args.web_release:
        import web_build

        gen = Path(args.web_gen).resolve() if args.web_gen else out / "gen"
        rc, build = web_build.configure_and_build(
            gen,
            optimize="Release" if args.web_release else args.web_opt,
            exe=args.web_exe,
            name=args.web_name or ("build-web-release" if args.web_release else "build-web"),
            capture=not args.web_release,
        )
        if rc == 0:
            for name in ("dreams.js", "dreams.wasm"):
                print(f"{build / name}  {(build / name).stat().st_size:,} bytes")
        return rc
    build = recomp_env.build_dir(out, trace=args.trace, render_audit=args.render_audit, vm=args.vm)
    rc = recomp_env.configure_and_build(
        build, out / "gen", trace=args.trace, render_audit=args.render_audit, vm=args.vm
    )
    if rc == 0:
        exe = build / recomp_env.exe_name("windream_recomp")
        print(f"{exe}  {exe.stat().st_size:,} bytes")
    return rc


if __name__ == "__main__":
    sys.exit(main())
