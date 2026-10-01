"""Launch WINDREAM.EXE / GDIDREAM.EXE with a frame-rate limiter patched in memory.

The executable on disk is never modified. The process starts suspended, one
call is redirected into a small wait routine, then the process resumes.

Where it hooks (docs/research/engine.md "The fixed step", docs/research/animation-timing.md):
GAME_TickFrame (0x416d45) computes the next frame's delta at 0x4170a6. It reads
the 200 Hz counter with MGM message 0x11 (call 0x43a306 at 0x4170b7) and
subtracts the previous reading, which is stored as a float in [ebp-8]. The
routine below repeats that call until at least N ticks (5 ms each) have passed,
so a frame never takes less than N * 5 ms. It sleeps while more than about
15 ms remain and yields the rest of the time.

    ticks   frame time   rate     delta (30 Hz frames)
      6      30 ms       33 fps   0.90
      7      35 ms       28.6 fps 1.05   (default; closest whole tick to 30 Hz)
     10      50 ms       20 fps   1.50

Only the gameplay frame handler is limited. The boot, menu and caption loops
use a different timer path and are left alone.

Usage:
    uv run python re/tools/fps_limit_launcher.py R:\\CRYO\\DREAMS\\GDIDREAM.EXE
    uv run python re/tools/fps_limit_launcher.py GDIDREAM.EXE --ticks 6
"""

from __future__ import annotations

import argparse
import ctypes
import struct
import sys
from ctypes import wintypes
from pathlib import Path

HOOK_CALL = 0x4170B7  # call MGM_SendMessage(0x11): read the 200 Hz counter
HOOK_ORIGINAL = bytes.fromhex("e8 4a 32 02 00")  # call 0x43a306
MGM_SEND_MESSAGE = 0x43A306
SLEEP_THUNK = 0x49B0D2  # jmp dword ptr [KERNEL32!Sleep]
SLEEP_THUNK_BYTES = bytes.fromhex("ff 25 dc c3 49 00")

CREATE_SUSPENDED = 0x4
MEM_COMMIT_RESERVE = 0x3000
PAGE_EXECUTE_READWRITE = 0x40


def rel32(source_end: int, target: int) -> bytes:
    return struct.pack("<I", (target - source_end) & 0xFFFFFFFF)


