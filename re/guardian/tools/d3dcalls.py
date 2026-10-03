"""Annotate IDirect3DDevice3 calls in a PE image, decoding arguments from the SDK headers.

Method order comes from DECLARE_INTERFACE_(IDirect3DDevice3, ...) in d3d.h and the
enum values from d3dtypes.h (Windows SDK, legacy DirectX 6/7 headers), so no
value is typed from memory. Arguments are the pushes between the previous call
and the COM call (stdcall: the last push is `this`). A push of a register or a
memory operand is printed as is, preceded by the nearest constant moved into
that register when there is one.

  uv run --with pefile --with capstone python re/guardian/tools/d3dcalls.py EXE ADDR[:SIZE]... @LIST

SIZE defaults to the next `ret` that is not followed by more code of the same
function; give it for functions whose body continues after a ret.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

SDK = Path(r"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um")


def methods(header: str, iface: str) -> list[str]:
    text = (SDK / header).read_text(encoding="latin-1")
    start = text.index(f"DECLARE_INTERFACE_({iface},")
    body = text[start : text.index("};", start)]
    return re.findall(r"STDMETHOD(?:_\([^,]+,\s*|\()(\w+)\)", body)


def enum(name: str) -> dict[int, str]:
    text = (SDK / "d3dtypes.h").read_text(encoding="latin-1")
    m = re.search(r"typedef enum _?" + name + r"\s*\{(.*?)\}", text, re.S)
    out, value = {}, -1
    for line in m.group(1).splitlines():
        line = line.split("//")[0].split("/*")[0].strip().rstrip(",")
        if not line or line.startswith("#"):
            continue
        k, _, v = line.partition("=")
        k, v = k.strip(), v.strip()
        if not re.fullmatch(r"\w+", k):
            continue
        if v:
            try:
                value = int(v.rstrip("lLuU"), 0)
            except ValueError:
                continue  # alias of another member, e.g. D3DRENDERSTATE_WRAPBIAS + 1
        else:
            value += 1
        out.setdefault(value, k)
    return out


def defines(prefix: str) -> dict[int, str]:
    text = (SDK / "d3dtypes.h").read_text(encoding="latin-1")
    return {
        int(v, 0): k
        for k, v in re.findall(r"#define\s+(" + prefix + r"\w+)\s+(0x[0-9a-fA-F]+|\d+)\b", text)
    }


DEV3 = methods("d3d.h", "IDirect3DDevice3")
RS = enum("D3DRENDERSTATETYPE")
TSS = enum("D3DTEXTURESTAGESTATETYPE")
VALUES = {
    "BLEND": enum("D3DBLEND"),
    "FUNC": enum("D3DCMPFUNC"),
    "REF": None,
    "CULLMODE": enum("D3DCULL"),
    "SHADEMODE": enum("D3DSHADEMODE"),
    "FILLMODE": enum("D3DFILLMODE"),
    "FOGTABLEMODE": enum("D3DFOGMODE"),
    "FOGVERTEXMODE": enum("D3DFOGMODE"),
    "TEXTUREMAPBLEND": enum("D3DTEXTUREBLEND"),
    "TEXTUREMAG": enum("D3DTEXTUREFILTER"),
    "TEXTUREMIN": enum("D3DTEXTUREFILTER"),
    "ADDRESS": enum("D3DTEXTUREADDRESS"),
    "OP": enum("D3DTEXTUREOP"),
    "MAGFILTER": enum("D3DTEXTUREMAGFILTER"),
    "MINFILTER": enum("D3DTEXTUREMINFILTER"),
    "MIPFILTER": enum("D3DTEXTUREMIPFILTER"),
}
TA = defines("D3DTA_")
PRIM = enum("D3DPRIMITIVETYPE")


def value_names(state: str) -> dict[int, str] | None:
    for key, table in sorted(VALUES.items(), key=lambda kv: -len(kv[0])):
        if state.endswith(key):
            return table
    return None


def decode_ta(v: int) -> str:
    base = TA.get(v & 0x0F, hex(v & 0x0F))
    flags = [n for b, n in TA.items() if b in (0x10, 0x20) and v & b]
    return "|".join([base, *flags])


def num(arg: str) -> int | None:
    try:
        return int(arg, 0)
    except ValueError:
        return None


def describe(method: str, args: list[str]) -> str:
    vals = [num(a) for a in args]
    if method == "SetRenderState" and len(args) >= 2 and vals[0] is not None:
        state = RS.get(vals[0], hex(vals[0]))
        names = value_names(state)
        v = names.get(vals[1], args[1]) if names and vals[1] is not None else args[1]
        if state.endswith("ENABLE") and vals[1] in (0, 1):
            v = ("FALSE", "TRUE")[vals[1]]
        return f"SetRenderState({state}, {v})"
    if method == "SetTextureStageState" and len(args) >= 3 and vals[1] is not None:
        state = TSS.get(vals[1], hex(vals[1]))
        if "ARG" in state and vals[2] is not None:
            v = decode_ta(vals[2])
        else:
            names = value_names(state)
            v = names.get(vals[2], args[2]) if names and vals[2] is not None else args[2]
        return f"SetTextureStageState(stage {args[0]}, {state}, {v})"
    if method in ("DrawPrimitive", "DrawIndexedPrimitive") and args and vals[0] is not None:
        rest = ", ".join(args[1:])
        return f"{method}({PRIM.get(vals[0], args[0])}, {rest})"
    return f"{method}({', '.join(args)})"


def main() -> None:
    pe = pefile.PE(sys.argv[1])
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    specs = []
    for arg in sys.argv[2:]:
        specs += Path(arg[1:]).read_text().split() if arg.startswith("@") else [arg]
    for spec in specs:
        a, _, size = spec.partition(":")
        start = int(a, 16)
        size = int(size, 0) if size else 0x4000
        print(f"=== {start:08x}")
        pushes: list[str] = []
        regs: dict[str, str] = {}
        for ins in md.disasm(bytes(img[start - base : start - base + size]), start):
            op = ins.op_str
            if ins.mnemonic == "mov" and re.fullmatch(r"e[a-z]{2}, (0x[0-9a-f]+|\d+)", op):
                r, v = op.split(", ")
                regs[r] = v
            elif ins.mnemonic in ("xor",) and re.fullmatch(r"(e[a-z]{2}), \1", op):
                regs[op.split(",")[0]] = "0"
            elif ins.mnemonic == "push":
                pushes.append(regs.get(op, op) if op in regs else op)
            elif ins.mnemonic == "call":
                m = re.fullmatch(r"dword ptr \[e[a-z]{2} \+ (0x[0-9a-f]+)\]", op)
                if m:
                    idx = int(m.group(1), 16) // 4
                    name = DEV3[idx] if idx < len(DEV3) else f"vtbl+{m.group(1)}"
                    # stdcall pushes the last argument first and `this` last
                    args = pushes[:-1][::-1]
                    print(f"  {ins.address:08x}  {describe(name, args)}")
                else:
                    print(f"  {ins.address:08x}  call {op}")
                pushes, regs = [], {}
            if ins.mnemonic == "ret" and ":" not in spec:
                break


if __name__ == "__main__":
    main()
