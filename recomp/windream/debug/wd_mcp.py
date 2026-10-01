"""MCP server over the development control channel: lets an AI coding assistant
drive a running game as tools.

Development only, like the channel itself (recomp/windream/devtools, WD_CTL):
nothing in the game or the release depends on this file. It has no game logic
of its own; every tool is one call into wdctl.py (Ctl, start_game), which is
where the protocol and the meaning of the game's addresses live.

Run it over stdio (an MCP client starts it; the repository's .mcp.json does):

    uv run --with mcp --with pillow python recomp/windream/debug/wd_mcp.py

Pillow is only needed by game_screenshot, to turn the software renderer's BMP
into a PNG the assistant can look at. One game at a time: game_start launches
the host the way run.py does and keeps it; game_attach connects to one that is
already running (run.py --ctl). The game the server started is ended when the
server exits (wdctl.Game's atexit and, on Windows, a job object).

Works with the MCP Python SDK 1.x (FastMCP) and 2.x (MCPServer, the same class
renamed).
"""

from __future__ import annotations

import functools
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wdctl  # noqa: E402

try:  # mcp 2.x
    from mcp.server.mcpserver import Image, MCPServer
    from mcp.server.mcpserver.exceptions import ToolError
except ImportError:  # mcp 1.x
    from mcp.server.fastmcp import FastMCP as MCPServer
    from mcp.server.fastmcp import Image
    from mcp.server.fastmcp.exceptions import ToolError

mcp = MCPServer(
    "dreams-game",
    instructions=(
        "Drive a running Dreams to Reality recomp through its development control channel. "
        "game_start (or game_attach) first, then game_key / game_wait_until / game_read / "
        "game_screenshot; game_stop ends a game this server started. Addresses are guest "
        "virtual addresses, as ints or hex strings."
    ),
)


def tool(fn):
    """Register fn as a tool. What the channel (or a bad argument) says goes back as a tool
    error with that message: the SDK keeps the text of a ToolError only."""

    @functools.wraps(fn)
    def guarded(*args, **kwargs):
        try:
            return fn(*args, **kwargs)
        except (wdctl.CtlError, ValueError, OSError) as error:
            raise ToolError(str(error)) from error

    return mcp.tool()(guarded)


# The one game this server holds: a started Game (owns the process) or just a
# Ctl and its run directory (attached to someone else's game).
_game: wdctl.Game | None = None
_ctl: wdctl.Ctl | None = None
_run_dir: Path | None = None
_shots = 0


def _addr(value: int | str) -> int:
    """An address or number given as an int or as a string ("0x661e04", "4096")."""
    return value if isinstance(value, int) else int(str(value).strip(), 0)


def _channel() -> wdctl.Ctl:
    """The connected Ctl; a tool error when there is no game."""
    if _ctl is None:
        raise wdctl.CtlError("no game: call game_start or game_attach first")
    if _game is not None and _game.process.poll() is not None:
        raise wdctl.CtlError(
            f"the game exited with code {_game.process.returncode}; see game_stderr"
        )
    return _ctl


def _forget(remove: bool = False) -> None:
    global _game, _ctl, _run_dir
    if _game is not None:
        _game.close(remove=remove)
    elif _ctl is not None:
        _ctl.close()
    _game = _ctl = _run_dir = None


def _holding() -> bool:
    if _ctl is None:
        return False
    if _game is not None and _game.process.poll() is not None:
        return False  # it ended by itself; its Game is still ours to clean up
    return True


# ---- lifetime ----


