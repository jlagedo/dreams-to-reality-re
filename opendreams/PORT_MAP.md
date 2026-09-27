# Port map policy

`port-map.tsv` is the authoritative, versioned mapping for implemented retail
functions. Spec 001 added only new foundation code; the first adapted retail
functions are the file-root getters used by the upcoming VFS port. Use one row
per original function and program with these tab-separated columns:

`program`, `address`, `checked_name`, `source_block`, `cpp_file`,
`cpp_symbol`, `status`, `evidence`, `adaptation`, `coverage`, `remaining_work`,
`reviewed`.

`checked_name` must match the checked registry in `../re/names/`; this map does
not create names. `source_block` cites the report or research note and says
whether the boundary is proven, candidate or unknown. `status` is one of
`ported`, `adapted`, `replaced` or `omitted`; explain deviations and the original
call site in `adaptation`. `cpp_file` is a repository-relative path and
`cpp_symbol` names the implementation entry point; leave both empty for an
omitted function. Use these instead of line numbers, which move during edits.
A provisional file assignment remains explicit until source evidence improves.
New paths under `shared/` are our organizational names, never claims about
Cryo's original filenames.

`status` describes **how** the C++ implementation relates to retail.
`coverage` separately describes **how much retail behavior is accounted for**:

| Coverage | Rule |
|---|---|
| `complete` | The original function's supported branches, required callees, state transitions, outputs and cleanup have been reviewed and implemented. Portable source ownership and recoverable errors may differ when stated in `adaptation`. No known retail behavior is deferred. |
| `partial` | A known retail branch, callee, output or side effect is still missing, even if the 002 metadata result is correct. `remaining_work` names the missing behavior or callees. |
| `unverified` | The C++ function exists and has evidence, but its full retail call closure or behavior has not been audited enough to claim completeness. `remaining_work` names the needed audit. |
| `none` | No implementation exists; valid only with `status=omitted`. `remaining_work` explains the omission. |

`remaining_work` must be `-` for `complete` and a specific nonempty explanation
for every other coverage value. Passing a fixture or the known-disc corpus does not by itself
prove `complete`. Before changing a row to `complete`, inspect every relevant
return path and transitive callee in Ghidra, check mutable state and cleanup,
compare outputs against independent evidence, and remove the listed work.
An `adapted` implementation may be either `complete` or `partial`.
`reviewed` is a separate `yes`/`no` sign-off by the project owner. Set it to
`yes` only after the owner has personally reviewed the function, understands
its `status`, `coverage`, and any `remaining_work`, and explicitly confirms that
review. New rows start at `no`. A `partial` or `unverified` port can still be
reviewed; review does not change coverage. Do not infer `yes` from tests, an
AI review, or discussion of the function. Reset it to `no` when the
implementation, `status`, `coverage`, or `remaining_work` changes materially,
so the owner's prior review is not mistaken for review of the new state.
Run `uv run python tools/check_port_map.py` before committing a map change;
it validates these rules, checked names, C++ locations and Windows twin rows,
including matching owner-review state. Its summary reports the reviewed count.

## Show the map inside Ghidra

The TSV is the authoritative, versioned record. The local Ghidra project is
gitignored and disposable. For each mapped function, mirror its `status` as one
Ghidra Function Tag (`PORT:ported`, `PORT:adapted`, `PORT:replaced` or
`PORT:omitted`) plus a coverage tag (`PORT:complete`, `PORT:partial`,
`PORT:unverified` or `PORT:none`). Partial and unverified rows also get
`PORT:needs-work`. Owner review adds `PORT:reviewed` or
`PORT:review-pending`, independent of coverage. The generated `[PORT_MAP]`
plate comment includes status, coverage, C++ location, remaining work and
owner-review state. That puts the answer beside the
decompilation and lets Ghidra's Function Tags window list incomplete ports.
Do not encode the changing
source path in tag names or edit a second copy of the status by hand.

`ghidra_scripts/ApplyPortMap.java` reapplies this view from `port-map.tsv`,
removing only prior `PORT:*` tags and `[PORT_MAP]` comment blocks before writing
current values. It preserves `[NAME]`, `[DOCS_SYNC]`, manual notes and every
unrelated tag. Run it in Ghidra's Script Manager for each program after changing
the map, or run it through headless Ghidra without `-readOnly`:

```powershell
. .\tools\dreams-env.ps1
$ghidra = Get-DreamsSetting DREAMS_GHIDRA_ROOT
& (Join-Path $ghidra 'support\analyzeHeadless.bat') ghidra dreams -process WINDREAM.EXE -noanalysis -scriptPath ghidra_scripts -postScript ApplyPortMap.java
& (Join-Path $ghidra 'support\analyzeHeadless.bat') ghidra dreams -process GDIDREAM.EXE -noanalysis -scriptPath ghidra_scripts -postScript ApplyPortMap.java
```

`tools/ghidra-import.ps1 -ImportSymbols` also runs `ApplyPortMap.java` after
restoring names. `ExportSymbols.java` exports comments but not function tags;
the TSV rebuilds both on a fresh project. Keep implementation status in the
TSV, not solely in Ghidra or its symbol export. Commit `port-map.tsv` with the
C++ change it describes; `tools/re-checkpoint.ps1` stages `re/` and `docs/`,
not `opendreams/`.

## One-function port loop

1. Check the function's address and name against `re/names/`. Inspect its
   comment-stripped body, callers, callees, globals, allocation and cleanup in
   Ghidra. Verify the twin binary before adding a twin row.
2. Implement the function in `shared/port/`, with an explicit source or state
   object where the original used process globals. Record any changed boundary
   or error behavior in `adaptation`.
3. Exercise the call through an ODRuntime-compatible shared caller; compare
   observable results with the decompilation and corpus or Python oracle.
4. Add the row only when the C++ symbol exists and has been tested. Set
   `coverage` conservatively; a deferred leaf makes it `partial`, and an
   incomplete closure audit makes it `unverified`. Initialize `reviewed=no`
   until the owner explicitly signs off. Apply the map in Ghidra and
   verify both tags and the implementation-location plate comment.
5. Run `uv run python tools/check_port_map.py`, then commit the implementation,
   test, map and documentation together.
