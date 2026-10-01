import match
DISC = r'E:\dev_game\Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\extracted'
cases = [l.split() for l in open('cases.txt') if l.strip()]
for exe, cc, fl in (('DREAMS.EXE', 'wc110', '-5r -otexan -s'), ('DREAMSFX.EXE', 'wc106', '-5r -otexan -d1+ -s')):
    blob = open(rf'{DISC}\{exe}', 'rb').read()
    for f, fn, _ in cases:
        got = match.extract(match.compile_c(f'src/{f}.c', fl, cc=cc), fn)[0]
        print(exe, cc, fn, hex(blob.find(got)))
