"""Run the recompiled game from DREAMS_OUT/recomp/windream/run (sandbox, logs,
snapshots and crash dumps land there).

  run.py                                   play (SDL window, sound, keyboard, pads)
  run.py --fullscreen --pad keys           play fullscreen, pad buttons as keys
  run.py --seconds 60 --keys 2000:ESC,5000:RETURN --snap-ms 4000
                                           unattended test: scripted keys, a
                                           snapshot every 4 s, stopped after 60 s
  run.py --discs                           play from the two disc images (.cue)
                                           instead of the extracted trees
  run.py --overlays                        play with the retail debug flags on
                                           (keypad 1-4 toggle them)
  run.py --vm ledger --vm-log              run the build-vm-ledger executable and
                                           write every virtual-memory call to
                                           <run dir>/vm.log (compare two with
                                           verify/vm_compare.py)
  run.py --vm shadow --vm-shadow abort     run the shadow build (both virtual
                                           memory implementations side by side),
                                           stopping at the first disagreement
  run.py --headless --discs --ctl          run with the development control
                                           channel on a free port, written to
                                           <run dir>/ctl.port; drive it with
                                           debug/wdctl.py (development builds only)

Afterwards it prints the crash block from stderr.txt (host stack, registers,
guest stack, minidump path), or otherwise the lines worth a look. With --vm-log
it also reports the size of vm.log; with --vm shadow it prints the [vm-shadow]
lines.

--vm picks the build directory (see build.py --vm); --vm-log sets WD_VM_LOG and
--vm-shadow sets WD_VM_SHADOW (log or abort).

--exe defaults to DREAMS_DISC1/GDIDREAM.EXE. --read-roots is where the guest's
C:\\CRYO\\DREAMS\\... resolves: by default the directory holding CRYO/DREAMS in
DREAMS_INSTALL_ROOT.

--discs, or --disc1 with --disc2, is disc mode (WD_DISC1, WD_DISC2 and
WD_DATA_DIR; host/sdl/files.c): the host reads the game, GDIDREAM.EXE and the
CD music from the two discs, switches between them when the game asks for the
other one, and keeps what the game writes in <run dir>/sandbox. Nothing is
taken from DREAMS_INSTALL_ROOT, and --exe and --read-roots do not apply.
--discs uses the one .cue beside each of DREAMS_DISC1 and DREAMS_DISC2 (the
folder that holds the extracted tree); --disc1 and --disc2 take a .cue, an
.iso or an extracted directory.

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


def schedule(value: str, kind: str) -> str:
    """Validate the bounded host event grammar before starting the guest."""
    pattern = (
        r"(\d+):([1-9]\d*)x([1-9]\d*)"
        if kind == "resize"
        else r"(\d+):(move|left-down|left-up|right-down|right-up):(-?\d+):(-?\d+)"
    )
    previous = -1
    entries = value.split(",") if value else []
    if len(entries) > 128:
        raise argparse.ArgumentTypeError("at most 128 scheduled events are supported")
    for entry in entries:
        match = re.fullmatch(pattern, entry, flags=re.ASCII)
        if not match or int(match[1]) < previous or int(match[1]) > 0xFFFFFFFF:
            raise argparse.ArgumentTypeError(f"invalid or unordered {kind} schedule: {entry}")
        previous = int(match[1])
        numbers = match.groups()[1:] if kind == "resize" else match.groups()[2:]
        if kind == "resize" and any(not 64 <= int(number) <= 16384 for number in numbers):
            raise argparse.ArgumentTypeError("event dimensions must be between 64 and 16384")
        if any(abs(int(number)) > 32767 for number in numbers):
            raise argparse.ArgumentTypeError("event dimensions/coordinates must fit signed 16 bits")
    return value


def read_roots() -> str:
    install = paths.configured("install_root")
    if install is None:
        sys.exit("set DREAMS_INSTALL_ROOT (the retail CRYO/DREAMS tree) or pass --read-roots")
    parts = [p.upper() for p in install.parts[-2:]]
    return str(install.parent.parent if parts == ["CRYO", "DREAMS"] else install)


def find_cue(tree: Path) -> Path:
    """The disc image an extracted tree came from: the one .cue in the folder
    that holds the tree."""
    cues = sorted(p for p in tree.parent.iterdir() if p.suffix.lower() == ".cue")
    if len(cues) != 1:
        found = ", ".join(p.name for p in cues) or "none"
        raise ValueError(f"expected exactly one .cue in {tree.parent} (found: {found})")
    return cues[0]


def disc_sources(discs: bool, disc1: str | None, disc2: str | None) -> tuple[str, str] | None:
    """The two disc paths of a disc-mode run (absolute: the host starts in the
    run directory), or None for the directory read roots. ValueError when the
    options do not name both discs."""
    if (disc1 is None) != (disc2 is None):
        raise ValueError("--disc1 and --disc2 must be supplied together")
    if disc1 is not None:
        if discs:
            raise ValueError("--discs cannot be combined with --disc1 and --disc2")
        return str(Path(disc1).resolve()), str(Path(disc2).resolve())
    if not discs:
        return None
    return str(find_cue(paths.disc(1))), str(find_cue(paths.disc(2)))


def vm_log_summary(path: Path) -> str:
    """One line about a WD_VM_LOG file: call lines and whether the exit state is there."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    end = next((i for i, line in enumerate(lines) if line.startswith("--- ")), len(lines))
    trailer = any(line.startswith("--- state at exit") for line in lines)
    note = (
        "with exit state"
        if trailer
        else "no exit state: the run was stopped, let the game exit for one"
    )
    return f"vm log: {end} calls, {note} ({path})"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    """The options, checked; args.disc_paths is disc_sources' answer."""
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
        "--render-profile", action="store_true", help="record nonblocking CPU/GPU renderer timings"
    )
    ap.add_argument(
        "--capture-scene",
        action="store_true",
        help="capture original scene inputs on the first main 3D frame",
    )
    ap.add_argument("--read-roots", help="extra read roots, ';'-separated")
    ap.add_argument(
        "--discs", action="store_true",
        help="disc mode: read the game from the .cue beside DREAMS_DISC1 and DREAMS_DISC2",
    )  # fmt: skip
    ap.add_argument("--disc1", metavar="PATH", help="disc mode: disc 1 (.cue, .iso or directory)")
    ap.add_argument("--disc2", metavar="PATH", help="disc mode: disc 2 (with --disc1)")
    ap.add_argument("--seconds", type=int, default=0, help="stop after N s (0 = play)")
    ap.add_argument("--keys", default="", help="scripted keys, ms:KEY,...")
    ap.add_argument("--snap-ms", type=int, default=0, help="snapshot interval")
    ap.add_argument("--fps", type=int, default=25, help="frame cap (0 = uncapped)")
    ap.add_argument("--scale", type=int, default=2, help="window size in multiples of 640x480")
    ap.add_argument("--width", type=int, help="initial client width; requires --height")
    ap.add_argument("--height", type=int, help="initial client height; requires --width")
    ap.add_argument(
        "--resize",
        default="",
        type=lambda s: schedule(s, "resize"),
        help="scheduled client sizes: ms:WIDTHxHEIGHT,...",
    )
    ap.add_argument(
        "--mouse",
        default="",
        type=lambda s: schedule(s, "mouse"),
        help="scheduled client coordinates: ms:move|left-down|left-up|right-down|right-up:x:y,...",
    )
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
    ap.add_argument(
        "--vm", choices=recomp_env.VM_CHOICES, default=recomp_env.VM_DEFAULT,
        help="virtual memory implementation; runs the matching build directory",
    )  # fmt: skip
    ap.add_argument(
        "--vm-log", action="store_true",
        help="write every virtual-memory call and the state at exit to <run dir>/vm.log",
    )  # fmt: skip
    ap.add_argument(
        "--vm-shadow", choices=("log", "abort"),
        help="shadow build: log disagreements or stop at the first one",
    )  # fmt: skip
    ap.add_argument(
        "--ctl", nargs="?", type=int, const=0, metavar="PORT",
        help="development control channel (WD_CTL) on 127.0.0.1:PORT, default a free port "
        "written to <run dir>/ctl.port; the client is debug/wdctl.py",
    )  # fmt: skip
    args = ap.parse_args(argv)
    if (args.width is None) != (args.height is None):
        ap.error("--width and --height must be supplied together")
    if args.width is not None and not (64 <= args.width <= 16384 and 64 <= args.height <= 16384):
        ap.error("--width and --height must be between 64 and 16384")
    if args.headless and args.fullscreen:
        ap.error("--headless and --fullscreen cannot be combined")
    if args.tag and not re.fullmatch(r"[A-Za-z0-9_-]+", args.tag):
        ap.error("--tag must contain only letters, numbers, underscores or hyphens")
    try:
        args.disc_paths = disc_sources(args.discs, args.disc1, args.disc2)
    except ValueError as error:
        ap.error(str(error))
    if args.disc_paths and (args.exe or args.read_roots):
        ap.error("disc mode reads everything from the discs: --exe and --read-roots do not apply")
    return args


