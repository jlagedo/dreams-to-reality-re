# ruff: noqa: E501 (the embedded C test program and the form regexes)
"""Generate t_insn.c: one test per instruction form the game uses.

Every form coverage.py finds in the game's reachable code gets one or more
instances, each an inline-asm routine (`#pragma aux`) that loads registers,
flags and a scratch buffer from a State block, runs the instruction, and stores
everything back. main() runs each instance on many inputs and prints one hash
per instance; difftest.py compares the native Watcom build with the lifted one.

  - register/immediate forms reuse the game's own instruction bytes (`db`), so
    the exact encoding the game runs is what gets tested;
  - memory forms are written out with the operand at [edi+N] (edi points at the
    scratch buffer);
  - flags are compared through a per-instance mask of the flags the instruction
    defines (Intel SDM), so undefined flags cannot cause false alarms;
  - x87 forms run on a stack of four loaded doubles, then store the whole stack
    and the condition codes.

usage:
  uv run --with capstone --with pefile python recomp/difftest/gen_insn.py
      GAME.EXE game_bounds.csv [out.c]
"""

import re
import sys

import coverage
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM

coverage.COARSE = True

CF, PF, AF, ZF, SF, OF, DF = 0x1, 0x4, 0x10, 0x40, 0x80, 0x800, 0x400
ALL = CF | PF | AF | ZF | SF | OF

R = {
    "r32": ["eax", "ecx", "edx", "ebx", "esi", "ebp"],
    "r16": ["ax", "cx", "dx", "bx", "si", "bp"],
    "r8": ["al", "cl", "dl", "bl"],
    "r8h": ["ah", "ch", "dh", "bh"],
}
PTR = {8: "byte", 16: "word", 32: "dword", 64: "qword", 80: "tbyte"}

SKIP = re.compile(
    r"^(les|lds|lss|lfs|lgs|call|jmp (?!rel)|ljmp|lcall|ret|retf|int|into|in |out|ins|outs|rdtsc|cli|sti|hlt|bound|arpl|"
    r"sldt|fninit|fnclex|fldcw|fnstcw|fwait|wait|lock|salc|iret|frstor|fnsave|fsave|fldenv|fnstenv|fstenv)"
)
UNARY_FLAGS = {"inc": ALL & ~CF, "dec": ALL & ~CF}  # CF preserved -> still defined
FPU_PUSH = {
    "fld",
    "fild",
    "fld1",
    "fldz",
    "fldpi",
    "fldl2e",
    "fldl2t",
    "fldlg2",
    "fldln2",
    "fsincos",
    "fptan",
    "fxtract",
}
FPU_POP2 = {"fcompp", "fucompp"}
FPU_POP1 = {"fpatan", "fyl2x", "fyl2xp1"}
APPROX = {"fsin", "fcos", "fsincos", "fptan", "fpatan", "f2xm1", "fyl2x", "fyl2xp1"}
FPU_INT = {
    "fild",
    "fist",
    "fistp",
    "fiadd",
    "fisub",
    "fisubr",
    "fimul",
    "fidiv",
    "fidivr",
    "ficom",
    "ficomp",
}


def flag_mask(m, count=None):
    """Flags the instruction defines (the rest are architecturally undefined)."""
    base = m.split()[-1]
    if base in UNARY_FLAGS:
        return UNARY_FLAGS[base]
    if base in ("and", "or", "xor", "test"):
        return ALL & ~AF
    if base in ("shl", "sal", "shr", "sar", "shld", "shrd"):
        return (CF | PF | ZF | SF) | (OF if count == 1 else 0) if count != 0 else ALL
    if base in ("rol", "ror", "rcl", "rcr"):
        return (ALL & ~OF) | (OF if count == 1 else 0)
    if base in ("mul", "imul"):
        return CF | OF
    if base in ("div", "idiv"):
        return 0
    if base in ("bsf", "bsr"):
        return ZF
    if base in ("bt", "bts", "btr", "btc"):
        return CF
    if base in ("aam", "aad"):
        return SF | ZF | PF
    if base in ("daa", "das"):
        return ALL & ~OF
    if base in ("aaa", "aas"):
        return AF | CF
    if base in ("cld", "std"):
        return ALL | DF
    return ALL