def build_cave(base: int, ticks: int) -> bytes:
    """Assemble the wait routine at `base`; returns the machine code."""
    code = bytearray()

    def emit(data: bytes) -> None:
        code.extend(data)

    emit(bytes.fromhex("83 ec 08"))  # sub esp, 8
    loop = len(code)
    emit(bytes.fromhex("31 db"))  # xor ebx, ebx
    emit(bytes.fromhex("31 d2"))  # xor edx, edx
    emit(bytes.fromhex("b8 11 00 00 00"))  # mov eax, 0x11
    emit(b"\xe8" + rel32(base + len(code) + 5, MGM_SEND_MESSAGE))  # call counter
    emit(bytes.fromhex("89 04 24"))  # mov [esp], eax          counter
    emit(bytes.fromhex("db 04 24"))  # fild dword [esp]
    emit(bytes.fromhex("d8 65 f8"))  # fsub dword [ebp-8]      - previous reading
    emit(bytes.fromhex("db 5c 24 04"))  # fistp dword [esp+4]  elapsed ticks
    emit(bytes.fromhex("8b 4c 24 04"))  # mov ecx, [esp+4]
    emit(bytes.fromhex("83 f9") + bytes([ticks]))  # cmp ecx, N
    jge_at = len(code)
    emit(bytes.fromhex("7d 00"))  # jge done (patched below)
    emit(bytes.fromhex("31 c0"))  # xor eax, eax
    emit(bytes.fromhex("83 f9") + bytes([max(ticks - 3, 0)]))  # cmp ecx, N-3
    emit(bytes.fromhex("0f 9e c0"))  # setle al                Sleep(1) if >= 3 ticks left
    emit(bytes.fromhex("50"))  # push eax
    emit(b"\xe8" + rel32(base + len(code) + 5, SLEEP_THUNK))  # call Sleep thunk
    emit(b"\xeb" + bytes([(loop - (len(code) + 2)) & 0xFF]))  # jmp loop
    done = len(code)
    code[jge_at + 1] = done - (jge_at + 2)
    emit(bytes.fromhex("8b 04 24"))  # mov eax, [esp]          return the counter
    emit(bytes.fromhex("83 c4 08"))  # add esp, 8
    emit(bytes.fromhex("c3"))  # ret
    return bytes(code)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("exe", type=Path, help="path to WINDREAM.EXE or GDIDREAM.EXE")
    parser.add_argument(
        "--ticks", type=int, default=7, help="minimum 5 ms ticks per frame (default 7)"
    )
    parser.add_argument("--no-limit", action="store_true", help="launch without patching")
    args = parser.parse_args()
    if not 4 <= args.ticks <= 100:
        parser.error("--ticks must be between 4 and 100")
    exe = args.exe.resolve()
    if not exe.is_file():
        parser.error(f"{exe} not found")

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)

    class STARTUPINFOW(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("lpReserved", wintypes.LPWSTR),
            ("lpDesktop", wintypes.LPWSTR),
            ("lpTitle", wintypes.LPWSTR),
            ("dwX", wintypes.DWORD),
            ("dwY", wintypes.DWORD),
            ("dwXSize", wintypes.DWORD),
            ("dwYSize", wintypes.DWORD),
            ("dwXCountChars", wintypes.DWORD),
            ("dwYCountChars", wintypes.DWORD),
            ("dwFillAttribute", wintypes.DWORD),
            ("dwFlags", wintypes.DWORD),
            ("wShowWindow", wintypes.WORD),
            ("cbReserved2", wintypes.WORD),
            ("lpReserved2", ctypes.c_void_p),
            ("hStdInput", wintypes.HANDLE),
            ("hStdOutput", wintypes.HANDLE),
            ("hStdError", wintypes.HANDLE),
        ]

    class PROCESS_INFORMATION(ctypes.Structure):
        _fields_ = [
            ("hProcess", wintypes.HANDLE),
            ("hThread", wintypes.HANDLE),
            ("dwProcessId", wintypes.DWORD),
            ("dwThreadId", wintypes.DWORD),
        ]

    k32.CreateProcessW.argtypes = [
        wintypes.LPCWSTR,
        wintypes.LPWSTR,
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.BOOL,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.LPCWSTR,
        ctypes.POINTER(STARTUPINFOW),
        ctypes.POINTER(PROCESS_INFORMATION),
    ]
    k32.ReadProcessMemory.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
    ]
    k32.WriteProcessMemory.argtypes = k32.ReadProcessMemory.argtypes
    k32.VirtualAllocEx.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        ctypes.c_size_t,
        wintypes.DWORD,
        wintypes.DWORD,
    ]
    k32.VirtualAllocEx.restype = ctypes.c_void_p
    k32.VirtualProtectEx.argtypes = [
        wintypes.HANDLE,
        ctypes.c_void_p,
        ctypes.c_size_t,
        wintypes.DWORD,
        ctypes.POINTER(wintypes.DWORD),
    ]
    k32.FlushInstructionCache.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t]
    k32.ResumeThread.argtypes = [wintypes.HANDLE]
    k32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]

    startup = STARTUPINFOW()
    startup.cb = ctypes.sizeof(startup)
    info = PROCESS_INFORMATION()
    command = ctypes.create_unicode_buffer(f'"{exe}"')
    if not k32.CreateProcessW(
        str(exe),
        command,
        None,
        None,
        False,
        CREATE_SUSPENDED,
        None,
        str(exe.parent),
        ctypes.byref(startup),
        ctypes.byref(info),
    ):
        raise ctypes.WinError(ctypes.get_last_error())

    def read(address: int, size: int) -> bytes:
        buffer = ctypes.create_string_buffer(size)
        done = ctypes.c_size_t()
        if not k32.ReadProcessMemory(info.hProcess, address, buffer, size, ctypes.byref(done)):
            raise ctypes.WinError(ctypes.get_last_error())
        return buffer.raw[: done.value]

    def write(address: int, data: bytes) -> None:
        old = wintypes.DWORD()
        if not k32.VirtualProtectEx(
            info.hProcess, address, len(data), PAGE_EXECUTE_READWRITE, ctypes.byref(old)
        ):
            raise ctypes.WinError(ctypes.get_last_error())
        done = ctypes.c_size_t()
        if not k32.WriteProcessMemory(info.hProcess, address, data, len(data), ctypes.byref(done)):
            raise ctypes.WinError(ctypes.get_last_error())
        k32.VirtualProtectEx(info.hProcess, address, len(data), old.value, ctypes.byref(old))
        k32.FlushInstructionCache(info.hProcess, address, len(data))

    try:
        if not args.no_limit:
            # This patch is specific to the traced build; refuse anything else.
            if read(HOOK_CALL, len(HOOK_ORIGINAL)) != HOOK_ORIGINAL:
                raise SystemExit(
                    f"{exe.name}: bytes at {HOOK_CALL:#x} do not match the traced build"
                )
            if read(SLEEP_THUNK, len(SLEEP_THUNK_BYTES)) != SLEEP_THUNK_BYTES:
                raise SystemExit(f"{exe.name}: Sleep thunk at {SLEEP_THUNK:#x} not found")
            cave_size = 128
            cave = k32.VirtualAllocEx(
                info.hProcess, None, cave_size, MEM_COMMIT_RESERVE, PAGE_EXECUTE_READWRITE
            )
            if not cave:
                raise ctypes.WinError(ctypes.get_last_error())
            if cave >= 1 << 32:
                raise SystemExit("code cave landed above 4 GiB; cannot reach it from a 32-bit call")
            code = build_cave(cave, args.ticks)
            assert len(code) <= cave_size
            write(cave, code)
            write(HOOK_CALL, b"\xe8" + rel32(HOOK_CALL + 5, cave))
            print(
                f"limiter: {args.ticks} ticks/frame = {200 / args.ticks:.1f} fps max, "
                f"cave at {cave:#x}"
            )
        k32.ResumeThread(info.hThread)
        print(f"started {exe.name}, pid {info.dwProcessId}")
    except BaseException:
        k32.TerminateProcess(info.hProcess, 1)
        raise
    finally:
        k32.CloseHandle(info.hThread)
        k32.CloseHandle(info.hProcess)
    return 0


if __name__ == "__main__":
    sys.exit(main())
