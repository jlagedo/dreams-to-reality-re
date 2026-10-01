"""Blind scorer, protocol v2: compile one C file with compiler A and compiler B
under the same flag profile and byte-compare the named function against the
retail function in WINDREAM.EXE.

usage: uv run --no-project -q --with capstone --with pefile python score2.py <fn_dir> <file.c> <symbol> --profile opt|debug [--diff A|B]
  <fn_dir>   e.g. fn_0040464f (the retail address comes from the name)
  <symbol>   public name in the object: a watcall function `foo` is `foo_`
  --profile  opt    -5r -otexan -s  optimized, no stack check
             debug  -5r -od         unoptimized, stack checking on
                                    (retail starts with push N / call __CHK)

Each new (file contents, profile) pair that compiles is one attempt, logged to
<fn_dir>/attempts2.tsv and copied to <fn_dir>/attempts2/NN.c. Re-running the
same file and profile (for example to get --diff) is free. At most
MAX_ATTEMPTS per function. Compiler identities are hidden; do not look them up.
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

MAX_ATTEMPTS = 12
PROFILES = {'opt': '-5r -otexan -s', 'debug': '-5r -od'}
KEYFILE = os.path.join(HERE, '..', '.blind-key')  # "A=<cc>\nB=<cc>", written by the orchestrator
HEADER = 'attempt\tsha1\tprofile\tA_ok\tA_n\tA_exact\tB_ok\tB_n\tB_exact\n'


def run(fn_dir, src, sym, cc, flags):
    va = int(os.path.basename(os.path.normpath(fn_dir)).split('_')[1], 16)
    ret = match.retail(va)
    got, fixed = match.extract(match.compile_c(src, flags, cc=cc), sym)
    while len(ret) > len(got) and ret[-1] in (0x90, 0x00, 0xCC):
        ret = ret[:-1]
    ok, n = match.score(ret, got, fixed)
    return ret, got, fixed, ok, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fn_dir')
    ap.add_argument('src')
    ap.add_argument('sym')
    ap.add_argument('--profile', choices=list(PROFILES), required=True)
    ap.add_argument('--diff', choices=['A', 'B'])
    a = ap.parse_args()
    fn_dir = os.path.join(HERE, os.path.basename(os.path.normpath(a.fn_dir)))
    log = os.path.join(fn_dir, 'attempts2.tsv')
    prev = [line.rstrip('\n').split('\t') for line in open(log)][1:] if os.path.exists(log) else []
    sha = hashlib.sha1(open(a.src, 'rb').read()).hexdigest()[:10]
    repeat = any(r[1] == sha and r[2] == a.profile for r in prev)
    if len(prev) >= MAX_ATTEMPTS and not repeat:
        raise SystemExit(f'attempt cap reached ({MAX_ATTEMPTS}); stop and report')
    key = dict(line.strip().split('=') for line in open(KEYFILE) if '=' in line)
    res = {}
    for label in ('A', 'B'):
        try:
            res[label] = run(fn_dir, a.src, a.sym, key[label], PROFILES[a.profile])
        except SystemExit as e:
            print(f'{label}: compile/extract failed (not counted): {str(e)[:400]}')
            return 2
    row = [str(len(prev) + 1), sha, a.profile]
    for label in ('A', 'B'):
        ret, got, fixed, ok, n = res[label]
        exact = ok == n and len(ret) == len(got)
        row += [str(ok), str(n), str(int(exact))]
        print(f'{label}: {"EXACT" if exact else "     "} {ok}/{n} bytes ({100 * ok / n:.1f}%)  retail {len(ret)} obj {len(got)}')
    if repeat:
        print('same file and profile as an earlier attempt: not counted')
    else:
        os.makedirs(os.path.join(fn_dir, 'attempts2'), exist_ok=True)
        shutil.copy(a.src, os.path.join(fn_dir, 'attempts2', f'{len(prev) + 1:02d}.c'))
        if not os.path.exists(log):
            open(log, 'w').write(HEADER)
        open(log, 'a').write('\t'.join(row) + '\n')
        print(f'attempt {len(prev) + 1}/{MAX_ATTEMPTS} logged ({a.profile})')
    if a.diff:
        ret, got, fixed, _, _ = res[a.diff]
        masked = bytes(b if fixed[i] else 0 for i, b in enumerate(got))
        rmask = bytes(0 if i < len(fixed) and not fixed[i] else b for i, b in enumerate(ret))
        for line in difflib.unified_diff(match.disasm(rmask, 0), match.disasm(masked, 0), 'retail', a.diff, lineterm='', n=2):
            print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
