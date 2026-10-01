"""Matching decompilation: compile C with Watcom 11.0 (the Windows builds'
compiler) and byte-compare one function against WINDREAM.EXE.

Bytes patched by an OMF fixup in the compiled .obj (calls, globals) are
wildcards; everything else must be identical. Prints a score and, on a
mismatch, a side-by-side disassembly.

usage: uv run --with capstone --with pefile python re/matchdecomp/match.py \
           <file.c> <func> <va> [--flags "-5r -d2"] [--cc wc110] [--quiet]
  <func>  public symbol in the .obj (Watcom register call adds a trailing _)
  <va>    retail address (hex); the extent comes from windream/bounds.csv
  --flags defaults to the unoptimized, stack-checked profile most game code
          uses (-d2: full debug info, which also keeps -od's branch shapes);
          optimized modules match "-5r -otexan -s" (docs/research/toolchain.md)
  --cc    wc110 (DREAMS_WATCOM_COMPILER, the default), wc106
          (DREAMS_WATCOM_COMPILER_106), wc110a, ow19, ow2
"""
import argparse
import csv
import difflib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'src'))

import pefile  # noqa: E402
from capstone import CS_ARCH_X86, CS_MODE_32, Cs  # noqa: E402

from dreams import paths  # noqa: E402
from dreams.watcom import _parse_module  # noqa: E402

W = str(paths.get('watcom_compiler_106'))
OW2 = r'E:\tools\ow2'
OW19 = r'E:\tools\ow19'
WC110 = str(paths.get('watcom_compiler'))
WC110A = str(paths.get('watcom') / 'wc110' / '11.0a')
EXE = str(paths.disc(1) / 'WINDREAM.EXE')
FLAGS = '-5r -d2'
CC = 'wc110'
BOUNDS = os.path.join(REPO, 'recomp', 'windream', 'lift', 'bounds.csv')

_pe = None


def retail(va):
    global _pe
    if _pe is None:
        _pe = pefile.PE(EXE, fast_load=True)
    end = None
    with open(BOUNDS) as f:
        for row in csv.reader(f):
            if int(row[0], 16) == va:
                end = int(row[1], 16)
                break
    if end is None:
        raise SystemExit(f'{va:#x} not a function start in bounds.csv')
    rva = va - _pe.OPTIONAL_HEADER.ImageBase
    return _pe.get_data(rva, end - va)


def compile_c(src, flags, include=(), cpp=False, cc=CC):
    out = tempfile.mkdtemp(prefix='wmatch')
    obj = os.path.join(out, 'f.obj')
    root, bin_ = {'ow2': (OW2, 'binnt64'), 'ow19': (OW19, 'binnt'), 'wc110': (WC110, 'BINNT'), 'wc110a': (WC110A, 'BINNT')}.get(cc, (W, 'BINNT'))
    env = dict(os.environ, WATCOM=root, INCLUDE=f'{root}\\H;{root}\\H\\NT')
    exe = 'WPP386.EXE' if cpp else 'WCC386.EXE'
    cmd = [f'{root}\\{bin_}\\{exe}', '-zq', '-bt=nt', *flags.split(), *(f'-i={i}' for i in include), f'-fo={obj}', os.path.abspath(src)]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode or not os.path.exists(obj):
        raise SystemExit(f'wcc386 failed:\n{r.stdout}{r.stderr}')
    with open(obj, 'rb') as f:
        mod, _ = _parse_module(f.read(), 0)
    return mod


def extract(mod, name):
    """Bytes and fixed-mask of a public, up to the next public in its segment."""
    pubs = sorted((o, n, s) for n, s, o in mod.publics)
    for off, n, seg in pubs:
        if n == name or (name.rstrip('_') in n and n.startswith('W?')):
            nxt = [o for o, _, s in pubs if s == seg and o > off]
            s = mod.segments[seg]
            end = min(nxt) if nxt else s.length
            return bytes(s.data[off:end]), bytes(s.fixed[off:end])
    raise SystemExit(f'{name} not public in obj; have {[n for _, n, _ in pubs]}')


def disasm(buf, base):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    return [f'{i.address - base:4x}  {i.mnemonic} {i.op_str}'.rstrip() for i in md.disasm(buf, base)]


def score(ret, got, fixed):
    """Matching bytes over the longer length; fixup bytes count as matches."""
    n = max(len(ret), len(got))
    ok = sum(1 for i in range(min(len(ret), len(got))) if not fixed[i] or ret[i] == got[i])
    return ok, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('func')
    ap.add_argument('va')
    ap.add_argument('--flags', default=FLAGS)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--cc', default=CC, choices=['wc106', 'wc110', 'wc110a', 'ow19', 'ow2'])
    ap.add_argument('--cpp', action='store_true', help='compile with wpp386 (C++)')
    a = ap.parse_args()
    va = int(a.va, 16)
    ret = retail(va)
    # Watcom pads functions to alignment with filler; drop trailing padding from the retail extent.
    got, fixed = extract(compile_c(a.src, a.flags, [HERE], a.cpp, a.cc), a.func)
    while len(ret) > len(got) and ret[-1] in (0x90, 0x00, 0xCC):
        ret = ret[:-1]
    ok, n = score(ret, got, fixed)
    exact = ok == n and len(ret) == len(got)
    print(f'{"MATCH" if exact else "diff "} {a.func} {va:#x}: {ok}/{n} bytes ({100 * ok / n:.1f}%)  retail {len(ret)} obj {len(got)}  flags {a.flags}')
    if not exact and not a.quiet:
        # Blank out relocated operands so only real differences show.
        masked = bytes(b if fixed[i] else 0 for i, b in enumerate(got))
        rmask = bytes(0 if i < len(fixed) and not fixed[i] else b for i, b in enumerate(ret))
        for line in difflib.unified_diff(disasm(rmask, 0), disasm(masked, 0), 'retail', 'obj', lineterm='', n=3):
            print(line)
    return 0 if exact else 1


if __name__ == '__main__':
    sys.exit(main())
