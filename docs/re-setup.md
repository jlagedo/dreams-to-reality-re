# Reverse-engineering setup

Ghidra, and how this repo is organised so the analysis survives in git without
any game data going near it.

## The organising principle

> **The Ghidra project is a build artefact. `re/` is the source of truth.**

A `.rep` directory is an opaque binary blob. Git cannot merge it, it bloats
history, and — the part that actually matters here — **it embeds copies of the
game executables**. Committing one would put Cryo's code in the repo.

So the split is:

| Path | Committed? | What it is |
|---|---|---|
| `ghidra/` | **no** | the Ghidra project — regenerate with `tools/ghidra-import.ps1` |
| `ghidra_scripts/` | yes | our Ghidra scripts |
| `re/symbols/*.tsv` | yes | function names and comments we assigned |
| `re/structs/*.h` | yes | C struct definitions for the formats |
| `src/dreams/` | yes | the Python decoding toolkit |
| `docs/` | yes | findings |

Round trip: annotate in Ghidra → `ExportSymbols.java` → commit the TSV; C layouts
live in `re/structs/*.h`. A fresh project runs `ghidra-import.ps1 -ImportSymbols
-ImportStructs` to restore both kinds of analysis from the discs and text sources.

## Installed on this machine

| Component | Version | Location |
|---|---|---|
| Ghidra | 12.1.3 (2026-08-18) | `DREAMS_GHIDRA_ROOT` |
| JDK | Temurin 25.0.3 LTS | already on PATH; Ghidra needs 21+ |

The GUI is `ghidraRun.bat` under `DREAMS_GHIDRA_ROOT`. Use it to look at a
program and to run scripts from the Script Manager; everything reproducible goes
through headless.

## Headless workflow

For anything reproducible, prefer headless over the GUI:

```powershell
.\tools\ghidra-import.ps1                     # create project, import, analyse
.\tools\ghidra-import.ps1 -ImportSymbols      # …and re-apply saved names
.\tools\ghidra-import.ps1 -ImportSymbols -ImportStructs # restore symbols and C layouts
.\tools\ghidra-import.ps1 -Analyze:$false     # skip auto-analysis
```

Defaults to importing `WINDREAM.EXE`, `GDIDREAM.EXE`, `SETUP.EXE` and
`CRYO.DLL`. Paths come from `DREAMS_DISC1` / `DREAMS_DISC2`, same as the Python
toolkit.

## Which binary to attack

**`WINDREAM.EXE` is the primary target.** It is PE32, so Ghidra loads it with no
extra loader, and since Cryo compiled one portable Watcom core three ways, what
you learn transfers to the DOS builds. See [engine.md](engine.md).

