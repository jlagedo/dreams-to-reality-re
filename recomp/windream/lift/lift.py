"""Lift WINDREAM.EXE or GDIDREAM.EXE to C with pcrecomp lift32 (static
recompilation build). The two differ in one code byte, the immediate of the
video-mode store at 0x446036 in VID_Init (0 DirectDraw, 1 GDI window), so the
same bounds.csv serves both. The build currently lifts GDIDREAM.EXE.

Function bounds come from Ghidra (bounds.csv, pcrecomp DumpBounds.java). The
catalog is then closed: every literal call/jump target the generated C hands to
the dispatcher that lies in the code image becomes an entry, repeated until no
new entry appears. Output: gen/recomp_NNNN.c, gen/recomp_funcs.h,
gen/recomp_dispatch.c, and lift-report.json next to gen/. Generated code is
derived from the game binary and stays under out/ (never committed).

Defaults: the exe is DREAMS_DISC1/GDIDREAM.EXE, gen/ is
DREAMS_OUT/recomp/windream/gen, pcrecomp is DREAMS_PCRECOMP.

usage:
  uv run --with capstone --with pefile python recomp/windream/lift/lift.py [exe]
      [bounds.csv] [gen_dir]
"""

import bisect
import collections
import json
import os
import re
import sys
import time

from render_audit import memory_probes
from render_bulk import wrap_bulk
from replacements import RENDER_ENTRIES, wrap_entry

from dreams import paths  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = str(paths.get("pcrecomp") / "tools")
for d in ("pe", "lift"):
    sys.path.insert(0, os.path.join(TOOLS, d))

from capstone import CS_ARCH_X86, CS_MODE_32, Cs  # noqa: E402
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_ST0  # noqa: E402
from generate import lift_function_linear, linear_disassemble_function  # noqa: E402
from lift32 import Lifter, reg_name  # noqa: E402
from pe_analyze import analyze_pe, build_iat_map  # noqa: E402

SPLIT = 80  # functions per translation unit (parallel compile)
# Read-only diagnostic hooks (host/*/*.c): called at a function's entry with
# (eax, edx, ebx, [esp]); they must not write guest registers or memory.
HOOKS = {
    0x0045D10C: "wd_hook_add_candidate",  # PHYS_AddCandidate: host/hooks/phys_hook.c
    0x0045D25C: "wd_hook_remove_candidate",  # PHYS_RemoveCandidate
    0x0045D420: "wd_hook_sweep_axis",  # PHYS_SweepAxis
}
# Read-only probes before single instructions: wd_probe(va, eax, ecx, edx, ebx,
# esp, ebp, esi, edi) in host/hooks/phys_hook.c.
PROBES = set()  # e.g. {0x0045D15D}: PHYS_AddCandidate after the record allocation
# Runtime calls inserted before single instructions (no arguments). Unlike the
# probes these may run guest code; they restore every guest register.
CALLS = {
    # The Dreams Editor draw 0x44d46d has no caller in retail; it goes back where
    # GAME_TickFrame's tail handles the editor's other state, before
    # GAME_HandleHotkeys (spec 005). host/sdl/user.c runs it while keypad 5 is on.
    0x0041743A: "wd_editor_frame",
}
# Self-modifying code. Five span blitters (0x4024B8, 0x40254D, 0x4027B8,
# 0x40294D, 0x4029AF) write their texture steps, pointer steps and loop limits
# into their own instructions before running: `lea ebx, [0x4029D8]` then a store
# through ebx. The file holds placeholders (`sub dl, 0x12`, `cmp ebx,
# 0x12345678`), so these operands must be read from guest memory. The addresses
# are Ghidra's "Read-only address (ram,X) is written" warnings (DecompileAll.java
# over GDIDREAM.EXE); each is the last byte (imm8) or last dword (imm32) of its
# instruction. The stores go through a register, so POD's absolute-store scan
# (find_patch_sites) cannot find them.
PATCH_SITES = {
    0x4029D8,
    0x4029DB,
    0x4029DE,
    0x4029E6,
    0x4029E9,
    0x4029EC,
    0x4029EF,
    0x402A20,
    0x402A28,
    0x402A2B,
    0x402A2E,
    0x402A47,
    0x402A4F,
    0x402A52,
    0x402A55,
    0x402A72,
    0x402A7A,
    0x402A7D,
    0x402A80,
    0x402AA5,
    0x402AAC,
    0x402C48,
    0x402C4B,
    0x402C4E,
    0x402C56,
    0x402C83,
    0x402C8B,
    0x402C8E,
    0x402C91,
    0x402C9D,
    0x402CA4,
    0x402E05,
    0x402E08,
    0x402E0B,
    0x402E13,
    0x402E27,
    0x402E2F,
    0x402E32,
    0x402E35,
    0x402E41,
    0x402E48,
}


