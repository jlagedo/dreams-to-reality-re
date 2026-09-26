# Port map policy

Spec 001 contains only new foundation code. It ports no recovered game
functions, so there are no mapping rows yet.

When the first function is ported, add a tab-separated `port-map.tsv` beside
this file. Use one row per original function with these columns:

`program`, `address`, `checked_name`, `source_block`, `cpp_file`, `status`,
`evidence`, `adaptation`.

`checked_name` must match the checked registry in `../re/names/`; this map does
not create names. `source_block` cites the report or research note and says
whether the boundary is proven, candidate or unknown. `status` is one of
`ported`, `adapted`, `replaced` or `omitted`; explain deviations and the original
call site in `adaptation`. A provisional file assignment remains explicit until
source evidence improves. New paths under `shared/` are our organizational
names, never claims about Cryo's original filenames.