@tool
def game_start(
    tag: str,
    discs: bool = True,
    headless: bool = True,
    renderer: str = "software",
    extra_env: dict[str, str] | None = None,
) -> dict:
    """Start the game under the control channel (wdctl.start_game, run.py's options) and keep it.

    tag names the run directory (out/recomp/windream/run-<tag>); discs plays from the two disc
    images; headless hides the window and mutes; renderer is "software" or "direct"; extra_env
    adds environment variables (WD_* settings). One game at a time. Returns the channel's port,
    the run directory and the process id."""
    global _game, _ctl, _run_dir
    if _holding():
        raise wdctl.CtlError("a game is already held; call game_stop first")
    _forget()
    if renderer not in ("software", "direct"):
        raise wdctl.CtlError(f"renderer must be software or direct, not {renderer!r}")
    game = wdctl.start_game(
        tag=tag,
        discs=discs,
        headless=headless,
        extra_env=extra_env,
        args=["--renderer", renderer],
    )
    _game, _ctl, _run_dir = game, game.ctl, game.run_dir
    return {"port": game.ctl.port, "run_dir": str(game.run_dir), "pid": game.process.pid}


@tool
def game_attach(run_dir: str | None = None, port: int | None = None) -> dict:
    """Attach to a game that is already running with the channel (run.py --ctl), by its run
    directory (reads ctl.port there) or by port. game_stop only disconnects from it."""
    global _ctl, _run_dir
    if run_dir is None and port is None:
        raise wdctl.CtlError("game_attach needs run_dir or port")
    if _holding():
        raise wdctl.CtlError("a game is already held; call game_stop first")
    _forget()
    ctl = wdctl.Ctl.connect(port=port, run_dir=run_dir, wait=5.0)
    _ctl, _run_dir = ctl, Path(run_dir).resolve() if run_dir else None
    return {"port": ctl.port, "run_dir": str(_run_dir) if _run_dir else None}


@tool
def game_stop() -> dict:
    """End the game this server started (asks it to quit, then kills it). For an attached game,
    only drops the connection and leaves it running."""
    started = _game is not None
    held = _ctl is not None
    _forget()
    return {"stopped": started, "detached": held and not started}


@tool
def game_status() -> dict:
    """The host's status (frame count, paused, disc, CD track, event numbers), plus whether the
    process is alive and where the run directory is."""
    if _ctl is None:
        return {"running": False, "held": False}
    answer: dict = {"held": True, "run_dir": str(_run_dir) if _run_dir else None}
    if _game is not None:
        code = _game.process.poll()
        answer["running"] = code is None
        if code is not None:
            answer["exit_code"] = code
            return answer
    answer["channel"] = _channel().status()
    answer.setdefault("running", True)
    return answer


# ---- input and time ----


@tool
def game_key(name: str, action: str = "tap", frames: int | None = None) -> dict:
    """Press a game key by name (ESC, RETURN, UP, SPACE, CTRL, ...). action is tap (default),
    down or up; a tap is held for `frames` presented frames (default about 150 ms)."""
    return _channel().key(name, action, frames)


@tool
def game_wait(frames: int | None = None, ms: int | None = None) -> dict:
    """Wait for that many presented frames or host milliseconds (the first to pass)."""
    return _channel().wait(frames=frames, ms=ms)


@tool
def game_wait_until(
    cond: str,
    value: int | str | None = None,
    addr: int | str | None = None,
    op: str = "==",
    size: int = 4,
    path: str | None = None,
    since: int | None = None,
    ok: bool | None = None,
    timeout_ms: int | None = None,
    timeout_frames: int | None = None,
) -> dict:
    """Wait until a condition holds; an error if it does not within the timeout (60 s when none).

    cond "opened": a file whose guest path contains `path` is opened (since: count opens after that
    event number, the "seq" of an earlier answer; ok: require success or failure).
    cond "mem": guest memory at addr (size bytes) `op` value, op one of == != < > & .
    cond "disc": the game is on disc `value`. cond "cd_track": the CD plays track `value`.
    cond "frame": the presented frame count reaches `value`."""
    ctl = _channel()
    timeouts = {"timeout_ms": timeout_ms, "timeout_frames": timeout_frames}
    if cond == "opened":
        if not path:
            raise wdctl.CtlError("cond opened needs path")
        return ctl.wait_until_opened(path, ok=ok, since=since, **timeouts)
    if cond == "mem":
        if addr is None or value is None:
            raise wdctl.CtlError("cond mem needs addr and value")
        return ctl.wait_until_mem(_addr(addr), op, _addr(value), size, **timeouts)
    if cond in ("disc", "cd_track", "frame"):
        if value is None:
            raise wdctl.CtlError(f"cond {cond} needs value")
        return ctl.wait_until(cond, value=_addr(value), **timeouts)
    raise wdctl.CtlError("cond must be opened, mem, disc, cd_track or frame")


