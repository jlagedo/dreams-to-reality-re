"""Is the switch-table CS prefix a property of the compiler version, or of a flag?"""
import itertools, match
cpus = ['-3r', '-4r', '-5r', '-6r', '-3s', '-5s']
opts = ['-otexan', '-oaxt', '-ox', '-os', '-od', '']
extra = ['', '-d1+', '-d2', '-of+', '-zc', '-zdp', '-zdf', '-zu', '-mf', '-ms', '-bt=dos', '-zff', '-zgf', '-ei', '-zp1', '-zm']
for cc in ('wc106', 'wc110'):
    seen = {}
    for c, o, e in itertools.product(cpus, opts, extra):
        fl = f'{c} {o} {e} -s'
        try:
            b, _ = match.extract(match.compile_c('probes/p_switch.c', fl, cc=cc), 'f_switch_' if 'r' in c else 'f_switch')
        except SystemExit:
            continue
        cs = b'\x2e\xff\x24' in b
        plain = b'\xff\x24' in b.replace(b'\x2e\xff\x24', b'')
        seen.setdefault((cs, plain), []).append(fl)
    for k, v in seen.items():
        print(cc, f'cs-prefix={k[0]} plain={k[1]}', len(v), 'flag sets, e.g.', v[:4])
