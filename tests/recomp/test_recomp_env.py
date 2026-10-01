"""Two hosts share one DREAMS_OUT: the Windows names stay, the others get a suffix."""

import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


def load(relative):
    spec = importlib.util.spec_from_file_location(Path(relative).stem, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


recomp_env = load("recomp/recomp_env.py")
OUT = Path("out")


def names(monkeypatch, platform):
    monkeypatch.setattr(recomp_env.sys, "platform", platform)
    return [
        recomp_env.build_dir(OUT).name,
        recomp_env.build_dir(OUT, trace=True).name,
        recomp_env.build_dir(OUT, render_audit=True).name,
        recomp_env.build_dir(OUT, vm="shadow").name,
    ]


def test_windows_build_directories_keep_their_names(monkeypatch):
    assert names(monkeypatch, "win32") == ["build", "build-trace", "build-audit", "build-vm-shadow"]
    assert recomp_env.exe_name("windream_recomp") == "windream_recomp.exe"


@pytest.mark.parametrize("platform", ["linux", "darwin"])
def test_other_hosts_append_their_platform(monkeypatch, platform):
    assert names(monkeypatch, platform) == [
        f"build-{platform}",
        f"build-trace-{platform}",
        f"build-audit-{platform}",
        f"build-vm-shadow-{platform}",
    ]
    assert recomp_env.exe_name("windream_recomp") == "windream_recomp"


def vm_files(tmp_path, vm):
    return [path.name for path, _ in recomp_env.vm_sources(tmp_path, vm)]


def test_ledger_stands_on_the_host_os_layer(monkeypatch, tmp_path):
    monkeypatch.setattr(recomp_env.sys, "platform", "win32")
    assert vm_files(tmp_path, "ledger") == ["vm_front.c", "vm_ledger.c", "vm_os_win32.c"]
    assert "vm_win32.c" in vm_files(tmp_path, "shadow")
    monkeypatch.setattr(recomp_env.sys, "platform", "linux")
    assert vm_files(tmp_path, "ledger") == ["vm_front.c", "vm_ledger.c", "vm_os_posix.c"]


@pytest.mark.parametrize("vm", ["win32", "shadow"])
def test_windows_only_vm_is_refused_elsewhere(monkeypatch, tmp_path, vm):
    monkeypatch.setattr(recomp_env.sys, "platform", "linux")
    with pytest.raises(ValueError, match="Windows only"):
        recomp_env.vm_sources(tmp_path, vm)