@tool
def game_pause() -> dict:
    """Hold the guest still (the host keeps answering)."""
    return _channel().pause()


@tool
def game_resume() -> dict:
    """Let a paused guest run again."""
    return _channel().resume()


@tool
def game_step(frames: int = 1) -> dict:
    """Let that many frames be presented, then pause."""
    return _channel().step(frames)


# ---- memory and state ----


@tool
def game_read(addr: int | str, size: int) -> dict:
    """Read `size` bytes of guest memory at addr; returns them as a hex string."""
    return {"addr": _addr(addr), "hex": _channel().read(_addr(addr), size).hex()}


@tool
def game_read_cstr(addr: int | str) -> dict:
    """Read the NUL-terminated string at addr."""
    return {"text": _channel().read_cstr(_addr(addr))}


@tool
def game_write(addr: int | str, hex: str) -> dict:
    """Write bytes (a hex string, e.g. "01000000") to guest memory at addr."""
    return _channel().write(_addr(addr), bytes.fromhex(hex.replace(" ", "")))


@tool
def game_project() -> dict:
    """The project record the game is in (name, level, first OBJET), or {"project": null} before
    it is set (wdctl.Ctl.current_project)."""
    project = _channel().current_project()
    return project if project is not None else {"project": None}


@tool
def game_log(since: int = 0) -> dict:
    """Events after number `since`: file opens, CD and disc changes."""
    return _channel().log(since)


# ---- audio ----


@tool
def game_audio_dump(path: str) -> dict:
    """Write the mixer's output from now on to a WAV file at path, until game_audio_dump_stop."""
    return _channel().audio_dump(path)


@tool
def game_audio_dump_stop() -> dict:
    """Stop the audio dump and close the WAV file."""
    return _channel().audio_dump_stop()


# ---- seeing the game ----


@tool
def game_stderr(tail: int = 50) -> dict:
    """The last `tail` lines of the run's stderr.txt (the host's log)."""
    if _run_dir is None:
        raise wdctl.CtlError("no run directory known: game_attach by run_dir, or game_start")
    log = _run_dir / "stderr.txt"
    if not log.is_file():
        raise wdctl.CtlError(f"{log} does not exist")
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    return {"path": str(log), "lines": lines[-tail:] if tail > 0 else []}


@tool
def game_screenshot() -> list:
    """The next presented frame, saved under the run directory as a PNG and returned as an image
    (with its path). While paused, the host steps one frame to have one."""
    global _shots
    ctl = _channel()
    folder = (
        _run_dir or wdctl.WINDREAM.parents[1] / "out" / "recomp" / "windream" / "mcp-shots"
    ) / "mcp-shots"
    _shots += 1
    raw = folder / f"shot-{_shots:04d}.bmp"
    ctl.screenshot(raw)
    data = raw.read_bytes()
    if data[:8] == b"\x89PNG\r\n\x1a\n":  # the direct renderer writes a PNG whatever the name says
        png = raw.with_suffix(".png")
        raw.replace(png)
    else:
        try:
            from PIL import Image as PilImage
        except ImportError:
            raise wdctl.CtlError(
                "game_screenshot needs Pillow to convert the frame to PNG: "
                f"start the server with `uv run --with mcp --with pillow ...` (raw frame: {raw})"
            ) from None
        png = raw.with_suffix(".png")
        with PilImage.open(raw) as frame:
            frame.save(png, "PNG")
        raw.unlink()
    return [Image(path=png), f"saved {png}"]


if __name__ == "__main__":
    mcp.run()
