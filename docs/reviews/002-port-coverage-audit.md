# 002 retail-function behavior coverage

`opendreams/port-map.tsv` now separates implementation method (`status`) from
retail behavior coverage (`coverage`). `adapted` describes the portable C++
boundary; it is not a claim of complete retail behavior. The map has 50
distinct checked functions, mirrored in WINDREAM.EXE and GDIDREAM.EXE.
Run `uv run python tools/check_port_map.py` for the current counts and exact
remaining work. On this audit: 28 `complete`, 7 `partial`, and 15
`unverified` per executable.

## Known partial ports

| Function | Work needed for full retail behavior |
|---|---|
| `VFS_Open` | Writable/install-source modes. |
| `BF_Mount` | The retail alternate install-root search and path flag. |
| `DSN_LoadHeader` | MEM stack reset and derived `.3DI`/`.3DM` path state. |
| `DAN_ReadAnimChunks` | The `DAN_Read3DC` / `DAN_ReadTextureChunks` call sequence and its `LZ_Unpack` dependency before type-3 reads. |
| `DRD_LoadEntry` | The WAVE length/duration global and remaining entry side effects. |
| `VID_Open` | Codec initializers and sound setup beneath the six-magic dispatch. |
| `VID_Close` | Codec-specific teardown and sound stop once those resources exist. |

The other 15 non-complete functions are `unverified`: their 002-facing
behavior has implementation and tests, but the full original branch/callee
closure is not yet proven. They are listed individually in the TSV. A known
missing leaf must move a row to `partial`; a function reaches `complete` only
after the audit rule in `opendreams/PORT_MAP.md` is met. Both non-complete
states receive `PORT:needs-work` in Ghidra and an explicit `Remaining:` plate
comment.

This audit records porting fidelity. It does not claim that ODViewer has
integrated the shared loaders or that the broader 002 navigation UI is done.
