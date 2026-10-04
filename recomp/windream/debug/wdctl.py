"""Client of the development control channel (recomp/windream/devtools, WD_CTL).

A host started with WD_CTL listens on 127.0.0.1 and takes one JSON command per
line: press a game key, wait for a condition, save a frame, read guest memory,
pause and step, tail the file opens, record the mixer's output. The channel
exists in development builds only (CMake WD_DEVTOOLS), never in a release.

As a library (standard library only):

    with start_game(tag="mytest", discs=True, headless=True) as game:
        ctl = game.ctl
        ctl.wait_until_opened("intro.hnm")
        ctl.tap("ESC")
        print(ctl.current_project())

Ctl has one method per command; an answer with ok:false raises CtlError.
start_game runs the game the way run.py does (same options, same run
directory) and always ends the process when the block is left.

As a command line, against a game started with `run.py ... --ctl`:

    uv run python recomp/windream/debug/wdctl.py --run-dir out/recomp/windream/run-NAME status
    ... key ESC                    tap; `key ESC down`, `key ESC up` hold and release
    ... type A A shift             a typed key through the host's key path (KEY [TEXT [MODS]])
    ... wait frames 25             or: wait ms 1000
    ... wait_until opened H18ANGKR.DSN [--since SEQ] [--timeout-ms N] [--timeout-frames N]
                                   --since: the "seq" the key before it answered
                                   with, so an open that has already happened counts
    ... wait_until mem 0x49d5c0 == 1 [SIZE]
    ... wait_until disc 2          also: cd_track N, frame N
    ... read 0x661e04 4            read32 ADDR, read_cstr ADDR, write ADDR HEX, write32 ADDR VALUE
    ... screenshot out/tmp/x.bmp   the next frame (while paused: steps one frame)
    ... overlay_shot A.bmp B.bmp   Develop: the frame before and after the editor and tools
    ... pause | resume | step [N]
    ... log [SINCE]
    ... audio_dump out/tmp/x.wav   then: audio_dump_stop
    ... project                    the game's current project record (current_project)
    ... quit [CODE]
    ... raw '{"cmd":"ping"}'

--run-dir reads the port from <dir>/ctl.port (default: the untagged run
directory); --port gives it directly. Each invocation is one connection; a
pause, a held key or an audio dump stays in force after it ends. The answer is
printed as JSON; the exit code is 1 when the host answers with an error.

What the game's addresses mean is knowledge of this file, not of the host:
current_project reads the record the dword at 0x661e04 points to
(docs/research/install-and-discs.md, src/dreams/formats/project.py).
"""

from __future__ import annotations

import argparse
import atexit
import json
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

WINDREAM = Path(__file__).resolve().parents[1]

# The current project: a dword at this guest address points to the unpacked
# 0x2200-byte record (the layout of src/dreams/formats/project.py).
PROJECT_POINTER = 0x661E04
RECORD_SIZE = 0x2200
NAME_AT, NAME_SIZE = 0x00, 0x20  # "Project0"
LEVEL_AT = 0x1FC  # the section of the game, 0 to 4 (SCENE_GetLevelNumber)
OBJET_AT, OBJET_SIZE = 0x600, 0xC0  # OBJET table: name at +0 (12 bytes), asset at +12 (32)


class CtlError(RuntimeError):
    """The host answered ok:false, or did not answer."""


def _cstr(cell: bytes) -> str:
    return cell.split(b"\x00", 1)[0].decode("latin-1")


