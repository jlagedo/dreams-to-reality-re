"""Retail-x86 oracle for the KERNEL32 host bridges (files, kernel, threads).

uv run --with unicorn --with capstone --with pefile python \
    recomp/windream/verify/kernel_bridge_smoke.py [--capture] [--case-walk] [--host TREE] \
    [--vm win32|ledger|shadow]

Unicorn runs original GDIDREAM.EXE code (the Watcom startup up to WinMain, then
runtime and game file functions) and small call stubs for the imports no
convenient retail caller reaches. Every import lands in the production bridge
C, built into a DLL with the production runtime setup; the guest arena is
mapped into Unicorn, so both sides see the same memory. Guest threads run on
the host thread the bridge creates, each in its own Unicorn instance.

A plain run compares its observations with kernel_bridge_baseline.json, next
to this script. That baseline was captured from the Win32 implementation of
files.c, kernel.c and threads.c (the commit before they moved to host/sdl,
built with --host from a worktree of it), so a pass says the SDL3 bridges give
original code the answers Win32 gave. --capture replaces the baseline with
this run's observations: only for a deliberate change of behaviour.
Observations named "host: ..." depended on the machine's ANSI code page under
Win32; they are reported, not compared.

Checked in place on every run, independent of the baseline: the argument count
each bridge pops against the Windows SDK import libraries, file times against
the fixture's own, local times against the machine's zone, and (for the sdl/
layout) the cases the Win32 bridges were never asked or got wrong: wildcard
patterns against FindFirstFileW on the same directory, over-long paths, a
conversion with no destination, a second open of a file being written, a
closed standard handle.

--case-walk makes files.c look every path segment up without regard to case,
the path a case-sensitive host takes; the observations must not change.
--host builds the bridges of another source tree (win32/ or sdl/ layout).
--vm picks the virtual memory implementation under the bridges (host/vm). The
baseline was captured from the Win32 one, so `--vm ledger` (or shadow) must
give the same observations.

Outputs: DREAMS_OUT/recomp/kernel-bridge (fixture tree, DLL, results.json).
Windows only, because of the DLL and the clang-cl build: the host DLL asks
vm_state for the committed ranges Unicorn maps.
"""

from __future__ import annotations

import argparse
import ctypes
import datetime
import hashlib
import itertools
import json
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

from unicorn import (
    UC_ARCH_X86,
    UC_HOOK_CODE,
    UC_HOOK_MEM_UNMAPPED,
    UC_MODE_32,
    UC_PROT_ALL,
    Uc,
    UcError,
)
from unicorn import x86_const as xr

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import recomp_env  # noqa: E402

from dreams import paths  # noqa: E402

sys.path.insert(0, str(paths.get("pcrecomp") / "tools" / "pe"))
from pe_analyze import analyze_pe, build_iat_map  # noqa: E402
from stdcall_argc import ArgcResolver  # noqa: E402

IMAGE = 0x400000
GATE = 0x00020000  # Unicorn-only page: one 4-byte slot per import
GDT = 0x00030000  # Unicorn-only page: segment descriptors
RETADDR = 0xDEAD0000  # RECOMP_RETADDR: where a finished guest call returns
CODE = 0x00110000  # thread procedures (kernel_bridge_host.c KB_CODE)
CODE_STEP = 0x100
STUBS = 0x00120000  # import call stubs, never reused (no stale translations)
STUBS_END = 0x001F0000
INVALID = 0xFFFFFFFF
BRIDGES = ("files", "kernel", "threads")
# What the static SDL3 needs on Windows (its sdl3.pc).
SDL_SYSTEM_LIBRARIES = (
    "user32", "gdi32", "winmm", "imm32", "ole32", "oleaut32", "version",
    "uuid", "advapi32", "setupapi", "shell32", "dinput8", "cfgmgr32",
)  # fmt: skip

# Imports whose result differs between runs or hosts for reasons that are not
# the bridge's: ids, the standard handles' type, host TLS indices.
VOLATILE = {"GetCurrentThreadId", "GetCurrentProcessId", "GetFileType", "TlsAlloc"}
# The guest's last-error value changes only where Win32 documents it: on these
# imports' failures (and always for the two whose success also reports whether
# the object existed). Elsewhere the value before the call is kept, so a
# bridge that calls into the host on a success path cannot leak its side
# effects into a later GetLastError.
ERROR_ON_FAILURE = {
    "FindFirstFileA": INVALID,
    "FindNextFileA": 0,
    "GetFileAttributesA": INVALID,
    "DeleteFileA": 0,
    "MoveFileA": 0,
    "MultiByteToWideChar": 0,
    "WideCharToMultiByte": 0,
    "DosDateTimeToFileTime": 0,
    "FileTimeToDosDateTime": 0,
    "ReadFile": 0,
    "WriteFile": 0,
    "SetFilePointer": INVALID,
}
ERROR_ALWAYS = {"CreateFileA", "CreateFileMappingA", "SetLastError", "GetLastError"}

FILE_ATTRIBUTE_DIRECTORY = 0x10
GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
CREATE_NEW, CREATE_ALWAYS, OPEN_EXISTING, OPEN_ALWAYS, TRUNCATE_EXISTING = 1, 2, 3, 4, 5
# Watcom <fcntl.h>
O_RDONLY, O_WRONLY, O_RDWR, O_APPEND, O_CREAT, O_TRUNC, O_BINARY, O_EXCL = (
    0, 1, 2, 0x10, 0x20, 0x40, 0x200, 0x400,
)  # fmt: skip

# The fixture: read root 0 (the guest's C:\, holding the pretend exe), a second
# read root, and files already in the write sandbox. mtime in Unix seconds.
ROOT_FILES = {
    "DATA/ALPHA.DAT": (bytes(range(256)) * 4, 866377844),  # 1997-06-15 12:30:44 UTC
    "DATA/BETA.DAT": (b"", 981173107),  # 2001-02-03 04:05:07 UTC, odd second
    "DATA/GAMMA.TXT": (b"gamma\r\n", 946684800),  # 2000-01-01 00:00:00 UTC
    "DATA/SUB/DELTA.DAT": (b"delta", 1000000000),
    "DOCS/b.txt": (b"bb", 915148800),
    "DOCS/A.TXT": (b"a", 915148802),
    "DOCS/c.TXT": (b"ccc", 915148804),
    "DOCS/NOEXT": (b"n", 915148806),
    "README.TXT": (b"read me\r\n", 852076800),
    # The markers the game's startup looks for (CD_InitPaths); contents are ours.
    "DATA/1CD.ID": (b"disc", 874000000),
    "CRYO/DREAMS/DATA/HD.ID": (b"cache", 874000002),
    "CRYO/DREAMS/DATA/FULL.ID": (b"full", 874000004),
}
ROOT2_FILES = {"EXTRA/SECOND.DAT": (b"second root", 900000000)}
# Names for the wildcard comparison with FindFirstFileW. All are valid 8.3
# names: NTFS also matches a pattern against the short name it makes up for any
# other, which the bridge does not have.
FUZZ_NAMES = (
    "A.TXT", "B.TXT", "C.TXT", "NOEXT", "X.Y", "LONGNAME.EXT", "TXT", "A", "Z.T",
    "GAME1.DAT", "GAME12.DAT", "GAME.DAT", "AB.C",
)  # fmt: skip
SANDBOX_FILES = {"DATA/OMEGA.DAT": (b"omega in the sandbox", 950000000)}
BASELINE = Path(__file__).with_name("kernel_bridge_baseline.json")


def filetime(unix_seconds: int) -> int:
    return (unix_seconds + 11644473600) * 10_000_000


def dos_stamp(unix_seconds: int) -> tuple[int, int]:
    """The DOS date and time Win32 gives a file time: local by the zone's
    current offset, rounded up to an even second."""
    local = unix_seconds + time.localtime().tm_gmtoff
    d = datetime.datetime.fromtimestamp(local + local % 2, datetime.UTC)
    date = ((d.year - 1980) << 9) | (d.month << 5) | d.day
    return date, (d.hour << 11) | (d.minute << 5) | (d.second // 2)


class GuestExit(Exception):
    pass


def subtract(span, others):
    """Parts of span (start, end) not covered by the sorted disjoint others."""
    start, end = span
    out = []
    for lo, hi in others:
        if hi <= start or lo >= end:
            continue
        if lo > start:
            out.append((start, lo))
        start = max(start, hi)
        if start >= end:
            break
    if start < end:
        out.append((start, end))
    return out


class Machine:
    """The bridge DLL, the shared arena and the import gate."""

    def __init__(self, dll, exe: Path):
        self.dll = dll
        self.arena = dll.kb_arena()
        self.kb = {dll.kb_import_name(i).decode(): i for i in range(dll.kb_import_count())}
        argc = ArgcResolver()
        self.gate = []
        self.slot = {}
        self.argc = {}
        for index, (va, (library, name)) in enumerate(
            sorted(build_iat_map(analyze_pe(str(exe))).items())
        ):
            self.gate.append((library, name))
            self.slot[name] = va
            self.argc[name] = argc.lookup(library, name)
            self.write(va, struct.pack("<I", GATE + 4 * index))
        self.thread_errors: list[str] = []
        self.lock = threading.Lock()
        self.callback = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint32))(self._thread)
        dll.kb_set_thread_callback(self.callback)

    def read(self, va, size):
        return ctypes.string_at(self.arena + va, size)

    def write(self, va, data):
        ctypes.memmove(self.arena + va, data, len(data))

    def u32(self, va):
        return struct.unpack("<I", self.read(va, 4))[0]

    def committed(self):
        buffer = (ctypes.c_uint32 * 8192)()
        count = self.dll.kb_committed(buffer, 4096)
        assert count <= 4096
        return [(buffer[i * 2], buffer[i * 2] + buffer[i * 2 + 1]) for i in range(count)]

    def _thread(self, regs):
        """A guest thread procedure, on the host thread the bridge started."""
        try:
            cpu = Cpu(self)
            cpu.set(eax=regs[0], esp=regs[1])
            cpu.set_fs(regs[2])
            cpu.run(regs[3], RETADDR)
            regs[0], regs[1] = cpu.reg("eax"), cpu.reg("esp")
        except BaseException as error:  # noqa: BLE001 - reported by the main thread
            with self.lock:
                self.thread_errors.append(repr(error))