def mem_text(size, off, seg=""):
    return f"{PTR[size]} ptr {seg}[edi+{off}]"


def fpu_mem_off(m, size):
    if m in FPU_INT or m.startswith("fi"):
        return 152 - 128
    return {32: 128, 64: 136, 80: 232}.get(size, 128) - 128


class Gen:
    def __init__(self):
        self.tests = []  # (name, asm lines, flag mask, kind)

    def add(self, name, lines, mask, kind=""):
        self.tests.append((name, lines, mask, kind))


def imm_values(m, size):
    if m in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr"):
        return {8: [1, 3, 7], 16: [1, 5, 15], 32: [1, 4, 31]}[size]
    return {8: [0x5A, 0xFF, 1], 16: [0x1234, 0xFFFF, 0x8000], 32: [0x12345678, 0xFFFFFFFF, 3]}[size]


def operand_size(kind):
    m = re.match(r"(?:\w+:)?[rm](\d+)", kind)
    return int(m.group(1)) if m else (8 if kind == "r8h" else 32)


def build(gen, form, insn):
    """Instances for one form. insn: the game's first instruction of that form."""
    parts = form.split(" ", 1)
    m = parts[0]
    kinds = parts[1].split(",") if len(parts) > 1 else []
    if m in ("rep", "repe", "repne"):
        m = form.split(" ")[0] + " " + form.split(" ")[1]
        kinds = []
    base = m.split()[-1]
    fpu = base.startswith("f")
    if SKIP.match(form) or any(k in ("sreg", "?") for k in kinds) or "fs:" in form or "gs:" in form:
        return
    # string instructions: operands implicit
    if re.match(r"(rep\w* )?(movs|stos|lods|cmps|scas)[bwd]$", m):
        pre = ["lea esi,[edi+64]", "add edi,32", "and ecx,7"]
        gen.add(form, pre + ["popfd", m], ALL | DF)
        return
    if m == "xlatb":
        gen.add(form, ["lea ebx,[edi+16]", "popfd", "xlatb"], ALL)
        return
    if base in ("push", "pop") or m in (
        "pushal",
        "popal",
        "pushfd",
        "popfd",
        "pushf",
        "popf",
        "enter",
        "leave",
    ):
        stack_form(gen, form, m, kinds)
        return
    if kinds == ["rel"]:
        branch(gen, form, m)
        return
    if fpu:
        fpu_form(gen, form, m, kinds, insn)
        return
    has_mem = any("m" in k for k in kinds)
    if (
        not has_mem
        and insn is not None
        and "esp" not in insn.op_str
        and base not in ("div", "idiv", "bsf", "bsr", "bt", "bts", "btr", "btc")
    ):
        # the game's own bytes: its exact encoding and registers
        cnt = None
        if base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr", "shld", "shrd"):
            imm = [o for o in insn.operands if o.type == X86_OP_IMM]
            cnt = (imm[0].imm & 31) if imm else None
        pre = []
        if cnt is None and base in (
            "shl",
            "sal",
            "shr",
            "sar",
            "rol",
            "ror",
            "rcl",
            "rcr",
            "shld",
            "shrd",
        ):
            # count in cl: keep it below the operand width (CF is undefined past it)
            w = operand_size(kinds[0])
            pre = [f"and ecx,{w - 1}"]
        if base in ("aam", "aad") and insn.bytes[1] == 0:
            return
        gen.add(
            form + " @game",
            pre + ["popfd", "db " + ",".join(f"0x{b:02X}" for b in insn.bytes)],
            flag_mask(m, cnt) if cnt is not None or pre == [] else flag_mask(m, None) & ~OF,
        )
        if (
            pre == []
            and cnt is None
            and not has_mem
            and len(kinds) >= 1
            and all(k.startswith("r") for k in kinds)
            and base not in ("xchg",)
        ):
            pass
    # written-out instances (all memory forms; extra variants of register forms)
    for v in range(3):
        ops, pre, cnt = [], [], None
        for k, kind in enumerate(kinds):
            seg = ""
            mm = re.match(r"(\w+):(m\d+)", kind)
            if mm:
                seg, kind = mm.group(1) + ":", mm.group(2)
            if kind in R:
                pool = R[kind]
                if base in ("div", "idiv", "mul", "imul") and len(kinds) == 1:
                    pool = [
                        r
                        for r in pool
                        if r not in ("eax", "edx", "ax", "dx", "al", "ah", "dl", "dh")
                    ]
                if (
                    base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr", "shld", "shrd")
                    and k == len(kinds) - 1
                    and kind == "r8"
                ):
                    ops.append("cl")
                    w = operand_size(kinds[0])
                    pre.append(f"and ecx,{w - 1}")
                    continue
                if base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr", "shld", "shrd"):
                    pool = [r for r in pool if r not in ("ecx", "cx", "cl", "ch")]
                ops.append(pool[(v * 2 + k * 3) % len(pool)])
            elif re.match(r"m\d+", kind):
                size = int(kind[1:])
                if base == "lea":
                    ops.append(["[ecx+edx*4+8]", "[ebx*8]", "[esi+ebp*2-3]"][v])
                    continue
                if size not in PTR:
                    return
                ops.append(mem_text(size, [4, 8, 13][v], seg))
            elif kind == "imm":
                size = operand_size(kinds[0]) if k else 32
                if base in ("shld", "shrd"):
                    val = [1, 7, 31][v]
                elif base in ("aam", "aad"):
                    val = [10, 16, 7][v]
                elif base in ("bt", "bts", "btr", "btc"):
                    val = [0, 7, 31][v] % operand_size(kinds[0])
                elif base == "imul" and len(kinds) == 3:
                    val = [3, 0xFFFFFFF9 if operand_size(kinds[0]) == 32 else 0xFFF9, 1000][v]
                else:
                    val = imm_values(base, size)[v]
                if base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr", "shld", "shrd"):
                    cnt = val & 31
                ops.append(f"0x{val:X}")
            else:
                return
        if base in ("div", "idiv"):
            pre += div_prelude(base, kinds[0], ops[0])
        if base in ("bsf", "bsr"):
            src = ops[1]
            pre.append(
                f"or {src},0x800"
                if not src.startswith(("byte", "word", "dword"))
                else f"or {src},0x800"
            )
        if base in ("bt", "bts", "btr", "btc") and kinds[1] in R:
            w = operand_size(kinds[0])
            pre.append(f"and {ops[1]},{w - 1}")
        # same-register variant for two-register forms (xor eax,eax; sub cx,cx)
        if (
            v == 2
            and len(kinds) == 2
            and kinds[0] == kinds[1]
            and kinds[0] in R
            and "cl" not in ops
        ):
            ops[1] = ops[0]
        gen.add(
            f"{form} #{v}",
            pre + ["popfd", f"{m} " + ",".join(ops)],
            flag_mask(m, cnt)
            if cnt is not None
            else (
                flag_mask(m, None) & ~OF
                if base in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr", "shld", "shrd")
                else flag_mask(m, None)
            ),
        )
        if not kinds:
            return


