"""Tally the blind sample from the attempt logs (not from the writers' reports),
then unblind A/B with the key. Reads the protocol-v2 logs (attempts2.tsv).

usage: uv run --no-project -q python tally.py
"""
import csv
import os
import statistics

CODE = os.path.dirname(os.path.abspath(__file__))
# Work data (sample.tsv, fn_*/, the blind key) is game-derived and stays out of git.
HERE = os.path.join(os.environ.get('DREAMS_OUT') or os.path.join(CODE, '..', '..', '..', 'out'),
                    'recomp', 'matchdecomp', 'blind')
key = dict(line.strip().split('=') for line in open(os.path.join(HERE, '..', '.blind-key')) if '=' in line)
# The three functions next to the runtime's scanf code may be unrecognised library code.
MAYBE_LIB = {'004896ec', '0048a64a', '0048d7e8'}

rows = []
for r in csv.DictReader(open(os.path.join(HERE, 'sample.tsv')), delimiter='\t'):
    va = r['va']
    log = os.path.join(HERE, f'fn_{va}', 'attempts2.tsv')
    att = list(csv.DictReader(open(log), delimiter='\t')) if os.path.exists(log) else []
    if not att:
        rows.append((va, int(r['size']), 0, None))
        continue
    pct = lambda a, L: int(a[f'{L}_ok']) / int(a[f'{L}_n'])
    first = {L: pct(att[0], L) for L in 'AB'}
    best = {L: max(pct(a, L) for a in att) for L in 'AB'}
    exact = {L: any(a[f'{L}_exact'] == '1' for a in att) for L in 'AB'}
    prof = '/'.join(sorted({a['profile'] for a in att if a[f'A_exact'] == '1' or a['B_exact'] == '1'})) or '/'.join(sorted({a['profile'] for a in att}))
    rows.append((va, int(r['size']), len(att), (first, best, exact, prof)))

print(f"{'va':9} {'size':>4} {'att':>3}   first A / B     best A / B     exact A B   profile")
for va, size, n, res in rows:
    if res is None:
        print(f'{va:9} {size:4} {n:3}   (no attempts)')
        continue
    first, best, exact, prof = res
    tag = '  maybe-lib' if va in MAYBE_LIB else ''
    print(f'{va:9} {size:4} {n:3}   {first["A"]:6.1%} {first["B"]:6.1%}   {best["A"]:6.1%} {best["B"]:6.1%}   '
          f'{"Y" if exact["A"] else "-"} {"Y" if exact["B"] else "-"}   {prof:9}{tag}')


def summary(label, sel):
    done = [r for r in sel if r[3]]
    if not done:
        return
    print(f'\n{label}: {len(done)} functions')
    for L in 'AB':
        ex = sum(r[3][2][L] for r in done)
        fm = statistics.mean(r[3][0][L] for r in done)
        bm = statistics.mean(r[3][1][L] for r in done)
        print(f'  {L} = {key[L]:6}: exact {ex:2}/{len(done)}   mean first {fm:.1%}   mean best {bm:.1%}')
    a_only = sum(r[3][2]['A'] and not r[3][2]['B'] for r in done)
    b_only = sum(r[3][2]['B'] and not r[3][2]['A'] for r in done)
    both = sum(r[3][2]['A'] and r[3][2]['B'] for r in done)
    wins = sum(r[3][1]['A'] > r[3][1]['B'] for r in done), sum(r[3][1]['B'] > r[3][1]['A'] for r in done)
    print(f'  exact only under {key["A"]}: {a_only}, only under {key["B"]}: {b_only}, under both: {both}')
    print(f'  higher best score: {key["A"]} {wins[0]}, {key["B"]} {wins[1]}, tied {len(done) - sum(wins)}')
    # exact sign test on the discordant exact matches
    n = a_only + b_only
    if n:
        k = max(a_only, b_only)
        from math import comb
        p = sum(comb(n, i) for i in range(k, n + 1)) / 2 ** n * 2
        print(f'  two-sided sign test on discordant exact matches: p = {min(p, 1):.2g}')


summary('All sampled functions', rows)
summary('Excluding the three possible runtime functions', [r for r in rows if r[0] not in MAYBE_LIB])


def natural(label, sel):
    """First attempt only: the writer's natural C, written blind, scored under both."""
    from math import comb
    done = [r for r in sel if r[3]]
    fa = {L: {r[0] for r in done if r[3][0][L] == 1.0} for L in 'AB'}
    a_only, b_only, both = fa['A'] - fa['B'], fa['B'] - fa['A'], fa['A'] & fa['B']
    n, k = len(a_only) + len(b_only), max(len(a_only), len(b_only))
    p = min(1, 2 * sum(comb(n, i) for i in range(k, n + 1)) / 2 ** n) if n else 1
    print(f'\n{label}: first-attempt (natural C) exact matches')
    print(f'  only {key["A"]}: {len(a_only):2} {sorted(a_only)}')
    print(f'  only {key["B"]}: {len(b_only):2} {sorted(b_only)}')
    print(f'  both: {len(both)} {sorted(both)}   two-sided sign test p = {p:.2g}')


natural('All sampled functions', rows)
natural('Excluding the three possible runtime functions', [r for r in rows if r[0] not in MAYBE_LIB])