REGS = {
    name: getattr(xr, "UC_X86_REG_" + name.upper())
    for name in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "eip")
}


class Cpu:
    """One guest thread: a Unicorn instance over the shared arena."""

    def __init__(self, machine: Machine):
        self.m = machine
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.mapped: list[tuple[int, int]] = []
        for page in (GATE, RETADDR):
            self.uc.mem_map(page, 0x1000)
            self.uc.mem_write(page, b"\x90" * 0x1000)
        self.uc.hook_add(UC_HOOK_CODE, self._gate, begin=GATE, end=GATE + 0xFFF)
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._unmapped)
        self.uc.reg_write(xr.UC_X86_REG_FPCW, 0x027F)
        self.uc.mem_map(GDT, 0x1000)
        self.pending = None
        self.trace: list[list] = []
        self.sync()

    def set_fs(self, base):
        """Flat ring-0 segments, and FS over the thread's TIB as Win32 has it."""

        def entry(base, limit, access, flags):
            return struct.pack(
                "<Q",
                (limit & 0xFFFF)
                | (base & 0xFFFFFF) << 16
                | access << 40
                | (limit >> 16 & 0xF) << 48
                | flags << 52
                | (base >> 24 & 0xFF) << 56,
            )

        table = bytes(8) + entry(0, 0xFFFFF, 0x9B, 0xC) + entry(0, 0xFFFFF, 0x93, 0xC)
        table += entry(base, 0xFFF, 0x93, 0x4)
        self.uc.mem_write(GDT, table)
        self.uc.reg_write(xr.UC_X86_REG_GDTR, (0, GDT, len(table) - 1, 0))
        self.uc.reg_write(xr.UC_X86_REG_CS, 0x08)
        for register in (xr.UC_X86_REG_SS, xr.UC_X86_REG_DS, xr.UC_X86_REG_ES):
            self.uc.reg_write(register, 0x10)
        self.uc.reg_write(xr.UC_X86_REG_FS, 0x18)

    def reg(self, name):
        return self.uc.reg_read(REGS[name])

    def set(self, **values):
        for name, value in values.items():
            self.uc.reg_write(REGS[name], value & 0xFFFFFFFF)

    def sync(self, unmap=False):
        """Map what the host has committed since the last look."""
        committed = self.m.committed()
        for span in committed:
            for start, end in subtract(span, self.mapped):
                self.uc.mem_map_ptr(start, end - start, UC_PROT_ALL, self.m.arena + start)
        if unmap:
            for span in self.mapped:
                for start, end in subtract(span, committed):
                    self.uc.mem_unmap(start, end - start)
        self.mapped = committed

    def _unmapped(self, uc, access, address, size, value, _):
        self.sync()
        return any(start <= address < end for start, end in self.mapped)

    def _gate(self, uc, address, size, _):
        self.pending = (address - GATE) // 4
        uc.emu_stop()

    def run(self, start, until):
        pc = start
        while pc != until:
            self.pending = None
            try:
                self.uc.emu_start(pc, until)
            except UcError as error:
                raise RuntimeError(f"{error} at eip {self.reg('eip'):08x}") from error
            if self.pending is None:
                return
            pc = self.do_import(self.pending)

    def do_import(self, index):
        library, name = self.m.gate[index]
        esp = self.reg("esp")
        back = self.m.u32(esp)
        if name == "ExitProcess":
            raise GuestExit(f"ExitProcess({self.m.u32(esp + 4)}) from {back:08x}")
        if name == "CharUpperBuffA":
            # USER32, not under test: the Watcom startup upper-cases with it.
            text, count = self.m.u32(esp + 4), self.m.u32(esp + 8)
            self.m.write(text, self.m.read(text, count).upper())
            self.set(eax=count, esp=esp + 12)
            return back
        if name not in self.m.kb:
            raise RuntimeError(f"{library}!{name} has no bridge here (called from {back:08x})")
        dll = self.m.dll
        regs = (ctypes.c_uint32 * 3)(self.reg("eax"), esp, 0)
        before = dll.kb_error()
        dll.kb_call(self.m.kb[name], regs)
        popped = (regs[1] - esp - 4) // 4
        if popped != self.m.argc[name]:
            raise RuntimeError(
                f"{name} popped {popped} arguments, the SDK says {self.m.argc[name]}"
            )
        eax = regs[0]
        failed = name in ERROR_ON_FAILURE and eax == ERROR_ON_FAILURE[name]
        if not (failed or name in ERROR_ALWAYS):
            dll.kb_set_error(before)
        self.set(eax=eax, esp=regs[1])
        self.trace.append([name, "*" if name in VOLATILE else eax])
        if name == "VirtualFree":
            self.sync(unmap=True)
        return back


class Guest:
    """The main guest thread and the helpers the scenarios call."""

    def __init__(self, machine: Machine, entry: int, symbols: dict[str, list[int]]):
        self.m = machine
        self.cpu = Cpu(machine)
        self.symbols = symbols
        self.entry = entry
        self.stub = STUBS
        self.stubs: dict[bytes, int] = {}
        self.scratch = self.scratch_end = 0
        self.esp0 = 0
        self.code = 0

    def va(self, name, which=0):
        return self.symbols[name][which]

    # ---- memory
    def alloc(self, size):
        size = (size + 3) & ~3
        assert self.scratch + size <= self.scratch_end, "scratch exhausted"
        self.scratch += size
        self.m.write(self.scratch - size, bytes(size))
        return self.scratch - size

    def put(self, data: bytes):
        va = self.alloc(len(data))
        self.m.write(va, data)
        return va

    def text(self, value: str):
        return self.put(value.encode("cp1252") + b"\0")

    def until(self, va, end: bytes, limit=4096):
        """Bytes up to a terminator, a page at a time: the next page may not exist."""
        data = b""
        while end not in data and len(data) < limit:
            at = va + len(data)
            data += self.m.read(at, 0x1000 - (at & 0xFFF))
        return data[: data.index(end)]

    def string(self, va, limit=4096):
        return self.until(va, b"\0", limit).decode("cp1252")

    # ---- calls
    def start(self):
        """The retail entry point, up to the call of WinMain."""
        self.cpu.set(esp=self.m.dll.kb_esp() - 4)
        self.m.write(self.cpu.reg("esp"), struct.pack("<I", RETADDR))
        self.cpu.set_fs(self.m.dll.kb_fs_base())
        self.cpu.trace = []
        self.cpu.run(self.entry, self.va("WinMain"))
        assert self.cpu.reg("eip") == self.va("WinMain"), f"stopped at {self.cpu.reg('eip'):08x}"
        self.esp0 = (self.cpu.reg("esp") - 0x4000) & ~0xF
        return self.cpu.trace

    def call(self, target, eax=0, edx=0, ebx=0, ecx=0, stack=()):
        """A retail function: Watcom register arguments, then the stack."""
        if isinstance(target, str):
            target = self.va(target)
        esp = self.esp0 - 4 * (len(stack) + 1)
        self.m.write(
            esp, struct.pack(f"<{len(stack) + 1}I", RETADDR, *(v & 0xFFFFFFFF for v in stack))
        )
        self.cpu.set(eax=eax, edx=edx, ebx=ebx, ecx=ecx, esp=esp)
        self.cpu.trace = []
        self.cpu.run(target, RETADDR)
        return self.cpu.reg("eax")

    def trace(self):
        return self.cpu.trace

    def imp(self, name, *args):
        """An import, through `push ...; call [slot]` executed by Unicorn."""
        code = b"".join(b"\x68" + struct.pack("<I", v & 0xFFFFFFFF) for v in reversed(args))
        code += b"\xff\x15" + struct.pack("<I", self.m.slot[name])
        start = self.stubs.get(code)
        if start is None:
            assert self.stub + len(code) < STUBS_END
            start, self.stub = self.stub, self.stub + len(code) + 1
            self.stubs[code] = start
            self.m.write(start, code)
        self.cpu.set(esp=self.esp0)
        self.cpu.trace = []
        self.cpu.run(start, start + len(code))
        assert self.cpu.reg("esp") == self.esp0, name
        return self.cpu.reg("eax")

    def error(self):
        return self.m.dll.kb_error()

    def thread_code(self, index, code: bytes):
        assert len(code) <= CODE_STEP
        self.m.write(CODE + index * CODE_STEP, code)
        return CODE + index * CODE_STEP


