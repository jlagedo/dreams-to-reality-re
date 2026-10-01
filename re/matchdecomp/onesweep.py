import match
ret = match.retail(0x45bb84)
sw = '-3 -4 -5 -6 -bd -bm -br -bw -bt=dos -bt=windows -d0 -d1 -d2 -d3 -ecc -ecd -ecf -ecp -ecr -ecs -ecw -ee -ei -em -ep -et -ez -fpc -fpi -fpi87 -fp2 -fp3 -fp5 -fpd -fpr -g=x -mc -mf -ml -mm -ms -nc=x -nd=x -nm=x -nt=x -of -of+ -ob -oc -od -oe -oh -oi -ol -ol+ -om -on -oo -op -or -os -ot -ou -ox -oz -r -ri -s -sg -st -u -v -wx -za -ze -zc -zdf -zdp -zdl -zev -zff -zfp -zgf -zgp -zk0 -zl -zld -zm -zp1 -zp4 -zq -zri -zro -zs -zu -zw -zz'.split()
for s in sw:
    try:
        got, fixed = match.extract(match.compile_c('src/copyvec3.c', f'-5r -otexan -s {s}'), 'MATH_CopyVec3_')
    except SystemExit as e:
        continue
    a, n = match.score(ret, got, fixed)
    if a != 13: print(s, a, n, got[:3].hex())
print('done')