def div_prelude(base, kind, op):
    """Operands that cannot fault: divisor >= 0x100 (positive for idiv) and a
    dividend whose quotient fits."""
    size = operand_size(kind)
    if size == 8:
        pre = [f"and {op},0x7F", f"or {op},0x40"]
        pre += ["and ah,0x3F"] if base == "div" else ["cbw"]
        return pre
    hi = {16: "dx", 32: "edx"}[size]
    mask = {16: "0x7FFF", 32: "0x7FFFFFFF"}[size]
    pre = [f"and {op},{mask}", f"or {op},0x100"]
    pre += [f"and {hi},0xFF"] if base == "div" else (["cwd"] if size == 16 else ["cdq"])
    return pre


def stack_form(gen, form, m, kinds):
    if m == "pushal":
        gen.add(
            form,
            [
                "popfd",
                "pushad",
                "mov eax,[esp+28]",
                "mov ecx,[esp]",
                "mov edx,[esp+4]",
                "mov ebx,[esp+16]",
                "mov esi,[esp+24]",
                "mov ebp,[esp+20]",
                "lea esp,[esp+32]",
            ],
            ALL,
        )
    elif m == "popal":
        gen.add(
            form,
            [
                "popfd",
                "push ebx",
                "push ecx",
                "push edx",
                "push esi",
                "push ebp",
                "push eax",
                "push ecx",
                "push edx",
                "popad",
            ],
            ALL,
        )
    elif m == "pushfd":
        gen.add(form, ["popfd", "pushfd", "pop eax", "and eax,0x8D5"], ALL)
    elif m == "popfd":
        gen.add(form, ["popfd", "and eax,0x8D5", "push eax", "popfd"], ALL)
    elif m == "pushf":
        gen.add(form, ["popfd", "pushf", "pop cx", "and ecx,0x8D5"], ALL)
    elif m == "popf":
        gen.add(form, ["popfd", "and eax,0x8D5", "push ax", "popf"], ALL)
    elif m == "enter":
        gen.add(form, ["popfd", "enter 8,0", "mov [ebp-4],eax", "mov ecx,[ebp-4]", "leave"], ALL)
    elif m == "leave":
        gen.add(
            form,
            [
                "popfd",
                "push ebp",
                "mov ebp,esp",
                "sub esp,16",
                "mov [ebp-8],ecx",
                "mov edx,[ebp-8]",
                "leave",
            ],
            ALL,
        )
    elif m == "push":
        k = kinds[0]
        src = {
            "r32": "ecx",
            "r16": "cx",
            "imm": "0x1234",
            "m32": "dword ptr [edi+4]",
            "m16": "word ptr [edi+4]",
        }.get(k)
        dst = "ax" if k in ("r16", "m16") else "eax"
        if src:
            gen.add(form, ["popfd", f"push {src}", f"pop {dst}"], ALL)
    elif m == "pop":
        k = kinds[0]
        dst = {
            "r32": "ecx",
            "r16": "cx",
            "m32": "dword ptr [edi+4]",
            "m16": "word ptr [edi+4]",
        }.get(k)
        src = "ax" if k in ("r16", "m16") else "eax"
        if dst:
            gen.add(form, ["popfd", f"push {src}", f"pop {dst}"], ALL)