def build(output: Path, host: Path, exe: Path, vm: str = recomp_env.VM_DEFAULT):
    """The bridge DLL: runtime.c, the vm sources of one implementation and the
    three bridge files of `host`."""
    vm_files = recomp_env.vm_sources(host, vm)
    sources = [path for path, _defines in vm_files]
    for name in BRIDGES:
        found = [host / directory / f"{name}.c" for directory in ("win32", "sdl")]
        found = [path for path in found if path.is_file()]
        assert len(found) == 1, f"expected one {name}.c under {host}"
        sources.append(found[0])
    manual = set()
    for source in sources:
        manual |= set(re.findall(r"^void imp_(\w+)\(void\)", source.read_text(), re.M))
    pe = analyze_pe(str(exe))
    imports = sorted(build_iat_map(pe).items())
    rows = [(va, name) for va, (library, name) in imports if library.upper() == "KERNEL32.DLL"]
    missing = [name for _va, name in rows if name not in manual]
    lines = [f"#define KB_ENTRY 0x{IMAGE + pe.entry_point_rva:08X}u"]
    lines += [f"void imp_{name}(void);" for _va, name in rows if name in manual]
    lines += ["static const struct kb_import kb_imports[] = {"]
    lines += [
        f'    {{ 0x{va:08X}u, imp_{name}, "{name}" }},' for va, name in rows if name in manual
    ]
    lines += ["};", ""]
    (output / "kernel_bridge_imports.inc").write_text("\n".join(lines), encoding="utf-8")

    sdl = recomp_env.ensure_sdl3()
    environment = recomp_env.build_env()
    compiler = shutil.which("clang-cl", path=environment.get("PATH"))
    if not compiler:
        raise RuntimeError("clang-cl unavailable")
    # "win32" stays for a --host tree from before the vm directory.
    includes = [f"/I{host / d}" for d in ("core", "sdl", "win32", "vm", "render", "hooks")]
    includes += [f"/I{output}", f"/I{sdl / 'include'}", f"/I{recomp_env.DISC}"]
    common = [compiler, "/nologo", "/Od", "/MD", "/w", "/D_CRT_SECURE_NO_WARNINGS", *includes]
    library = output / "kernel_bridge.dll"
    steps = [
        # runtime.c's main becomes the setup call (arena, image, main thread).
        [*common, "/c", "/Dmain=wd_unused_main", f"/Fo{output}/", str(host / "core" / "runtime.c")],
    ]  # fmt: skip
    # A vm source with compile definitions is its own step; its object is linked.
    objects = []
    plain = []
    for path, defines in vm_files:
        if defines:
            steps.append([*common, "/c", *(f"/D{d}" for d in defines), f"/Fo{output}/", str(path)])
            objects.append(str(output / path.with_suffix(".obj").name))
        else:
            plain.append(str(path))
    others = [str(path) for path in sources[len(vm_files) :]]
    # files.c reads disc images through the disc library (a --host tree from
    # before that does not use it; linking it anyway is harmless).
    others += [str(path) for path in recomp_env.disc_sources()]
    steps.append(
        [*common, "/LD", f"/Fe{library}", f"/Fo{output}/",
         str(Path(__file__).parent / "native" / "kernel_bridge_host.c"), *plain, *others,
         str(output / "runtime.obj"), *objects, "/link", f"/LIBPATH:{sdl / 'lib'}",
         "SDL3-static.lib", *(lib + ".lib" for lib in SDL_SYSTEM_LIBRARIES)],
    )  # fmt: skip
    log = ""
    for step in steps:
        result = subprocess.run(step, cwd=output, env=environment, capture_output=True, text=True)
        log += result.stdout + result.stderr
        (output / "build.log").write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"bridge host build failed; see {output / 'build.log'}")
    dll = ctypes.CDLL(str(library))
    u32, ptr = ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)
    dll.kb_init.argtypes, dll.kb_init.restype = [ctypes.c_char_p, ctypes.c_char_p], ctypes.c_int
    dll.kb_arena.restype = ctypes.c_void_p
    dll.kb_call.argtypes, dll.kb_call.restype = [u32, ptr], None
    dll.kb_committed.argtypes, dll.kb_committed.restype = [ptr, u32], u32
    dll.kb_import_name.argtypes, dll.kb_import_name.restype = [u32], ctypes.c_char_p
    dll.kb_import_address.argtypes, dll.kb_import_address.restype = [u32], u32
    for name in ("kb_fs_base", "kb_esp", "kb_import_count", "kb_error"):
        getattr(dll, name).restype = u32
    dll.kb_set_error.argtypes, dll.kb_set_error.restype = [u32], None
    return dll, [str(s.relative_to(host)).replace("\\", "/") for s in sources], missing


def make_fixture(output: Path):
    fuzz = {f"FUZZ/{name}": (b"", 950000000) for name in FUZZ_NAMES}
    trees = {"root": {**ROOT_FILES, **fuzz}, "root2": ROOT2_FILES, "sandbox": SANDBOX_FILES}
    for name, files in trees.items():
        tree = output / name
        if tree.exists():
            shutil.rmtree(tree)
        for relative, (data, mtime) in files.items():
            path = tree / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            os.utime(path, (mtime, mtime))
    return {name: output / name for name in trees}


