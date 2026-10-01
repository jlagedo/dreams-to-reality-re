"""Targeted generation tests (run with uv run --with capstone pytest ...)."""

import argparse
import ctypes
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # run.py
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "lift"))
from render_audit import memory_probes  # noqa: E402
from render_bulk import wrap_bulk  # noqa: E402
from replacements import wrap_entry  # noqa: E402


def probes(code):
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    instruction = next(decoder.disasm(bytes.fromhex(code), 0x401000))
    return memory_probes(instruction, lambda memory: "address")


def test_read_write_direction_and_address_only_operands():
    assert probes("8b00") == ["WD_AUDIT_MEMORY(0x00401000u, (uint32_t)(address), 4u, 0);"]
    assert probes("8900") == ["WD_AUDIT_MEMORY(0x00401000u, (uint32_t)(address), 4u, 1);"]
    assert len(probes("830001")) == 2  # add dword ptr [eax], 1: read + write
    assert probes("8d00") == []  # lea never reads memory


def test_bulk_access_count_width_and_direction_are_forwarded():
    assert probes("f3a5") == ["WD_AUDIT_STRING(0x00401000u, esi, edi, ecx, 4u, _df, 1, 1);"]
    assert probes("f266ab") == ["WD_AUDIT_STRING(0x00401000u, esi, edi, ecx, 2u, _df, 0, 1);"]
    assert probes("ac") == ["WD_AUDIT_STRING(0x00401000u, esi, edi, 1u, 1u, _df, 1, 0);"]


def test_original_recursion_and_tails_still_use_the_public_entry():
    # Renaming all occurrences would bypass native dispatch in recursive/tail
    # calls. Only the body's declaration may become the reference symbol.
    body = "void sub_0047E498(void) { RECOMP_CALL(sub_0047E498); sub_0047E498(); }"
    result = wrap_entry(body, 0x47E498)
    assert result.startswith("void wd_original_0047E498(void)")
    assert "RECOMP_CALL(sub_0047E498); sub_0047E498();" in result
    assert "if (!wd_try_replace(0x0047E498u)) wd_original_0047E498();" in result


@pytest.mark.parametrize(
    "code, footprints",
    [
        ("50", [("esp - 4u", 4, 1)]),
        ("6650", [("esp - 2u", 2, 1)]),
        ("06", [("esp - 4u", 4, 1)]),  # segment selector still reserves four bytes
        ("6606", [("esp - 2u", 2, 1)]),
        ("58", [("esp", 4, 0)]),
        ("9c", [("esp - 4u", 4, 1)]),
        ("9d", [("esp", 4, 0)]),
        ("e800000000", [("esp - 4u", 4, 1)]),
        ("c21800", [("esp", 4, 0)]),  # cleanup bytes are not reads
        ("60", [("esp - 32u", 32, 1)]),
        ("61", [("esp", 12, 0), ("esp + 16u", 16, 0)]),
        ("c9", [("ebp", 4, 0)]),
        ("c8100000", [("esp - 4u", 4, 1)]),
        ("6866000000", [("esp - 4u", 4, 1)]),  # immediate 66 is not a prefix
    ],
)
def test_implicit_stack_footprints(code, footprints):
    assert probes(code) == [
        f"WD_AUDIT_MEMORY(0x00401000u, (uint32_t)({address}), {size}u, {write});"
        for address, size, write in footprints
    ]


def test_memory_push_audits_source_and_implicit_destination():
    assert probes("ff30") == [
        "WD_AUDIT_MEMORY(0x00401000u, (uint32_t)(esp - 4u), 4u, 1);",
        "WD_AUDIT_MEMORY(0x00401000u, (uint32_t)(address), 4u, 0);",
    ]


def test_memory_pop_uses_incremented_stack_pointer_for_destination():
    assert probes("8f0424") == [
        "WD_AUDIT_MEMORY(0x00401000u, (uint32_t)(esp), 4u, 0);",
        "WD_AUDIT_MEMORY(0x00401000u, (uint32_t)((address) + 4u), 4u, 1);",
    ]