def branch(gen, form, m):
    n = len(gen.tests)
    pre = ["and ecx,3"] if m.startswith(("loop", "jecxz", "jcxz")) else []
    gen.add(
        form,
        pre + ["popfd", f"{m} L{n}a", "mov ebx,1", f"jmp L{n}b", f"L{n}a:", "mov ebx,2", f"L{n}b:"],
        ALL,
    )


def fpu_form(gen, form, m, kinds, insn):
    if m in FPU_PUSH:
        delta = 2 if m == "fsincos" and False else 1
    elif m in FPU_POP2:
        delta = -2
    elif m in FPU_POP1 or m.endswith("p") or m in ("fistp", "fcomp", "ficomp", "fucomp", "fbstp"):
        delta = -1
    else:
        delta = 0
    if m in ("fsincos", "fptan", "fxtract"):
        delta = 1
    if m.startswith("fnstsw") or m.startswith("fstsw"):
        delta = 0
    has_mem = any("m" in k for k in kinds)
    if has_mem:
        k = next(k for k in kinds if "m" in k)
        size = int(re.search(r"m(\d+)", k).group(1))
        if size not in PTR:
            return
        off = fpu_mem_off(m, size)
        if m in FPU_INT and size == 64 and m not in ("fild", "fistp"):
            return
        op = mem_text(size, off)
        if m.startswith(("fnstsw", "fstsw")):
            return
        body = [f"{m} {op}"]
        pre = []
        if size == 80 and m == "fld":
            pre = ["fld qword ptr [edi+8]", "fstp tbyte ptr [edi+104]"]
        if m == "fld" and size == 80:
            pass
    elif m.startswith(("fnstsw", "fstsw")):
        body = ["fcom st(1)", "db " + ",".join(f"0x{b:02X}" for b in insn.bytes), "and eax,0x4500"]
        pre = []
    elif insn is not None and "esp" not in insn.op_str:
        body = ["db " + ",".join(f"0x{b:02X}" for b in insn.bytes)]
        pre = []
    else:
        return
    depth = 4 + delta
    load = [f"fld qword ptr [edi+{32 + 8 * i}]" for i in range(4)]
    store = ["fnstsw word ptr [edi+120]"] + [
        f"fstp qword ptr [edi+{64 + 8 * i}]" for i in range(depth)
    ]
    # condition codes only where the instruction defines them (compares, fxam, fprem)
    cc = m.startswith(("fcom", "fucom", "ficom", "ftst", "fxam", "fprem"))
    kind = "fpu" + (" approx" if m in APPROX else "") + (" cc" if cc else "")
    gen.add(
        form + (" @game" if not has_mem else ""),
        ["add edi,128"] + pre + load + ["popfd"] + body + store,
        ALL,
        kind,
    )