def tree_state(root: Path):
    return {
        str(path.relative_to(root)).replace("\\", "/"): hashlib.sha1(path.read_bytes()).hexdigest()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def load_symbols():
    symbols: dict[str, list[int]] = {}
    for line in (
        (paths.REPO_ROOT / "re" / "symbols" / "gdidream.exe.tsv")
        .read_text(encoding="utf-8", errors="replace")
        .splitlines()
    ):
        fields = line.split("\t")
        if len(fields) > 2 and fields[0] == "FUNC":
            symbols.setdefault(fields[2], []).append(IMAGE + int(fields[1], 16))
    return symbols


# --------------------------------------------------------------------------
# Scenarios. Each returns the observations to compare; anything that must hold
# on every host (independent of the baseline) is asserted in place.


def names(trace):
    return [
        f"{name}={value:#x}" if isinstance(value, int) else f"{name}={value}"
        for name, value in trace
    ]


def scenario_startup(g: Guest):
    trace = g.start()
    return {"imports": names(trace)}


def scenario_process(g: Guest):
    out = {}
    out["GetVersion"] = g.imp("GetVersion")
    out["GetACP"] = g.imp("GetACP")
    out["GetOEMCP"] = g.imp("GetOEMCP")
    info = g.alloc(24)
    g.m.write(info, b"\xcc" * 24)
    out["GetCPInfo"] = [g.imp("GetCPInfo", 1252, info), g.m.read(info, 24).hex()]
    out["GetCommandLineA"] = g.string(g.imp("GetCommandLineA"))
    wide = g.imp("GetCommandLineW")
    out["GetCommandLineW"] = g.m.read(wide, 32).decode("utf-16-le").split("\0")[0]
    block = g.until(g.imp("GetEnvironmentStrings"), b"\0\0")
    out["GetEnvironmentStrings"] = block.decode("ascii").split("\0")
    out["GetEnvironmentStrings again"] = g.imp("GetEnvironmentStrings") == g.imp(
        "GetEnvironmentStrings"
    )
    out["SetEnvironmentVariableA"] = g.imp("SetEnvironmentVariableA", g.text("X"), g.text("1"))
    out["GetModuleHandleA(NULL)"] = g.imp("GetModuleHandleA", 0)
    out["GetModuleHandleA(name)"] = g.imp("GetModuleHandleA", g.text("KERNEL32"))
    for label, capacity in (("GetModuleFileNameA", 260), ("GetModuleFileNameA small", 6)):
        buffer = g.alloc(264)
        g.m.write(buffer, b"\xcc" * 264)
        result = g.imp("GetModuleFileNameA", 0, buffer, capacity)
        out[label] = [result, g.m.read(buffer, 24).hex()]
    buffer = g.alloc(128)
    result = g.imp("GetModuleFileNameW", 0, buffer, 64)
    out["GetModuleFileNameW"] = [result, g.m.read(buffer, 64).decode("utf-16-le").split("\0")[0]]
    out["LoadLibraryA"] = g.imp("LoadLibraryA", g.text("NOSUCH.DLL"))
    out["GetProcAddress"] = g.imp("GetProcAddress", 0, g.text("Nothing"))
    g.imp("SetLastError", 1234)
    out["SetLastError/GetLastError"] = g.imp("GetLastError")
    g.imp("GetACP")
    out["GetLastError survives a call"] = g.imp("GetLastError")
    out["SetUnhandledExceptionFilter"] = g.imp("SetUnhandledExceptionFilter", 0x401000)
    out["UnhandledExceptionFilter"] = g.imp("UnhandledExceptionFilter", 0)
    counter = g.alloc(16)
    first = [
        g.imp("QueryPerformanceCounter", counter),
        struct.unpack("<q", g.m.read(counter, 8))[0],
    ]
    time.sleep(0.02)
    second = [
        g.imp("QueryPerformanceCounter", counter),
        struct.unpack("<q", g.m.read(counter, 8))[0],
    ]
    out["QueryPerformanceCounter"] = [first[0], second[0], second[1] > first[1] > 0]
    handles = [g.imp("GetStdHandle", which & 0xFFFFFFFF) for which in (-10, -11, -12)]
    out["GetStdHandle"] = handles
    out["GetStdHandle again"] = [
        g.imp("GetStdHandle", w & 0xFFFFFFFF) for w in (-10, -11, -12)
    ] == handles
    out["SetStdHandle"] = g.imp("SetStdHandle", -11 & 0xFFFFFFFF, handles[1])
    mode = g.alloc(4)
    out["GetConsoleMode"] = g.imp("GetConsoleMode", handles[0], mode)
    out["SetConsoleMode"] = g.imp("SetConsoleMode", handles[0], 0)
    out["SetConsoleCtrlHandler"] = g.imp("SetConsoleCtrlHandler", 0, 1)
    count = g.put(struct.pack("<I", 0xCCCCCCCC))
    out["PeekConsoleInputA"] = [
        g.imp("PeekConsoleInputA", handles[0], g.alloc(32), 1, count),
        g.m.u32(count),
    ]
    g.m.write(count, struct.pack("<I", 0xCCCCCCCC))
    out["ReadConsoleInputA"] = [
        g.imp("ReadConsoleInputA", handles[0], g.alloc(32), 1, count),
        g.m.u32(count),
    ]
    message = g.text("[guest console] kernel_bridge_smoke\n")
    out["WriteConsoleA"] = [
        g.imp("WriteConsoleA", handles[1], message, 36, count, 0),
        g.m.u32(count),
    ]
    out["GetCurrentThread"] = g.imp("GetCurrentThread")
    out["GetCurrentProcessId is ours"] = g.imp("GetCurrentProcessId") == os.getpid()
    out["GetCurrentThreadId is ours"] = g.imp("GetCurrentThreadId") == threading.get_native_id()
    began = time.perf_counter()
    g.imp("Sleep", 40)
    out["Sleep(40) took at least 35 ms"] = time.perf_counter() - began >= 0.035
    return out


def convert(g: Guest, name, page, flags, source: bytes, count, capacity, *extra):
    src = g.put(source)
    dst = g.alloc(96)
    g.m.write(dst, b"\xcc" * 96)
    g.imp("SetLastError", 0)
    result = g.imp(name, page, flags, src, count & 0xFFFFFFFF, dst if capacity is not None else 0,
                   capacity or 0, *extra)  # fmt: skip
    return [result, g.error() if not result else 0, g.m.read(dst, 48).hex()]


def scenario_text(g: Guest):
    out = {}
    sample = "Héllo €ž".encode("cp1252") + b"\0"
    mb = "MultiByteToWideChar"
    out["mb 1252 nul"] = convert(g, mb, 1252, 0, sample, -1, 32)
    out["mb 1252 counted"] = convert(g, mb, 1252, 0, sample, 5, 32)
    out["mb 1252 sizing"] = convert(g, mb, 1252, 0, sample, -1, None)
    out["mb 1252 too small"] = convert(g, mb, 1252, 0, sample, -1, 3)
    out["host: mb CP_ACP"] = convert(g, mb, 0, 0, sample, -1, 32)
    out["host: mb CP_OEMCP"] = convert(
        g, mb, 1, 0, bytes([0x80, 0x9B, 0xE1, 0xFE, 0x41, 0]), -1, 32
    )
    out["mb 437"] = convert(g, mb, 437, 0, bytes(range(0x80, 0x98)) + b"\0", -1, 32)
    out["mb 1252 undefined bytes"] = convert(
        g, mb, 1252, 0, bytes([0x81, 0x8D, 0x8F, 0x90, 0x9D, 0]), -1, 32
    )
    out["mb precomposed flag"] = convert(g, mb, 1252, 1, sample, -1, 32)
    out["mb empty"] = convert(g, mb, 1252, 0, b"\0", 0, 32)
    out["mb unknown page"] = convert(g, mb, 12345, 0, sample, -1, 32)
    wide = "Héllo €ž\0".encode("utf-16-le")
    wc = "WideCharToMultiByte"
    used = g.put(struct.pack("<I", 0xCCCCCCCC))
    out["wc 1252 nul"] = convert(g, wc, 1252, 0, wide, -1, 32, 0, used) + [g.m.u32(used)]
    out["wc 1252 counted"] = convert(g, wc, 1252, 0, wide, 5, 32, 0, 0)
    out["wc 1252 sizing"] = convert(g, wc, 1252, 0, wide, -1, None, 0, 0)
    out["wc 1252 too small"] = convert(g, wc, 1252, 0, wide, -1, 3, 0, 0)
    out["host: wc CP_ACP"] = convert(g, wc, 0, 0, wide, -1, 32, 0, 0)
    out["wc 437"] = convert(g, wc, 437, 0, "Çüéâ ß■A\0".encode("utf-16-le"), -1, 32, 0, 0)
    unmapped = "A\u4e2dB\0".encode("utf-16-le")  # no code page 1252 character, no best fit
    g.m.write(used, struct.pack("<I", 0xCCCCCCCC))
    out["wc default char"] = convert(g, wc, 1252, 0, unmapped, -1, 32, 0, used) + [g.m.u32(used)]
    out["wc custom default"] = convert(g, wc, 1252, 0, unmapped, -1, 32, g.text("#"), 0)
    out["wc empty"] = convert(g, wc, 1252, 0, b"\0\0", 0, 32, 0, 0)
    out["wc unknown page"] = convert(g, wc, 12345, 0, wide, -1, 32, 0, 0)
    return out


def scenario_time(g: Guest):
    out = {}
    stamp = g.alloc(8)

    def dos(year, month, day, hour, minute, second):
        return ((year - 1980) << 9) | (month << 5) | day, (hour << 11) | (minute << 5) | (
            second // 2
        )

    for label, fields in (
        ("1997-06-15 13:45:58", (1997, 6, 15, 13, 45, 58)),
        ("1980-01-01 00:00:00", (1980, 1, 1, 0, 0, 0)),
        ("2000-02-29 23:59:58", (2000, 2, 29, 23, 59, 58)),
        ("2107-12-31 23:59:58", (2107, 12, 31, 23, 59, 58)),
        ("month 13", (1997, 13, 1, 0, 0, 0)),
        ("day 0", (1997, 6, 0, 0, 0, 0)),
        ("hour 24", (1997, 6, 15, 24, 0, 0)),
        ("february 30", (1997, 2, 30, 0, 0, 0)),
    ):
        date, clock = dos(*fields)
        g.m.write(stamp, b"\xcc" * 8)
        result = g.imp("DosDateTimeToFileTime", date, clock, stamp)
        out["dos->file " + label] = [result, g.m.read(stamp, 8).hex()]
    words = g.alloc(8)
    for label, value in (
        ("1997-06-15 12:30:44", filetime(866377844)),
        ("odd second", filetime(981173107)),
        ("odd second + 100 ns", filetime(981173107) + 1),
        ("even second + 1 s - 100 ns", filetime(981173108) + 9_999_999),
        ("1980-01-01", filetime(315532800)),
        ("1979-12-31 23:59:59", filetime(315532799)),
        ("2107-12-31 23:59:58", filetime(4354819198)),
        ("2108-01-01", filetime(4354819200)),
        ("zero", 0),
    ):
        g.m.write(stamp, struct.pack("<Q", value))
        g.m.write(words, b"\xcc" * 8)
        result = g.imp("FileTimeToDosDateTime", stamp, words, words + 4)
        out["file->dos " + label] = [result, g.m.read(words, 2).hex(), g.m.read(words + 4, 2).hex()]
    local = g.alloc(8)
    for label, value in (
        ("summer 1997", filetime(866377844)),
        ("winter 2001", filetime(981173107)),
    ):
        g.m.write(stamp, struct.pack("<Q", value))
        result = g.imp("FileTimeToLocalFileTime", stamp, local)
        shifted = struct.unpack("<Q", g.m.read(local, 8))[0]
        back = g.alloc(8)
        result2 = g.imp("LocalFileTimeToFileTime", local, back)
        assert struct.unpack("<Q", g.m.read(back, 8))[0] == value, label
        # Win32 applies the zone's current bias to every date.
        bias = -(time.localtime().tm_gmtoff) * 10_000_000
        assert shifted == value - bias, (label, shifted, value, bias)
        out["file->local " + label] = [result, result2]
    return out


def read_find(g: Guest, data, times):
    attributes, _created, _accessed, written, high, low = struct.unpack(
        "<IQQQII", g.m.read(data, 36)
    )
    name = g.string(data + 44, 260)
    entry = {"name": name, "attributes": attributes, "size": (high << 32) | low,
             "short": g.string(data + 304, 14)}  # fmt: skip
    if name in times:
        assert written == filetime(times[name]), (name, written)
        entry["written"] = written
    return entry


def find_all(g: Guest, pattern, times=()):
    data = g.alloc(320)
    g.imp("SetLastError", 0)
    handle = g.imp("FindFirstFileA", g.text(pattern), data)
    if handle == INVALID:
        return {"handle": handle, "error": g.error()}
    entries = [read_find(g, data, times)]
    while g.imp("FindNextFileA", handle, data):
        entries.append(read_find(g, data, times))
        assert len(entries) < 64
    return {"entries": entries, "end error": g.error(), "close": g.imp("FindClose", handle)}


def create(g: Guest, name, access, disposition, flags=0x80):
    g.imp("SetLastError", 0xAAAA)
    handle = g.imp("CreateFileA", g.text(name), access, 0, 0, disposition, flags, 0)
    return handle, g.error()


def scenario_files(g: Guest, fixture):
    out = {}
    count = g.alloc(4)
    buffer = g.alloc(4096)

    def read(handle, size):
        g.m.write(count, struct.pack("<I", 0xCCCCCCCC))
        g.m.write(buffer, b"\xcc" * 64)
        ok = g.imp("ReadFile", handle, buffer, size, count, 0)
        got = g.m.u32(count)
        return [
            ok,
            got,
            hashlib.sha1(g.m.read(buffer, max(got, 16) if got < 4096 else 16)).hexdigest(),
        ]

    def write(handle, data: bytes):
        g.m.write(count, struct.pack("<I", 0xCCCCCCCC))
        return [g.imp("WriteFile", handle, g.put(data), len(data), count, 0), g.m.u32(count)]

    def seek(handle, distance, method, high=None):
        g.imp("SetLastError", 0xAAAA)
        slot = g.put(struct.pack("<i", high)) if high is not None else 0
        result = g.imp("SetFilePointer", handle, distance & 0xFFFFFFFF, slot, method)
        return [result, g.m.u32(slot) if slot else None]

    # Reading from the read roots: case, separators, drive letters.
    handle, error = create(g, "DATA\\ALPHA.DAT", GENERIC_READ, OPEN_EXISTING)
    out["open read"] = [handle != INVALID, error]
    out["GetFileType"] = g.imp("GetFileType", handle)
    out["read 100"] = read(handle, 100)
    out["read 0"] = read(handle, 0)
    out["seek end"] = seek(handle, 0, 2)
    out["read at end"] = read(handle, 16)
    out["seek back 24"] = seek(handle, -24, 2)
    out["read short"] = read(handle, 100)
    out["seek current"] = seek(handle, -1000, 1)
    out["seek set 500"] = seek(handle, 500, 0)
    out["seek current 0"] = seek(handle, 0, 1)
    out["seek before start"] = seek(handle, -1, 0) + [g.error()]
    out["seek high"] = seek(handle, 16, 0, 0)
    out["seek past end"] = seek(handle, 5000, 0)
    out["read past end"] = read(handle, 16)
    g.imp("SetLastError", 0xAAAA)
    out["write to a read handle"] = [write(handle, b"no")[0], g.error()]
    out["FlushFileBuffers"] = g.imp("FlushFileBuffers", handle)
    out["close"] = g.imp("CloseHandle", handle)
    out["close twice"] = g.imp("CloseHandle", handle)
    out["read closed"] = g.imp("ReadFile", handle, buffer, 4, count, 0)
    for label, name in (
        ("lower case", "data\\alpha.dat"),
        ("forward slashes", "DATA/ALPHA.DAT"),
        ("drive letter", "D:\\DATA\\ALPHA.DAT"),
        ("dot segments", "DATA\\.\\SUB\\..\\ALPHA.DAT"),
        ("second root", "EXTRA\\SECOND.DAT"),
        ("sandbox first", "DATA\\OMEGA.DAT"),
    ):
        handle, error = create(g, name, GENERIC_READ, OPEN_EXISTING)
        out["open " + label] = [handle != INVALID, error, read(handle, 8)]
        g.imp("CloseHandle", handle)
    out["open missing file"] = create(g, "DATA\\MISSING.DAT", GENERIC_READ, OPEN_EXISTING)
    out["open missing directory"] = create(g, "NODIR\\MISSING.DAT", GENERIC_READ, OPEN_EXISTING)
    out["open a directory"] = create(g, "DATA", GENERIC_READ, OPEN_EXISTING)

    # Writing goes to the sandbox; the read roots stay as they are.
    handle, error = create(g, "SAVE\\NEW.DAT", GENERIC_WRITE, CREATE_ALWAYS)
    out["create always, new"] = [handle != INVALID, error]
    out["write"] = write(handle, b"0123456789")
    g.imp("SetLastError", 0xAAAA)
    out["read from a write handle"] = [g.imp("ReadFile", handle, buffer, 4, count, 0), g.error()]
    out["seek 4"] = seek(handle, 4, 0)
    out["overwrite"] = write(handle, b"ab")
    out["seek past the written end"] = seek(handle, 20, 0)
    out["write past end"] = write(handle, b"Z")
    out["flush"] = g.imp("FlushFileBuffers", handle)
    g.imp("CloseHandle", handle)
    handle, error = create(g, "SAVE\\NEW.DAT", GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING)
    out["reopen read/write"] = [handle != INVALID, error, read(handle, 64)]
    out["append"] = [seek(handle, 0, 2), write(handle, b"!")]
    g.imp("CloseHandle", handle)
    out["create new, exists"] = create(g, "SAVE\\NEW.DAT", GENERIC_WRITE, CREATE_NEW)
    handle, error = create(g, "SAVE\\NEW2.DAT", GENERIC_WRITE, CREATE_NEW)
    out["create new"] = [handle != INVALID, error, write(handle, b"new2")]
    g.imp("CloseHandle", handle)
    handle, error = create(g, "SAVE\\NEW.DAT", GENERIC_WRITE, CREATE_ALWAYS)
    out["create always, exists"] = [handle != INVALID, error]
    g.imp("CloseHandle", handle)
    for label, name in (
        ("open always, exists", "SAVE\\NEW2.DAT"),
        ("open always, new", "SAVE\\NEW3.DAT"),
    ):
        handle, error = create(g, name, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS)
        out[label] = [handle != INVALID, error, read(handle, 16)]
        g.imp("CloseHandle", handle)
    out["truncate missing"] = create(g, "SAVE\\NONE.DAT", GENERIC_WRITE, TRUNCATE_EXISTING)
    handle, error = create(g, "SAVE\\NEW2.DAT", GENERIC_WRITE, TRUNCATE_EXISTING)
    out["truncate"] = [handle != INVALID, error]
    g.imp("CloseHandle", handle)
    # Opening a read-root file for writing copies it into the sandbox first.
    handle, error = create(g, "DATA\\GAMMA.TXT", GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING)
    out["write a root file"] = [handle != INVALID, error, read(handle, 16), seek(handle, 0, 0),
                                write(handle, b"GAMMA")]  # fmt: skip
    g.imp("CloseHandle", handle)
    handle, error = create(g, "DATA\\GAMMA.TXT", GENERIC_READ, OPEN_EXISTING)
    out["read it back"] = read(handle, 16)
    g.imp("CloseHandle", handle)
    handle, error = create(g, "DATA\\GAME7.DAT", GENERIC_WRITE, CREATE_ALWAYS)
    out["save file"] = [handle != INVALID, error, write(handle, b"save")]
    g.imp("CloseHandle", handle)

    attributes = "GetFileAttributesA"
    for label, name in (
        ("file", "DATA\\ALPHA.DAT"),
        ("directory", "DATA"),
        ("root", "C:\\"),
        ("sandbox file", "SAVE\\NEW.DAT"),
        ("missing file", "DATA\\MISSING.DAT"),
        ("missing directory", "NODIR\\MISSING.DAT"),
    ):
        g.imp("SetLastError", 0xAAAA)
        result = g.imp(attributes, g.text(name))
        out["attributes " + label] = [result, g.error() if result == INVALID else 0]

    def failing(name, *args):
        g.imp("SetLastError", 0xAAAA)
        result = g.imp(name, *args)
        return [result, g.error() if not result else 0]

    out["delete"] = failing("DeleteFileA", g.text("SAVE\\NEW3.DAT"))
    out["delete missing"] = failing("DeleteFileA", g.text("SAVE\\NEW3.DAT"))
    out["delete root file"] = failing("DeleteFileA", g.text("DATA\\ALPHA.DAT"))
    out["delete directory"] = failing("DeleteFileA", g.text("SAVE"))
    out["move"] = failing("MoveFileA", g.text("SAVE\\NEW2.DAT"), g.text("SAVE\\MOVED.DAT"))
    out["move onto existing"] = failing(
        "MoveFileA", g.text("SAVE\\MOVED.DAT"), g.text("SAVE\\NEW.DAT")
    )
    out["move missing"] = failing("MoveFileA", g.text("SAVE\\NONE.DAT"), g.text("SAVE\\X.DAT"))
    out["move a root file"] = failing(
        "MoveFileA", g.text("DATA\\BETA.DAT"), g.text("SAVE\\BETA.DAT")
    )
    out["move to a new directory"] = failing(
        "MoveFileA", g.text("SAVE\\BETA.DAT"), g.text("DEEP\\ER\\B.DAT")
    )

    # Directory state as the guest names it.
    path = g.alloc(264)

    def current():
        g.m.write(path, b"\xcc" * 264)
        result = g.imp("GetCurrentDirectoryA", 260, path)
        return [result, g.string(path)]

    def full(name, capacity=260):
        part = g.put(struct.pack("<I", 0xCCCCCCCC))
        g.m.write(path, b"\xcc" * 264)
        result = g.imp("GetFullPathNameA", g.text(name), capacity, path, part)
        offset = g.m.u32(part) - path if g.m.u32(part) != 0xCCCCCCCC else None
        return [result, g.m.read(path, 32).hex(), offset]

    out["current directory"] = current()
    out["full path"] = full("DATA\\..\\DOCS\\.\\A.TXT")
    out["full path small"] = full("DATA\\ALPHA.DAT", 8)
    out["full path exact"] = full("DATA\\ALPHA.DAT", 18)
    out["full path of root"] = full("\\")
    out["chdir"] = g.imp("SetCurrentDirectoryA", g.text("C:\\DATA"))
    out["current directory after chdir"] = current()
    handle, error = create(g, "ALPHA.DAT", GENERIC_READ, OPEN_EXISTING)
    out["open relative"] = [handle != INVALID, error]
    g.imp("CloseHandle", handle)
    handle, error = create(g, "..\\README.TXT", GENERIC_READ, OPEN_EXISTING)
    out["open parent relative"] = [handle != INVALID, error]
    g.imp("CloseHandle", handle)
    out["full path relative"] = full("SUB\\DELTA.DAT")
    out["full path rooted"] = full("\\DOCS\\A.TXT")
    out["attributes relative"] = g.imp(attributes, g.text("SUB"))
    out["chdir relative"] = [g.imp("SetCurrentDirectoryA", g.text("SUB")), current()]
    out["chdir up"] = [g.imp("SetCurrentDirectoryA", g.text("..\\..")), current()]
    out["chdir missing"] = [g.imp("SetCurrentDirectoryA", g.text("NOWHERE")), current()]
    out["chdir root"] = [g.imp("SetCurrentDirectoryA", g.text("\\")), current()]

    times = {
        Path(name).name: mtime
        for name, (_data, mtime) in {**ROOT_FILES, **ROOT2_FILES, **SANDBOX_FILES}.items()
    }
    del times["BETA.DAT"], times["GAMMA.TXT"]  # moved and rewritten above
    for label, pattern in (
        ("root only, *.*", "DOCS\\*.*"),
        ("root only, *", "DOCS\\*"),
        ("extension", "DOCS\\*.TXT"),
        ("lower-case pattern", "docs\\*.txt"),
        ("question mark", "DOCS\\?.TXT"),
        ("exact name", "DOCS\\A.TXT"),
        ("exact name, other case", "docs\\a.txt"),
        ("a directory by name", "DATA\\SUB"),
        ("subdirectory", "DATA\\SUB\\*.*"),
        ("sandbox wins", "DATA\\*.DAT"),
        ("sandbox and roots", "DATA\\*.*"),
        ("second root", "EXTRA\\*.DAT"),
        ("root directory", "*.*"),
        ("no match", "DOCS\\*.XYZ"),
        ("missing directory", "NODIR\\*.*"),
        ("no extension", "DOCS\\NOEXT"),
        ("star dot", "DOCS\\*."),
        ("prefix", "DOCS\\NO*"),
        ("question mark at the end of the name", "DOCS\\A?.TXT"),
        ("two question marks", "DOCS\\??.TXT"),
        ("question mark in the extension", "DOCS\\*.T?T"),
        ("shorter extension", "DOCS\\*.TX"),
        ("longer extension pattern", "DOCS\\*.TXT?"),
        ("star inside", "DOCS\\*O*"),
        ("trailing dot", "DOCS\\NOEXT."),
        ("no extension, any extension", "DOCS\\NOEXT.*"),
        ("star suffix", "DOCS\\*T"),
        ("star then literal extension", "DOCS\\*.t*"),
        ("question marks only", "DOCS\\?????"),
        ("many question marks", "DOCS\\????????.???"),
    ):
        out["find " + label] = find_all(g, pattern, times)
    out["FindNextFileA invalid"] = g.imp("FindNextFileA", 0x4444, g.alloc(320))
    out["FindClose invalid"] = g.imp("FindClose", 0x4444)

    name = g.text(f"kernel_bridge_smoke {os.getpid()}")
    g.imp("SetLastError", 0xAAAA)
    first = g.imp("CreateFileMappingA", INVALID, 0, 4, 0, 32, name)
    out["mapping"] = [first != 0, g.error()]
    second = g.imp("CreateFileMappingA", INVALID, 0, 4, 0, 32, name)
    out["mapping again"] = [second != 0, g.error()]
    out["mapping close"] = [g.imp("CloseHandle", first), g.imp("CloseHandle", second)]
    third = g.imp("CreateFileMappingA", INVALID, 0, 4, 0, 32, name)
    out["mapping after close"] = [third != 0, g.error()]
    unnamed = g.imp("CreateFileMappingA", INVALID, 0, 4, 0, 32, 0)
    out["mapping unnamed"] = [unnamed != 0, g.error()]
    g.imp("CloseHandle", third)
    g.imp("CloseHandle", unnamed)
    out["trees"] = {name: tree_state(root) for name, root in fixture.items()}
    return out


def scenario_runtime(g: Guest, fixture):
    """The retail Watcom runtime and game file functions over the bridges."""
    out = {}
    block = g.call("malloc_", eax=0x300000)
    out["malloc 3 MB"] = [block != 0, names(g.trace())]
    g.m.write(block + 0x2FFFF0, b"0123456789ABCDEF")
    g.call("free_", eax=block)
    out["free"] = names(g.trace())
    small = g.call("malloc_", eax=64)
    out["malloc 64"] = [small != 0, names(g.trace())]

    def opened(name, flags, mode=0):
        fd = g.call("open_", stack=(g.text(name), flags, mode))
        return fd, names(g.trace())

    buffer = g.alloc(2048)
    fd, trace = opened("DATA\\ALPHA.DAT", O_RDONLY | O_BINARY)
    out["open_"] = [fd, trace]
    got = g.call("read_", eax=fd, edx=buffer, ebx=300)
    out["read_"] = [got, hashlib.sha1(g.m.read(buffer, 300)).hexdigest(), names(g.trace())]
    out["lseek_"] = [g.call("lseek_", eax=fd, edx=-4, ebx=2), names(g.trace())]
    out["tell_"] = [g.call("tell_", eax=fd), names(g.trace())]
    out["read_ short"] = [g.call("read_", eax=fd, edx=buffer, ebx=300), names(g.trace())]
    out["read_ at end"] = [g.call("read_", eax=fd, edx=buffer, ebx=300), names(g.trace())]
    out["filelength_"] = [g.call("filelength_", eax=fd), names(g.trace())]
    out["close_"] = [g.call("close_", eax=fd), names(g.trace())]
    out["open_ missing"] = opened("DATA\\MISSING.DAT", O_RDONLY | O_BINARY)
    out["open_ missing directory"] = opened("NODIR\\MISSING.DAT", O_RDONLY | O_BINARY)
    fd, trace = opened("RT\\OUT.BIN", O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0x180)
    out["open_ create"] = [fd, trace]
    out["write_"] = [
        g.call("write_", eax=fd, edx=g.put(b"runtime write\n"), ebx=14),
        names(g.trace()),
    ]
    out["close_ written"] = [g.call("close_", eax=fd), names(g.trace())]
    out["open_ exclusive, exists"] = opened(
        "RT\\OUT.BIN", O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0x180
    )
    fd, trace = opened("RT\\OUT.BIN", O_RDWR | O_APPEND | O_BINARY)
    out["open_ append"] = [fd, trace]
    out["write_ append"] = [g.call("write_", eax=fd, edx=g.put(b"more"), ebx=4), names(g.trace())]
    out["lseek_ start"] = g.call("lseek_", eax=fd, edx=0, ebx=0)
    out["read_ back"] = [g.call("read_", eax=fd, edx=buffer, ebx=64), g.m.read(buffer, 18).hex()]
    g.call("close_", eax=fd)
    fd, trace = opened("RT\\TEXT.TXT", O_WRONLY | O_CREAT | O_TRUNC, 0x180)  # text mode
    out["write_ text mode"] = [
        g.call("write_", eax=fd, edx=g.put(b"a\nb\n"), ebx=4),
        names(g.trace()),
    ]
    g.call("close_", eax=fd)

    stream = g.call("fopen_", eax=g.text("DATA\\ALPHA.DAT"), edx=g.text("rb"))
    out["fopen_"] = [stream != 0, names(g.trace())]
    got = g.call("fread_", eax=buffer, edx=1, ebx=600, ecx=stream)
    out["fread_"] = [got, hashlib.sha1(g.m.read(buffer, 600)).hexdigest(), names(g.trace())]
    out["fseek_"] = [g.call("fseek_", eax=stream, edx=1000, ebx=0), names(g.trace())]
    out["ftell_"] = [g.call("ftell_", eax=stream), names(g.trace())]
    out["fread_ short"] = [
        g.call("fread_", eax=buffer, edx=1, ebx=600, ecx=stream),
        names(g.trace()),
    ]
    out["fclose_"] = [g.call("fclose_", eax=stream), names(g.trace())]
    out["fopen_ missing"] = [
        g.call("fopen_", eax=g.text("NONE.DAT"), edx=g.text("rb")),
        names(g.trace()),
    ]
    stream = g.call("fopen_", eax=g.text("RT\\STREAM.BIN"), edx=g.text("wb"))
    out["fopen_ write"] = [stream != 0, names(g.trace())]
    out["fwrite_"] = [
        g.call("fwrite_", eax=g.put(bytes(range(200))), edx=4, ebx=50, ecx=stream),
        names(g.trace()),
    ]
    out["ftell_ written"] = g.call("ftell_", eax=stream)
    out["fclose_ written"] = [g.call("fclose_", eax=stream), names(g.trace())]

    find = g.alloc(512)
    times = {Path(name).name: mtime for name, (_data, mtime) in ROOT_FILES.items()}

    def dos_entry():
        raw = g.m.read(find, 512)
        attribute, clock, date, size = struct.unpack_from("<BHHI", raw, 21)
        name = raw[30:].split(b"\0")[0].decode("cp1252")
        if name in times:
            assert (date, clock) == dos_stamp(times[name]), (name, date, clock)
        return {"name": name, "attribute": attribute, "size": size}

    for label, pattern, attribute in (
        ("files", "DOCS\\*.*", 0),
        ("with directories", "DOCS\\*.*", 0x10),
        ("extension", "DOCS\\*.TXT", 0),
        ("none", "DOCS\\*.XYZ", 0),
    ):
        entries, traces = [], []
        result = g.call("_dos_findfirst_", eax=g.text(pattern), edx=attribute, ebx=find)
        traces.append(names(g.trace()))
        first = result
        while result == 0:
            entries.append(dos_entry())
            assert len(entries) < 32
            result = g.call("_dos_findnext_", eax=find)
            traces.append(names(g.trace()))
        closed = g.call("_dos_findclose_", eax=find) if first == 0 else None
        out["_dos_find " + label] = [first, entries, result, closed, traces, names(g.trace())]

    out["access_ file"] = [
        g.call("access_", eax=g.text("DATA\\ALPHA.DAT"), edx=0),
        names(g.trace()),
    ]
    out["access_ missing"] = [
        g.call("access_", eax=g.text("DATA\\NOPE.DAT"), edx=0),
        names(g.trace()),
    ]
    out["access_ write"] = [g.call("access_", eax=g.text("RT\\OUT.BIN"), edx=2), names(g.trace())]
    stream, renamed = g.text("RT\\STREAM.BIN"), g.text("RT\\RENAMED.BIN")
    out["rename_"] = [g.call("rename_", eax=stream, edx=renamed), names(g.trace())]
    out["rename_ missing"] = [
        g.call("rename_", eax=stream, edx=g.text("RT\\X.BIN")),
        names(g.trace()),
    ]
    cwd = g.alloc(264)
    out["getcwd_"] = [g.call("getcwd_", eax=cwd, edx=260) == cwd, g.string(cwd), names(g.trace())]
    # Two byte-identical runtime wrappers; the import each reaches says which is which.
    for which in range(len(g.symbols["chdir_"])):
        result = g.call(g.va("chdir_", which), eax=g.text("RT\\TEXT.TXT" if which else "DATA"))
        out[f"chdir_/unlink_ #{which}"] = [result, names(g.trace())]
    out["getcwd_ after"] = [g.string(g.call("getcwd_", eax=cwd, edx=260)), names(g.trace())]
    for which in range(len(g.symbols["chdir_"])):
        result = g.call(g.va("chdir_", which), eax=g.text("\\" if not which else "RT\\NEVER.TXT"))
        out[f"chdir_/unlink_ #{which} again"] = [result, names(g.trace())]
    out["unlink_"] = [g.call(g.va("chdir_", 1), eax=g.text("RT\\TEXT.TXT")), names(g.trace())]
    for variable in ("WD", "T_ALPHA", "T_BETA", "PATH"):
        value = g.call("getenv_", eax=g.text(variable))
        out["getenv_ " + variable] = g.string(value) if value else None

    out["FILE_Exists"] = [g.call("FILE_Exists", eax=g.text("DATA\\ALPHA.DAT")), names(g.trace())]
    out["FILE_Exists missing"] = [
        g.call("FILE_Exists", eax=g.text("DATA\\NOPE.DAT")),
        names(g.trace()),
    ]
    out["trees"] = {name: tree_state(root) for name, root in fixture.items()}
    return out


def scenario_sync(g: Guest):
    out = {}

    def wait(handle, timeout):
        return g.imp("WaitForSingleObject", handle, timeout & 0xFFFFFFFF)

    auto = g.imp("CreateEventA", 0, 0, 0, 0)
    out["auto-reset event"] = [
        auto != 0,
        wait(auto, 0),
        g.imp("SetEvent", auto),
        wait(auto, 0),
        wait(auto, 0),
    ]
    out["set twice, wait twice"] = [
        g.imp("SetEvent", auto),
        g.imp("SetEvent", auto),
        wait(auto, 0),
        wait(auto, 0),
    ]
    manual = g.imp("CreateEventA", 0, 1, 1, 0)
    out["manual-reset event, initially set"] = [
        manual != 0,
        wait(manual, 0),
        wait(manual, 0),
        wait(manual, -1),
    ]
    began = time.perf_counter()
    result = wait(auto, 60)
    out["wait times out"] = [result, time.perf_counter() - began >= 0.05]
    out["wait invalid"] = wait(0x4444, 0)
    out["set invalid"] = g.imp("SetEvent", 0x4444)
    out["wait on the current thread"] = wait(g.imp("GetCurrentThread"), 0)
    out["close event"] = [g.imp("CloseHandle", manual), g.imp("CloseHandle", manual)]
    out["close invalid"] = [
        g.imp("CloseHandle", 0),
        g.imp("CloseHandle", 0x4444),
        g.imp("CloseHandle", 0x1003),
    ]

    section = g.alloc(32)
    g.imp("InitializeCriticalSection", section)
    for _ in range(3):  # the owner may enter again
        g.imp("EnterCriticalSection", section)
    for _ in range(3):
        g.imp("LeaveCriticalSection", section)
    out["critical section re-entered"] = True

    index = g.imp("TlsAlloc")
    out["tls"] = [
        index != INVALID,
        g.imp("TlsGetValue", index),
        g.imp("TlsSetValue", index, 0x11111111),
        g.imp("TlsGetValue", index),
    ]
    other = g.imp("TlsAlloc")
    out["tls second slot"] = [
        other != index,
        g.imp("TlsGetValue", other),
        g.imp("TlsSetValue", other, 7),
        g.imp("TlsGetValue", index),
        g.imp("TlsGetValue", other),
    ]

    # worker(block): wait for the gate; `loops` times, inside the critical
    # section, read the counter, Sleep(1) and write it back incremented; then
    # leave a TLS value and the thread id in the block and signal `done`.
    #   block: +0 section, +4 counter, +8 done event, +12 loops, +16 tls index,
    #          +20 tls read back, +24 thread id, +28 gate event
    def call(name):
        return b"\xff\x15" + struct.pack("<I", g.m.slot[name])

    code = bytearray(b"\x56\x57\x53")  # push esi; push edi; push ebx
    code += b"\x8b\x74\x24\x10"  # mov esi, [esp+16]
    code += b"\x6a\xff\xff\x76\x1c" + call("WaitForSingleObject")  # (gate, INFINITE)
    code += b"\x8b\x5e\x0c"  # mov ebx, [esi+12]
    loop = len(code)
    code += b"\xff\x36" + call("EnterCriticalSection")
    code += b"\x8b\x7e\x04"  # mov edi, [esi+4]
    code += b"\x6a\x01" + call("Sleep")
    code += b"\x47\x89\x7e\x04"  # inc edi; mov [esi+4], edi
    code += b"\xff\x36" + call("LeaveCriticalSection")
    code += b"\x4b"  # dec ebx
    code += b"\x75" + struct.pack("<b", loop - (len(code) + 2))  # jnz loop
    code += b"\x68\x01\x00\xef\xbe\xff\x76\x10" + call("TlsSetValue")
    code += b"\xff\x76\x10" + call("TlsGetValue")
    code += b"\x89\x46\x14"  # mov [esi+20], eax
    code += call("GetCurrentThreadId")
    code += b"\x89\x46\x18"  # mov [esi+24], eax
    code += b"\xff\x76\x08" + call("SetEvent")
    code += b"\xb8\x34\x12\x00\x00\x5b\x5f\x5e\xc2\x04\x00"  # mov eax, 0x1234; pops; ret 4
    procedure = g.thread_code(0, bytes(code))

    gate = g.imp("CreateEventA", 0, 1, 0, 0)
    done = g.imp("CreateEventA", 0, 0, 0, 0)
    block = g.put(struct.pack("<8I", section, 0, done, 5, index, 0, 0, gate))
    identifier = g.alloc(4)
    thread = g.imp("CreateThread", 0, 0, procedure, block, 0, identifier)
    out["thread created"] = [thread != 0, g.m.u32(identifier) != 0]
    out["thread waits at the gate"] = wait(thread, 30)
    out["open the gate"] = g.imp("SetEvent", gate)
    out["thread signals done"] = wait(done, 20000)
    out["thread ends"] = [wait(thread, 20000), wait(thread, 0)]
    out["thread counter"] = g.m.u32(block + 4)
    out["thread tls"] = [g.m.u32(block + 20), g.imp("TlsGetValue", index)]
    out["thread id"] = [
        g.m.u32(block + 24) == g.m.u32(identifier),
        g.m.u32(block + 24) != g.imp("GetCurrentThreadId"),
    ]
    out["thread close"] = [g.imp("CloseHandle", thread), g.imp("CloseHandle", thread)]

    # Three workers on one counter: updates are lost unless the section excludes.
    loops, workers = 40, 3
    block = g.put(struct.pack("<8I", section, 0, done, loops, index, 0, 0, gate))
    threads = [g.imp("CreateThread", 0, 0, procedure, block, 0, 0) for _ in range(workers)]
    out["workers end"] = [wait(thread, 30000) for thread in threads]
    out["workers counter"] = g.m.u32(block + 4)
    assert g.m.u32(block + 4) == loops * workers, "the critical section did not exclude"
    out["workers close"] = [g.imp("CloseHandle", thread) for thread in threads]

    out["tls free"] = [g.imp("TlsFree", index), g.imp("TlsFree", other)]
    again = g.imp("TlsAlloc")
    out["tls fresh slot is zero"] = g.imp("TlsGetValue", again)
    g.imp("DeleteCriticalSection", section)
    assert not g.m.thread_errors, g.m.thread_errors
    return out


def scenario_game(g: Guest):
    """Game functions over the same bridges, as far as they run without the game's own state."""
    out = {}
    result = g.call("SYS_CreateInstanceMapping")
    out["SYS_CreateInstanceMapping"] = [result, names(g.trace())]
    buffer = g.alloc(256)
    handle = g.call("VFS_Open", eax=g.text("DATA\\ALPHA.DAT"), edx=O_RDONLY | O_BINARY)
    out["VFS_Open"] = [handle, names(g.trace())]
    got = g.call("VFS_Read", eax=handle, edx=buffer, ebx=200)
    out["VFS_Read"] = [got, hashlib.sha1(g.m.read(buffer, 200)).hexdigest(), names(g.trace())]
    out["VFS_Seek"] = [g.call("VFS_Seek", eax=handle, edx=1000, ebx=0), names(g.trace())]
    out["VFS_Read short"] = [g.call("VFS_Read", eax=handle, edx=buffer, ebx=200), names(g.trace())]
    out["VFS_Close"] = [g.call("VFS_Close", eax=handle), names(g.trace())]
    for name in ("CD_InitPaths", "CD_GetDiscNumber", "CD_CheckFullInstall"):
        out[name] = [g.call(name), names(g.trace())]
    for name in ("CD_GetRootPath", "FILE_GetInstallRoot", "FILE_GetDataRoot"):
        out[name] = g.string(g.call(name))
    return out


def real_find(directory: Path, pattern: str) -> list[str]:
    """FindFirstFileW's own answer."""
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32")
    k32.FindFirstFileW.restype = wintypes.HANDLE
    k32.FindFirstFileW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.WIN32_FIND_DATAW)]
    k32.FindNextFileW.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.WIN32_FIND_DATAW)]
    k32.FindClose.argtypes = [wintypes.HANDLE]
    data = wintypes.WIN32_FIND_DATAW()
    handle = k32.FindFirstFileW(str(directory / pattern), ctypes.byref(data))
    if handle == wintypes.HANDLE(-1).value:
        return []
    found = [data.cFileName]
    while k32.FindNextFileW(handle, ctypes.byref(data)):
        found.append(data.cFileName)
    k32.FindClose(handle)
    return found


