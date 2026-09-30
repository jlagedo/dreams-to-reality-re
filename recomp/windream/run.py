"""Run the recompiled game from DREAMS_OUT/recomp/windream/run (sandbox, logs,
snapshots and crash dumps land there).

  run.py                                   play (SDL window, sound, keyboard, pads)
  run.py --fullscreen --pad keys           play fullscreen, pad buttons as keys
  run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
                                           unattended test: scripted keys, a
                                           snapshot every 4 s, stopped after 60 s
  run.py --overlays                        play with the retail debug flags on
                                           (keypad 1-4 toggle them)

Afterwards it prints the crash block from stderr.txt (host stack, registers,
guest stack, minidump path), or otherwise the lines worth a look.

--exe defaults to DREAMS_DISC1/GDIDREAM.EXE. --read-roots is where the guest's
C:\\CRYO\\DREAMS\\... resolves: by default the directory holding CRYO/DREAMS in
DREAMS_INSTALL_ROOT.

usage: uv run python recomp/windream/run.py [options]
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402


def read_roots() -> str:
    install = paths.configured("install_root")
    if install is None:
        sys.exit("set DREAMS_INSTALL_ROOT (the retail CRYO/DREAMS tree) or pass --read-roots")
    parts = [p.upper() for p in install.parts[-2:]]
    return str(install.parent.parent if parts == ["CRYO", "DREAMS"] else install)


def main() -> int:
    ap = argparse.ArgumentParser(description="Run the recompiled game.")
    ap.add_argument("--exe", help="guest exe (default DREAMS_DISC1/GDIDREAM.EXE)")
    ap.add_argument("--renderer", choices=("software", "direct"), default="software")
    ap.add_argument("--headless", action="store_true", help="keep the window hidden and run muted")
    ap.add_argument(
        "--mute", action="store_true", help="mix silently without opening an audio device"
    )
    ap.add_argument("--tag", help="isolated run directory suffix under DREAMS_OUT/recomp/windream")
    ap.add_argument("--render-audit", action="store_true", help="run the build-audit executable")
    ap.add_argument(
        "--capture-scene",
        action="store_true",
        help="capture original scene inputs on the first main 3D frame",
    )
    ap.add_argument("--read-roots", help="extra read roots, ';'-separated")
    ap.add_argument("--seconds", type=int, default=0, help="stop after N s (0 = play)")
    ap.add_argument("--keys", default="", help="scripted keys, ms:KEY,...")
    ap.add_argument("--snap-ms", type=int, default=0, help="snapshot interval")
    ap.add_argument("--fps", type=int, default=25, help="frame cap (0 = uncapped)")
    ap.add_argument("--scale", type=int, default=2, help="window size in multiples of 640x480")
    ap.add_argument("--fullscreen", action="store_true", help="start fullscreen (F11 toggles)")
    ap.add_argument(
        "--filter", choices=("pixelart", "nearest", "linear"), default="pixelart",
        help="scaling filter",
    )  # fmt: skip
    ap.add_argument(
        "--pad", choices=("winmm", "keys", "off"), default="winmm",
        help="gamepads: WinMM joysticks (J in game), keyboard keys, or none",
    )  # fmt: skip
    ap.add_argument(
        "--deadzone", default="10,95", metavar="INNER,OUTER",
        help="stick and trigger deadzone, percent of full deflection",
    )  # fmt: skip
    ap.add_argument(
        "--dump", choices=("full", "mini", "0"), default="full",
        help="crash minidump: full = all memory incl. the guest arena",
    )  # fmt: skip
    ap.add_argument(
        "--overlays", action="store_true",
        help="start with the retail debug flags on (in game: keypad 1 Frame Rate/Mem 3DTR, "
        "2 object HUD, 3 collision view: hold Backspace; off here: 4 step 2.0, 5 editor flag)",
    )  # fmt: skip
    ap.add_argument(
        "--poke", action="append", default=[], metavar="VA=VALUE",
        help="write a dword into the loaded image before the entry point (repeatable)",
    )  # fmt: skip
    args = ap.parse_args()
    if args.headless and args.fullscreen:
        ap.error("--headless and --fullscreen cannot be combined")
    if args.tag and not re.fullmatch(r"[A-Za-z0-9_-]+", args.tag):
        ap.error("--tag must contain only letters, numbers, underscores or hyphens")

    # GAME_TickFrame draws Frame Rate/Mem 3DTR if 0x49d5c0 != 0 and the editor
    # flag 0x4a477c is 0; DBG_DrawObjectInfo (0x416606) needs 0x49d5d0 != 0;
    # the byte 0x4ac8c8 makes REND_DrawFrame skip rasterizing, which leaves the
    # Backspace collision wireframe visible (its next three bytes are also 0).
    overlays = ["0x49d5c0=1", "0x49d5d0=1", "0x4ac8c8=1"]
    pokes = (overlays if args.overlays else []) + args.poke
    exe = args.exe or str(paths.disc(1) / "GDIDREAM.EXE")
    out = recomp_env.out_dir("windream")
    run = out / ("run-" + args.tag if args.tag else "run")
    run.mkdir(exist_ok=True)
    if args.capture_scene:
        (run / "direct-scene.wds").unlink(missing_ok=True)
    for snap in run.glob("snap_*.bmp"):
        snap.unlink()
    unattended = args.seconds > 0
    env = dict(
        os.environ,
        WD_READ_ROOTS=args.read_roots or read_roots(),
        WD_KEYS=args.keys,
        WD_SNAP_MS=str(args.snap_ms) if args.snap_ms else "",
        WD_FPS=str(args.fps),  # "0" = uncapped (an empty value would unset it)
        WD_SCALE=str(args.scale),
        WD_FULLSCREEN="1" if args.fullscreen else "",
        WD_FILTER=args.filter,
        WD_PAD=args.pad,
        WD_DEADZONE=args.deadzone,
        WD_DUMP=args.dump,
        WD_POKE=",".join(pokes),
        WD_RENDERER=args.renderer,
        WD_MUTE="1" if args.mute or args.headless else os.environ.get("WD_MUTE", ""),
        WD_HEADLESS="1" if args.headless else os.environ.get("WD_HEADLESS", ""),
        WD_SCENE_CAPTURE=str(run / "direct-scene.wds") if args.capture_scene else "",
        WD_QUIET="1" if unattended or args.headless else "",
        WD_FOCUS="1" if unattended or args.headless else "",
    )
    binary = (
        out
        / ("build-audit" if args.render_audit else "build")
        / recomp_env.exe_name("windream_recomp")
    )
    with open(run / "stderr.txt", "wb") as err, open(run / "stdout.txt", "wb") as so:
        p = subprocess.Popen([str(binary), exe, "--run"], cwd=run, env=env, stdout=so, stderr=err)
        stopped = False
        try:
            p.wait(timeout=args.seconds or None)
            print(f"exited with code {p.returncode}")
        except subprocess.TimeoutExpired:
            stopped = True
            p.kill()
            p.wait()
            print(f"still running after {args.seconds} s (stopped)")

    log = (run / "stderr.txt").read_text(encoding="utf-8", errors="replace").splitlines()
    at = next((i for i, line in enumerate(log) if "=== recomp: CRASH" in line), None)
    if at is not None:
        print("\n".join(log[at : at + 71]))
    else:
        worth = [line for line in log if re.search(r"MessageBox|unresolved|ExitProcess", line)]
        print("\n".join(worth[:10]))
    if args.capture_scene:
        captured = [line for line in log if "[render-capture] captured" in line]
        print("\n".join(captured))
        if not captured or at is not None:
            print("scene capture failed or the run crashed", file=sys.stderr)
            return 1
    return 1 if at is not None or (not stopped and p.returncode != 0) else 0


if __name__ == "__main__":
    sys.exit(main())
