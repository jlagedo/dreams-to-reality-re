"""Which flag bits diverge, per failing t_insn test (native vs lifted, all trials).

Reads DREAMS_OUT/recomp/difftest/t_insn-run.txt (difftest.py t_insn --tag opt output)
and writes flagdiff.json next to it.

usage: uv run --with capstone --with pefile python recomp/difftest/flagdiff.py
"""

import json
import os
import re
import subprocess

from dreams import paths

here = str(paths.get("out") / "recomp" / "difftest")
work = os.path.join(here, "t_insn-opt")
names = [
    line.split()[1:]
    for line in open(os.path.join(here, "t_insn-run.txt"), encoding="utf-8", errors="replace")
    if line.startswith(" DIFF ")
]
out = {}
for parts in names:
    name = " ".join(parts[:-1])
    # software: the direct renderer (the Windows default) replaces game functions only
    env = dict(os.environ, T_ONLY=name, WD_RENDERER="software")
    n = subprocess.run(
        [os.path.join(work, "t_insn.exe")], env=env, cwd=work, capture_output=True, text=True
    ).stdout.splitlines()
    lifted = subprocess.run(
        [os.path.join(work, "build", "windream_recomp.exe"), "t_insn.exe", "--run"],
        env=env,
        cwd=work,
        capture_output=True,
        text=True,
    ).stdout.splitlines()
    fx, other = 0, False
    for a, b in zip(n, lifted, strict=False):
        if a == b or " k=" not in a:
            continue
        fa = re.search(r"out (.*) fl=(\w+) m=(\w+)", a)
        fb = re.search(r"out (.*) fl=(\w+) m=(\w+)", b)
        if fa and fb:
            fx |= int(fa.group(2), 16) ^ int(fb.group(2), 16)
            if fa.group(1) != fb.group(1) or fa.group(3) != fb.group(3):
                other = True
        else:
            other = True
    out[name] = {"flags": fx, "values": other}
    print(f"{name:34s} flags^={fx:03x}{'  VALUES DIFFER' if other else ''}")
json.dump(out, open(os.path.join(here, "flagdiff.json"), "w"), indent=1)
