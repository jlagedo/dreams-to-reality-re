# Disc layout

## Where the images live

Nothing in this repo contains game data. On this machine:

```
E:\dev_game\
├── Dreams-to-Reality_Manual_Win_de.pdf                    3.6 MB  German manual
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-1.zip       548.9 MB  TorrentZip
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-2.zip       568.0 MB  TorrentZip
├── Dreams-to-Reality_Win_EN_Disc-Image-Disk-1\
│   ├── Dreams to Reality (Europe) (Disc 1).cue
│   ├── ... (Track 01).bin   382,134,144 B   MODE1/2352 data
│   ├── ... (Track 02..12).bin                11 CD-audio tracks
│   ├── disc1.iso                             derived, 2048 B/sector
│   └── extracted\                            1219 files, 27 folders, 319 MB
└── Dreams-to-Reality_Win_EN_Disc-Image-Disk-2\
    ├── Dreams to Reality (Europe) (Disc 2).cue
    ├── ... (Track 01).bin   435,646,848 B   MODE1/2352 data
    ├── ... (Track 02..14).bin                13 CD-audio tracks
    ├── disc2.iso                             derived, 2048 B/sector
    └── extracted\                            409 files, 26 folders, 362 MB
```

Redump-style dumps: raw 2352-byte sectors, mixed-mode CD. Track 1 carries the
ISO9660 filesystem. **Every remaining track is redbook audio and is the game's
music** — it never appears as files in the extracted tree, which is why a `.cue`
must be mounted rather than an `.iso`. **[verified]**

Disc timestamps read `1996-12-24` on the archive entries but the files inside are
dated September/October 1997.

## Extracting a data track

Track 1 is raw 2352 B/sector: 16-byte sync+header, 2048 bytes of payload,
288 bytes of ECC. Strip to a mountable 2048 B/sector ISO:

```python
with open(src, "rb") as f, open(dst, "wb") as o:
    while True:
        s = f.read(2352)
        if len(s) < 2352:
            break
        o.write(s[16 : 16 + 2048])
```

Then `7z x disc2.iso -oextracted`.

## Disc 1 — program disc

Every game executable is here. **[verified]**

| File | Size | Notes |
|---|---|---|
| `SETUP.EXE` | 258,320 | Windows installer. MSVC-built (normal PE sections), unlike the game |
| `WINDREAM.EXE` | 864,768 | Windows game, DirectDraw default |
| `GDIDREAM.EXE` | 864,768 | Same build, GDI/windowed default |
| `DREAMS.EXE` | 1,176,454 | DOS build, LE + DOS/4GW |
| `DREAMSFX.EXE` | 1,025,154 | DOS 3dfx/Glide build |
| `DOS4GW.EXE` | 265,396 | Rational DOS extender |
| `UNINSTAL.EXE` | 35,328 | |
| `GETKEY.EXE` | 4,469 | keypress→errorlevel helper for the `.BAT` installers |
| `INSTALL.BAT` / `INSTALFX.BAT` | 1,274 / 1,279 | DOS and DOS-3dfx installers |
| `INST_SON.BAT` / `INST_SFX.BAT` | 1,682 / 1,684 | called by the above |
| `INSTALL.PIF` | 967 | Program Information File for the DOS installer |
| `SETUP.INI` | 5,091 | install manifest and all installer UI strings |
| `SETUP.ICO` | 8,422 | |
| `AUTORUN.INF` | 41 | `open=setup.exe` |
| `README.TXT` | 5,322 | official install/controls doc |
| `DREAMS.DAT` | 138,879 | binary offset table |
| `LISTL0..4.TXT` | 120–4,917 | per-level asset manifests |
| `3DFX\` | | Glide runtime. `GRTVGR.EXE` is a PKZIP archive holding `glide2x.ovl` |
| `DIRECTX\` | 43 MB | DirectX 5 redistributable, 849 files |
| `DEMO0\`, `DEMOS1\` | | installer UI art and other-game demos |

`DATA\` on disc 1 — 268 MB:

| Folder | Files | Size | Contents |
|---|---|---|---|
| `3DC` | 185 | 121.6 MB | geometry, animation, scenes, `DIALOG.DRD` |
| `HNM` | 49 | 136 MB | cutscenes |
| `ANIM` | 6 | 6.8 MB | HNM4 texture animations |
| `SOUND` | 17 | 0.9 MB | Miles drivers + `FSB.DAT` |
| `OBJET` | 9 | 0.5 MB | sprites, particles, 3D Studio leftovers |
| `FONT` | 3 | 0.1 MB | `.SPR` bitmap fonts |
| `LANG` | 2 | — | `FRANCAIS\DREAMS.INI`, `FRANCAIS\INIT.TXT` |
| `UNIVBE` | 2 | 0.3 MB | SciTech VESA driver |
| `ICONE` | 1 | 0.4 MB | `ICONES.BF` |
| `GAME`, `SYM`, `TGA` | 0 | — | **empty on disc 1** |

Note `DATA\LANG\FRANCAIS\` is the only language folder present — in an English
release. The installer strings in `SETUP.INI` are also entirely French.

## Disc 2 — data disc

**No game executables**, but it is not executable-free — it ships a DLL and two
standalone players: **[verified]**

```
644,608  DEMOS2/CRYO.DLL         registered in SETUP.INI as [PlusTest]
 31,744  DEMOS2/PLAYUBB.EXE      [DemoPlayerUbb]
 21,504  DEMOS2/PLAYTGA.EXE      [DemoPlayerTga]
