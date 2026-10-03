"""The release script's checks, and main's decision to run the launcher.

The decision tests start the built windream_recomp (uv run python
recomp/windream/build.py; skipped when it is not there) only as far as an
error: nothing here needs the discs or starts the game. They run against the
development build, which honours the launcher's test-only variables; the last
tests run the release build (release.py; skipped when it is not there) and check
that it has none of the development code and ignores those variables.
"""

import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = load("recomp/windream/release.py")
recomp_env = release.recomp_env


def test_release_build_directory(monkeypatch):
    out = Path("out")
    monkeypatch.setattr(recomp_env.sys, "platform", "win32")
    assert recomp_env.build_dir(out, release=True).name == "build-release"
    assert recomp_env.build_dir(out).name == "build"
    monkeypatch.setattr(recomp_env.sys, "platform", "linux")
    assert recomp_env.build_dir(out, release=True).name == "build-release-linux"


def test_foreign_dlls():
    system = ["KERNEL32.dll", "user32.DLL", "d3d11.dll", "dbghelp.dll", "WINMM.dll"]
    assert release.foreign_dlls(system) == []
    runtime = ["VCRUNTIME140.dll", "MSVCP140.dll", "api-ms-win-crt-heap-l1-1-0.dll", "SDL3.dll"]
    assert release.foreign_dlls(system + runtime) == sorted(runtime, key=str.lower)


def test_unexpected_files(tmp_path):
    expected = {"DreamsToReality.exe", "README.txt"}
    for name in expected:
        (tmp_path / name).write_text("x")
    assert release.unexpected_files(tmp_path, expected) == []
    (tmp_path / "dreams.ini").write_text("x")
    (tmp_path / "userdata").mkdir()
    (tmp_path / "userdata" / "log.txt").write_text("x")
    assert release.unexpected_files(tmp_path, expected) == [
        "dreams.ini",
        "userdata",
        "userdata/log.txt",
    ]


def test_each_release_check_looks_for_its_own_feature(tmp_path):
    exe, cache = tmp_path / "game.exe", tmp_path / "CMakeCache.txt"
    exe.write_bytes(b"MZ wd-devtools 1")
    cache.write_text("WD_DEVTOOLS:BOOL=OFF\nDREAMS_LAUNCHER_TESTING:BOOL=OFF\n")
    (problem,) = release.devtools_problems(exe, cache)
    assert "wd-devtools" in problem
    assert (
        release.launcher_testing_problems(exe, cache) == []
    )  # each check looks for its own feature


def test_release_refuses_the_launcher_testing_code(tmp_path):
    """The launcher's test script and test-only variables (DREAMS_LAUNCHER_TESTING)."""
    exe, cache = tmp_path / "game.exe", tmp_path / "CMakeCache.txt"
    exe.write_bytes(b"MZ plain host")
    cache.write_text("WD_RELEASE:BOOL=ON\nDREAMS_LAUNCHER_TESTING:BOOL=OFF\n")
    assert release.launcher_testing_problems(exe, cache) == []
    exe.write_bytes(b"MZ launcher-testing 1, host built")
    (marker,) = release.launcher_testing_problems(exe, cache)
    assert "launcher-testing" in marker
    exe.write_bytes(b"MZ")
    for text in ("DREAMS_LAUNCHER_TESTING:BOOL=ON\n", "WD_RELEASE:BOOL=ON\n"):  # ON, or never set
        cache.write_text(text)
        (option,) = release.launcher_testing_problems(exe, cache)
        assert "DREAMS_LAUNCHER_TESTING" in option
    exe.write_bytes(b"MZ wd-devtools launcher-testing")
    cache.write_text("DREAMS_LAUNCHER_TESTING:BOOL=ON\nWD_DEVTOOLS:BOOL=ON\n")
    assert len(release.launcher_testing_problems(exe, cache)) == 2


def test_the_launcher_demo_holds_the_marker():
    """The check's own premise: the text is in an executable built with the option on."""
    build = recomp_env.out_dir("launcher") / ("build" + recomp_env.platform_suffix())
    exe = build / recomp_env.exe_name("launcher_demo")
    cache = build / "CMakeCache.txt"
    if not exe.is_file() or "DREAMS_LAUNCHER_TESTING:BOOL=ON" not in cache.read_text():
        pytest.skip("no launcher build with DREAMS_LAUNCHER_TESTING on")
    assert len(release.launcher_testing_problems(exe, cache)) == 2  # the text, and the option on


def test_readme_names_what_the_user_needs():
    for words in (".cue", "--play", "dreams.ini", "userdata", "log.txt", "DreamsToReality.exe"):
        assert words in release.README


