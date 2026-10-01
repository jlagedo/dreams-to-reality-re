"""Configure and build windream_recomp with clang-cl and Ninja.

Sources: this directory. Generated C: DREAMS_OUT/recomp/windream/gen (lift.py,
gen_imports.py). Build: DREAMS_OUT/recomp/windream/build (build-trace with
--trace, build-audit with --render-audit, build-vm-<impl> with --vm other
than the default ledger); one unoptimized development build, see CMakeLists.txt.

usage: uv run python recomp/windream/build.py [--trace] [--render-audit] [--vm win32|ledger|shadow]
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
    args = ap.parse_args()
    out = recomp_env.out_dir("windream")
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
