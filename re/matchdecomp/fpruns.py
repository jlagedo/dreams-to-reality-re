"""Order the version markers by file offset and collapse them into runs.
If whole object files were compiled by one compiler, markers form long runs."""
import re, sys
import fpscan  # noqa: F401 (reuses patterns; prints its table on import)
from fpscan import SWITCH, DIV, BINS, DISC, sigs, watcom

for exe in sys.argv[1:] or ('DREAMS.EXE', 'DREAMSFX.EXE', 'WINDREAM.EXE'):
    kind = BINS[exe]
    blob = open(rf'{DISC}\{exe}', 'rb').read()
    lib = bytearray(len(blob))
    for h in watcom.match(sigs[kind], blob):
        if h.full:
            lib[h.offset:h.offset + h.length] = b'\x01' * h.length
    ev = [(m.start(), '6' if m.group(1) else 'B') for m in SWITCH.finditer(blob) if not lib[m.start()]]
    ev += [(m.start(), '6' if r == 'ebx' else 'B') for r, p in DIV.items() for m in p.finditer(blob) if not lib[m.start()]]
    ev.sort()
    runs = []
    for off, t in ev:
        if runs and runs[-1][0] == t:
            runs[-1][2] = off; runs[-1][3] += 1
        else:
            runs.append([t, off, off, 1])
    seq = ''.join(t for _, t in ev)
    # switches between versions, vs what a random shuffle of the same markers would give
    flips = sum(1 for a, b in zip(seq, seq[1:]) if a != b)
    p6 = seq.count('6') / len(seq)
    expect = 2 * p6 * (1 - p6) * (len(seq) - 1)
    print(f'\n{exe}: {len(seq)} markers (6 = 10.6, B = 11.0), {len(runs)} runs, {flips} flips vs ~{expect:.0f} expected if interleaved at random')
    print('  ' + seq)
    for t, a, b, n in runs:
        if n >= 3: print(f'   {"10.6" if t == "6" else "11.0"} x{n:3}  file 0x{a:06x}-0x{b:06x}')
