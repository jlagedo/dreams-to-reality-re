"""Find the compiler and flags that reproduce DREAMSFX.EXE's framed MATH_ code."""
import itertools
import match
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

DISC = r'E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted'
blob = open(rf'{DISC}\DREAMSFX.EXE', 'rb').read()
cases = [l.split() for l in open('cases.txt') if l.strip()]
cpus = ['-3r', '-4r', '-5r', '-6r']
opts = ['-otexan', '-oaxt', '-ox', '-os', '-ot', '-od', '']
extra = ['-of', '-of+', '-d1', '-d2', '-d1 -of', '-d1 -of+', '-d2 -of+', '-hc -d1', '-hw -d1', '-hd -d1', '-d1+']
for cc in ('wc106', 'wc110'):
    res = []
    for cpu, o, e in itertools.product(cpus, opts, extra):
        fl = f'{cpu} {o} {e} -s'.replace('  ', ' ')
        try:
            hits = [fn for f, fn, _ in cases
                    if match.extract(match.compile_c(f'src/{f}.c', fl, cc=cc), fn)[0] in blob]
        except SystemExit:
            continue
        res.append((len(hits), fl, hits))
    res.sort(key=lambda r: -r[0])
    print(cc, 'best', res[0][0], 'of', len(cases), [r[1] for r in res if r[0] == res[0][0]][:10])
# what the retail framed CopyVec3 looks like next to the nearest 10.6 guess
i = blob.find(bytes.fromhex('53558be58b1a8918'))
md = Cs(CS_ARCH_X86, CS_MODE_32)
print('retail DREAMSFX CopyVec3 @file', hex(i), [f'{x.mnemonic} {x.op_str}' for x in md.disasm(blob[i:i + 24], 0)][:12])
got = match.extract(match.compile_c('src/copyvec3.c', '-5r -otexan -of+ -s', cc='wc106'), 'MATH_CopyVec3_')[0]
print('10.6 -5r -otexan -of+ -s          ', [f'{x.mnemonic} {x.op_str}' for x in md.disasm(got, 0)])
