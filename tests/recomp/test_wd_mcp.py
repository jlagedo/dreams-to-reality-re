"""The MCP server over the development control channel (recomp/windream/debug/wd_mcp.py).

The server is started as a subprocess and driven over stdio with the MCP SDK's
own client. Run with: uv run --with mcp --with pillow pytest tests/recomp/test_wd_mcp.py
Skipped when the `mcp` package is not importable.

Two parts: the tool list and the error path need no game; the live part starts
ONE short headless disc-mode game (tag `mcp-test`, its own run directory,
removed afterwards), asks for its status and a screenshot, and stops it. It is
skipped when the development build, its WD_DEVTOOLS option, the discs or Pillow
are missing.
"""

import asyncio
import importlib.util
import json
import os
import shutil
import sys
from pathlib import Path

import pytest

mcp = pytest.importorskip("mcp")
from mcp import ClientSession, StdioServerParameters  # noqa: E402
from mcp.client.stdio import stdio_client  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "recomp" / "windream" / "debug" / "wd_mcp.py"
TAG = "mcp-test"

TOOLS = {
    "game_start", "game_attach", "game_stop", "game_status", "game_key", "game_type", "game_mouse",
    "game_wait", "game_wait_until", "game_read", "game_read_cstr", "game_write", "game_project",
    "game_log", "game_pause", "game_resume", "game_step", "game_audio_dump", "game_audio_dump_stop",
    "game_stderr", "game_screenshot",
}  # fmt: skip


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


run = load("recomp/windream/run.py")
recomp_env = run.recomp_env


def session(body):
    """Run `body(client_session)` against a fresh server process; its result."""

    async def go():
        # The server runs with the caller's environment, as it does under Claude Code: the
        # SDK's default environment has no DISPLAY, and a Linux host then has no video device.
        params = StdioServerParameters(
            command=sys.executable, args=[str(SERVER)], cwd=str(ROOT), env=dict(os.environ)
        )
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as client:
                await client.initialize()
                return await body(client)

    return asyncio.run(asyncio.wait_for(go(), timeout=150))


def failed(result):
    """CallToolResult.is_error (mcp 2.x) or .isError (1.x)."""
    return result.is_error if hasattr(result, "is_error") else result.isError


def mime(image):
    return image.mime_type if hasattr(image, "mime_type") else image.mimeType


def schema_of(tool):
    return tool.input_schema if hasattr(tool, "input_schema") else tool.inputSchema


def text_of(result):
    return " ".join(c.text for c in result.content if c.type == "text")


def test_tools_are_listed_and_answer_without_a_game():
    async def body(client):
        tools = {t.name: t for t in (await client.list_tools()).tools}
        assert set(tools) == TOOLS
        assert all(t.description for t in tools.values())
        status = await client.call_tool("game_status", {})
        assert not failed(status)
        assert "false" in text_of(status).lower()  # running: false
        # A channel error comes back as a tool error carrying the channel's own message.
        read = await client.call_tool("game_read", {"addr": "0x661e04", "size": 4})
        assert failed(read)
        assert "no game" in text_of(read)
        stop = await client.call_tool("game_stop", {})
        assert not failed(stop)
        return tools

    tools = session(body)
    # Addresses accept ints and hex strings.
    schema = schema_of(tools["game_read"])["properties"]["addr"]
    kinds = {part.get("type") for part in schema.get("anyOf", [schema])}
    assert {"integer", "string"} <= kinds


def test_attach_to_nothing_is_a_tool_error(tmp_path):
    async def body(client):
        missing = await client.call_tool("game_attach", {})
        assert failed(missing)
        nowhere = await client.call_tool("game_attach", {"run_dir": str(tmp_path)})
        assert failed(nowhere)
        assert "no control channel" in text_of(nowhere)

    session(body)


def live_requirements():
    pytest.importorskip("PIL")
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    if "WD_DEVTOOLS:BOOL=ON" not in (build / "CMakeCache.txt").read_text(errors="replace"):
        pytest.skip(f"{build} was built without WD_DEVTOOLS")
    try:
        run.disc_sources(True, None, None)
    except Exception as error:  # no DREAMS_DISC1/2, or no single .cue beside them
        pytest.skip(f"no disc images: {error}")


def test_one_short_live_session():
    live_requirements()
    run_dir = recomp_env.out_dir("windream") / f"run-{TAG}"

    async def body(client):
        started = await client.call_tool(
            "game_start", {"tag": TAG, "discs": True, "headless": True}
        )
        assert not failed(started), text_of(started)
        try:
            info = json.loads(text_of(started))
            assert info.get("port", 0) > 0 and TAG in info.get("run_dir", "")
            again = await client.call_tool("game_start", {"tag": TAG})
            assert failed(again) and "already" in text_of(again)  # one game at a time

            status = await client.call_tool("game_status", {})
            assert not failed(status), text_of(status)
            assert "true" in text_of(status).lower()
            # no renderer was named: run.py's default (direct on Windows)
            assert f'"renderer": "{recomp_env.renderer_default()}"' in text_of(status)

            await client.call_tool("game_wait", {"frames": 5})
            shot = await client.call_tool("game_screenshot", {})
            assert not failed(shot), text_of(shot)
            images = [c for c in shot.content if c.type == "image"]
            assert images and mime(images[0]) == "image/png" and images[0].data
            assert ".png" in text_of(shot)
            stderr = await client.call_tool("game_stderr", {"tail": 5})
            assert not failed(stderr)
        finally:
            stopped = await client.call_tool("game_stop", {})
        assert not failed(stopped)
        status = await client.call_tool("game_status", {})
        assert "false" in text_of(status).lower()

    try:
        session(body)
    finally:
        shutil.rmtree(run_dir, ignore_errors=True)
