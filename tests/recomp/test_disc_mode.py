"""Disc mode: the host reads the game from two discs (host/sdl/files.c) and
run.py's options for it.

The native part builds recomp/windream/verify/native/disc_mode_tests.c with the
production file bridges and runs it on two tiny "discs" (extracted directories,
which the disc library opens like an image) under a folder with non-ASCII
characters in its name. It needs clang-cl and the shared SDL3 build, and is
skipped without them. What it cannot reach: a real .cue (tests/recomp/test_disc.py
covers the library) and the game itself (a run.py --discs run).
"""

import importlib.util
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SDL_SYSTEM_LIBRARIES = (
    "user32", "gdi32", "winmm", "imm32", "ole32", "oleaut32", "version",
    "uuid", "advapi32", "setupapi", "shell32", "dinput8", "cfgmgr32",
)  # fmt: skip


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


recomp_env = load("recomp/recomp_env.py")
run = load("recomp/windream/run.py")


# ---- run.py ----


def folder(tmp_path, name, cues):
    tree = tmp_path / name / "extracted"
    tree.mkdir(parents=True)
    for cue in cues:
        (tree.parent / cue).write_text("")
    return tree


def test_find_cue_takes_the_one_cue_beside_the_extracted_tree(tmp_path):
    tree = folder(tmp_path, "one", ["Game (Disc 1).CUE"])
    (tree.parent / "Game (Disc 1) (Track 01).bin").write_text("")
    assert run.find_cue(tree) == tree.parent / "Game (Disc 1).CUE"


@pytest.mark.parametrize("cues", [[], ["a.cue", "b.cue"]])
def test_find_cue_refuses_none_or_several(tmp_path, cues):
    with pytest.raises(ValueError, match="exactly one .cue"):
        run.find_cue(folder(tmp_path, "bad", cues))


def test_disc_sources(tmp_path, monkeypatch):
    assert run.disc_sources(False, None, None) is None
    monkeypatch.chdir(tmp_path)  # given paths become absolute: the host runs elsewhere
    assert run.disc_sources(False, "a.iso", "b") == (str(tmp_path / "a.iso"), str(tmp_path / "b"))
    for bad in [(False, "a.cue", None), (False, None, "b.cue"), (True, "a.cue", "b.cue")]:
        with pytest.raises(ValueError):
            run.disc_sources(*bad)
    trees = {n: folder(tmp_path, f"disc{n}", [f"d{n}.cue"]) for n in (1, 2)}
    monkeypatch.setattr(run.paths, "disc", lambda n: trees[n])
    assert run.disc_sources(True, None, None) == (
        str(tmp_path / "disc1" / "d1.cue"),
        str(tmp_path / "disc2" / "d2.cue"),
    )


# ---- the file bridges ----


@pytest.fixture(scope="session")
def harness():
    env = recomp_env.build_env() if sys.platform == "win32" else {}
    compiler = shutil.which("clang-cl", path=env.get("PATH"))
    if not compiler:
        pytest.skip("needs clang-cl (Windows)")
    out = recomp_env.out_dir("disc-mode")
    sdl = recomp_env.ensure_sdl3()
    host = recomp_env.HOST
    sources = [host / "sdl" / f"{name}.c" for name in ("files", "kernel", "threads")]
    sources += [path for path, _ in recomp_env.vm_sources(host, "ledger")]
    sources += recomp_env.disc_sources()
    sources.append(ROOT / "recomp/windream/verify/native/disc_mode_tests.c")
    common = [compiler, "/nologo", "/Od", "/MD", "/w", "/D_CRT_SECURE_NO_WARNINGS"]
    common += [*recomp_env.host_includes(), f"/I{sdl / 'include'}"]
    exe = out / "disc_mode_tests.exe"
    steps = [
        # runtime.c's own main is not this program's
        [*common, "/c", "/Dmain=wd_unused_main", f"/Fo{out}/", str(host / "core" / "runtime.c")],
        [*common, f"/Fe{exe}", f"/Fo{out}/", *map(str, sources), str(out / "runtime.obj"),
         "/link", f"/LIBPATH:{sdl / 'lib'}", "SDL3-static.lib",
         *(name + ".lib" for name in SDL_SYSTEM_LIBRARIES)],
    ]  # fmt: skip
    for step in steps:
        p = subprocess.run(step, cwd=out, env=env, capture_output=True, text=True)
        assert p.returncode == 0, p.stdout + p.stderr
    return exe