class Ctl:
    """One connection to a host's control channel."""

    def __init__(self, port: int, timeout: float = 30.0):
        self.port = port
        self.timeout = timeout
        self._socket = socket.create_connection(("127.0.0.1", port), timeout=timeout)
        self._socket.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._lines = self._socket.makefile("rb")
        self._id = 0

    @classmethod
    def connect(
        cls,
        port: int | None = None,
        run_dir: str | Path | None = None,
        wait: float = 30.0,
        process: subprocess.Popen | None = None,
    ) -> Ctl:
        """Connect to a port, or to the one in <run_dir>/ctl.port, retrying for
        `wait` seconds while the host starts. Gives up at once when `process`
        (the host) has exited."""
        deadline = time.monotonic() + wait
        port_file = Path(run_dir) / "ctl.port" if run_dir is not None else None
        if port is None and port_file is None:
            raise ValueError("connect needs a port or a run directory")
        while True:
            try:
                found = port if port is not None else int(port_file.read_text().strip())
                return cls(found)
            except (OSError, ValueError) as error:
                reason = error
            if process is not None and process.poll() is not None:
                raise CtlError(f"the host exited with code {process.returncode} before listening")
            if time.monotonic() >= deadline:
                where = f"port {port}" if port is not None else str(port_file)
                raise CtlError(f"no control channel at {where} after {wait:g} s: {reason}")
            time.sleep(0.05)

    def close(self) -> None:
        for closing in (self._lines, self._socket):
            try:
                closing.close()
            except OSError:
                pass

    def __enter__(self) -> Ctl:
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # ---- the protocol ----

    def call(self, cmd: str, timeout: float | None = None, **args) -> dict:
        """Send one command and return its answer. Arguments that are None are
        left out. CtlError when the host says ok:false or stays silent for
        `timeout` seconds (then the connection is closed: it is out of step)."""
        self._id += 1
        request = {"id": self._id, "cmd": cmd, **{k: v for k, v in args.items() if v is not None}}
        try:
            # On a channel a failed call has already closed, the socket's own
            # calls raise OSError (EBADF): that is a CtlError too.
            self._socket.settimeout(self.timeout if timeout is None else timeout)
            self._socket.sendall(json.dumps(request).encode() + b"\n")
            while True:
                line = self._lines.readline()
                if not line:
                    raise CtlError(f"{cmd}: the host closed the connection")
                answer = json.loads(line.decode("utf-8", "replace"))
                if answer.get("id") == self._id:
                    break
        except (OSError, ValueError) as error:
            self.close()
            raise CtlError(f"{cmd}: no answer ({error})") from error
        if not answer.get("ok"):
            raise CtlError(f"{cmd}: {answer.get('error', 'failed')}")
        return answer

    # ---- commands ----

    def ping(self) -> dict:
        return self.call("ping")

    def status(self, opens: int | None = None) -> dict:
        return self.call("status", opens=opens)

    def key(
        self, name: str, action: str = "tap", frames: int | None = None, ms: int | None = None
    ) -> dict:
        """A game key, as a WD_KEYS script key is one (not remapped by
        WD_KEYMAP). A tap is held for `frames` presented frames or `ms`
        milliseconds (default 150 ms, as the script holds one)."""
        return self.call("key", name=name, action=action, frames=frames, ms=ms)

    def tap(self, name: str, frames: int | None = None, ms: int | None = None) -> dict:
        return self.key(name, "tap", frames, ms)

    def type(
        self, key: str, text: str | None = None, mods: str | None = None, ms: int | None = None
    ) -> dict:
        """A key as a person presses it, through the host's own key path (SDL
        events; `key` above bypasses it): `key` an SDL scan-code name ("A",
        "1", "Keypad 2", "F10", "Space", "Escape"), `text` the character the
        layout types, if any, `mods` "shift", "ctrl", "alt" joined by '+',
        held `ms` (150). In Develop this is what the DOS keys read."""
        return self.call("type", key=key, text=text, mods=mods, ms=ms)

    def mouse(
        self,
        x: int,
        y: int,
        action: str = "click",
        button: str = "left",
        ms: int | None = None,
        client: bool = False,
        dx: int | None = None,
        dy: int | None = None,
    ) -> dict:
        """The pointer through the host's own mouse path (SDL events): `x`, `y` in
        game pixels (mapped through the software renderer; `client` True takes
        window pixels, for the direct renderer), `action` move, down, up or
        click (held `ms`, 150), `button` left or right. In Develop this is the
        editor's cursor and buttons. `dx`, `dy` give a move relative deltas, as a
        mouse in relative mode reports them (Develop's free camera)."""
        rel = {k: v & 0xFFFFFFFF for k, v in (("dx", dx), ("dy", dy)) if v is not None}
        return self.call(
            "mouse",
            x=x,
            y=y,
            action=action,
            button=button,
            ms=ms,
            client=1 if client else None,
            **rel,
        )

    def key_down(self, name: str) -> dict:
        return self.key(name, "down")

    def key_up(self, name: str) -> dict:
        return self.key(name, "up")

    def wait(self, frames: int | None = None, ms: int | None = None) -> dict:
        """Answer after that many presented frames or host milliseconds (the
        first to pass when both are given)."""
        patience = self.timeout + (ms or 0) / 1000 + (frames or 0)
        return self.call("wait", timeout=patience, frames=frames, ms=ms)

    def wait_until(
        self,
        cond: str,
        timeout_frames: int | None = None,
        timeout_ms: int | None = None,
        **args,
    ) -> dict:
        """Answer when the condition holds; CtlError("... timeout ...") when
        it does not within the timeout (60 s of host time when none is given)."""
        if timeout_ms is None and timeout_frames is None:
            timeout_ms = 60000
        patience = self.timeout + (timeout_ms or 0) / 1000 + (timeout_frames or 0)
        return self.call(
            "wait_until",
            timeout=patience,
            cond=cond,
            timeout_frames=timeout_frames,
            timeout_ms=timeout_ms,
            **args,
        )

    def wait_until_mem(self, addr: int, op: str, value: int, size: int = 4, **timeouts) -> dict:
        """op: == != < > (unsigned) or & (any bit of value set)."""
        return self.wait_until("mem", addr=addr, op=op, value=value, size=size, **timeouts)

    def wait_until_opened(
        self, path: str, ok: bool | None = None, since: int | None = None, **timeouts
    ) -> dict:
        """A file whose guest path contains `path` (case ignored) is opened:
        after this call, or after event `since` (the "seq" of an earlier
        answer, so that an open caused by an earlier key is not missed)."""
        return self.wait_until("opened", path=path, ok=ok, since=since, **timeouts)

    def wait_until_disc(self, number: int, **timeouts) -> dict:
        return self.wait_until("disc", value=number, **timeouts)

    def wait_until_cd_track(self, track: int, **timeouts) -> dict:
        return self.wait_until("cd_track", value=track, **timeouts)

    def wait_until_frame(self, frame: int, **timeouts) -> dict:
        return self.wait_until("frame", value=frame, **timeouts)

    def read(self, addr: int, size: int) -> bytes:
        return bytes.fromhex(self.call("read", addr=addr, size=size)["hex"])

    def read8(self, addr: int) -> int:
        return self.read(addr, 1)[0]

    def read16(self, addr: int) -> int:
        return struct.unpack("<H", self.read(addr, 2))[0]

    def read32(self, addr: int) -> int:
        return struct.unpack("<I", self.read(addr, 4))[0]

    def read_cstr(self, addr: int, max: int | None = None) -> str:
        return self.call("read_cstr", addr=addr, max=max)["text"]

    def write(self, addr: int, data: bytes) -> dict:
        return self.call("write", addr=addr, hex=data.hex())

    def write32(self, addr: int, value: int) -> dict:
        return self.write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def screenshot(self, path: str | Path) -> dict:
        """The next presented frame, written by the host's snapshot code: a
        24-bit BMP of the game's frame (software renderer) or a PNG of the
        window (direct renderer), whatever the name says. While paused, the
        host steps one frame to have one to write."""
        target = Path(path).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        return self.call("screenshot", path=str(target))

    def overlay_shot(self, before: str | Path, after: str | Path) -> dict:
        """Develop, in a level: the game frame of the next gameplay frame just before
        the editor's call and the tools' overlays and just after them, as 24-bit
        BMPs (the GPU frame read back under the direct renderer). Their difference
        is what the overlays drew (spec 008 phase D). While paused, steps one frame."""
        paths = [Path(p).resolve() for p in (before, after)]
        for path in paths:
            path.parent.mkdir(parents=True, exist_ok=True)
        return self.call("overlay_shot", before=str(paths[0]), after=str(paths[1]))

    def pause(self) -> dict:
        return self.call("pause")

    def resume(self) -> dict:
        return self.call("resume")

    def step(self, frames: int = 1) -> dict:
        """Let that many frames be presented, then pause; answers when paused again."""
        return self.call("step", timeout=self.timeout + frames, frames=frames)

    def log(self, since: int = 0, max: int | None = None) -> dict:
        """Events after number `since`: file opens, CD and disc changes."""
        return self.call("log", since=since, max=max)

    def audio_dump(self, path: str | Path) -> dict:
        """Write what the mixer outputs from now on to a WAV file, until
        audio_dump_stop or the end of the host."""
        target = Path(path).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        return self.call("audio_dump", path=str(target))

    def audio_dump_stop(self) -> dict:
        return self.call("audio_dump_stop")

    def display_shot(self, path: str | Path, timeout: float = 5.0) -> Path:
        """The next display interpolation frame (WD_INTERPOLATE) as a PNG,
        written from the swapchain; waits for the file."""
        target = Path(path).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        target.unlink(missing_ok=True)
        self.call("display_shot", path=str(target))
        end = time.monotonic() + timeout
        while not target.exists() or not target.stat().st_size:
            if time.monotonic() > end:
                raise CtlError(f"display_shot: no display frame within {timeout} s")
            time.sleep(0.02)
        time.sleep(0.05)  # the PNG is written in one call; let it close
        return target

    def trace(self, path: str | Path, frames: int, ranges: list[tuple[int, int]]) -> dict:
        """After each of the next `frames` presents, write a line to `path`:
        the frame number, host nanoseconds, then each (va, size) range as hex
        ("-" when unmapped). The game is not paused. Answers at once."""
        target = Path(path).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)
        spec = ",".join(f"0x{va:x}:{size}" for va, size in ranges)
        return self.call("trace", path=str(target), frames=frames, ranges=spec)

    def quit(self, code: int = 0) -> dict:
        """End the host process with that exit code."""
        try:
            return self.call("quit", code=code)
        finally:
            self.close()

    # ---- the game ----

    def current_project(self) -> dict | None:
        """The project the game is in, from its unpacked record: name
        ("Project0"), level number (+0x1fc: 0 to 4) and the first OBJET's name
        and asset. The record is filled at start, before any level loads: a
        host that has only just opened its intro already answers Project0.
        None while the pointer is 0."""
        pointer = self.read32(PROJECT_POINTER)
        if not pointer:
            return None
        record = self.read(pointer, RECORD_SIZE)
        objet = record[OBJET_AT : OBJET_AT + OBJET_SIZE]
        return {
            "pointer": pointer,
            "name": _cstr(record[NAME_AT : NAME_AT + NAME_SIZE]),
            "level": struct.unpack_from("<i", record, LEVEL_AT)[0],
            "objet0": _cstr(objet[:12]),
            "objet0_asset": _cstr(objet[12:44]),
        }