120,295  DATA/SOUND/SETSOUND.EXE
  8,029  DATA/SOUND/MSSW95.EXE
269,682  DATA/UNIVBE/UVCONFIG.EXE
```

`CRYO.DLL` is the highest-value unexamined binary on either disc — see
`research-log.md`.

`DATA\` on disc 2 — 273 MB:

| Folder | Files | Size |
|---|---|---|
| `HNM` | 44 | 161 MB |
| `3DC` | 147 | 85 MB |
| `ANIM` | 14 | 24 MB |
| `ICONE` | 5 | 3 MB |
| `OBJET` | 10 | — |
| `SOUND` | 17 | — |
| `TGA` | 7 | — |
| `GAME` | 3 | — | `GAME.DAT`, `GAME0.DAT`, `GAME0.ICO` — **empty on disc 1** |
| `FONT`, `LANG`, `UNIVBE` | 3 / 2 / 2 | — |
| `SYM` | 0 | — |

Extras: `CRYOPLUS\` (20 numbered `.TGA` files, 12 MB bonus gallery) and
`DEMOS2\` (75 MB of demos for Ubik, 3 Millennia and Atlantis).

## Install manifest (`SETUP.INI`)

The Windows installer offers two sizes. **[verified]**

| | Mini | Maxi |
|---|---|---|
| Declared space | 30 MB | 100 MB |
| Directories created | `DATA`, `DATA\3DC`, `DATA\ANIM`, `DATA\GAME` | same |

**Mini** copies only eight files:

```
setup.exe  setup.ini  uninstal.exe  windream.exe  gdidream.exe
DATA\HD.ID  DATA\REPLAY.BIN  DATA\3DC\DIALOG.DRD
```

**Maxi** copies those plus **`DATA\FULL.ID`** and a set of frequently-used
geometry: `ARC.3DC`, `BOULE.3DC`, `CARRE.3DC`, `EPEE.3DC`, `GR00..GR04.3DC`,
`GUN.3DC`, `MANA.3DC`, `OMBRE.3DC`, `OMBRE2.3DC`, `PARTICL2.3DC`, `X01SOL.3DC`,
`X01SOL1.3DC`, `ESSAI.3DM`, `GRILLE.3DM`, `OMBRE2.3DM`, `SPRITE.3DM`.

Two things follow. First, `DIALOG.DRD` (24.6 MB) is copied even by the minimum
install, so the engine requires it resident rather than streamed. Second — and
this is the important one — **`FULL.ID` is written only by the maxi install**. It
is the "maximum installation" marker, matching `README.TXT`'s note that the maxi
version "also offers video-maps in the game". It is *not* self-evidently a
"no-disc-needed" flag.

Other useful keys:

```
[GET DIR]          C:\CRYO\DREAMS          default install path
[AppExe]           windream.exe            what the launcher runs
[DIRECTX INSTALL]  DirectX\DirectX\dxsetup.exe
[PlusDir]          Cryoplus
[PlusTest]         demos2\cryo.dll
[CHANGE CD TXT]    Insérer le CD 2.
[CHANGE CD 1 TXT]  Insérer le CD 1.
[CHANGE CD TXT2]   CD 1 ou 2 ne peut pas être trouvé ...
[COPYRIGHT]        Cryo Interactive © 1997
```

The disc-swap prompts are French even in the English release.

## Disc check

The game locates itself by probing for small marker files in `DATA\`. **[verified]**

| File | Disc 1 | Disc 2 | Bytes |
|---|---|---|---|
| `1CD.ID` | present | absent | `6B 6A 6B 0D 0A` = `kjk\r\n` |
| `2CD.ID` | absent | present | `6B 6A 6B 0D 0A` = `kjk\r\n` |
| `FULL.ID` | present | absent | `74 6F 74 6F 0D 0A` = `toto\r\n` |
| `HD.ID` | present | present | disc 1: `toto\r\n` — disc 2: `01 00 00 00` |

`WINDREAM.EXE` hardcodes the probe path `X:\CRYO\DREAMS\DATA\HD.ID`.

So: `1CD.ID` / `2CD.ID` identify which disc is mounted, `HD.ID` marks a hard-disk
install, and `FULL.ID` marks a maxi install. The `kjk` and `toto` payloads are
French keyboard-mash placeholders — the content is almost certainly irrelevant
and only the file's existence is tested. **[unverified]**

Disc 2's `HD.ID` being binary `01 00 00 00` while disc 1's is the text `toto`
is unexplained and is an open question.

## Merging both discs

Of 409 disc-2 files, 170 names collide with disc 1 and only **10 differ in
content**. **[verified]**

| File | Disc 1 | Disc 2 | Resolution |
|---|---|---|---|
| `DATA\HNM\INTRO.HNM` | 38,164,312 | 1,698,736 | **keep disc 1** — full intro, 2781 frames |
| `DATA\ICONE\ICONES.BF` | 372,358 | 440,029 | **keep disc 2** — larger |
| `DREAMS.DAT` | 138,879 | 138,835 | **unresolved** — diff the offset tables first |
| `DATA\UNIVBE\UVCONFIG.EXE` | 309,382 | 269,682 | keep disc 1 (DOS-only path) |
| `DATA\HD.ID` | `toto\r\n` | `01 00 00 00` | **keep disc 1** — see above |
| `DATA\3DC\DESCRIPT.ION` | 723 | 386 | junk — 4DOS file descriptions |
| `DATA\ANTI-VIR.DAT` | 192 | 192 | junk — AV checksum cache |
| `DATA\SOUND\ANTI-VIR.DAT` | 256 | 256 | junk |
| `DATA\UNIVBE\ANTI-VIR.DAT` | 192 | 192 | junk |
| `DEMO0\SETUP.GID` | 8,628 | 8,628 | junk — Windows help index |

Merged size is roughly **480 MB** excluding `DEMOS*`, `DEMO0` and the DirectX
redistributable. That matches `README.TXT`'s 500 MB minimum / 1 GB maximum.

### Reproducing the comparison

```bash
cd "$D1" && find . -type f | sed 's|^\./||' | sort > /tmp/d1.txt
cd "$D2" && find . -type f | sed 's|^\./||' | sort > /tmp/d2.txt
comm -12 /tmp/d1.txt /tmp/d2.txt | while IFS= read -r f; do
  [ "$(md5sum "$D1/$f" | cut -d' ' -f1)" != "$(md5sum "$D2/$f" | cut -d' ' -f1)" ] && echo "$f"
done
```
