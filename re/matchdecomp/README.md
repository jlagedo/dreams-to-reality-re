# Matching decompilation

C written for one retail function, compiled with a real Watcom compiler and
compared byte for byte with `WINDREAM.EXE`. Evidence and results:
`docs/research/toolchain.md` and `docs/specs/000-the-recomp/spec.md`.

```sh
uv run --with capstone --with pefile python re/matchdecomp/match.py <file.c> <func_> <va> [--flags "-5r -otexan -s"]
```

Defaults: Watcom 11.0 from `DREAMS_WATCOM_COMPILER`, flags `-5r -d2`; function
bounds from `recomp/windream/lift/bounds.csv`.

Only the scripts are in git. The matched C (`src/`), prototype matches
(`proto/`), blind-test work directories (`blind/fn_*`, `sample.tsv`, the
blind key) and linker outputs are game-derived and stay in
`DREAMS_OUT/recomp/matchdecomp`.

| Script | What |
|---|---|
| `match.py` | Compile one function and byte-diff it against retail |
| `flagsweep.py`, `onesweep.py`, `sweep.sh`, `cases.txt` | Flag sweeps over the `MATH_` pilot cases |
| `fpscan.py`, `fpruns.py`, `framescan.py`, `i64calls.py` | Compiler-version fingerprints in the binaries |
| `libversion.py`, `linkver.ps1`, `linkcross.ps1` | Runtime library and linker version evidence |
| `dossearch.py`, `fxsearch.py`, `pilotloc.py`, `pilotloc2.py` | Locate the pilot functions in the DOS builds |
| `probe.py`, `probeflags.py`, `probes/` | Small compiler probes (division, float conversion, debug levels) |
| `blind/` | Blind 24-function test: `sample.py`, `score.py`, `score2.py`, `recheck.py`, `flagfind.py`, `tally.py` |

These are evidence scripts, kept as they were run. Several read `cases.txt`
or `probes/` relative to the current directory (run them from
`re/matchdecomp`), and `linkver.ps1`/`linkcross.ps1` carry the Watcom paths of
the machine they ran on.
