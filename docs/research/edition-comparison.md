# Retail edition comparison

The comparison uses the complete ISO9660 file trees extracted under
`E:\dev_game\DREAMS_ISOS_EXTRACTED`. `Europe_reference` is byte-identical to
the CUE/BIN sources configured by `.dreams.local.env`. The second European
rip has identical ISO files, as do the two Dutch rips and the Spanish
original/portable ISOs. CD audio tracks lie outside these ISO filesystems.

## Build chronology and shared data

| Edition | `WINDREAM.EXE` PE timestamp (UTC) | Disc 1 files | Disc 2 files |
|---|---|---:|---:|
| European English reference | 1997-10-29 14:25:41 | 1,219 | 409 |
| Dutch | 1997-11-04 16:42:18 | 1,221 | 411 |
| Spanish | 1997-12-05 15:31:39 | 1,224 | 414 |
| Turkish | 1997-12-11 19:20:46 | 1,224 | 415 |

The same `SETUP.EXE` (PE timestamp 1997-09-30) ships in all four editions.
The Turkish ISO volume descriptors date their *images* to 2015-11-26; the
1997 PE timestamp is from the game executable. Timestamps describe headers,
not independently verified release dates. **[verified]**

Across the four editions, 1,442 disc/path file entries are byte-identical.
Of the files changed relative to Europe, promotional or installer demo trees
(`DEMO0`, `DEMOS1`, `DEMOS2`) account for 118/157 Dutch, 94/139 Spanish and
122/166 Turkish paths. All `.DSN` scene files match except the Spanish
Disc 2 `E29USINE.DSN`: one byte changes from `FB` to `FF` at file offset
`0x1c79a3`. It lies in a tag-4 32x32 texture tile (tile 18, row 27,
column 9), not in scene geometry. **[verified]**

The `DREAMS.DAT` project banks form three groups:

- European Disc 1 and both Dutch discs have identical decompressed records.
- European Disc 2 differs from European Disc 1 in six projects: 31, 41, 55,
  69, 75 and 87.
- Both Spanish discs and both Turkish discs are byte-identical to each other.
  Their decompressed bank differs from European Disc 1 in 13 of 150 projects.

These are not solely text changes. The current parser reads Project 0's spawn
position/heading as `(-319, -625, -3187)` / `3046` in Europe/Dutch and
`(-259, -720, -3171)` / `901` in Spanish/Turkish; several lighting and
object fields also differ. **[verified for bytes and parser output]**

## Localization media

Every edition's `DATA/3DC/DIALOG.DRD` declares and contains 178 voice clips,
but the bank differs by language and length: Europe 24.59 MB, Dutch 22.79 MB,
Spanish 23.08 MB and Turkish 24.83 MB. These clips are mono 11,025 Hz,
8-bit WAVE. Four clips in Europe/Turkish and five in Dutch/Spanish have an
impossible 8-bit IEEE-float format tag; the toolkit flags them for header
repair. **[verified]**

Spanish and Turkish Disc 1 add `DIALOG16.DRD` (41,879,738 bytes). Despite its
name, all 178 RIFF clips declare **mono 22,050 Hz, 8-bit PCM**, rather than
16-bit samples. The two editions' 178 audio clips are byte-identical; their
container files differ by one byte in a `TABLE` region between clips 73 and
74. Eight sampled clips from this higher-rate bank correlate 0.95–0.98 with
the English bank after 2:1 downsampling, and near zero with the Spanish and
Turkish localized banks. This strongly indicates a higher-rate **English**
voice bank retained on both later editions. **[verified for formats, bytes
and sampled correlation; inferred for language]**

Among the 93 game movie paths across both discs, comparison of decoded
container *payloads* finds:

| Compared with Europe | Fully identical | Same video/palette, different audio | Video and audio differ |
|---|---:|---:|---:|
| Dutch | 68 | 24 | 1 |
| Spanish | 67 | 25 | 1 |
| Turkish | 67 | 25 | 1 |

The common visual exception is Disc 2 `DATA/HNM/INTRO.HNM`: Europe has 101
frames, while Dutch/Spanish/Turkish share the same 231-frame video with
different audio. Disc 1's main `INTRO.HNM` has the same 2,781 video frames in
all four editions, but different audio. `GUARDIAN.UBB` likewise keeps its
video and palette while changing sound. The Dutch bonus `UBIK.UBB` changes
its video payload but retains the same audio as the other editions.
**[verified by separate IX/IV/PL/SD payload hashes]**

The CUE sheets expose a CD-audio difference outside the ISO filesystems.
Europe and Dutch Disc 2 have one data track and 13 audio tracks; Spanish Disc
2 has one data track and 12 audio tracks. Ten-sector samples from European
tracks 2–12 match the corresponding Spanish tracks exactly. A middle sample
from European track 13 (about 3 min 11 s) is absent from the whole Spanish
Disc 2 BIN. Spanish's final track 13 matches European track 14; the European
track 14 BIN is itself byte-identical to track 2. Whether the missing track
reflects an edition choice or this source's rip is unknown. Turkish data-only
ISOs do not carry any CD-audio tracks. **[verified for track inventory and
sample matches]**

## Leftovers and isolated bytes

- `DATA/LANG/FRANCAIS/DREAMS.INI` and `INIT.TXT` are byte-identical in every
  edition, including Spanish and Turkish. **[verified]**
- Spanish and Turkish have the same revised `HI*.SPR` fonts and the same three
  extra `TUR_*.SPR` fonts. Both also retain an identical Dutch `LEESMIJ.TXT`
  on Disc 1; it differs from the Dutch edition's `LEESMIJ.TXT`. **[verified]**
- Turkish Disc 2 repeats its Disc 1 `DIALOG.DRD` exactly; the other editions
  keep that voice bank only on Disc 1. **[verified]**
- Spanish Disc 2 `DATA/GAME/GAME0.ICO` differs from Europe by one byte
  (`EF`→`FF` at `0x1007`). Turkish Disc 1's bundled DirectX
  `CWBAUDIO.DRV` differs by one byte (`EF`→`FF` at `0x32323`). The isolated
  `DIALOG16.DRD` table byte differs `FF`→`FE`. Whether these are authoring
  edits, copying errors or media damage is unknown. **[verified for bytes]**
- The authoring leftovers `DATA/OBJET/CUBE.ASC`, `3DS.BAK` and the icon bank
  backups are unchanged across editions. **[verified]**

The full per-file hashes are in `file_manifest.csv`; `media_stream_hashes.csv`
separates each movie's video, audio and palette streams; and
`project_record_hashes.csv` hashes the 150 decompressed project records per
disc. All three files live under `E:\dev_game\DREAMS_ISOS_EXTRACTED`.
See [localized-build-symbols.md](localized-build-symbols.md) for the Dutch
and Spanish executable symbol residue.
