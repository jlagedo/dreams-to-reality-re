"""Which flag sets reproduce the retail code for a given (c, symbol, va), per compiler?"""
import itertools, sys
import os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import match
src, sym, va = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
ret0 = match.retail(va)
cpus = ['-3r', '-4r', '-5r', '-6r']
opts = ['-od', '-d2', '-d1', '-d1+', '-d2 -od', '-od -of', '-od -of+', '-ox', '-otexan', '-os', '']
extra = ['', '-of+', '-zu', '-ei']
for cc in ('wc106', 'wc110'):
    best = []
    for c, o, e in itertools.product(cpus, opts, extra):
        fl = ' '.join(x for x in (c, o, e) if x)
        try:
            got, fixed = match.extract(match.compile_c(src, fl, cc=cc), sym)
        except SystemExit:
            continue
        ret = ret0
        while len(ret) > len(got) and ret[-1] in (0x90, 0, 0xCC): ret = ret[:-1]
        ok, n = match.score(ret, got, fixed)
        best.append((ok / n, ok == n and len(ret) == len(got), fl))
    best.sort(reverse=True)
    print(cc, 'best', f'{best[0][0]:.1%}', 'exact' if best[0][1] else '', [b[2] for b in best if b[0] == best[0][0]][:8])
