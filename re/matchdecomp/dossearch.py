"""Compile the pilot cases with each compiler and flag set, then look for the
exact bytes in the DOS builds (leaf functions: no fixups, so a raw search works)."""
import itertools
import match

DISC = r'E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted'
blobs = {n: open(rf'{DISC}\{n}', 'rb').read() for n in ('DREAMS.EXE', 'DREAMSFX.EXE')}
cases = [l.split() for l in open('cases.txt') if l.strip()]
cpus = ['-3r', '-4r', '-5r', '-6r']
opts = ['-otexan', '-oaxt', '-ox', '-otexan -of', '-oaxt -of', '-ox -of', '-otexan -of+', '-oaxt -of+']
for cc in ('wc106', 'wc110'):
    best = []
    for cpu, o in itertools.product(cpus, opts):
        fl = f'{cpu} {o} -s'
        found = {}
        try:
            for f, fn, _ in cases:
                got, fixed = match.extract(match.compile_c(f'src/{f}.c', fl, cc=cc), fn)
                assert all(fixed), fn
                found[fn] = [n for n, b in blobs.items() if got in b]
        except SystemExit:
            continue
        n = sum(bool(v) for v in found.values())
        best.append((n, fl, found))
    best.sort(key=lambda x: -x[0])
    print(cc, 'best:', best[0][0], 'of', len(cases), [(b[1]) for b in best if b[0] == best[0][0]][:8])
    for fn, where in best[0][2].items(): print('   ', fn, where)