def put(root, files):
    for name, data in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def discs(base):
    big = bytes((i * 7 + 3) & 255 for i in range(70000))
    put(base / "disco 1", {
        "DATA/1CD.ID": b"", "DATA/HD.ID": b"", "DATA/FULL.ID": b"",
        "DATA/3DC/DIALOG.DRD": b"dialog", "DATA/3DC/A.DSN": b"one-a", "DATA/3DC/ONLY1.DSN": b"1",
        "DREAMS.DAT": b"bank1", "GDIDREAM.EXE": b"MZ-one", "BIG.BIN": big,
    })  # fmt: skip
    put(base / "disco 2", {
        "DATA/2CD.ID": b"", "DATA/HD.ID": b"", "DATA/3DC/A.DSN": b"two-a", "DATA/3DC/B.DSN": b"2",
        "DATA/GAME/GAME.DAT": b"stale", "DATA/GAME/GAME0.DAT": b"old save", "DREAMS.DAT": b"bank2",
    })  # fmt: skip
    return base / "disco 1", base / "disco 2"


def start(harness, cwd, **variables):
    import os

    env = {k: v for k, v in os.environ.items() if not k.startswith("WD_")}
    env.update({k: str(v) for k, v in variables.items()})
    p = subprocess.run([str(harness)], cwd=cwd, env=env, capture_output=True)
    return p.returncode, p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace")


def test_file_bridges_in_disc_mode(harness, tmp_path):
    # Non-ASCII in every path: the host must take them from the environment as UTF-8.
    base = tmp_path / "jogo-ção"
    disc1, disc2 = discs(base)
    data = base / "dados-usuário"
    code, out, err = start(harness, tmp_path, WD_DISC1=disc1, WD_DISC2=disc2, WD_DATA_DIR=data)
    assert code == 0, out + err
    assert "passed" in out
    assert err.count("[disc] active disc") == 3
    assert "[disc] active disc 1 -> 2" in err and "[disc] active disc 2 -> 1" in err
    assert 'open R "DATA\\1CD.ID" -> disc1:DATA/1CD.ID' in err
    # The data directory holds what was written and nothing a disc has.
    written = {p.relative_to(data).as_posix().lower(): p.read_bytes() for p in data.rglob("*")
               if p.is_file()}  # fmt: skip
    assert written == {
        "cryo/dreams/data/full.id": b"x",
        "cryo/dreams/data/game/game.dat": b"mine",
        "dreams.dat": b"bank2!",
    }
    assert not (tmp_path / "sandbox").exists()
    # The discs are read-only.
    assert (disc2 / "DREAMS.DAT").read_bytes() == b"bank2"


def test_disc_mode_needs_both_discs_and_the_right_ones(harness, tmp_path):
    disc1, disc2 = discs(tmp_path)
    for variables, message in [
        ({"WD_DISC1": disc1}, "WD_DISC2 is not"),
        ({"WD_DISC1": disc1, "WD_DISC2": tmp_path / "nope.cue"}, "WD_DISC2: cannot open"),
        ({"WD_DISC1": disc2, "WD_DISC2": disc1}, "is not disc 1"),
        ({"WD_DISC1": disc1, "WD_DISC2": disc1}, "is not disc 2"),
    ]:
        code, out, err = start(harness, tmp_path, **variables)
        assert code != 0 and "FATAL" in err and message in err, err