HEAD = r"""/* Generated by gen_insn.py: one test per instruction form the game uses. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { unsigned long r[7], pad, fl, sw, pad2[6]; unsigned long mem[64]; } State;
static State st;
static unsigned long H;
static void mix(const void* p, int n) {
    const unsigned char* b = (const unsigned char*)p;
    while (n--) H = (H ^ *b++) * 16777619UL;
}
static unsigned long seed = 99991;
static unsigned long rnd(void) { seed = seed * 1103515245UL + 12345UL; return (seed >> 16) ^ (seed << 16); }
static double rndd(void) { return (double)((long)(rnd() % 200001) - 100000) / 997.0; }

/* load r[] and fl, run, store; eax holds the State pointer */
#define PRE "pushad" "push eax" "push dword ptr [eax+32]" \
    "mov ecx,[eax+4]" "mov edx,[eax+8]" "mov ebx,[eax+12]" "mov esi,[eax+16]" "mov edi,[eax+20]" \
    "mov ebp,[eax+24]" "mov eax,[eax]"
#define POST "pushfd" "cld" "xchg eax,[esp+4]" "mov [eax+4],ecx" "mov [eax+8],edx" "mov [eax+12],ebx" \
    "mov [eax+16],esi" "mov [eax+20],edi" "mov [eax+24],ebp" "pop dword ptr [eax+32]" "pop dword ptr [eax]" "popad"
"""

