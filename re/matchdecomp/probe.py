"""Compile each probe with 10.6 and 11.0 at the game flags and show where the code differs."""
import glob, os, match
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
md = Cs(CS_ARCH_X86, CS_MODE_32)
FL = '-5r -otexan -s'
def dis(b, fx): return [f'{x.mnemonic} {x.op_str}' + ('  ;fix' if not all(fx[x.address:x.address + x.size]) else '') for x in md.disasm(b, 0)]
for src in sorted(glob.glob('probes/*.c')):
    fn = os.path.basename(src)[2:-2]
    out = {}
    for cc in ('wc106', 'wc110'):
        try:
            mod = match.compile_c(src, FL, cc=cc)
            b, fx = match.extract(mod, 'f_' + fn + '_')
            out[cc] = dis(b, fx)
        except SystemExit as e:
            out[cc] = [f'ERROR {str(e)[:100]}']
    same = out['wc106'] == out['wc110']
    print(f'== {fn}: {"same" if same else "DIFFERENT"}')
    if not same:
        n = max(len(v) for v in out.values())
        for i in range(n):
            a = out['wc106'][i] if i < len(out['wc106']) else ''
            b = out['wc110'][i] if i < len(out['wc110']) else ''
            print(f'   {a:40} | {b}')
