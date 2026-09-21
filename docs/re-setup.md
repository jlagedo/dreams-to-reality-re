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

Round trip: annotate in Ghidra → `ExportSymbols.java` → commit the TSV → someone
else runs `ghidra-import.ps1 -ImportSymbols` and gets your annotated project
from their own copy of the discs.

## Installed on this machine

| Component | Version | Location |
|---|---|---|
| Ghidra | 12.1.3 (2026-08-18) | `E:\tools\ghidra_12.1.3_PUBLIC` |
| JDK | Temurin 25.0.3 LTS | already on PATH; Ghidra needs 21+ |

The GUI is `E:\tools\ghidra_12.1.3_PUBLIC\ghidraRun.bat`. Use it to look at a
program and to run scripts from the Script Manager; everything reproducible goes
through headless.

## Headless workflow

For anything reproducible, prefer headless over the GUI:

```powershell
.\tools\ghidra-import.ps1                     # create project, import, analyse
.\tools\ghidra-import.ps1 -ImportSymbols      # …and re-apply saved names
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
Ghidra does not ship. Worth it later — the DOS builds carry richer symbol
residue — but not the place to start.

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
analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis `
  -scriptPath ghidra_scripts -postScript ApplyWatcall.java `
  E:\dev_game\watcom\sigs\windream.csv
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

`re/` is the source of truth. The Ghidra project is disposable and rebuildable
from the discs with `ghidra-import.ps1 -ImportSymbols`.

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
