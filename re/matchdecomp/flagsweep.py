"""Score every flag combination over cases.txt; print the best."""
import itertools, sys
import match
CC = sys.argv[1] if len(sys.argv) > 1 else match.CC
cases = [l.split() for l in open('cases.txt') if l.strip()]
targets = [(f'src/{f}.c', fn, match.retail(int(va, 16))) for f, fn, va in cases]
cpus = ['-3r', '-4r', '-5r', '-6r', '-3s', '-5s']
opts = ['-ox', '-otexan', '-oneatx', '-otexanr', '-oxs', '-os', '-ot', '-oaxt', '-oneatxl+', '-oxr', '-ol', '-od']
extra = ['', '-s', '-s -or', '-s -oi', '-s -ol+', '-s -om', '-s -oh', '-s -ob', '-s -ou', '-s -op', '-s -oc']
res = []
for c, o, e in itertools.product(cpus, opts, extra):
    fl = f'{c} {o} {e}'.strip()
    tot = ok = exact = 0
    try:
        for src, fn, ret0 in targets:
            got, fixed = match.extract(match.compile_c(src, fl, cc=CC), fn)
            ret = ret0
            while len(ret) > len(got) and ret[-1] in (0x90, 0, 0xCC): ret = ret[:-1]
            a, n = match.score(ret, got, fixed); ok += a; tot += n; exact += a == n and len(ret) == len(got)
    except SystemExit:
        continue
    res.append((exact, ok / tot, fl))
res.sort(reverse=True)
for r in res:
    if r[0] == len(targets): print(r[2])
print("perfect", sum(r[0] == len(targets) for r in res), "of", len(res))
