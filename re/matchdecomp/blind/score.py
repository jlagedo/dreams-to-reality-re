"""Blind scorer: compile one C file with compiler A and compiler B and byte-compare
the named function against the retail function in WINDREAM.EXE.

usage: uv run --no-project -q --with capstone --with pefile python score.py <fn_dir> <file.c> <symbol> [--diff A|B]
  <fn_dir>  e.g. fn_0040464f (the retail address comes from the name)
  <symbol>  public name in the object: a watcall function `foo` is `foo_`

Every successful compile is one attempt, logged to <fn_dir>/attempts.tsv and
copied to <fn_dir>/attempts/NN.c. At most MAX_ATTEMPTS per function.
Compiler identities are hidden; do not look them up.
"""
import argparse
import difflib
import hashlib
import os
import shutil
import sys

CODE = os.path.dirname(os.path.abspath(__file__))
# Work data (sample.tsv, fn_*/, the blind key) is game-derived and stays out of git.
HERE = os.path.join(os.environ.get('DREAMS_OUT') or os.path.join(CODE, '..', '..', '..', 'out'),
                    'recomp', 'matchdecomp', 'blind')
sys.path.insert(0, os.path.dirname(CODE))
import match  # noqa: E402

MAX_ATTEMPTS = 10
FLAGS = '-5r -otexan -s'
KEYFILE = os.path.join(HERE, '..', '.blind-key')  # "A=<cc>\nB=<cc>", written by the orchestrator


def load_key():
    return dict(line.strip().split('=') for line in open(KEYFILE) if '=' in line)


def run(fn_dir, src, sym, cc):
    va = int(os.path.basename(os.path.normpath(fn_dir)).split('_')[1], 16)
    ret = match.retail(va)
    got, fixed = match.extract(match.compile_c(src, FLAGS, cc=cc), sym)
    while len(ret) > len(got) and ret[-1] in (0x90, 0x00, 0xCC):
        ret = ret[:-1]
    ok, n = match.score(ret, got, fixed)
    return ret, got, fixed, ok, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fn_dir')
    ap.add_argument('src')
    ap.add_argument('sym')
    ap.add_argument('--diff', choices=['A', 'B'])
    a = ap.parse_args()
    fn_dir = os.path.join(HERE, os.path.basename(os.path.normpath(a.fn_dir)))
    log = os.path.join(fn_dir, 'attempts.tsv')
    done = sum(1 for _ in open(log)) - 1 if os.path.exists(log) else 0
    if done >= MAX_ATTEMPTS:
        raise SystemExit(f'attempt cap reached ({MAX_ATTEMPTS}); stop and report')
    key = load_key()
    res = {}
    for label in ('A', 'B'):
        try:
            res[label] = run(fn_dir, a.src, a.sym, key[label])
        except SystemExit as e:
            print(f'{label}: compile/extract failed (not counted): {str(e)[:400]}')
            return 2
    n_att = done + 1
    os.makedirs(os.path.join(fn_dir, 'attempts'), exist_ok=True)
    shutil.copy(a.src, os.path.join(fn_dir, 'attempts', f'{n_att:02d}.c'))
    if not os.path.exists(log):
        open(log, 'w').write('attempt\tsha1\tA_ok\tA_n\tA_exact\tB_ok\tB_n\tB_exact\n')
    row = [str(n_att), hashlib.sha1(open(a.src, 'rb').read()).hexdigest()[:10]]
    for label in ('A', 'B'):
        ret, got, fixed, ok, n = res[label]
        exact = ok == n and len(ret) == len(got)
        row += [str(ok), str(n), str(int(exact))]
        print(f'{label}: {"EXACT" if exact else "     "} {ok}/{n} bytes ({100 * ok / n:.1f}%)  retail {len(ret)} obj {len(got)}')
    open(log, 'a').write('\t'.join(row) + '\n')
    print(f'attempt {n_att}/{MAX_ATTEMPTS} logged')
    if a.diff:
        ret, got, fixed, _, _ = res[a.diff]
        masked = bytes(b if fixed[i] else 0 for i, b in enumerate(got))
        rmask = bytes(0 if i < len(fixed) and not fixed[i] else b for i, b in enumerate(ret))
        for line in difflib.unified_diff(match.disasm(rmask, 0), match.disasm(masked, 0), 'retail', a.diff, lineterm='', n=2):
            print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
