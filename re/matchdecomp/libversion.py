"""Which Watcom runtime (10.6 or 11.0) is linked into a game binary: count
full-extent matches of library functions whose bytes exist in only one version.

usage: uv run python libversion.py [win|dos] [EXE ...]
"""
import os
import sys

from dreams import watcom

DISC = r'E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted'
ROOTS = {'10.6': r'E:\dev_game\watcom\wc106\watcom10.6', '11.0': r'E:\dev_game\watcom\wc110\11.0'}
if os.environ.get('ROOTS'):  # e.g. ROOTS='11.0=E:\...\11.0;11.0a=E:\...\11.0a'
    ROOTS = dict(r.split('=', 1) for r in os.environ['ROOTS'].split(';'))
LIBS = {
    'win': (r'NT\CLIB3R.LIB', 'MATH387R.LIB', 'MATH3R.LIB'),
    'dos': (r'DOS\CLIB3R.LIB', 'MATH387R.LIB', 'MATH3R.LIB', r'DOS\GRAPH.LIB', r'DOS\EMU387.LIB'),
}
DEFAULT = {'win': ['WINDREAM.EXE', 'GDIDREAM.EXE'], 'dos': ['DREAMS.EXE', 'DREAMSFX.EXE']}

kind = sys.argv[1] if len(sys.argv) > 1 else 'win'
exes = sys.argv[2:] or DEFAULT[kind]

sigs = {}
for tag, root in ROOTS.items():
    mods = [m for lib in LIBS[kind] for m in watcom.read_library(rf'{root}\LIB386\{lib}')]
    sigs[tag] = watcom.signatures(mods)
key = lambda s: (s.data, s.mask)
keys = {t: {key(s) for s in v} for t, v in sigs.items()}

for exe in exes:
    blob = open(rf'{DISC}\{exe}', 'rb').read()
    tags = list(ROOTS)
    for tag, other in ((tags[0], tags[1]), (tags[1], tags[0])):
        only = [s for s in sigs[tag] if key(s) not in keys[other]]
        names = sorted({n for h in watcom.match(only, blob) if h.full for n in h.names})
        print(f'{exe:13} {tag}: {len(names):4} of {len(only)} version-specific functions', names[:8])
