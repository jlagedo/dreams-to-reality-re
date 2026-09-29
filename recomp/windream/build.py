"""Configure and build windream_recomp with clang-cl and Ninja.

Sources: this directory. Generated C: DREAMS_OUT/recomp/windream/gen (lift.py,
gen_imports.py). Build: DREAMS_OUT/recomp/windream/build (build-trace with
--trace); one unoptimized development build, see CMakeLists.txt.

usage: uv run python recomp/windream/build.py [--trace]
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
    args = ap.parse_args()
    out = recomp_env.out_dir("windream")
    build = out / (
        "build" + ("-trace" if args.trace else "") + ("-audit" if args.render_audit else "")
    )
    rc = recomp_env.configure_and_build(
        build, out / "gen", trace=args.trace, render_audit=args.render_audit
    )
    if rc == 0:
        exe = build / recomp_env.exe_name("windream_recomp")
        print(f"{exe}  {exe.stat().st_size:,} bytes")
    return rc


if __name__ == "__main__":
    sys.exit(main())