@pytest.mark.parametrize("opcode", ["a6", "66a7", "a7", "ae", "66af", "af"])
@pytest.mark.parametrize("prefix", ["", "f3", "f2"])
def test_compare_scan_audits_only_executed_iterations(opcode, prefix):
    # Exercise the actual upstream generated body, without lifting game data.
    from dreams import paths

    upstream = paths.get("pcrecomp") / "tools" / "lift"
    if not upstream.is_dir():
        pytest.skip("requires the local pcrecomp lifter")
    sys.path.insert(0, str(upstream))
    from generate import LinearInstruction
    from lift32 import Lifter

    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    instruction = LinearInstruction(next(decoder.disasm(bytes.fromhex(prefix + opcode), 0x401000)))
    original = Lifter().lift_instruction(instruction)
    result = wrap_bulk(instruction, original, memory_probes(instruction, lambda memory: "address"))
    code = "\n".join(result)
    assert memory_probes(instruction, lambda memory: "address") == []
    width = 2 if "66" in opcode else (1 if opcode in ("a6", "ae") else 4)
    addresses = ("esi", "edi") if opcode.endswith(("a6", "a7")) else ("edi",)
    audit = " ".join(
        f"WD_AUDIT_MEMORY(0x00401000u, {address}, {width}u, 0);" for address in addresses
    )
    assert code.count(audit) == 1
    if prefix:
        assert code.index("while (ecx) {") < code.index(audit) < code.index("ecx--")
    assert code.replace(audit + " ", "") == "\n".join(original)


def test_sse_cmpsd_is_not_a_string_access():
    assert len(probes("f20fc20000")) == 1  # cmpsd xmm0, qword ptr [eax], 0