# ---- starting a game under control ----


def _tie_to_this_process(process: subprocess.Popen):
    """Windows: put the child in a job that the system ends when this process
    does, however it ends (a killed test runner leaves no game behind). The
    returned handle must stay referenced. Elsewhere: nothing."""
    if sys.platform != "win32":
        return None
    import ctypes
    from ctypes import wintypes

    class BasicLimits(ctypes.Structure):
        _fields_ = [
            ("PerProcessUserTimeLimit", ctypes.c_int64),
            ("PerJobUserTimeLimit", ctypes.c_int64),
            ("LimitFlags", wintypes.DWORD),
            ("MinimumWorkingSetSize", ctypes.c_size_t),
            ("MaximumWorkingSetSize", ctypes.c_size_t),
            ("ActiveProcessLimit", wintypes.DWORD),
            ("Affinity", ctypes.c_size_t),
            ("PriorityClass", wintypes.DWORD),
            ("SchedulingClass", wintypes.DWORD),
        ]

    class ExtendedLimits(ctypes.Structure):
        _fields_ = [
            ("BasicLimitInformation", BasicLimits),
            ("IoInfo", ctypes.c_uint64 * 6),
            ("ProcessMemoryLimit", ctypes.c_size_t),
            ("JobMemoryLimit", ctypes.c_size_t),
            ("PeakProcessMemoryUsed", ctypes.c_size_t),
            ("PeakJobMemoryUsed", ctypes.c_size_t),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateJobObjectW.restype = wintypes.HANDLE
    kernel32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
    kernel32.SetInformationJobObject.argtypes = [
        wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD,
    ]  # fmt: skip
    kernel32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    job = kernel32.CreateJobObjectW(None, None)
    if not job:
        return None
    limits = ExtendedLimits()
    limits.BasicLimitInformation.LimitFlags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    extended_limit_information = 9
    kernel32.SetInformationJobObject(
        job, extended_limit_information, ctypes.byref(limits), ctypes.sizeof(limits)
    )
    kernel32.AssignProcessToJobObject(job, wintypes.HANDLE(int(process._handle)))
    return job


class Game:
    """A running host: its process, the connected Ctl (None without the
    channel) and its run directory. close() ends the process."""

    def __init__(self, process: subprocess.Popen, run_dir: Path, files: list):
        self.process = process
        self.run_dir = run_dir
        self.ctl: Ctl | None = None
        self._files = files
        self._job = _tie_to_this_process(process)
        atexit.register(self.close)

    @property
    def stderr_text(self) -> str:
        """The host's log so far."""
        return (self.run_dir / "stderr.txt").read_text(encoding="utf-8", errors="replace")

    def close(self, remove: bool = False) -> None:
        """End the host: ask it to quit, then kill what is left. With
        remove, delete the run directory too."""
        if self.process.poll() is None and self.ctl is not None:
            try:
                self.ctl.call("quit", timeout=3.0, code=0)
            except CtlError:
                pass
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        if self.ctl is not None:
            self.ctl.close()
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait()
        for file in self._files:
            file.close()
        atexit.unregister(self.close)
        if remove:
            shutil.rmtree(self.run_dir, ignore_errors=True)

    def __enter__(self) -> Game:
        return self

    def __exit__(self, *exc) -> None:
        self.close()


def start_game(
    tag: str | None = None,
    discs: bool = True,
    headless: bool = True,
    extra_env: dict[str, str] | None = None,
    args: list[str] | tuple[str, ...] = (),
    ctl: bool = True,
    wait: float = 30.0,
) -> Game:
    """Start the game as run.py does and connect to its control channel.

    tag, discs and headless are run.py's --tag, --discs and --headless; args
    are more run.py options (["--renderer", "direct"]); extra_env is added to
    the environment run.py builds. ctl=False starts it without WD_CTL (then
    Game.ctl is None). The log is <run dir>/stderr.txt, as with run.py.

    Use the result as a context manager, or call close(): either ends the
    process. CtlError (after ending the process) when the build is missing or
    the channel does not come up within `wait` seconds."""
    sys.path.insert(0, str(WINDREAM))
    try:
        import run as run_py
    finally:
        sys.path.remove(str(WINDREAM))
    options = [*args]
    if tag:
        options += ["--tag", tag]
    if discs:
        options.append("--discs")
    if headless:
        options.append("--headless")
    if ctl:
        options += ["--ctl", "0"]
    prepared = run_py.prepare(run_py.parse_args(options))
    if prepared is None:
        raise CtlError("the windream build is missing: uv run python recomp/windream/build.py")
    command, env, run_dir = prepared
    env.update(extra_env or {})
    files = [open(run_dir / "stderr.txt", "wb"), open(run_dir / "stdout.txt", "wb")]
    process = subprocess.Popen(command, cwd=run_dir, env=env, stderr=files[0], stdout=files[1])
    game = Game(process, run_dir, files)
    if ctl:
        try:
            game.ctl = Ctl.connect(run_dir=run_dir, wait=wait, process=process)
        except BaseException:
            game.close()
            raise
    return game


# ---- command line ----


def _number(text: str) -> int:
    return int(text, 0)


def _timeouts(options: argparse.Namespace) -> dict:
    return {"timeout_frames": options.timeout_frames, "timeout_ms": options.timeout_ms}


def run_cli(ctl: Ctl, command: str, rest: list[str], options: argparse.Namespace):
    """One command line command; returns what to print as JSON."""
    if command == "key":
        return ctl.key(rest[0], rest[1] if len(rest) > 1 else "tap")
    if command == "tap":
        return ctl.tap(rest[0])
    if command == "type":  # type KEY [TEXT [MODS]]
        text, mods = (rest[1:] + [None, None])[:2]
        return ctl.type(rest[0], text, mods)
    if command == "mouse":  # mouse X Y [ACTION [BUTTON]]
        action, button = (rest[2:] + ["click", "left"])[:2]
        return ctl.mouse(_number(rest[0]), _number(rest[1]), action, button)
    if command == "wait":
        return ctl.wait(**{rest[0]: _number(rest[1])})
    if command == "wait_until":
        cond, values = rest[0], rest[1:]
        if cond == "opened":
            return ctl.wait_until_opened(values[0], since=options.since, **_timeouts(options))
        if cond == "mem":
            size = _number(values[3]) if len(values) > 3 else 4
            addr, value = _number(values[0]), _number(values[2])
            return ctl.wait_until_mem(addr, values[1], value, size, **_timeouts(options))
        return ctl.wait_until(cond, value=_number(values[0]), **_timeouts(options))
    if command == "read":
        return {"hex": ctl.read(_number(rest[0]), _number(rest[1])).hex()}
    if command == "read32":
        value = ctl.read32(_number(rest[0]))
        return {"value": value, "hex": f"{value:#010x}"}
    if command == "read_cstr":
        return {"text": ctl.read_cstr(_number(rest[0]))}
    if command == "write":
        return ctl.write(_number(rest[0]), bytes.fromhex(rest[1]))
    if command == "write32":
        return ctl.write32(_number(rest[0]), _number(rest[1]))
    if command == "screenshot":
        return ctl.screenshot(rest[0])
    if command == "overlay_shot":
        return ctl.overlay_shot(rest[0], rest[1])
    if command == "step":
        return ctl.step(_number(rest[0]) if rest else 1)
    if command == "log":
        return ctl.log(_number(rest[0]) if rest else 0)
    if command == "audio_dump":
        return ctl.audio_dump(rest[0])
    if command == "quit":
        return ctl.quit(_number(rest[0]) if rest else 0)
    if command == "project":
        return ctl.current_project()
    if command == "raw":
        request = json.loads(rest[0])
        return ctl.call(request.pop("cmd"), **request)
    if command in ("ping", "status", "pause", "resume", "audio_dump_stop"):
        return ctl.call(command)
    raise SystemExit(f"unknown command {command!r}; see --help")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--port", type=int, help="the channel's port (default: read ctl.port)")
    ap.add_argument(
        "--run-dir", help="the run directory holding ctl.port (default: the untagged run directory)"
    )
    ap.add_argument("--wait", type=float, default=5.0, help="seconds to wait for the channel")
    ap.add_argument("--timeout-frames", type=int, help="wait_until: give up after N frames")
    ap.add_argument("--timeout-ms", type=int, help="wait_until: give up after N host ms")
    ap.add_argument("--since", type=int, help="wait_until opened: count opens after this event")
    ap.add_argument("command")
    ap.add_argument("args", nargs="*")
    options = ap.parse_args()
    run_dir = options.run_dir
    if options.port is None and run_dir is None:
        sys.path.insert(0, str(WINDREAM.parent))
        import recomp_env

        run_dir = recomp_env.out_dir("windream") / "run"
    try:
        with Ctl.connect(port=options.port, run_dir=run_dir, wait=options.wait) as ctl:
            try:
                answer = run_cli(ctl, options.command, options.args, options)
            except IndexError:
                ap.error(f"{options.command}: missing argument")
    except CtlError as error:
        print(json.dumps({"ok": False, "error": str(error)}, indent=2))
        return 1
    print(json.dumps(answer, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
