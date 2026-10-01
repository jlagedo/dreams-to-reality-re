"""Unoptimized (-od, stack-checked) frames: `push ebp; mov ebp,esp; sub esp,imm32`.
10.6 emits `sub esp,0` for a function with no locals; 11.0 reserves 4 bytes instead
and never emits 0 (probes/d_*.c). Count frames with 0 vs non-zero, outside the runtime."""
import re, struct
from fpscan import sigs, watcom, DISC, BINS
FRAME = re.compile(rb'\x55\x89\xe5\x81\xec(....)', re.S)
for exe, kind in BINS.items():
    blob = open(rf'{DISC}\{exe}', 'rb').read()
    lib = bytearray(len(blob))
    for h in watcom.match(sigs[kind], blob):
        if h.full: lib[h.offset:h.offset + h.length] = b'\x01' * h.length
    sizes = [struct.unpack('<I', m.group(1))[0] for m in FRAME.finditer(blob) if not lib[m.start()]]
    zero = sizes.count(0)
    four = sizes.count(4)
    print(f'{exe:13} debug frames {len(sizes):5}   sub esp,0 (10.6 only): {zero:4}   sub esp,4: {four:4}')