`DREAMS.EXE` and `DREAMSFX.EXE` are LE (DOS/4GW) and need a loader extension
Ghidra does not ship; see [LE loader for the DOS builds](#le-loader-for-the-dos-builds).
`DREAMSFX.EXE` is imported and analysed in the local project. Use the DOS
builds to answer DOS-specific questions (hardware access, the 3dfx path) and
as a cross-check; `WINDREAM.EXE` stays the main target.

**For rendering, read `DREAMSFX.EXE`.** Its Glide calls are typed
(`ApplyGlideImports.java`), so render state reads as named Glide API calls,
where the Windows build does the same work in anonymous software-rasterizer
code. Carry names between the builds with the matcher
([Matching functions across builds](#matching-functions-across-builds)); the
Glide-to-DirectDraw/GDI map is in [engine.md](engine.md), *Presentation and
2D*.

`CRYO.DLL` is a **debug build with 165 named exports**. It is a different
codebase from the game (see [cryolib.md](cryolib.md)), but it is the best
available guide to Cryo's naming and structure conventions.

## The scripts

### `FindFormatParsers.java` — start here

Containers first. Every Cryo format opens with a four-character tag and the
loader must validate it. A compiler emits that check one of two ways:

1. **immediate compare** — `cmp dword ptr [x], 0x464E5344` ("DSNF" as an int)
2. **memcmp/strncmp** against a string literal in `.data`

**Measured on `WINDREAM.EXE`: zero immediates, exactly one ASCII literal per
tag.** Watcom emitted form 2 here, so the literals are the anchors — find the
references to them and you land in the parser. The script checks both and
reports whichever applies.

Scripts are **Java, not Python**, deliberately. Ghidra compiles Java scripts with
no external dependency; PyGhidra needs a matching jpype wheel and tops out at
Python 3.13, which would break on this machine's 3.14.

#### First results — the parsers are located

Run against `WINDREAM.EXE` (image base `0x400000`): **[verified]**

| Tag | Literal | Referencing function | What it is |
|---|---|---|---|
| `DSNF` | `0x004c421e` | **`FUN_004175bc`** (2 xrefs) | **scene loader — top target** |
| `DANF` | `0x004c39f1` | `FUN_0040fff7` | animation loader |
| `DRDF` | `0x004c3a48` | `FUN_0041072c` | dialogue bank |
| `UBIK` | `0x004c5511` | `FUN_0043ad40` | image bundle (`.BF`) |
| `F3DC` | `0x00456e0d` | none | inside a data blob, reached indirectly |
| `HNM4`/`HNM6`/`HNS6`/`UBB2`/`UBS2` | `0x004086e0`–`0x004087a3` | none | **clustered in 0xc3 bytes — a signature table, not five compares** |

`PAK0` has no literal at all, consistent with `.PAK` being parsed by whatever
reads the embedded `F3DC` chunk rather than by its own loader.

String anchors additionally implicate `FUN_0041c666` (4 xrefs), `FUN_00456038`,
`FUN_00427f11`, `FUN_0041020f` and `FUN_00426143` — the file I/O and disc-check
layer.

### LE loader for the DOS builds

[yetmorecode/ghidra-lx-loader](https://github.com/yetmorecode/ghidra-lx-loader)
loads DOS/4GW LE executables and ships a Watcom language
(`watcom:LE:32:default`). Its newest release targets Ghidra 12.0.1, so build it
from source; at commit `60bae51` it compiles against 12.1.3 unchanged.

```powershell
git clone https://github.com/yetmorecode/ghidra-lx-loader E:\tools\src\ghidra-lx-loader
Copy-Item tools\lx-loader-watcom.cspec E:\tools\src\ghidra-lx-loader\data\languages\watcom.cspec
$g = Get-DreamsSetting DREAMS_GHIDRA_ROOT   # after . .\tools\dreams-env.ps1
Push-Location E:\tools\src\ghidra-lx-loader
& "$g\support\gradle\gradlew.bat" "-PGHIDRA_INSTALL_DIR=$g" buildExtension
Pop-Location
Expand-Archive E:\tools\src\ghidra-lx-loader\dist\*.zip "$g\Ghidra\Extensions" -Force
.\tools\ghidra-import.ps1 -Binaries (Join-Path (Get-DreamsSetting DREAMS_DISC1) DREAMSFX.EXE)
```

`-Binaries` imports only the listed files, so the existing programs are left
alone. Restart Ghidra afterwards.

**Replace the loader's `watcom.cspec`** with `tools/lx-loader-watcom.cspec`, as
above. Upstream's `__watcall` has a fixed `extrapop`, no `ST0` float return and
no `EBX` clobber. The replacement uses the prototype from
`tools/watcall-cspec.patch`, so the DOS and Windows builds decompile under the
same contract. It is the loader's default prototype, so `ApplyWatcall.java` is
not needed for the calling convention. The file also adds a `__stdcall` model
for the Glide stubs.

The bespoke-register runtime helpers still decompile wrong. The Watcom library
match already exists (`sigs/dreamsfx.csv`, 282 functions; see
[toolchain.md](toolchain.md)), but it records file offsets only. The loaded
program can now map them (`Memory.locateAddressesForFileOffset`); that step and
applying the names are not done yet.

LE addresses are the loader's object bases (code at `0x20000` in
`DREAMSFX.EXE`), not file offsets.

### `ApplyGlideImports.java` — Glide 2 on `DREAMSFX.EXE`

Run after importing `DREAMSFX.EXE`, against a checkout of the released Glide
source. The headers are 3dfx-licensed and stay outside the repo; the parsed
archive goes to `out/ghidra/glide2x.gdt`.

```powershell
git clone --depth 1 https://github.com/sezero/glide E:\tools\src\glide
$env:DREAMS_REPO = (Get-Location).Path
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') `
  ghidra dreams -process DREAMSFX.EXE -noanalysis `
  -scriptPath ghidra_scripts -postScript ApplyGlideImports.java E:\tools\src\glide
```

It parses `glide2x/sst1` (Voodoo Graphics) with `__WATCOMC__`/`__DOS__`
defined so `FX_CALL` becomes `__stdcall`, then finds the `glimport.asm` tables
and applies a name, prototype and `@N` stack purge to each of the 130 stubs.
Three auto-analysis artefacts had to be undone, and the script does so every
run:

- **No-return.** `__loadme` leaves by jumping into `glide2x.ovl`, so analysis
  marks it and every stub no-return and cuts each caller at its first Glide
  call. The script clears the flags and `CALL_RETURN` overrides, disassembles
  the lost code, regrows the truncated callers and creates functions for
  routines reached only by pointer.
- **Thunks.** Each 5-byte stub becomes a thunk of `__loadme`, and a thunk
  shares its target's signature, so every prototype landed on `__loadme`. The
  script unlinks them first.
- **Name table.** The list ends with `_CONVERTANDDOWNLOADRLE@64`, not a
  `_GR`/`_GU` name; stopping early shifted every name by one stub. Check a
  known call's argument count (`grBufferClear` pushes 3) after any change.

121 of 130 get prototypes; the other 9 (`grSstConfigPipeline`, `grSstVidMode`,
`guMovie*`, `guMp*`) are not declared in the final `sst1` headers and keep their
decorated names. `ImportSymbols.java` restores names only, so re-run this
script on a fresh project.

### Matching functions across builds

The executables are one Watcom engine compiled for different targets, so most
functions have a twin in each build at another address. Identical code is rare
(the Windows build adds a `__CHK` stack probe, `0x454feb`, to nearly every
function, and register allocation differs), but constants, strings, instruction
shape and the call graph survive.

```powershell
$env:DREAMS_REPO = (Get-Location).Path
$h = Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat'
foreach ($p in 'DREAMSFX.EXE','WINDREAM.EXE') {
  & $h ghidra dreams -process $p -noanalysis -readOnly `
    -scriptPath ghidra_scripts -postScript ExportFunctionFeatures.java
}
uv run python tools/match_functions.py DREAMSFX.EXE WINDREAM.EXE --renames
& $h ghidra dreams -process DREAMSFX.EXE -noanalysis -scriptPath ghidra_scripts `
  -postScript Rename.java "@$PWD\out\ghidra\match\renames-DREAMSFX.EXE.tsv"
```

- `ExportFunctionFeatures.java` writes `out/ghidra/features/<program>.json`:
  masked instructions, constants below `0x10000`, string literals and call
  order per function.
- `match_functions.py` seeds on unique identical code, identical string sets
  and single-owner strings, then propagates through callers, callees and
  **call-slot alignment**: an unmatched call between the same matched
  neighbours in both call sequences. It writes
  `out/ghidra/match/<a>--<b>.tsv` with a score and a callee agreement ratio
  per pair.
- `--renames` proposes names only for confident pairs (score ≥ 0.60, or at
  least two agreeing callees at ≥ 75 %), never overwrites a name, reports
  conflicts, and never carries platform-layer names (`Glide_`, `DDraw_`,
  `GDI_`, `Video_`, `Kbd_`, `Timer_`, `DPMI_`, `AIL_`, `Joy_`, `Input_`).
  `Rename.java @file` applies them and adds the match evidence to each plate
  comment.

First run, `DREAMSFX.EXE` against `WINDREAM.EXE`: 599 pairs of 1,450/1,272
functions; 5 of the 6 hand-matched renderer pairs recovered without names;
10 names carried to the 3dfx build. Calibration: on known pairs, constant
multisets agree at Jaccard 1.00 where exact code never matches, and mnemonic
bigrams at 0.4–0.87. Treat low-score `slot` pairs as leads, not facts.

### `ApplyWatcall.java` — run this on any fresh project

Ghidra ships **no Watcom compiler spec**, and all four game executables are
Watcom C/C++ 10.6, which passes arguments in `EAX, EDX, EBX, ECX`. Without it
every decompilation is lossy: arguments surface as `extraout_*` and `unaff_*`
and cannot be read at all.

1. Apply `tools/watcall-cspec.patch` to
   `<ghidra>/Ghidra/Processors/x86/data/languages/x86win.cspec`. It adds a
   `__watcall` prototype model. Re-apply after any Ghidra upgrade, then restart.
2. Run the script, pointing it at the Watcom library match:

```powershell
. .\tools\dreams-env.ps1
& (Join-Path (Get-DreamsSetting DREAMS_GHIDRA_ROOT) 'support\analyzeHeadless.bat') `
  ghidra dreams -process WINDREAM.EXE -noanalysis `
  -scriptPath ghidra_scripts -postScript ApplyWatcall.java `
  (Join-Path (Get-DreamsSetting DREAMS_WATCOM) 'sigs\windream.csv')
```

It sets `__watcall` on every function except the 31 runtime helpers with
bespoke register contracts, which the CSV identifies for free — Watcom
decorates register-convention symbols with a **trailing underscore**
(`memcpy_`, `strlen_`), and the ones without (`__CHK`, `IF@DSIN`, `__FDD`) are
hand-written assembly. Applied to `WINDREAM.EXE`: 1,251 converted, 26 skipped.

It also resets each signature to `SourceType.DEFAULT`. That step is essential
and easy to miss — while a signature inferred under the wrong convention
stands, the decompiler will not promote `EBX` to a parameter however right the
prototype model is.

Ground truth: `FUN_0045c278` is `memcpy_` per the library match, and under
`__watcall` it decompiles as a textbook `memcpy(dst, src, len)`.

### `SetWatcall.java`

Same convention on one function at a time, printing before and after. Useful
for checking the model is doing what you expect.

### `ExportSymbols.java` / `ImportSymbols.java`

Round-trip named functions and comments through `re/symbols/<program>.tsv`.

TSV rather than JSON: no dependency, and one line per symbol gives clean git
diffs when someone renames a single function. Addresses are stored as **RVAs**,
so a rebased project still matches. The export records the binary's SHA-256 and
the import warns on mismatch. Ghidra's auto-generated `FUN_xxxxxxxx` names are
skipped so re-analysis does not churn the diff.

### `ImportStructs.java`

`re/structs/windream.h` is the source for verified format records and the
partial runtime actor layout. The script parses it into the WINDREAM/GDI DREAM
program's Data Type Manager. Unknown spans stay byte arrays. The `DREAMS.DAT`
records are external decoded buffers, so their types are not applied to arbitrary
executable addresses. The active actor pointer at `DAT_004fbb48` is typed when
that BSS location is present in the loaded program.

## Not losing work

### How Ghidra saves

- **Headless saves automatically** — the import run logged
  `Save succeeded for processed file: /WINDREAM.EXE`. Pass `-readOnly` to
  suppress it.
- **The GUI saves on Ctrl+S** and prompts on close.
- Auto-recovery snapshots (Edit → Tool Options → Recovery, every 5 min) exist as
  well, but those are *crash* recovery — they will not save you from closing a
  program without saving.

### Two layers of durability

| Layer | What | Durability |
|---|---|---|
| `ghidra/dreams.rep` | the saved project | durable on disk, but **gitignored** — binary, unmergeable, embeds the game executables |
| `re/symbols/*.tsv` | exported names + comments | **durable and in git** — the record that outlives everything |
| `re/structs/*.h` | parsed C layouts | **durable and in git** — imported by `ImportStructs.java` |

`re/` is the source of truth. The Ghidra project is disposable and rebuildable
from the discs with `ghidra-import.ps1 -ImportSymbols -ImportStructs`.

### The project lock

Ghidra locks a project while it is open — a `dreams.lock` appears beside
`dreams.gpr`, and **headless cannot touch a project the GUI holds**. That gives
two checkpoint routes.

**GUI closed** — the normal one:

```powershell
.\tools\re-checkpoint.ps1 -Message "batch rename stream helpers"
```

Exports every program headless, then commits. It detects the lock and redirects
you to the GUI route rather than failing obscurely.

**GUI open:**

1. Ctrl+S.
2. **Window → Script Manager → Dreams → `ExportSymbols.java`** — writes the TSV
   from the live program, no lock conflict.
3. `.\tools\re-checkpoint.ps1 -SkipExport -Message "name the DSN header reader"`

Flags: `-NoCommit` to inspect first, `-SkipExport` to commit an existing export.

### What a checkpoint captures

From the first real run:

| Program | Named functions | Comments |
|---|---|---|
| `CRYO.DLL` | **574** | 1125 |
| `SETUP.EXE` | 258 | 905 |
| `WINDREAM.EXE` | 1 | 350 |

CryoLib's 574 come free — it is a debug build, so Ghidra resolves exports and
signatures:

```
FUNC  1000  GL_InitHnmScreen   undefined GL_InitHnmScreen(void)
FUNC  1019  GL_AddProgramItems undefined4 GL_AddProgramItems(undefined4, uint *, uint *)
FUNC  101e  GL_ShellExec       BOOL GL_ShellExec(LPCSTR, LPCSTR, LPCSTR)
```

`WINDREAM.EXE` shows 1 because nothing has been named in it yet. Watching that
number climb is the measure of progress.

### If you want real version history

Everything above gives you git history of the *annotations*. If you want Ghidra's
native check-in/check-out with per-revision comments and program-level diffing,
run a local **Ghidra Server** (`server/ghidraSvr.bat`) and convert the project to
a shared one. Heavier to operate, and largely redundant for a solo project given
the TSV round-trip — but it is the native answer and it exists.

### Guardrails

- Exports are **RVA-keyed** and stamped with the binary's SHA-256, so rebasing or
  re-importing still matches and a wrong binary warns on import.
- Auto-generated `FUN_xxxxxxxx` names are skipped, so re-analysis does not churn
  the diff.
- `re-checkpoint.ps1` **refuses to commit** if anything matching a game-asset
  extension appears in the working tree.

### Discipline

Checkpoint at every natural pause. It costs seconds, and the TSV diff doubles as
a readable log of what was learned.

## Suggested order of attack

1. ~~Run `FindFormatParsers.java` on `WINDREAM.EXE`.~~ **Done** — see the table
   above.
2. ~~**Decompile `FUN_004175bc`.**~~ **Done** — it is the `.DSN` header reader,
   and the body is now fully decoded. See
   [scene-geometry.md](scene-geometry.md). Kept for context: 157 MB of packed
   level geometry and textures sits behind it, and it is the top open question in
   [research-log.md](research-log.md).
3. Read outward from the magic check: the code immediately after it parses the
   header we already decoded (`u8 flag`, `u32 size @5`, counts, 11-byte name
   table), which gives you a known anchor to orient against.
4. Whatever consumes the bytes past `0x12e` **is the unpacker**. That is the
   answer we cannot get from the outside.
5. Name it, export symbols, commit.

Cross-check anything you find against the measurements in
[assets.md](assets.md) — the decoded headers there are verified byte-exact, so
they make good ground truth for a decompilation you are unsure of.
