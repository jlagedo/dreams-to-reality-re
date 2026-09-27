# Port map policy

Spec 001 contains only new foundation code. It ports no recovered game
functions, so there are no mapping rows yet.

When the first function is ported, add a tab-separated `port-map.tsv` beside
this file. Use one row per original function with these columns:

`program`, `address`, `checked_name`, `source_block`, `cpp_file`,
`cpp_symbol`, `status`, `evidence`, `adaptation`.

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

## Show the map inside Ghidra

The TSV is the authoritative, versioned record. The local Ghidra project is
gitignored and disposable. For each mapped function, mirror its `status` as one
Ghidra Function Tag (`PORT:ported`, `PORT:adapted`, `PORT:replaced` or
`PORT:omitted`) and its path/symbol/status as a generated `[PORT_MAP]` plate
comment. That puts the C++ location beside the decompilation and lets Ghidra's
Function Tags window list functions by port status. Do not encode the changing
source path in tag names or edit a second copy of the status by hand.

When the first mapping row exists, add a script that reapplies this view from
`port-map.tsv`, removing only prior `PORT:*` tags and `[PORT_MAP]` comment blocks
before writing current values. It must preserve `[NAME]`, `[DOCS_SYNC]`, manual
notes and every unrelated tag. `ExportSymbols.java` currently exports comments
but not function tags; a fresh Ghidra project must therefore be able to rebuild
the port view from the TSV. Keep the actual implementation status in the TSV,
not solely in Ghidra or its symbol export. Commit `port-map.tsv` with the C++
change it describes; `tools/re-checkpoint.ps1` stages `re/` and `docs/`, not
`opendreams/`.