def wildcard_patterns():
    alphabet = ["*", "?", ".", "A", "T", "X", "GAME", "DAT", "NOEXT", "TXT", "B", "1"]
    patterns = {
        "*.*", "*", "*.", "?????", "????????.???", "*.TXT", "A?.TXT", "*.TXT?", "NOEXT.",
        "NOEXT.*", "GAME?.DAT", "GAME??.DAT", "GAME*.DAT", "*.t*", "*T", "*O*", "*.?", "?.",
        "*.*.*", "game*.dat", "a.txt",
    }  # fmt: skip
    for size in (1, 2):
        patterns.update("".join(combo) for combo in itertools.product(alphabet, repeat=size))
    rng = random.Random(7)
    while len(patterns) < 1500:
        patterns.add("".join(rng.choice(alphabet) for _ in range(rng.randint(3, 6))))
    # Left out: "." and "..", and what starts with "..", which path
    # normalisation takes for the parent directory before any matching.
    return sorted(p for p in patterns if not p.startswith("..") and p != ".")


def scenario_checks(g: Guest, fixture):
    """Cases the Win32 bridges were never asked, or got wrong: asserted here,
    not compared with the baseline."""
    out = {}
    mask = 0xFFFFFFFF

    def failed(name, *args):
        g.imp("SetLastError", 0)
        return g.imp(name, *args), g.error()

    # A destination size without a destination is invalid; a negative source
    # length means NUL-terminated (both as kernel32 does).
    narrow, wide, dst = g.put(b"AB\0"), g.put("AB\0".encode("utf-16-le")), g.alloc(32)
    assert failed("MultiByteToWideChar", 1252, 0, narrow, mask, 0, 8) == (0, 87)
    assert failed("WideCharToMultiByte", 1252, 0, wide, mask, 0, 8, 0, 0) == (0, 87)
    assert g.imp("MultiByteToWideChar", 1252, 0, narrow, -5 & mask, dst, 8) == 3
    assert g.imp("WideCharToMultiByte", 1252, 0, wide, -5 & mask, dst, 8, 0, 0) == 3
    out["conversions"] = 4

    # The current directory and a relative path, together longer than MAX_PATH.
    deep, long = "\\" + "D" * 100, "A" * 200
    assert g.imp("SetCurrentDirectoryA", g.text(deep)) == 1
    assert create(g, long, GENERIC_READ, OPEN_EXISTING) == (INVALID, 206)
    assert create(g, long, GENERIC_WRITE, CREATE_ALWAYS) == (INVALID, 206)
    assert failed("GetFileAttributesA", g.text(long)) == (INVALID, 206)
    assert failed("FindFirstFileA", g.text(long + "\\*.*"), g.alloc(320)) == (INVALID, 206)
    assert failed("DeleteFileA", g.text(long)) == (0, 206)
    assert failed("MoveFileA", g.text(long), g.text("\\X.DAT")) == (0, 206)
    assert failed("MoveFileA", g.text("\\X.DAT"), g.text(long)) == (0, 206)
    assert g.imp("SetCurrentDirectoryA", g.text(long)) == 0
    assert g.imp("GetFullPathNameA", g.text(long), 600, g.alloc(600), 0) == 0
    path = g.alloc(264)
    g.imp("GetCurrentDirectoryA", 260, path)
    assert g.string(path) == "C:" + deep
    assert g.imp("SetCurrentDirectoryA", g.text("\\")) == 1
    out["long paths"] = 9

    # SDL opens a file for writing exclusively: the second open is refused,
    # logged, and reported as a sharing violation.
    writer, error = create(g, "SAVE\\SHARED.DAT", GENERIC_WRITE, CREATE_ALWAYS)
    assert writer != INVALID and error == 0
    assert create(g, "SAVE\\SHARED.DAT", GENERIC_READ, OPEN_EXISTING) == (INVALID, 32)
    assert g.imp("CloseHandle", writer) == 1
    reader, error = create(g, "SAVE\\SHARED.DAT", GENERIC_READ, OPEN_EXISTING)
    assert reader != INVALID and error == 0
    again, error = create(g, "SAVE\\SHARED.DAT", GENERIC_READ, OPEN_EXISTING)
    assert again != INVALID and error == 0, "two readers share"
    assert g.imp("CloseHandle", reader) == 1 and g.imp("CloseHandle", again) == 1
    out["share modes"] = 4

    # A closed handle: the object is gone, the calls fail cleanly.
    count, buffer = g.alloc(4), g.alloc(16)
    handle, error = create(g, "DATA\\ALPHA.DAT", GENERIC_READ, OPEN_EXISTING)
    assert g.imp("CloseHandle", handle) == 1
    assert failed("ReadFile", handle, buffer, 4, count, 0) == (0, 6)
    assert failed("WriteFile", handle, buffer, 4, count, 0) == (0, 6)
    assert failed("SetFilePointer", handle, 0, 0, 0) == (INVALID, 6)
    assert failed("FindNextFileA", handle, g.alloc(320)) == (0, 6)
    # A closed standard handle keeps its number and no file takes it over.
    standard = g.imp("GetStdHandle", -11 & mask)
    assert g.imp("CloseHandle", standard) == 1
    assert g.imp("GetStdHandle", -11 & mask) == standard
    handle, error = create(g, "DATA\\ALPHA.DAT", GENERIC_READ, OPEN_EXISTING)
    assert handle not in (INVALID, standard)
    assert g.imp("CloseHandle", handle) == 1
    out["closed handles"] = 7

    # A timeout that does not fit SDL's signed milliseconds still waits.
    event = g.imp("CreateEventA", 0, 1, 1, 0)
    assert g.imp("WaitForSingleObject", event, 0x90000000) == 0
    assert g.imp("CloseHandle", event) == 1
    out["large timeout"] = 1

    # Wildcards: the bridge's listing of root\FUZZ against FindFirstFileW's.
    pattern, data = g.alloc(64), g.alloc(320)

    def listing(expression):
        g.m.write(pattern, ("FUZZ\\" + expression).encode("ascii") + b"\0")
        handle = g.imp("FindFirstFileA", pattern, data)
        if handle == INVALID:
            return []
        found = [g.string(data + 44, 260)]
        while g.imp("FindNextFileA", handle, data):
            found.append(g.string(data + 44, 260))
        g.imp("FindClose", handle)
        return found

    patterns = wildcard_patterns()
    wrong = [
        f"{expression!r}: FindFirstFileW {real} bridge {mine}"
        for expression in patterns
        if (real := real_find(fixture["root"] / "FUZZ", expression))
        != (mine := listing(expression))
    ]
    assert not wrong, "\n".join(wrong[:20])
    out["wildcard patterns"] = len(patterns)
    return out


