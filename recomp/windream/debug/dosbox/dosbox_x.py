"""Run and capture the DOS builds in DOSBox-X on Linux (the counterpart of
drive-x.ps1 / run-x.ps1 on Windows).

DOSBox-X comes from the distribution (apt: dosbox-x, 2024.03.01 checked; its
software Voodoo runs DREAMSFX.EXE). Keys go in through xdotool on a private
Xvfb display; a key must be held about 150 ms, a bare `xdotool key` is
missed. Frames come from DOSBox-X's own screenshot (host key F12 on Linux,
F12+P; PNG of the emulated output, Voodoo included) or from an X grab of its
window (`grab`); both give the 640x480 game frame with the menu bar hidden.

  dosbox_x.py setup                  install copy on C: (INST_SFX.BAT's copies, plus
                                     GLIDE2X.OVL from 3DFX\\GRTVGR.EXE)
  dosbox_x.py start [--exe DREAMS.EXE] [--disc <cue|dir>]
  dosbox_x.py steps "ESC,wait:3000,RETURN,wait:45000,snap:level"
  dosbox_x.py stop
  dosbox_x.py run --steps "..."      start, steps, stop

Steps: a key name (held 150 ms), hold:<KEY>:<ms>, down:<KEY>, up:<KEY>,
wait:<ms>, snap:<name> (DOSBox-X screenshot, copied to shots/<name>.png),
grab:<name> (X grab of the window), video (F12+I: start or stop an AVI in
capture/: ZMBV, 640x480, every emulated frame, with the mixer's audio).
Key names are xdotool's (Escape, Return, Up, Control_L, F10, a); ESC,
RETURN, UP, DOWN, LEFT, RIGHT, CTRL, ALT, SPACE are accepted too.

Work directory: DREAMS_OUT/recomp/dosbox (c/, capture/, shots/, x-linux.conf,
session-linux.json). C: is a copy because the game autosaves into it on every
level entry; D: is disc 1's .cue (CD music works) or, for a patched
DREAMS.DAT, a directory (no CD music).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402

WORK = recomp_env.out_dir("dosbox")
INSTALL = WORK / "c" / "CRYO" / "DREAMS"
CONF = WORK / "x-linux.conf"
SESSION = WORK / "session-linux.json"
CAPTURE, SHOTS = WORK / "capture", WORK / "shots"
DISPLAY = ":97"
HOLD_S = 0.15
ALIASES = {
    "ESC": "Escape",
    "RETURN": "Return",
    "ENTER": "Return",
    "UP": "Up",
    "DOWN": "Down",
    "LEFT": "Left",
    "RIGHT": "Right",
    "CTRL": "Control_L",
    "ALT": "Alt_L",
    "SPACE": "space",
    "SHIFT": "Shift_L",
    "TAB": "Tab",
}

CONF_TEMPLATE = """\
[sdl]
fullscreen = false
output = surface
windowresolution = original
autolock = false
showmenu = false

[dosbox]
machine = svga_s3
memsize = 32
captures = {capture}
quit warning = false
fastbioslogo = true
startbanner = false

[render]
aspect = false
scaler = none

[cpu]
core = dynamic
cputype = pentium_mmx
cycles = fixed {cycles}

[voodoo]
voodoo_card = software
voodoo_maxmem = false
glide = false