MAIN = r"""
typedef struct { const char* name; void (*fn)(State*); unsigned long mask; int kind; } Test;
Test tests[] = {   /* not const: Watcom puts const data in the code section */
%s
};

static const unsigned long edge[] = { 0, 0xFFFFFFFFUL, 0x80000000UL, 0x7FFFFFFFUL, 1, 0x7F, 0x80, 0xFF, 0x8000 };

int main(void) {
    int t, k, i;
    const char* only = getenv("T_ONLY");   /* T_ONLY=<name prefix>: print every trial */
    unsigned long in[9];
    setvbuf(stdout, NULL, _IONBF, 0);
    for (t = 0; t < sizeof tests / sizeof tests[0]; t++) {
        if (only && strncmp(tests[t].name, only, strlen(only))) continue;
        H = 2166136261UL;
        seed = 99991 + t;
        for (k = 0; k < 40; k++) {
            for (i = 0; i < 7; i++) st.r[i] = k < 9 ? edge[k] ^ (i & k ? rnd() & 0xFF : 0) : rnd();
            if (k >= 9 && k < 20) for (i = 0; i < 7; i++) st.r[i] &= 0xFF;   /* small: counts, bytes */
            for (i = 0; i < 64; i++) st.mem[i] = k < 9 ? edge[(k + i) %% 9] : rnd();
            st.r[5] = (unsigned long)&st.mem[0];                          /* edi: the scratch buffer */
            st.fl = (rnd() & 0x8D5) | 0x202;
            *(float*)&st.mem[32] = (float)rndd();                          /* [edi+128] */
            *(double*)&st.mem[34] = rndd();                                /* [edi+136] */
            *(long*)&st.mem[38] = (long)(rnd() %% 20001) - 10000;          /* [edi+152] */
            st.mem[39] = rnd() & 1 ? 0 : 0xFFFFFFFFUL;                     /* int64 high */
            for (i = 0; i < 4; i++) *(double*)&st.mem[40 + 2 * i] = k == 3 ? 0.0 : (k == 4 ? 1.0 : rndd());
            if (tests[t].kind & 2) *(double*)&st.mem[46] = k & 1 ? 0.5 : 0.25 * (double)((long)(rnd() %% 7) - 3);
            memcpy(in, &st, sizeof in);
            tests[t].fn(&st);
            st.fl &= tests[t].mask;
            if (only) {
                printf("%%-26s k=%%2d in", tests[t].name, k);
                for (i = 0; i < 7; i++) if (i != 5) printf(" %%08lx", in[i]);
                printf(" fl=%%03lx out", in[8] & 0x8D5);
                for (i = 0; i < 7; i++) if (i != 5) printf(" %%08lx", st.r[i]);
                printf(" fl=%%03lx m=%%08lx\n", st.fl, st.mem[1]);
                if (tests[t].kind) {
                    printf("    x87 sw=%%04lx st:", st.mem[62]);
                    for (i = 48; i < 58; i += 2) printf(" %%08lx%%08lx", st.mem[i + 1], st.mem[i]);
                    printf(" m: %%08lx %%08lx%%08lx %%08lx%%08lx\n", st.mem[32], st.mem[35], st.mem[34], st.mem[39], st.mem[38]);
                }
            }
            if (tests[t].kind) {                                            /* x87 */
                st.mem[62] &= tests[t].kind & 4 ? 0x4500 : 0;              /* C3 C2 C0 where defined */
                st.mem[62] &= 0xFFFF;
                for (i = 48; i < 62; i += 2) {
                    unsigned long hi = st.mem[i + 1];
                    if ((hi & 0x7FF00000UL) == 0x7FF00000UL && ((hi & 0xFFFFF) || st.mem[i])) {
                        st.mem[i] = 0; st.mem[i + 1] = 0x7FF80000UL;        /* one NaN */
                    } else if (tests[t].kind & 2) {
                        st.mem[i] = 0; st.mem[i + 1] &= 0xFFFFFFF0UL;       /* approx: ~30 bits */
                    }
                }
            }
            st.r[5] -= (unsigned long)&st.mem[0];   /* +128 for x87 tests */
            mix(&st, sizeof st);
        }
        printf("%%-34s %%08lx\n", tests[t].name, H);
    }
    return 0;
}
"""


def main():
    game, gb = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else str(coverage.WORK / "t_insn.c")
    import pefile

    pe = pefile.PE(game, fast_load=True)
    text = next(s for s in pe.sections if s.Characteristics & 0x20)
    base = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    code = text.get_data()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    first = {}
    for s, e in coverage.function_bounds(gb, base, len(code)):
        for insn in coverage.reachable(md, code, base, s, e):
            first.setdefault(coverage.form(insn), insn)
    gen = Gen()
    for f in sorted(first):
        build(gen, f, first[f])
    lines = [HEAD]
    entries = []
    for i, (name, asm, mask, kind) in enumerate(gen.tests):
        body = " ".join('"' + a + '"' for a in asm)
        lines.append(
            f"void T{i}(State*);\n#pragma aux T{i} = PRE {body} POST parm [eax] modify exact [];\n"
            f"static void W{i}(State* s) {{ T{i}(s); }}\n"
        )
        k = (
            (1 if "fpu" in kind else 0)
            | (2 if "approx" in kind else 0)
            | (4 if " cc" in kind else 0)
        )
        entries.append(f'    {{ "{name}", W{i}, 0x{mask:X}UL, {k} }},')
    lines.append(MAIN % "\n".join(entries))
    open(out, "w").write("\n".join(lines))
    print(f"{len(first)} forms -> {len(gen.tests)} tests -> {out}")


if __name__ == "__main__":
    main()