def difference(expected, actual, where=""):
    if type(expected) is not type(actual):
        return [f"{where}: {expected!r} -> {actual!r}"]
    if isinstance(expected, dict):
        out = []
        for key in dict.fromkeys([*expected, *actual]):
            if key.startswith("host: "):
                continue  # depends on the machine's settings; listed separately
            if key not in expected or key not in actual:
                out.append(f"{where}/{key}: {'missing' if key not in actual else 'new'}")
            else:
                out += difference(expected[key], actual[key], f"{where}/{key}")
        return out
    if isinstance(expected, list) and len(expected) == len(actual):
        return [d for i, (e, a) in enumerate(zip(expected, actual, strict=True))
                for d in difference(e, a, f"{where}[{i}]")]  # fmt: skip
    return [] if expected == actual else [f"{where}: {expected!r} -> {actual!r}"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", action="store_true", help="store this run as the baseline")
    parser.add_argument("--baseline", type=Path, default=BASELINE, help="baseline file")
    parser.add_argument(
        "--host", type=Path, default=recomp_env.HOST, help="host source tree to build"
    )
    parser.add_argument("--tag", help="separate output directory")
    parser.add_argument(
        "--case-walk", action="store_true", help="force the case-insensitive path lookup"
    )
    parser.add_argument("--exe", type=Path, help="guest exe (default DREAMS_DISC1/GDIDREAM.EXE)")
    parser.add_argument(
        "--vm",
        choices=recomp_env.VM_CHOICES,
        default=recomp_env.VM_DEFAULT,
        help="virtual memory implementation of the bridge DLL (host/vm)",
    )
    args = parser.parse_args()
    exe = args.exe or paths.disc(1) / "GDIDREAM.EXE"
    output = recomp_env.out_dir("kernel-bridge", *([args.tag] if args.tag else []))
    fixture = make_fixture(output)
    for name in [name for name in os.environ if name.upper().startswith("T_")]:
        del os.environ[name]
    os.environ.update(
        WD_READ_ROOTS=str(fixture["root2"]),
        WD_WRITE_ROOT=str(fixture["sandbox"]),
        WD_FILES_CASE_WALK="1" if args.case_walk else "",
        T_BETA="two",
        T_ALPHA="1",
    )
    dll, sources, missing = build(output, args.host.resolve(), exe, args.vm)
    os.chdir(output)
    if not dll.kb_init(str(exe).encode(), str(fixture["root"] / "GDIDREAM.EXE").encode()):
        raise RuntimeError("bridge host setup failed")
    machine = Machine(dll, exe)
    guest = Guest(machine, IMAGE + analyze_pe(str(exe)).entry_point_rva, load_symbols())
    results = {"startup": scenario_startup(guest)}
    guest.scratch = guest.imp("VirtualAlloc", 0, 0x200000, 0x3000, 4)
    guest.scratch_end = guest.scratch + 0x200000
    assert guest.scratch, "no scratch memory"
    results["process"] = scenario_process(guest)
    results["text"] = scenario_text(guest)
    results["time"] = scenario_time(guest)
    results["files"] = scenario_files(guest, fixture)
    results["runtime"] = scenario_runtime(guest, fixture)
    results["sync"] = scenario_sync(guest)
    results["game"] = scenario_game(guest)
    report = {"sources": sources, "unbridged KERNEL32 imports": missing, "results": results}
    # The Win32 layout's bridges cannot take these: one overruns a stack buffer there.
    asserted = scenario_checks(guest, fixture) if "sdl/files.c" in sources else {}
    assert not machine.thread_errors, machine.thread_errors
    text = json.dumps(report, indent=1, ensure_ascii=False) + "\n"
    (output / "results.json").write_text(text, encoding="utf-8")
    checks = sum(len(v) for v in results.values())
    if asserted:
        print(f"in place: {', '.join(f'{count} {name}' for name, count in asserted.items())}")
    baseline = args.baseline
    if args.capture:
        baseline.write_text(text, encoding="utf-8", newline="\n")
        print(f"captured {checks} observations from {', '.join(sources)} -> {baseline}")
        return 0
    if not baseline.is_file():
        print(f"{checks} observations in {output / 'results.json'}; no baseline at {baseline}")
        return 1
    expected = json.loads(baseline.read_text(encoding="utf-8"))
    actual = json.loads(text)["results"]
    changes = difference(expected["results"], actual)
    for line in changes[:80]:
        print(line)
    for scenario, observed in actual.items():
        for key, value in observed.items():
            before = expected["results"].get(scenario, {}).get(key)
            if key.startswith("host: ") and before != value:
                print(f"note: /{scenario}/{key}: {before!r} -> {value!r}")
    print(
        f"{'FAIL' if changes else 'PASS'}: {checks} observations, {len(changes)} differ from the "
        f"baseline ({', '.join(expected['sources'])} -> {', '.join(sources)})"
    )
    return 1 if changes else 0


if __name__ == "__main__":
    raise SystemExit(main())
