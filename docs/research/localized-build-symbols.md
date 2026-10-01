# Localized build symbol residue

The Dutch and Spanish retail Windows game executables contain useful Watcom
OMF object records absent from the configured English release. This is a
source of original function names, even though none of the editions ships a
standalone game PDB, MAP, DBG or symbol file. **[verified]**

The `REND_` prefixes in this repository are descriptive names, not recovered
Cryo spellings. The OMF records use names such as `Process_Obj_` without a
module prefix. The trailing underscore is consistent with Watcom symbol
decoration. The source's precise naming rules are not known. **[verified for
the recorded spellings; inferred for the decoration]**

## What survived

`WINDREAM.EXE` and `GDIDREAM.EXE` in both editions contain checksum-valid
OMF `THEADR`, `EXTDEF`, `PUBDEF`, `LEDATA` and `FIXUPP` records inside the PE
initialized-data section. A `THEADR` record names
`C:\SOURCES\BEN11\3DC_MATH.C`; associated comments name `3dc_priv.h` and
`include\3dc_h.h`. `LEDATA` byte sequences also occur in the executable's
live code. The Dutch and Spanish `PUBDEF` records define the same eight
functions. **[verified]**

The eight function definitions are in a partial renderer object stream that
ends at a valid OMF `MODEND` (`0xac0fd` Dutch, `0xae127` Spanish). The next
object begins at a `THEADR` for `3DC_MATH.C` (`0xac14c` Dutch, `0xae16c`
Spanish). Thus the `3DC_MATH.C` path belongs to the following object; the
renderer object's own source filename is not exposed by the surviving
records. This is a fragment of object metadata, not a whole-program symbol
database. **[verified]**

| Recovered name | Dutch VA | Spanish VA | English VA | Existing English Ghidra name |
|---|---:|---:|---:|---|
| `Build_Obj_Lights_` | `0047b880` | `0047c580` | `0047b7e0` | `REND_LightObject` |
| `Build_Obj_Env_Mapping_` | `0047e134` | `0047ef00` | `0047e094` | `REND_ComputeEnvMapUVs` |
| `Build_Obj_Miror_` | `0047e424` | `0047f1f0` | `0047e384` | no function entry |
| `Process_Obj_` | `0047e538` | `0047f304` | `0047e498` | `REND_DrawObject` |
| `Update_Obj_` | `0047e6d4` | `0047f4d0` | `0047e634` | no function entry |
| `Process_Hierarchie_` | `0047e7a0` | `0047f59c` | `0047e700` | `REND_DrawScene` |
| `Update_Hierarchie_` | `0047e84c` | `0047f698` | `0047e7ac` | no function entry |
| `Update_Miror_` | `0047e8c4` | `0047f704` | `0047e824` | no function entry |

For Dutch, matching `LEDATA` against linked PE code fixes the object-code
file base at `0x77e20`; for Spanish it is `0x78f70`. The `PUBDEF` offsets
then give the Dutch and Spanish VAs. To transfer into the English executable,
the Dutch code is aligned against it. From each recovered entry to the next,
the code has zero byte differences after masking paired base relocations and
relative branch/call operands; the final entry was checked for its first
128 bytes. The four missing English entries are executable code, not labels
inside a data block. **[verified]**

The Dutch `EXTDEF` records also preserve names such as `Cull_Obj_`,
`XForm_Free_Verts_`, `XForm_Verts_`, `XForm_Norms_`, `Cull_Faces_`,
`Compute_Face_Illum_`, `Project_Verts_`, `XForm_Lights_`, `Get_Obj_`,
`Heap_Init_` and `Mat_Mul_`. They are references, so the names alone do
not assign target addresses. **[verified]**

The Spanish `PUBDEF` function addresses are also the Turkish Windows function
addresses: the two later builds have matching code layout and instruction
bytes after accounting for relocation values. Turkish lacks the OMF records
themselves. See [binary-edition-comparison.md](binary-edition-comparison.md).
**[verified]**

Two more `PUBDEF` records define data symbols. Both builds have
`_Build_Obj`, `_CK`, and `_flag1` through `_flag4`; Dutch also has
`_vertices_processed` and `_faces_processed`, while Spanish has
`_nb_objects1` and `_nb_objects2`. Their OMF segment offsets are recorded in
`E:\dev_game\DREAMS_ISOS_EXTRACTED\omf_data_pubdefs.tsv`; addresses in the
English executable have not been assigned. The 42 `EXTDEF` entries from both
builds are in `omf_extdefs.tsv`. **[verified]**

## Conventional debug data

All four editions' `WINDREAM.EXE` and `GDIDREAM.EXE` have zero PE debug
directory and COFF symbol table. Their `DREAMS.EXE` and `DREAMSFX.EXE` have
zero LE debug offset and length. The disc trees have no standalone game
PDB, DBG, MAP, SYM, OBJ or source file; `DATA/SYM` is empty. **[verified]**

A scan of all 25 distinct non-DirectX executable/library binaries across the
four editions found no further plausible, checksum-valid OMF `PUBDEF` records
or game PDB references. The English and Turkish Windows game builds contain
none of this OMF fragment. Why these records survived in Dutch and Spanish
while absent from English and Turkish is not established. **[verified for
the scan; unknown cause]**

The identical `DEMOS2/CRYO.DLL` in every edition retains a CodeView `NB10`
record referring to `E:\visual\CryoLib\debug\cryo.pdb` (signature
`0x3356218c`, age 530) and 165 named exports. The PDB itself is absent.
This is the separate CryoLib component described in [cryolib.md](cryolib.md).
The common `SETUP.EXE` has installer debug-directory records, including a
`./Debug/setup.exe` path, but no usable game symbols. **[verified]**

The extracted files and a machine-readable table of the eight `PUBDEF`
mappings are under `E:\dev_game\DREAMS_ISOS_EXTRACTED` on this machine.
