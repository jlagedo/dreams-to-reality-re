"""Re-score every blind attempt that was exact under 11.0 with match.py's
defaults (no --cc), to check the retargeted tools reproduce the recorded result."""
import glob
import os
import re
import sys

CODE = os.path.dirname(os.path.abspath(__file__))
# Work data (sample.tsv, fn_*/, the blind key) is game-derived and stays out of git.
HERE = os.path.join(os.environ.get('DREAMS_OUT') or os.path.join(CODE, '..', '..', '..', 'out'),
                    'recomp', 'matchdecomp', 'blind')
sys.path.insert(0, os.path.dirname(CODE))
import match  # noqa: E402

PROFILES = {'opt': '-5r -otexan -s', 'debug': match.FLAGS}
DEF = re.compile(r'^(?!extern|static|typedef)[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;]*$', re.M)

ok = bad = 0
for log in sorted(glob.glob(os.path.join(HERE, 'fn_*', 'attempts2.tsv'))):
    d = os.path.dirname(log)
    va = int(os.path.basename(d).split('_')[1], 16)
    rows = [r.rstrip('\n').split('\t') for r in open(log)][1:]
    for r in rows:
        if r[5] != '1':
            continue
        src = os.path.join(d, 'attempts2', f'{int(r[0]):02d}.c')
        name = DEF.findall(open(src).read())[-1] + '_'
        ret = match.retail(va)
        got, fixed = match.extract(match.compile_c(src, PROFILES[r[2]]), name)
        while len(ret) > len(got) and ret[-1] in (0x90, 0x00, 0xCC):
            ret = ret[:-1]
        n_ok, n = match.score(ret, got, fixed)
        exact = n_ok == n and len(ret) == len(got)
        ok += exact
        bad += not exact
        print(f'{"MATCH" if exact else "diff "} {va:#x} {name} attempt {r[0]} {r[2]}')
        break
print(f'{ok} exact, {bad} not')