[autoexec]
mount c "{c}"
{mount_d}
c:
cd \\CRYO\\DREAMS
{exe}
"""


def disc1_cue() -> Path:
    tree = paths.disc(1)
    cues = sorted(p for p in tree.parent.iterdir() if p.suffix.lower() == ".cue")
    if len(cues) != 1:
        raise SystemExit(f"expected one .cue beside {tree}; pass --disc")
    return cues[0]


def setup(force: bool = False) -> None:
    """What INST_SFX.BAT copies, from DREAMS_DISC1, plus the Voodoo DOS Glide driver."""
    src = paths.disc(1)
    if INSTALL.exists() and not force:
        print(f"{INSTALL} exists (--force to rebuild)")
    else:
        if INSTALL.exists():
            shutil.rmtree(INSTALL)
        data = INSTALL / "DATA"
        for sub in ("3DC", "SOUND", "UNIVBE", "GAME", "ANIM"):
            (data / sub).mkdir(parents=True, exist_ok=True)
        for f in src.iterdir():
            if f.is_file():
                shutil.copy2(f, INSTALL / f.name)
        for name in ("HD.ID", "REPLAY.BIN"):
            shutil.copy2(src / "DATA" / name, data / name)
        for f in (src / "DATA" / "3DC").iterdir():
            if f.name.upper() == "DIALOG.DRD" or f.suffix.upper() in (".3DC", ".3DM"):
                shutil.copy2(f, data / "3DC" / f.name)
        for sub in ("SOUND", "UNIVBE"):
            for f in (src / "DATA" / sub).iterdir():
                shutil.copy2(f, data / sub / f.name)
    ovl = INSTALL / "GLIDE2X.OVL"
    if not ovl.exists():
        with zipfile.ZipFile(src / "3DFX" / "GRTVGR.EXE") as archive:
            ovl.write_bytes(archive.read("Glide/Drivers/Voodoo/Dos/glide2x.ovl"))
    CAPTURE.mkdir(parents=True, exist_ok=True)
    SHOTS.mkdir(parents=True, exist_ok=True)
    print(f"C: {INSTALL.parent.parent} ({sum(1 for _ in INSTALL.rglob('*'))} entries, GLIDE2X.OVL)")


def write_conf(exe: str, disc: Path, cycles: int) -> Path:
    if disc.is_dir():
        mount_d = f'mount d "{disc}" -t cdrom'
    else:
        mount_d = f'imgmount d "{disc}" -t cdrom'
    CONF.write_text(
        CONF_TEMPLATE.format(capture=CAPTURE, cycles=cycles, c=WORK / "c", mount_d=mount_d, exe=exe)
    )
    return CONF


def xenv(display: str) -> dict:
    return {**os.environ, "DISPLAY": display, "SDL_AUDIODRIVER": "dummy"}


def ensure_display(display: str) -> None:
    probe = subprocess.run(["xdpyinfo"], env=xenv(display), capture_output=True)
    if probe.returncode == 0:
        return
    subprocess.Popen(
        ["Xvfb", display, "-screen", "0", "1280x1024x24", "-nolisten", "tcp"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(50):
        time.sleep(0.1)
        if subprocess.run(["xdpyinfo"], env=xenv(display), capture_output=True).returncode == 0:
            return
    raise SystemExit(f"Xvfb {display} did not come up")


def start(exe: str, disc: Path, cycles: int, display: str) -> dict:
    for tool in ("dosbox-x", "xdotool", "xwd", "convert", "Xvfb", "xdpyinfo"):
        if not shutil.which(tool):
            raise SystemExit(
                f"{tool} is missing: apt install dosbox-x xdotool x11-apps x11-utils "
                "imagemagick xvfb"
            )
    if not INSTALL.exists():
        setup()
    stop(quiet=True)
    ensure_display(display)
    conf = write_conf(exe, disc, cycles)
    log = open(WORK / "dosbox-linux.log", "wb")
    process = subprocess.Popen(
        ["dosbox-x", "-conf", str(conf), "-fastlaunch"],
        env=xenv(display),
        cwd=WORK,
        stdout=log,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    window = None
    for _ in range(100):
        time.sleep(0.1)
        found = subprocess.run(
            ["xdotool", "search", "--pid", str(process.pid)],
            env=xenv(display),
            capture_output=True,
            text=True,
        ).stdout.split()
        if found:
            window = found[-1]
            break
    if window is None:
        process.kill()
        raise SystemExit("no DOSBox-X window")
    session = {
        "pid": process.pid,
        "display": display,
        "window": window,
        "exe": exe,
        "disc": str(disc),
    }
    SESSION.write_text(json.dumps(session))
    print(f"dosbox-x pid {process.pid} window {window} on {display}: {exe}, D: {disc.name}")
    return session


def load_session() -> dict:
    if not SESSION.exists():
        raise SystemExit("no DOSBox-X session: dosbox_x.py start")
    return json.loads(SESSION.read_text())


def stop(quiet: bool = False) -> None:
    if not SESSION.exists():
        return
    session = json.loads(SESSION.read_text())
    try:
        os.kill(session["pid"], 15)
    except ProcessLookupError:
        pass
    SESSION.unlink()
    if not quiet:
        print(f"stopped dosbox-x pid {session['pid']}")


class Driver:
    def __init__(self, session: dict):
        self.env = xenv(session["display"])
        self.window = session["window"]
        self.pid = session["pid"]

    def xdo(self, *args: str) -> None:
        subprocess.run(["xdotool", *args], env=self.env, check=True)

    def focus(self) -> None:
        self.xdo("windowfocus", "--sync", self.window)

    def key(self, name: str, seconds: float = HOLD_S) -> None:
        name = ALIASES.get(name.upper(), name)
        self.xdo("keydown", name)
        time.sleep(seconds)
        self.xdo("keyup", name)

    def hotkey(self, name: str) -> None:
        """F12 (DOSBox-X's host key on Linux) held around a key."""
        self.xdo("keydown", "F12")
        time.sleep(0.1)
        self.key(name)
        time.sleep(0.05)
        self.xdo("keyup", "F12")

    def snap(self, name: str) -> str:
        before = set(CAPTURE.glob("*.png"))
        self.hotkey("p")
        for _ in range(40):
            time.sleep(0.1)
            new = sorted(set(CAPTURE.glob("*.png")) - before, key=lambda p: p.stat().st_mtime)
            if new:
                time.sleep(0.2)
                dst = SHOTS / f"{name}.png"
                shutil.copy2(new[-1], dst)
                return f"snap {dst} <- {new[-1].name}"
        return f"snap {name}: no capture written"

    def grab(self, name: str) -> str:
        dst = SHOTS / f"{name}.png"
        xwd = subprocess.run(
            ["xwd", "-id", self.window, "-silent"], env=self.env, capture_output=True, check=True
        )
        subprocess.run(["convert", "xwd:-", str(dst)], input=xwd.stdout, check=True)
        return f"grab {dst}"

    def video(self) -> str:
        before = set(CAPTURE.glob("*.avi"))
        self.hotkey("i")
        time.sleep(0.5)
        new = set(CAPTURE.glob("*.avi")) - before
        return f"video toggled{': ' + ', '.join(p.name for p in new) if new else ''}"

    def steps(self, text: str) -> None:
        self.focus()
        for step in text.split(","):
            parts = step.strip().split(":")
            what = parts[0]
            if what == "wait":
                time.sleep(int(parts[1]) / 1000)
            elif what == "snap":
                print(self.snap(parts[1]), flush=True)
            elif what == "grab":
                print(self.grab(parts[1]), flush=True)
            elif what == "hold":
                self.key(parts[1], int(parts[2]) / 1000)
            elif what == "down":
                self.xdo("keydown", ALIASES.get(parts[1].upper(), parts[1]))
            elif what == "up":
                self.xdo("keyup", ALIASES.get(parts[1].upper(), parts[1]))
            elif what == "video":
                print(self.video(), flush=True)
            elif what:
                self.key(what)
                time.sleep(0.15)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="command", required=True)
    s = sub.add_parser("setup")
    s.add_argument("--force", action="store_true")
    for name in ("start", "run"):
        p = sub.add_parser(name)
        p.add_argument("--exe", default="DREAMSFX.EXE", help="DREAMSFX.EXE (3dfx) or DREAMS.EXE")
        p.add_argument(
            "--disc", type=Path, help="D: (.cue, .iso or a directory); default disc 1's .cue"
        )
        p.add_argument("--cycles", type=int, default=200000)
        p.add_argument("--display", default=DISPLAY)
        if name == "run":
            p.add_argument("--steps", required=True)
    st = sub.add_parser("steps")
    st.add_argument("steps")
    sub.add_parser("stop")
    options = ap.parse_args()
    if options.command == "setup":
        setup(options.force)
    elif options.command in ("start", "run"):
        session = start(options.exe, options.disc or disc1_cue(), options.cycles, options.display)
        if options.command == "run":
            try:
                Driver(session).steps(options.steps)
            finally:
                stop()
    elif options.command == "steps":
        Driver(load_session()).steps(options.steps)
    elif options.command == "stop":
        stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