@pytest.mark.skipif(sys.platform != "win32", reason="uses the Windows CPU test compiler")
def test_generated_compare_scan_execution():
    """Run real emitted code: no probe for zero count or unvisited tail bytes."""
    from dreams import paths

    upstream = paths.get("pcrecomp") / "tools" / "lift"
    if not upstream.is_dir():
        pytest.skip("requires the local pcrecomp lifter")
    sys.path.insert(0, str(upstream))
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import recomp_env
    from generate import LinearInstruction
    from lift32 import Lifter

    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    bodies = []
    cases = []
    for prefix in ("", "f3", "f2"):
        for opcode in ("a6", "66a7", "a7", "ae", "66af", "af"):
            instruction = LinearInstruction(
                next(decoder.disasm(bytes.fromhex(prefix + opcode), 0x401000))
            )
            emitted = "\n".join(wrap_bulk(instruction, Lifter().lift_instruction(instruction), []))
            name = "test_" + prefix + opcode
            width = 2 if "66" in opcode else (1 if opcode in ("a6", "ae") else 4)
            sources = 2 if opcode.endswith(("a6", "a7")) else 1
            iterations = "1"
            if prefix:
                conditional = "(equal ? count : 1)" if prefix == "f3" else "(equal ? 1 : count)"
                iterations = f"(count ? {conditional} : 0)"
            bodies.append(
                f"void {name}(uint32_t count, int direction, bool equal) {{ "
                "uint32_t esi=128, edi=512, eax=7, ecx=count, _flag_a=0, _flag_b=0, "
                "_cf=0, _flag_k=0; int _df=direction; matched=equal; probes=reads=0;\n"
                + emitted
                + f"\nunsigned iterations = {iterations}; "
                f"assert(probes == iterations * {sources}); assert(reads == probes); "
                f"assert(edi == uint32_t(512 + direction * int(iterations * {width}))); "
                + ("assert(ecx == count - iterations); " if prefix else "assert(ecx == count); ")
                + "}\n"
            )
            cases.append(
                f"for (auto count : {{0u, 1u, 3u}}) for (int direction : {{-1, 1}}) "
                f"for (bool equal : {{false, true}}) {name}(count, direction, equal);"
            )
    source = (
        r"""
#include <cassert>
#include <cstdint>
#include <initializer_list>
unsigned probes, reads; uint32_t addresses[16]; bool matched;
void audit(uint32_t address) { addresses[probes++] = address; }
uint32_t load(uint32_t address) {
    assert(reads < probes && addresses[reads++] == address);
    return address < 256 || matched ? 7 : 9;
}
#define WD_AUDIT_MEMORY(ip, address, bytes, write) audit(address)
#define MEM8(address) load(address)
#define MEM16(address) load(address)
#define MEM32(address) load(address)
#define LO8(value) ((value) & 255u)
#define LO16(value) ((value) & 65535u)
#define CMP_B(a,b) ((a)<(b))
#define FK_CMP 1
"""
        + "\n".join(bodies)
        + "\nint main() {\n"
        + "\n".join(cases)
        + "\n}\n"
    )
    out = recomp_env.out_dir("boundary-agent", "codegen-tests")
    cpp = out / "compare_scan.cpp"
    cpp.write_text(source, encoding="utf-8")
    env = recomp_env.build_env()
    compiler = shutil.which("cl.exe", path=env["PATH"])
    assert compiler, "Windows CPU test compiler is required"
    executable = out / "compare_scan.exe"
    subprocess.run(
        [compiler, "/nologo", "/std:c++17", "/EHsc", str(cpp), "/Fe:" + str(executable)],
        cwd=out,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run([str(executable)], check=True, capture_output=True, text=True)


@pytest.mark.skipif(sys.platform != "win32", reason="uses the Windows host bridges")
def test_host_bridge_access_footprints():
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import recomp_env

    out = recomp_env.out_dir("boundary-agent", "host-tests")
    env = recomp_env.build_env()
    compiler = shutil.which("cl.exe", path=env["PATH"])
    assert compiler, "Windows CPU test compiler is required"
    sources = [
        recomp_env.HOST / name
        for name in (
            "core/runtime.c",
            "win32/files.c",
            "win32/kernel.c",
            "win32/vm.c",
            "render/render_boundary.cpp",
        )
    ]
    sources.append(Path(__file__).parent / "native" / "render_host_audit_tests.c")
    for source in sources:
        command = [compiler, "/nologo", "/c", "/Gy", "/DWD_RENDER_AUDIT"]
        command += recomp_env.host_includes()
        if source.suffix == ".cpp":
            command += ["/std:c++17", "/EHsc"]
        if source.name == "runtime.c":
            command.append("/Dmain=wd_unused_main")
        subprocess.run(
            command + [str(source)], cwd=out, env=env, check=True, capture_output=True, text=True
        )
    executable = out / "host_audit.exe"
    subprocess.run(
        [
            compiler,
            "/nologo",
            *(str(out / source.with_suffix(".obj").name) for source in sources),
            "/Fe:" + str(executable),
            "/link",
            "/OPT:REF",
        ],
        cwd=out,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run([str(executable)], cwd=out, check=True, capture_output=True, text=True)


@pytest.mark.skipif(sys.platform != "win32", reason="uses the Windows CPU compiler")
def test_display_environment_and_cli_grammar_agree():
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    import recomp_env
    from run import schedule

    out = recomp_env.out_dir("boundary-agent", "display-parser")
    source = out / "display_parser.cpp"
    source.write_text(
        r"""
#include "display_script_parse.h"
extern "C" __declspec(dllexport) int validate(int mouse, const char* text) {
    uint32_t previous = 0, ms = 0; unsigned count = 0;
    while (*text) {
        int a, b, kind;
        if (count == 128 || !(mouse ? wd_script_mouse(&text, &ms, &kind, &a, &b)
                                   : wd_script_resize(&text, &ms, &a, &b)) ||
            (count && ms < previous)) return 0;
        previous = ms; ++count;
    }
    return 1;
}
extern "C" __declspec(dllexport) int dimension(const char* text) {
    int value; return wd_script_dimension(&text, &value) && !*text;
}
""",
        encoding="utf-8",
    )
    env = recomp_env.build_env()
    compiler = shutil.which("cl.exe", path=env["PATH"])
    assert compiler
    dll = out / "display_parser.dll"
    subprocess.run(
        [
            compiler,
            "/nologo",
            "/LD",
            *recomp_env.host_includes(),
            str(source),
            "/Fe:" + str(dll),
        ],
        cwd=out,
        env=env,
        check=True,
        capture_output=True,
        text=True,
    )
    library = ctypes.CDLL(str(dll))
    library.validate.argtypes = [ctypes.c_int, ctypes.c_char_p]
    library.dimension.argtypes = [ctypes.c_char_p]
    cases = {
        "resize": [
            "",
            "0:640x480",
            "4294967295:16384x64",
            "0:640x480,0:64x64",
            "-1:640x480",
            "+1:640x480",
            "4294967296:640x480",
            "1:640x480,",
            "1:640x480,,2:640x480",
            "2:640x480,1:640x480",
            "1:0640x480",
            "1:63x480",
            "1:16385x480",
            "1:640x480junk",
            "1:640.0x480",
            " 1:640x480",
            "1:640x480 ",
            "١:640x480",
            "1:640x480," * 128 + "1:64x64",
        ],
        "mouse": [
            "",
            "0:move:0:0",
            "4294967295:right-up:-32767:32767",
            "0:left-down:-0:00,1:left-up:0:0",
            "1:move:1.5:2",
            "1:move:1e2:2",
            "1:move:nan:2",
            "1:move:inf:2",
            "1:move:32768:0",
            "1:move:-32768:0",
            "-1:move:0:0",
            "4294967296:move:0:0",
            "1:move:0:0,",
            "1:move:+1:0",
            "1:move:0:0junk",
            "1:move:0:0,0:move:0:0",
            "1:other:0:0",
            "1:move:٠:0",
        ],
    }
    for kind, values in cases.items():
        for value in values:
            try:
                schedule(value, kind)
                accepted = True
            except argparse.ArgumentTypeError:
                accepted = False
            assert bool(library.validate(kind == "mouse", value.encode())) == accepted, (
                kind,
                value,
            )
    for value in ("64", "640", "16384"):
        assert library.dimension(value.encode())
    for value in ("", "63", "16385", "4294967296", "0640", "+640", "-640", "640 ", " 640"):
        assert not library.dimension(value.encode())