class WinDreamLifter(Lifter):
    """lift32 plus the instructions WINDREAM uses that it lacks."""

    _insn = None
    leaders = frozenset()  # block starts of the function being lifted (set by main)

    def _lift_instruction(self, insn):
        self._insn = insn
        # A block start can be reached by a jump, so the flags there are not
        # necessarily those of the instruction before it in address order.
        # generate.py kept that static flag state across labels: after
        # `test ecx, ecx; je ..; mov eax, ecx; jmp ..` it lifted the next
        # block's `jae` (reached from `cmp eax, ebp; jbe`) as `if (1)`, since
        # CF is 0 after test. That made PHYS_RemoveCandidate (0x45D398) treat
        # every larger key in its search as a match and delete the wrong
        # overlap records, so colliders lost their floor triangles and Duncan
        # fell through the map at level start. Decide at run time instead.
        if insn.address in self.leaders:
            self._flag_state = None
        try:
            return super()._lift_instruction(insn)
        finally:
            self._insn = None

    def _fmt_read(self, op):
        # lift32 reads a patched imm32 from memory (patch_sites, site = last
        # dword); the blitters also patch imm8 operands (site = last byte),
        # which an 8-bit instruction uses as is and a 16/32-bit one sign-extends.
        i = self._insn
        if op.type == X86_OP_IMM and i is not None and i.address + i.size - 1 in self.patch_sites:
            site = i.address + i.size - 1
            if (op.imm & 0xFF) == i.bytes[-1]:
                if op.size == 1:
                    return f"MEM8(0x{site:08X}u)"
                cast = "(uint16_t)(int16_t)" if op.size == 2 else "(uint32_t)(int32_t)"
                return f"{cast}(int8_t)MEM8(0x{site:08X}u)"
        return super()._fmt_read(op)

    def lift_instruction(self, insn):
        probes = memory_probes(insn, self._fmt_mem_addr)
        body = self._lift_game_instruction(insn)
        return wrap_bulk(insn, body, probes) or (probes + body)

    def _lift_game_instruction(self, insn):
        if insn.address in self.leaders:  # see _lift_instruction
            self._flag_state = None
        m = insn.mnemonic
        if m in ("push", "pop") and len(insn.operands) == 1:
            op = insn.operands[0]
            if op.type == X86_OP_REG:
                segment = self._fmt_read(op)
                if segment.startswith("_seg_") and 0x66 not in insn.bytes:
                    # Segment selectors are 16 bits, but default operand size
                    # in this 32-bit image reserves FOUR stack bytes. Capstone's
                    # operand width alone made memcpy_'s return address drift.
                    comment = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
                    if m == "push":
                        return [f"PUSH32(esp, (uint32_t){segment}); {comment}"]
                    return [f"{segment} = (uint16_t)POP32_VAL(esp); {comment}"]
        if m in ("bsr", "bsf") and len(insn.operands) == 2:
            dst, src = insn.operands
            if dst.type == X86_OP_REG and dst.size == 4:
                s = self._fmt_read(src)
                d = reg_name(dst.reg)
                scan = "RECOMP_BSR(_bs)" if m == "bsr" else "RECOMP_BSF(_bs)"
                c = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
                # ZF = source is zero; the destination is left unchanged then.
                self._flag_state = None
                return [
                    f"{{ uint32_t _bs = (uint32_t)({s}); "
                    f"if (_bs) {{ {d} = {scan}; }} "
                    f"_flag_k = FK_TEST; _flag_a = _bs; _flag_b = _bs; }} {c}"
                ]
        if m in ("aam", "aad"):
            # BCD digit split / join. Watcom's float formatting (__Bin2String)
            # turns two-digit chunks into characters with `aam`: AH = AL / 10,
            # AL = AL % 10. SF/ZF/PF follow the new AL.
            base = insn.operands[0].imm & 0xFF if insn.operands else 10
            c = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
            if m == "aam":
                op = (
                    f"uint8_t _al = LO8(eax); if ({base}u) {{ SET_HI8(eax, _al / {base}u); "
                    f"SET_LO8(eax, _al % {base}u); }}"
                )
            else:
                op = f"SET_LO8(eax, (uint8_t)(LO8(eax) + HI8(eax) * {base}u)); SET_HI8(eax, 0);"
            self._flag_state = None
            return [
                f"{{ {op} _flag_a = (uint32_t)LO8(eax) << 24; _flag_b = _flag_a; "
                f"_cf = 0; _flag_k = FK_TEST; }} {c}"
            ]
        if m == "fsincos":
            # ST(0) <- sin, then push cos: ST(0) = cos, ST(1) = sin. lift32 has it
            # the other way round, which swaps MATH_InitTrigTables' (0x45b040)
            # sine and cosine tables and turns every rotation by 90 degrees.
            return [
                f"{{ double _a = _st[0]; _st[0] = sin(_a); fp_push(cos(_a)); }} _fpu_cmp = 1; "
                f"/* 0x{insn.address:08X}: fsincos */"
            ]
        if m in ("fcom", "fcomp", "fcompp", "fucom", "fucomp", "fucompp", "ftst"):
            # These set only the FPU status word (read back by fnstsw), never
            # EFLAGS; lift32 also makes them the flag setter for the next jcc.
            # Watcom's IF@POW (0x478222) branches on the EFLAGS of an earlier
            # `sahf` across an `fcomp` -- "x >= 0" was read as "round(y) >= y",
            # so pow(x, 0.5) reported a domain error and returned garbage.
            saved = (self._flag_state, self._flag_seq)
            lines = [
                ln
                for ln in self._lift_instruction(insn)
                if ln.strip() != "_flag_a = (uint32_t)_fpu_cmp; _flag_b = 0;"
            ]
            self._flag_state, self._flag_seq = saved
            return lines
        if (
            m in ("mul", "imul", "div", "idiv")
            and len(insn.operands) == 1
            and insn.operands[0].size in (1, 2)
        ):
            return self._narrow_muldiv(insn, m)
        if m in ("mul", "imul"):
            return self._muldiv_flags(insn, m)
        if m == "sahf":
            # SAHF writes SF ZF AF PF CF and leaves OF alone; lift32's EFLAGS
            # image dropped it.
            return [
                ln.replace(
                    "_flag_a = (eax >> 8) & 0xD5u;",
                    "_flag_a = ((eax >> 8) & 0xD5u) | "
                    "(recomp_eflags(_flag_k, _flag_a, _flag_b, _cf, _df) & 0x800u);",
                )
                for ln in super().lift_instruction(insn)
            ]
        base = Lifter._string_cmp_base(insn, m)
        if base and m.split()[0] in ("rep", "repe", "repz", "repne", "repnz", base):
            return self._string_cmp(insn, m, base)
        if m in ("faddp", "fsubp", "fsubrp", "fmulp", "fdivp", "fdivrp"):
            ops = insn.operands
            c = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
            if ops and ops[0].type == X86_OP_REG and ops[0].reg == X86_REG_ST0:
                # `fOPp st(0), st(0)`: the result lands in st(0), which the pop
                # then discards; lift32 wrote it into the old st(1).
                return [f"fp_pop(); {c}"]
            if m in ("fdivp", "fdivrp"):
                # IEEE division, as the x87 does with ZE masked: x/0 = +-inf,
                # 0/0 = NaN. lift32 returned the dividend unchanged instead.
                d = self._fpu_popdst(ops)
                expr = f"{d} / _v" if m == "fdivp" else f"_v / {d}"
                return [f"{{ double _v = fp_pop(); {d} = {expr}; }} {c}"]
        return super().lift_instruction(insn)

    def _make_condition(self, jcc_mnemonic):
        c = super()._make_condition(jcc_mnemonic)
        if c.startswith("/* no flag state for"):
            j = jcc_mnemonic
            j = "j" + j[4:] if j.startswith("cmov") else "j" + j[3:] if j.startswith("set") else j
            if j in ("jp", "jpe", "jnp", "jpo"):
                # Unknown setter (a join, or POPFD): PF from the flags word.
                pf = "((recomp_eflags(_flag_k, _flag_a, _flag_b, _cf, _df) >> 2) & 1u)"
                return pf if j in ("jp", "jpe") else f"(!{pf})"
        return c

    def _string_cmp(self, insn, m, base):
        """cmps/scas, lift32's loop with two hardware details added: narrow
        elements are captured left-aligned like every other narrow compare (SF
        came from bit 31 of a 32-bit byte difference), and a rep form with
        ecx = 0 compares nothing and leaves every flag as it was."""
        c = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
        parts = m.split()
        rep = parts[0] if len(parts) > 1 else ""
        if not rep:  # capstone can leave the prefix in the bytes only
            for b in insn.bytes:
                if b == 0xF3:
                    rep = "repe"
                elif b == 0xF2:
                    rep = "repne"
                elif b not in (0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0):
                    break
        size = {"b": 1, "w": 2, "d": 4}[base[-1]]
        mem = {1: "MEM8", 2: "MEM16", 4: "MEM32"}[size]
        step = "_df" if size == 1 else f"(_df * {size})"
        if base.startswith("cmps"):
            load = f"_a = {mem}(esi); _b = {mem}(edi); esi += {step}; edi += {step};"
        else:
            acc = {1: "LO8(eax)", 2: "LO16(eax)", 4: "eax"}[size]
            load = f"_a = {acc}; _b = {mem}(edi); edi += {step};"
        sh = 32 - 8 * size
        cap = (
            f"_flag_a = _a << {sh}; _flag_b = _b << {sh};" if sh else "_flag_a = _a; _flag_b = _b;"
        )
        setf = f"{cap} _cf = (uint32_t)CMP_B(_a, _b); _flag_k = FK_CMP;"
        self._flag_seq += 1
        if not rep:
            self._flag_state = ("cmp", "_flag_a, _flag_b")
            self._flag_width = 8 * size
            return [f"{{ uint32_t _a, _b; {load} {setf} }} {c}"]
        stop = "_a != _b" if rep in ("rep", "repe", "repz") else "_a == _b"
        # ecx may be 0: whether the flags changed is a runtime fact, so the next
        # jcc decides from _flag_k at runtime.
        self._flag_state = None
        return [
            f"{{ uint32_t _a = 0, _b = 0; if (ecx) {{ "
            f"do {{ {load} ecx--; if ({stop}) break; }} while (ecx); {setf} }} }} {c}"
        ]

    def _muldiv_flags(self, insn, m):
        """32-bit mul/imul (and 16-bit 2/3-operand imul) with CF = OF = "the
        product did not fit" (SF/ZF/PF are undefined; derived from the low
        result). lift32 set no flags, leaving the previous instruction's."""
        ops = insn.operands
        c = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
        self._flag_state = None
        fl = (
            "_flag_a = recomp_flags_pack((uint32_t)_r, _o, 0, _o); _flag_b = 0; "
            "_cf = _o; _flag_k = FK_EFLAGS;"
        )
        if len(ops) == 1:
            a = self._fmt_read(ops[0])
            if m == "mul":
                body = (
                    f"uint64_t _r = (uint64_t)eax * (uint64_t)(uint32_t)({a}); eax = (uint32_t)_r; "
                    f"edx = (uint32_t)(_r >> 32); uint32_t _o = edx != 0;"
                )
            else:
                body = (
                    f"int64_t _r = (int64_t)(int32_t)eax * (int64_t)(int32_t)({a}); "
                    "eax = (uint32_t)_r; "
                    "edx = (uint32_t)((uint64_t)_r >> 32); "
                    "uint32_t _o = _r != (int64_t)(int32_t)_r;"
                )
            return [f"{{ {body} {fl} }} {c}"]
        x, y = (ops[0], ops[1]) if len(ops) == 2 else (ops[1], ops[2])
        a, b = self._fmt_read(x), self._fmt_read(y)
        if ops[0].size == 2:
            body = (
                f"int32_t _r = (int32_t)(int16_t)({a}) * (int32_t)(int16_t)({b}); "
                f"uint32_t _o = _r != (int32_t)(int16_t)_r;"
            )
            write = self._fmt_write(ops[0], "(uint16_t)_r")
        else:
            body = (
                f"int64_t _r = (int64_t)(int32_t)({a}) * (int64_t)(int32_t)({b}); "
                f"uint32_t _o = _r != (int64_t)(int32_t)_r;"
            )
            write = self._fmt_write(ops[0], "(uint32_t)_r")
        return [f"{{ {body} {write}; {fl} }} {c}"]

    def _narrow_muldiv(self, insn, m):
        """8/16-bit one-operand mul/imul/div/idiv (lift32 treats all as 32-bit).

        8-bit:  AX = AL * src;          AL = AX / src, AH = AX % src
        16-bit: DX:AX = AX * src;       AX = DX:AX / src, DX = DX:AX % src
        Other bits of eax/edx are preserved. A zero divisor or an overflowing
        quotient (#DE on x86) leaves the registers alone, as the 32-bit path's
        guard does for zero."""
        s = self._fmt_read(insn.operands[0])
        c = f"/* 0x{insn.address:08X}: {m} {insn.op_str} */"
        self._flag_state = None
        if insn.operands[0].size == 1:
            if m == "mul":
                body = f"SET_LO16(eax, (uint16_t)(LO8(eax) * (uint8_t)({s})));"
            elif m == "imul":
                body = f"SET_LO16(eax, (uint16_t)(int16_t)((int8_t)LO8(eax) * (int8_t)({s})));"
            elif m == "div":
                body = (
                    f"uint16_t _n = LO16(eax); uint8_t _d = (uint8_t)({s}); "
                    "if (_d && _n / _d <= 0xFF) { "
                    "SET_LO8(eax, _n / _d); SET_HI8(eax, _n % _d); }"
                )
            else:
                body = (
                    f"int16_t _n = (int16_t)LO16(eax); int8_t _d = (int8_t)({s}); "
                    "if (_d && _n / _d >= -128 && _n / _d <= 127) { "
                    "SET_LO8(eax, (uint8_t)(_n / _d)); SET_HI8(eax, (uint8_t)(_n % _d)); }"
                )
        else:
            if m == "mul":
                body = (
                    f"uint32_t _r = (uint32_t)LO16(eax) * (uint16_t)({s}); "
                    f"SET_LO16(eax, _r); SET_LO16(edx, _r >> 16);"
                )
            elif m == "imul":
                body = (
                    f"int32_t _r = (int32_t)(int16_t)LO16(eax) * (int16_t)({s}); "
                    f"SET_LO16(eax, (uint32_t)_r); SET_LO16(edx, (uint32_t)_r >> 16);"
                )
            elif m == "div":
                body = (
                    "uint32_t _n = ((uint32_t)LO16(edx) << 16) | LO16(eax); "
                    f"uint16_t _d = (uint16_t)({s}); "
                    "if (_d && _n / _d <= 0xFFFF) { "
                    "SET_LO16(eax, _n / _d); SET_LO16(edx, _n % _d); }"
                )
            else:
                body = (
                    "int32_t _n = (int32_t)(((uint32_t)LO16(edx) << 16) | LO16(eax)); "
                    f"int16_t _d = (int16_t)({s}); "
                    "if (_d && _n / _d >= -32768 && _n / _d <= 32767) { "
                    "SET_LO16(eax, (uint32_t)(_n / _d)); SET_LO16(edx, (uint32_t)(_n % _d)); }"
                )
        return [f"{{ {body} }} {c}"]


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else str(paths.disc(1) / "GDIDREAM.EXE")
    bounds_csv = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "bounds.csv")
    out = (
        os.path.abspath(sys.argv[3])
        if len(sys.argv) > 3
        else str(paths.get("out") / "recomp" / "windream" / "gen")
    )
    os.makedirs(out, exist_ok=True)
    info = analyze_pe(exe)
    iat = build_iat_map(info)
    data = open(exe, "rb").read()
    text = next(s for s in info.sections if s.is_code)
    cs = info.image_base + text.virtual_address
    ce = cs + (text.virtual_size or text.raw_size)
    code = data[text.raw_offset : text.raw_offset + text.raw_size]
    entry = info.image_base + info.entry_point_rva if hasattr(info, "entry_point_rva") else None
    print(f"code {text.name!r} 0x{cs:08X}-0x{ce:08X}  IAT {len(iat)}  entry {entry and hex(entry)}")

    ghidra = {}
    for line in open(bounds_csv):
        s, e = (int(x, 16) for x in line.strip().split(","))
        if cs <= s < ce:
            ghidra[s] = min(e, ce)
    print(f"{len(ghidra)} Ghidra functions")

    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True

    def read32(va):
        for s in info.sections:
            lo = info.image_base + s.virtual_address
            if lo <= va and va + 4 <= lo + s.raw_size:
                o = s.raw_offset + va - lo
                return int.from_bytes(data[o : o + 4], "little")
        return None

    def table_targets(base):
        """Entries of a jump table at `base`: consecutive dwords pointing into code."""
        out = []
        for k in range(256):
            v = read32(base + 4 * k)
            if v is None or not cs <= v < ce:
                break
            out.append(v)
        return out

    def extent(t, hard_end):
        """End of the code reachable from `t` (branches followed, calls stepped
        over), capped at hard_end. An entry Ghidra does not know about runs only
        this far: bounding it by the next known function start instead made a
        pointer into an unanalysed stretch lift the whole stretch, once per
        pointer and again every closure round."""
        seen, work, end = set(), [t], t + 1
        while work and len(seen) < 20000:
            pc = work.pop()
            while t <= pc < hard_end and pc not in seen:
                insn = next(md.disasm(code[pc - cs : pc - cs + 16], pc), None)
                if insn is None:
                    break
                seen.add(pc)
                end = max(end, pc + insn.size)
                m = insn.mnemonic
                if m in ("ret", "retf", "hlt", "int3"):
                    break
                op = insn.operands[0] if insn.operands else None
                is_imm = op is not None and op.type == X86_OP_IMM
                if m == "jmp":
                    if is_imm:
                        work.append(op.imm)
                    elif (
                        op is not None
                        and op.type == X86_OP_MEM
                        and bool(op.mem.base) != bool(op.mem.index)
                    ):
                        work.extend(
                            x for x in table_targets(op.mem.disp & 0xFFFFFFFF) if t <= x < hard_end
                        )
                    break
                if is_imm and (m.startswith("j") or m.startswith("loop")):
                    work.append(op.imm)
                pc += insn.size
        return min(end, hard_end)

    bounds = dict(ghidra)
    added = {}
    if os.environ.get("WD_SCAN_DATA_PTRS"):
        # Bounds from a linker map miss static functions reached only through
        # pointers stored in data (the C library's handler tables): seed every
        # data dword that points into code. The game's bounds come from Ghidra
        # and do not need this.
        starts = sorted(bounds)
        # Data sections, plus the parts of the code section no function covers:
        # Watcom puts `static const` data (CONST) in BEGTEXT ahead of the code.
        ranges = []
        for s in info.sections:
            lo = info.image_base + s.virtual_address
            if not s.is_code:
                ranges.append((lo, lo + s.raw_size))
                continue
            pos = lo
            for a in starts:
                if a > pos:
                    ranges.append((pos, a))
                pos = max(pos, bounds[a])
            if pos < lo + s.raw_size:
                ranges.append((pos, lo + s.raw_size))
        for lo, hi in ranges:
            # Byte steps: the CRT's XI/YI initialiser records are 6 bytes
            # ({flag, priority, dd routine}), so their pointers are unaligned.
            for va in range(lo, hi - 3):
                t = read32(va)
                if t is not None and cs <= t < ce and t not in bounds:
                    i = bisect.bisect_right(starts, t) - 1
                    if i >= 0 and t < bounds[starts[i]]:
                        bounds[t] = bounds[starts[i]]
                    else:  # outside every known function: its reachable code
                        j = bisect.bisect_right(starts, t)
                        bounds[t] = extent(t, starts[j] if j < len(starts) else ce)
                    added[t] = bounds[t]
        print(f"{len(added)} entries from code pointers in data")
    literal = re.compile(
        r"RECOMP_(?:ICALL|ITAIL|CALL)(?:_RA)?\((?:sub_([0-9A-F]{8})|0x([0-9A-F]{8})u)[,)]"
    )
    # `jmp [reg*4 + table]` / `jmp [reg + table]`: the lifter resolves targets
    # inside the function locally; a target outside it (Ghidra split the routine)
    # goes through the dispatcher and must be a catalog entry.
    call_ra = re.compile(r"RECOMP_(I?)CALL\((.+)\); /\* 0x([0-9A-F]{8}): call", re.M)
    jtable = re.compile(r"_itail_tgt = \(uint32_t\)\(MEM32\(\w+(?: \* 4)? \+ 0x([0-9A-F]+)\)\)")
    rounds = 0
    t0 = time.time()
    while True:
        rounds += 1
        starts = sorted(bounds)
        lifter = WinDreamLifter(
            iat_map=iat, lifted=set(bounds), precise_carry=True, patch_sites=PATCH_SITES
        )
        bodies = {}
        unimpl = collections.Counter()
        for a in starts:
            insns, leaders = linear_disassemble_function(md, code, cs, a, bounds[a])
            if not insns:
                continue
            # `push <label>; ...; ret` inside one function: VID_DecodeHnm4Image
            # (0x44D921) does `pushal; push 0x44D93B` and both decoders end in
            # `ret`, which lands on 0x44D93B (`inc; popal; ret`). A lifted ret
            # returns to the C caller instead, skipping the popal: the caller
            # then ran with the decoder's registers (ebp = 0) and crashed.
            # Such rets check [esp] against the pushed labels first.
            starts_ = {i.address for i in insns}
            conts = {
                int.from_bytes(i.bytes[1:5], "little")
                for i in insns
                if i.bytes[0] == 0x68 and i.size == 5
            } & starts_
            conts.discard(a)
            if conts:
                leaders = set(leaders) | conts
            lifter.leaders = leaders
            c = lift_function_linear(lifter, f"sub_{a:08X}", insns, leaders, a)
            if conts:
                guard = "".join(
                    f"if (MEM32(esp) == 0x{v:08X}u) {{ esp += 4; goto L_{v:08X}; }} "
                    for v in sorted(conts)
                )
                c = re.sub(
                    r"(RECOMP_FLAGS_OUT\(\); esp \+= 4; \{ RECOMP_REGS_OUT\(\); return; \} "
                    r"/\* 0x[0-9A-F]{8}: ret  \*/)",
                    lambda m, guard=guard: guard + m.group(1),
                    c,
                )
            # Push the real return address, not the 0xDEAD0000 marker: Watcom's
            # CRT reads it (`call get_pc` returns [esp], then `cs:[edi]` reads a
            # constant table placed right after the call, e.g. __CmpBigInt).
            nxt = {i.address: i.address + i.size for i in insns}
            c = call_ra.sub(
                lambda m, nxt=nxt: (
                    f"RECOMP_{m.group(1)}CALL_RA({m.group(2)}, "
                    f"0x{nxt.get(int(m.group(3), 16), 0xDEAD0000):08X}u); "
                    f"/* 0x{m.group(3)}: call"
                ),
                c,
            )
            if a not in ghidra:
                # A closure entry is a fragment entered by a jump, mid-frame: esp
                # does not point at a return address, so the non-local-return
                # check (RECOMP_UNWIND_CHECK) would misfire there.
                c = c.replace(
                    f"RECOMP_ENTER(0x{a:08X}u);", f"RECOMP_ENTER_FRAGMENT(0x{a:08X}u);", 1
                )
            inserts = {
                p: f"    wd_probe(0x{p:08X}u, eax, ecx, edx, ebx, esp, ebp, esi, edi);"
                for p in PROBES
            }
            inserts.update(
                {p: f"    RECOMP_REGS_OUT(); {fn}(); RECOMP_REGS_IN();" for p, fn in CALLS.items()}
            )
            for p, line in inserts.items():
                tag = f"/* 0x{p:08X}:"
                if tag in c:
                    lines_ = c.split("\n")
                    k = next(
                        i
                        for i, ln in enumerate(lines_)
                        if tag in ln and not ln.lstrip().startswith("L_")
                    )
                    lines_.insert(k, line)
                    c = "\n".join(lines_)
            if a in HOOKS:
                c = c.replace(
                    f"RECOMP_ENTER(0x{a:08X}u);",
                    f"RECOMP_ENTER(0x{a:08X}u); {HOOKS[a]}(eax, edx, ebx, MEM32(esp));",
                    1,
                )
            bodies[a] = c
            for m in re.finditer(r"UNIMPLEMENTED: (\S+)", c):
                unimpl[m.group(1)] += 1
        new = set()
        if os.environ.get("WD_SCAN_DATA_PTRS"):
            # ... and code addresses loaded as immediates (`mov eax, offset f`).
            for c in bodies.values():
                for m in re.finditer(r"[=,(] ?0x([0-9A-F]{8})u", c):
                    t = int(m.group(1), 16)
                    if cs <= t < ce and t not in bounds:
                        new.add(t)
        for a, c in bodies.items():
            for m in literal.finditer(c):
                t = int(m.group(1) or m.group(2), 16)
                if cs <= t < ce and t not in bounds:
                    new.add(t)
            for m in jtable.finditer(c):
                for t in table_targets(int(m.group(1), 16)):
                    if not a <= t < bounds[a] and t not in bounds:
                        new.add(t)
        print(f"round {rounds}: {len(bodies)} bodies, {len(new)} new targets")
        if not new:
            break
        for t in sorted(new):
            i = bisect.bisect_right(starts, t) - 1
            if i >= 0 and t < bounds[starts[i]]:
                end = bounds[starts[i]]  # inside a function: run to its end
            else:
                j = bisect.bisect_right(starts, t)
                end = extent(t, starts[j] if j < len(starts) else ce)
            bounds[t] = end
            added[t] = end

    lifted = sorted(bodies)
    head = (
        '#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'
        '#include "recomp_funcs.h"\n#include <math.h>\n#include <string.h>\n\n'
    )
    head += (
        "".join(f"void {h}(uint32_t, uint32_t, uint32_t, uint32_t);\n" for h in HOOKS.values())
        + "\n"
    )
    head += (
        "void wd_probe(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, "
        "uint32_t, uint32_t);\n"
    )
    head += "".join(f"void {fn}(void);\n" for fn in sorted(set(CALLS.values()))) + "\n"
    head += '#include "render_boundary.h"\n'
    for address in RENDER_ENTRIES.intersection(bodies):
        bodies[address] = wrap_entry(bodies[address], address)
    for n in os.listdir(out):
        if re.match(r"recomp_\d{4}\.c$", n):
            os.remove(os.path.join(out, n))
    lines = 0
    for i in range(0, len(lifted), SPLIT):
        part = [bodies[a] for a in lifted[i : i + SPLIT]]
        lines += sum(p.count("\n") + 1 for p in part)
        with open(os.path.join(out, f"recomp_{i // SPLIT:04d}.c"), "w") as f:
            f.write(head + "\n\n".join(part) + "\n")
    with open(os.path.join(out, "recomp_funcs.h"), "w") as f:
        f.write("#pragma once\n")
        for a in lifted:
            f.write(f"void sub_{a:08X}(void);\n")
            if a in RENDER_ENTRIES:
                f.write(f"void wd_original_{a:08X}(void);\n")
    with open(os.path.join(out, "recomp_dispatch.c"), "w") as f:
        f.write(
            '#include "recomp_types.h"\n#include "recomp_funcs.h"\n'
            "const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n"
        )
        for a in lifted:
            f.write(f"    {{ 0x{a:08X}u, sub_{a:08X} }},\n")
        f.write(f"}};\nconst uint32_t recomp_dispatch_count = {len(lifted)};\n")
        f.write("recomp_func_t recomp_lookup_reference(uint32_t va) { switch (va) {\n")
        for a in sorted(RENDER_ENTRIES.intersection(lifted)):
            f.write(f"case 0x{a:08X}u: return wd_original_{a:08X};\n")
        f.write("default: return NULL; } }\n")
    json.dump(
        {
            "ghidra": len(ghidra),
            "added": {f"0x{k:08X}": f"0x{v:08X}" for k, v in sorted(added.items())},
            "functions": len(lifted),
            "lines": lines,
            "unimplemented": dict(unimpl),
        },
        open(os.path.join(os.path.dirname(out), "lift-report.json"), "w"),
        indent=1,
    )
    print(
        f"{len(lifted)} functions ({len(added)} added by closure), {lines:,} lines, "
        f"{(len(lifted) + SPLIT - 1) // SPLIT} files, {time.time() - t0:.1f}s"
    )
    print(f"unimplemented: {dict(unimpl)}")


if __name__ == "__main__":
    main()
