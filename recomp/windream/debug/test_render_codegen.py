"""Targeted generation tests (run with uv run --with capstone pytest ...)."""

import sys
from pathlib import Path

from capstone import CS_ARCH_X86, CS_MODE_32, Cs

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from render_audit import memory_probes  # noqa: E402
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