def prepare(args: argparse.Namespace) -> tuple[list[str], dict[str, str], Path] | None:
    """The command, its environment and the run directory (made ready: old
    snapshots and logs of this script removed) for parse_args' options. None,
    with the reason printed, when the build is missing. main runs the command
    and waits; debug/wdctl.py start_game runs it and drives it."""
    discs = args.disc_paths
    # GAME_TickFrame draws Frame Rate/Mem 3DTR if 0x49d5c0 != 0 and the editor
    # flag 0x4a477c is 0; DBG_DrawObjectInfo (0x416606) needs 0x49d5d0 != 0;
    # the byte 0x4ac8c8 makes REND_DrawFrame skip rasterizing, which leaves the
    # Backspace collision wireframe visible (its next three bytes are also 0).
    overlays = ["0x49d5c0=1", "0x49d5d0=1", "0x4ac8c8=1"]
    pokes = (overlays if args.overlays else []) + args.poke
    # Disc mode starts the host without an exe path: it loads the disc's.
    exe = [] if discs else [args.exe or str(paths.disc(1) / "GDIDREAM.EXE")]
    out = recomp_env.out_dir("windream")
    run = out / ("run-" + args.tag if args.tag else "run")
    run.mkdir(exist_ok=True)
    if args.capture_scene:
        (run / "direct-scene.wds").unlink(missing_ok=True)
    for snap in run.glob("snap_*.bmp"):
        snap.unlink()
    (run / "vm.log").unlink(missing_ok=True)
    (run / "ctl.port").unlink(missing_ok=True)
    unattended = args.seconds > 0
    env = dict(
        os.environ,
        WD_READ_ROOTS="" if discs else args.read_roots or read_roots(),
        WD_DISC1=discs[0] if discs else "",
        WD_DISC2=discs[1] if discs else "",
        WD_DATA_DIR=str(run / "sandbox") if discs else "",
        WD_KEYS=args.keys,
        WD_SNAP_MS=str(args.snap_ms) if args.snap_ms else "",
        WD_FPS=str(args.fps),  # "0" = uncapped (an empty value would unset it)
        WD_SCALE=str(args.scale),
        WD_WIDTH=str(args.width) if args.width else "",
        WD_HEIGHT=str(args.height) if args.height else "",
        WD_RESIZE=args.resize,
        WD_MOUSE=args.mouse,
        WD_FULLSCREEN="1" if args.fullscreen else "",
        WD_FILTER=args.filter,
        WD_PAD=args.pad,
        WD_DEADZONE=args.deadzone,
        WD_DUMP=args.dump,
        WD_POKE=",".join(pokes),
        WD_RENDERER=args.renderer,
        WD_RENDER_PROFILE="1" if args.render_profile else os.environ.get("WD_RENDER_PROFILE", ""),
        WD_MUTE="1" if args.mute or args.headless else os.environ.get("WD_MUTE", ""),
        WD_HEADLESS="1" if args.headless else os.environ.get("WD_HEADLESS", ""),
        WD_SCENE_CAPTURE=str(run / "direct-scene.wds") if args.capture_scene else "",
        WD_VM_LOG=str(run / "vm.log") if args.vm_log else "",
        WD_VM_SHADOW=args.vm_shadow or "",
        WD_QUIET="1" if unattended or args.headless else "",
        WD_FOCUS="1" if unattended or args.headless else "",
        WD_CTL="" if args.ctl is None else str(args.ctl),
    )
    binary = recomp_env.build_dir(
        out, render_audit=args.render_audit, vm=args.vm
    ) / recomp_env.exe_name("windream_recomp")
    if not binary.exists():
        flags = (" --render-audit" if args.render_audit else "") + (
            f" --vm {args.vm}" if args.vm != recomp_env.VM_DEFAULT else ""
        )
        print(
            f"{binary} does not exist; build it first: "
            f"uv run python recomp/windream/build.py{flags}",
            file=sys.stderr,
        )
        return None
    return [str(binary), *exe, "--run"], env, run


def main() -> int:
    args = parse_args()
    prepared = prepare(args)
    if prepared is None:
        return 1
    command, env, run = prepared
    with open(run / "stderr.txt", "wb") as err, open(run / "stdout.txt", "wb") as so:
        p = subprocess.Popen(command, cwd=run, env=env, stdout=so, stderr=err)
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
    if args.vm_log:
        vm_log = run / "vm.log"
        print(vm_log_summary(vm_log) if vm_log.exists() else f"no vm log was written ({vm_log})")
    if args.vm == "shadow":
        shadow = [line for line in log if line.startswith("[vm-shadow]")]
        summary = [line for line in shadow if re.search(r"(\d+|no) mismatches", line)]
        shown = [line for line in shadow[:20] if line not in summary]
        print("\n".join(shown + summary[-1:]))
    if args.capture_scene:
        captured = [line for line in log if "[render-capture] captured" in line]
        print("\n".join(captured))
        if not captured or at is not None:
            print("scene capture failed or the run crashed", file=sys.stderr)
            return 1
    return 1 if at is not None or (not stopped and p.returncode != 0) else 0


if __name__ == "__main__":
    sys.exit(main())