def test_readme_and_file_names_carry_the_version(monkeypatch):
    assert "Version v0.1.0\n" in release.README.format(version="v0.1.0")
    monkeypatch.setattr(release.sys, "platform", "win32")
    assert release.release_stem("v0.1.0") == "DreamsToReality-v0.1.0-windows-x64"
    assert release.release_stem("v0.1.0-3-gabc1234").endswith("-3-gabc1234-windows-x64")
    for bad in ("", "v0.1 beta", 'v1"', "../v1"):
        with pytest.raises(ValueError):
            release.release_stem(bad)


@pytest.fixture(scope="module")
def host():
    build = recomp_env.build_dir(recomp_env.out_dir("windream"))
    exe = build / recomp_env.exe_name("windream_recomp")
    if not exe.is_file():
        pytest.skip(f"{exe} is not built; run: uv run python recomp/windream/build.py")
    return exe


def start(host, tmp_path, *args, **env):
    """(exit code, stderr) of the host started in tmp_path, the launcher's files there too."""
    clean = {k: v for k, v in os.environ.items() if not k.upper().startswith("WD_")}
    p = subprocess.run(
        [str(host), *args],
        cwd=tmp_path,
        env=dict(clean, DREAMS_LAUNCHER_HOME=str(tmp_path / "home"), **env),
        capture_output=True,
        timeout=120,
    )
    return p.returncode, p.stderr.decode("utf-8", "replace")


def test_no_exe_and_no_disc_runs_the_launcher(host, tmp_path):
    missing = tmp_path / "não há.cue"
    code, err = start(host, tmp_path, "--play", "--disc1", missing, "--disc2", missing)
    assert code == 2
    assert err.startswith("launcher: disc 1:")
    assert "não há.cue" in err  # argv reaches the launcher as UTF-8
    assert "[launcher]" not in err and "FATAL" not in err


def test_an_exe_path_skips_the_launcher(host, tmp_path):
    code, err = start(host, tmp_path, tmp_path / "não há.exe", "--run", "--play")
    assert code == 1
    assert "launcher" not in err
    assert "FATAL: cannot open" in err and "não há.exe" in err


def test_wd_disc1_skips_the_launcher(host, tmp_path):
    code, err = start(host, tmp_path, "--play", WD_DISC1="nowhere.cue", WD_DISC2="nowhere.cue")
    assert code == 1
    assert "launcher" not in err
    assert "FATAL: WD_DISC1: cannot open nowhere.cue" in err


@pytest.mark.skipif(sys.platform != "win32", reason="the legacy EXE path is opened as UTF-8")
def test_exe_path_with_non_ascii_characters_is_read(host, tmp_path):
    folder = tmp_path / "dados-ção"
    folder.mkdir()
    (folder / "x.exe").write_bytes(b"MZ")
    code, err = start(host, tmp_path, folder / "x.exe")
    assert code == 1
    assert "is not a Windows executable" in err  # opened and read, then refused


@pytest.fixture(scope="module")
def release_build():
    """(executable, CMake cache) of the release build that release.py made."""
    build = recomp_env.build_dir(recomp_env.out_dir("windream"), release=True)
    exe = build / recomp_env.exe_name(release.NAME)
    if not exe.is_file():
        pytest.skip(
            f"{exe} is not built; run: uv run --with pefile python recomp/windream/release.py"
        )
    return exe, build / "CMakeCache.txt"


def test_the_release_build_has_no_development_code(release_build):
    exe, cache = release_build
    assert release.devtools_problems(exe, cache) == []
    assert release.launcher_testing_problems(exe, cache) == []


def test_the_release_executable_ignores_the_launcher_test_variables(release_build, tmp_path):
    """The release is driven as a user drives it: --play, --disc1/--disc2, --data, dreams.ini
    beside the executable. DREAMS_LAUNCHER_HOME, a test-only variable, is not read there: the
    development build (above) honours it, the release must not."""
    exe, _ = release_build
    app = tmp_path / "app"
    app.mkdir()
    shutil.copy2(exe, app / exe.name)
    home = tmp_path / "home"
    clean = {
        k: v for k, v in os.environ.items() if not k.upper().startswith(("WD_", "DREAMS_LAUNCHER_"))
    }
    missing = tmp_path / "não há.cue"
    p = subprocess.run(
        [str(app / exe.name), "--play", "--disc1", str(missing), "--disc2", str(missing),
         "--data", str(tmp_path / "data")],
        cwd=app,
        env=dict(clean, WD_HEADLESS="1", DREAMS_LAUNCHER_HOME=str(home)),
        capture_output=True,
        timeout=120,
    )  # fmt: skip
    err = p.stderr.decode("utf-8", "replace")
    assert p.returncode == 2
    assert err.startswith("launcher: disc 1:") and "não há.cue" in err
    assert not home.exists()  # DREAMS_LAUNCHER_HOME would have created it
