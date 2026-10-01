"""Count compiler-version fingerprints across whole game binaries, outside the
Watcom runtime library (matched by signature and excluded).

  switch   jmp [idx*4+table]: 10.6 flat model always emits a CS: prefix,
           11.0 flat model never does (11.0 -ms does).
  divide   constant divisor loaded into a register for (i)div:
           10.6 picks EBX, 11.0 picks ECX.
  int64    Watcom __int64 helpers (11.0 only; 10.6 rejects __int64).

usage: uv run python fpscan.py
"""
import re
import sys

sys.path.insert(0, r'E:\dev\dreams\src')
from dreams import watcom  # noqa: E402

DISC = r'E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted'
ROOTS = {'10.6': r'E:\dev_game\watcom\wc106\watcom10.6', '11.0': r'E:\dev_game\watcom\wc110\11.0'}
LIBS = {
    'win': (r'NT\CLIB3R.LIB', 'MATH387R.LIB', 'MATH3R.LIB'),
    'dos': (r'DOS\CLIB3R.LIB', 'MATH387R.LIB', 'MATH3R.LIB', r'DOS\GRAPH.LIB', r'DOS\EMU387.LIB'),
}
BINS = {'WINDREAM.EXE': 'win', 'GDIDREAM.EXE': 'win', 'DREAMS.EXE': 'dos', 'DREAMSFX.EXE': 'dos'}

# jmp dword ptr [reg*4 + disp32]: FF 24 SIB, SIB = scale 4, no base, index != esp
SWITCH = re.compile(rb'(\x2e)?\xff\x24[\x85\x8d\x95\x9d\xad\xb5\xbd]', re.S)
# mov r32, imm32 ; up to 8 bytes of set-up (cdq / mov edx,eax / sar edx,31 / xor edx,edx) ; (i)div r32
DIV = {
    'ebx': re.compile(b'\xbb....(?:\x99|\x89\xc2|\x8b\xd0|\xc1\xfa\x1f|\x31\xd2|\x33\xd2){1,3}\xf7[\xfb\xf3]', re.S),
    'ecx': re.compile(b'\xb9....(?:\x99|\x89\xc2|\x8b\xd0|\xc1\xfa\x1f|\x31\xd2|\x33\xd2){1,3}\xf7[\xf9\xf1]', re.S),
}
INT64 = re.compile(r'^__[IU]8')

sigs = {}
for kind, libs in LIBS.items():
    sigs[kind] = [s for root in ROOTS.values() for s in watcom.signatures(
        [m for lib in libs for m in watcom.read_library(rf'{root}\LIB386\{lib}')])]

for exe, kind in BINS.items():
    blob = open(rf'{DISC}\{exe}', 'rb').read()
    lib = bytearray(len(blob))
    i64 = set()
    for h in watcom.match(sigs[kind], blob):
        if h.full:
            lib[h.offset:h.offset + h.length] = b'\x01' * h.length
            i64.update(n for n in h.names if INT64.match(n))
    game = lambda m: not lib[m.start()]
    sw = [m for m in SWITCH.finditer(blob) if game(m)]
    cs = sum(1 for m in sw if m.group(1))
    div = {r: sum(1 for m in p.finditer(blob) if game(m)) for r, p in DIV.items()}
    print(f'{exe:13} lib bytes {sum(lib):7}  switch cs:{cs:4} plain:{len(sw) - cs:4}   '
          f'div-by-const ebx:{div["ebx"]:3} ecx:{div["ecx"]:3}   int64 helpers linked: {sorted(i64) or "none"}')
