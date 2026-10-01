"""Who calls the __int64 helpers in WINDREAM.EXE: game code or the runtime library?"""
import struct
import pefile
from fpscan import sigs, watcom, DISC

exe = rf'{DISC}\WINDREAM.EXE'
pe = pefile.PE(exe, fast_load=True)
blob = open(exe, 'rb').read()
base = pe.OPTIONAL_HEADER.ImageBase
off2va = lambda o: pe.get_rva_from_offset(o) + base
lib = []
helpers = {}
for h in watcom.match(sigs['win'], blob):
    if h.full:
        lib.append((off2va(h.offset), off2va(h.offset) + h.length, h.names[0]))
        for n in h.names:
            if n.startswith(('__I8', '__U8')):
                helpers[off2va(h.offset)] = n
lib.sort()
print('helpers at', {hex(k): v for k, v in helpers.items()})
def owner(va):
    for a, b, n in lib:
        if a <= va < b:
            return 'lib:' + n
    return 'game'
code = [s for s in pe.sections if s.Name.startswith(b'AUTO')][0]
start = code.PointerToRawData
data = blob[start:start + code.SizeOfRawData]
cva = base + code.VirtualAddress
for i in range(len(data) - 5):
    if data[i] == 0xE8:
        tgt = cva + i + 5 + struct.unpack_from('<i', data, i + 1)[0]
        if tgt in helpers:
            print(f'  call {helpers[tgt]:6} from {cva + i:#x}  ({owner(cva + i)})')
