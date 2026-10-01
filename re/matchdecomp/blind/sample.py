"""Draw the blind sample: random WINDREAM.EXE functions outside the Watcom
runtime, 24-160 bytes, excluding the pilot. Seed fixed before any results.

Writes blind/sample.tsv (va, end, size) and blind/fn_<va>/retail.asm.
"""
import csv
import os
import random
import sys

CODE = os.path.dirname(os.path.abspath(__file__))
# Work data (sample.tsv, fn_*/, the blind key) is game-derived and stays out of git.
HERE = os.path.join(os.environ.get('DREAMS_OUT') or os.path.join(CODE, '..', '..', '..', 'out'),
                    'recomp', 'matchdecomp', 'blind')
sys.path.insert(0, os.path.dirname(CODE))
sys.path.insert(0, r'E:\dev\dreams\src')

import pefile  # noqa: E402
from capstone import CS_ARCH_X86, CS_MODE_32, Cs  # noqa: E402

from dreams import watcom  # noqa: E402

SEED = 19971029  # the day the game was linked
N = 24
EXE = r'E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted\WINDREAM.EXE'
BOUNDS = os.path.join(CODE, '..', '..', '..', 'recomp', 'windream', 'lift', 'bounds.csv')
PILOT = {0x45bb84, 0x45b9b0, 0x45b968, 0x45b98c, 0x45ba10, 0x45b154, 0x45b928}
ROOTS = (r'E:\dev_game\watcom\wc106\watcom10.6', r'E:\dev_game\watcom\wc110\11.0', r'E:\dev_game\watcom\wc110\11.0a')
LIBS = (r'NT\CLIB3R.LIB', 'MATH387R.LIB', 'MATH3R.LIB')

pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
blob = open(EXE, 'rb').read()

# Runtime-library code, from every candidate version's signatures.
lib = set()
sigs = [s for r in ROOTS for s in watcom.signatures([m for lib_ in LIBS for m in watcom.read_library(rf'{r}\LIB386\{lib_}')])]
for h in watcom.match(sigs, blob):
    if h.full:
        va = pe.get_rva_from_offset(h.offset) + base
        lib.update(range(va, va + h.length))

md = Cs(CS_ARCH_X86, CS_MODE_32)
cands = []
for row in csv.reader(open(BOUNDS)):
    s, e = int(row[0], 16), int(row[1], 16)
    if not 24 <= e - s <= 160 or s in PILOT or s in lib:
        continue
    code = pe.get_data(s - base, e - s)
    ins = list(md.disasm(code, s))
    if not ins or ins[0].mnemonic == 'jmp' or sum(i.size for i in ins) < (e - s) * 0.9:
        continue  # thunks and ranges that are not clean code
    cands.append((s, e))

random.Random(SEED).shuffle(cands)
pick = sorted(cands[:N])
print(f'{len(cands)} candidates, picked {len(pick)}')
with open(os.path.join(HERE, 'sample.tsv'), 'w') as f:
    f.write('va\tend\tsize\n')
    for s, e in pick:
        f.write(f'{s:08x}\t{e:08x}\t{e - s}\n')
        d = os.path.join(HERE, f'fn_{s:08x}')
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'retail.asm'), 'w') as a:
            for i in md.disasm(pe.get_data(s - base, e - s), s):
                a.write(f'{i.address:08x}  {i.bytes.hex():20} {i.mnemonic} {i.op_str}\n')
        print(f'{s:08x} {e - s:4}')
