"""The browser build of the host (recomp/windream, build.py --web): configuration checks.

Nothing here builds or runs the game: the Emscripten build takes minutes and
needs the machine-local tools (out/recomp/web-tools/tools.json, see
recomp/web-env.ps1). The checks keep the pieces the page depends on from
disappearing: the link options of the module contract (recomp/web/CONTRACT.md),
the frame relay's two halves, and the tool environment. The real run is
`recomp/web/browser_check.py --real`.
"""

import importlib.util
import re
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
WINDREAM = ROOT / "recomp" / "windream"


def _load_web_build():
    sys.path.insert(0, str(ROOT / "recomp"))
    sys.path.insert(0, str(WINDREAM))
    spec = importlib.util.spec_from_file_location("web_build_t", WINDREAM / "web_build.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def test_cmake_link_options_follow_the_contract():
    text = (WINDREAM / "CMakeLists.txt").read_text()
    for needed in (
        "-sMODULARIZE=1",
        "-sEXPORT_NAME=createDreams",
        "-sPROXY_TO_PTHREAD",
        "-sOFFSCREENCANVAS_SUPPORT",
        "-lidbfs.js",
        "--wrap=SDL_GL_SwapWindow",
        "FS,ENV,GL,addRunDependency,removeRunDependency",
        "OUTPUT_NAME dreams",
    ):
        assert needed in text, needed
    assert "set(_wd_launcher_default OFF)" in text


def test_frame_relay_halves_agree():
    """The guest thread posts the handler that the page-side script defines."""
    glue = (WINDREAM / "host" / "web" / "web_glue.c").read_text()
    pre = (WINDREAM / "host" / "web" / "pre.js").read_text()
    assert "handler: 'dreamsFrame'" in glue
    # The message number is the SDK's, read by CMake, not a literal in the glue.
    assert "cmd: cmd" in glue and "WD_WEB_CALL_HANDLER" in glue
    cmake = (WINDREAM / "CMakeLists.txt").read_text()
    assert "libpthread.js" in cmake and "WD_WEB_CALL_HANDLER=${WD_WEB_CALL_HANDLER}" in cmake
    assert "Module['dreamsFrame']" in pre
    assert "__wrap_SDL_GL_SwapWindow" in glue
    assert "bitmaprenderer" in pre


def test_sdk_has_the_relay_message():
    """The number CMake reads is where the pattern in CMakeLists.txt looks for it."""
    web_build = _load_web_build()
    if not web_build.tools_config().is_file():
        pytest.skip("no browser tools (out/recomp/web-tools/tools.json)")
    emsdk = Path(web_build.load_tools()["emsdk"])
    text = (emsdk / "upstream" / "emscripten" / "src" / "lib" / "libpthread.js").read_text()
    assert re.search(r"^const CMD_CALL_HANDLER = \d+", text, re.M)


def test_web_flag_in_build_py():
    text = (WINDREAM / "build.py").read_text()
    for flag in ("--web", "--web-opt", "--web-gen", "--web-exe", "--web-release"):
        assert flag in text
    # The deployable build leaves the verification capture out, explicitly
    # (a cached ON must not survive into a release).
    assert "capture=not args.web_release" in text
    assert "-DWD_WEB_CAPTURE=" in (WINDREAM / "web_build.py").read_text()


def test_web_env_finds_the_toolchain():
    web_build = _load_web_build()
    cfg = web_build.tools_config()
    if not cfg.is_file():
        pytest.skip("no browser tools (out/recomp/web-tools/tools.json)")
    tools = web_build.load_tools()
    env = web_build.web_env(tools)
    assert shutil.which("emcc", path=env["PATH"])
    assert shutil.which("cmake", path=env["PATH"])
    assert (Path(env["SDL3_DIR"]) / "SDL3Config.cmake").is_file()
