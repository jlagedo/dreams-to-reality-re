# Game content

> **Function names verified (2026-09-26).** Every `WINDREAM.EXE` function this page names is in [`re/names/WINDREAM.EXE.tsv`](../re/names/WINDREAM.EXE.tsv) with two independent sources (these docs and a blind review of the decompilation) and facts checked against the binary by `tools/check_names.py`.

Derived from `DATA\LANG\FRANCAIS\DREAMS.INI`, the game's text resource file.
**[verified]** — this is a plain-text file shipped on both discs.

The companion `INIT.TXT` says only: *"Placer le fichier DREAMS.INI dans
DATA\LANG\FRANCAIS\"*. `FRANCAIS` is the **only** language directory present, in
an English-labelled release.

## The English build never reads this file **[verified]** (2026-09-26)

The loader (code at `0x433d2c`, inside `0x433cfa`, called from `UI_InitIcons`;
Ghidra has not disassembled the body, read with capstone) opens
`sprintf("%s\%s\%s", "data\lang", language, "dreams.ini")` in text mode
and parses it line by line: `#` comments, blank lines, `[` section headers
compared with `[NEW]`, `[OBJECT]`, `[DIALOG]`, `[PROJECT]`, `[SYSTEM]`,
`[END]`, and text lines appended to the current record. The `language` pointer
(`0x4a2f65`) is initialised to **`ENGLISH`** and nothing writes it; the same
string sits in `DREAMS.EXE` and `DREAMSFX.EXE`. The discs carry only
`DATA\LANG\FRANCAIS`, and `SETUP.INI` copies nothing from `DATA\LANG`. So the
English release never finds a `DREAMS.INI`, and the tables keep their
**compiled-in English text**:

- **Items** at `0x49e022`: 30 records of three 33-byte lines (name, two
  description lines), types at `0x49dfda` (1 = spell, items 0-13; 2 = object,
  14-29). The English descriptions are real text ("Fire ball: Creation of an
  energy ball aimed against enemies"; "Healing: Exchange of manna for life"),
  and item 28 is "Pyramid", where the French file has a perfume bottle.
- **System strings** at `0x4a102c`, 0x63 bytes each: "Reload and play again
  a game", "Save the game in progress...", ..., "Error on disk / Disk full".
- **`[DIALOG]`**'s one entry is "Again" (`0x4a1584`), the French "Encore".

The French placeholders below are therefore what a French build shows, not
what an English player saw.

## The file is full of placeholder text

`DREAMS.INI` has a documented syntax header (`# demarre une ligne de
commentaires`) and four sections: `[OBJECT]`, `[PROJECT]`, `[DIALOG]`,
`[SYSTEM]`. Records are separated by `[NEW]`.

Critically, **most of the item descriptions are developer placeholders, not
shipped text**:

| Item | "Description" |
|---|---|
| Mine | `BOOM` |
| Shaman | `Tiens un schtroumps` ("here's a smurf") |
| Vitesse (Speed) | `Bip Bip ...` |
| Disque (Record) | `Yesterday All my troubles / seems so far away...` — Beatles lyrics |
| Sort de connaissance | `Etre ou ne pas �tre / la est la question` — Hamlet |
| Ralentisseur de temps | `Onnnn eeessstt paaas / ennn sssuuiissse` |
| Arc de fl�ches de cristal | `La fonction de l'arc / de fl�ches de cristal` — restates the name |
| Boule de feu | `Et voici la description / du sort de feu` — literally "and here is the description of the fire spell" |
| Spirit | `I'm the best with / the spirit power` — **in English**, in the French file |

`[DIALOG]` contains one entry: `Encore`. This is a string in the language
resource file, separate from the 178 spoken entries and timed caption lines in
`DIALOG.DRD`. The retail voice/caption path is now traced, but whether this
single `DREAMS.INI` string appears in a particular UI remains open. See
[sprites-ui-dialog.md](sprites-ui-dialog.md).

## Inventory items (30)

In file order — this is likely the in-game item enumeration:

```
 0  Boule de feu (fireball)        15  Corne d'appel des baleines (whale horn)
 1  Arc de fl�ches de cristal      16  Disque (record)
 2  Ep�e (sword)                   17  Lettre 1
 3  Gu�rison (healing)             18  Lettre 2
 4  Bouclier (shield)              19  Lettre 3
 5  Sort de connaissance           20  Tourne Disque (record player)
 6  Ralentisseur de temps          21  Note Noire (black note)
 7  Spirit                         22  Note Blanche (white note)
 8  Hologramme                     23  Omega
 9  Resurection                    24  Moulin � poivre (pepper mill)
10  Invisibilit�                   25  Masque Africain (African mask)
11  Mine                           26  Sucette (lollipop)
12  Shaman                         27  Piece (coin)
13  Vitesse (speed)                28  Bouteille de parfum (perfume bottle)
14  Cl� (key)                      29  Plans du surf (surfboard plans)
```

Items 0-13 are abilities and spells; 14-29 are quest objects (the Key starts
the second group). This 14/16 split also matches the retail menu's category
flags for ability versus object icons.

## Levels — 150 "projects"

`[PROJECT]` enumerates `Project0` through `Project149`, each with a French
descriptive name. Only 98 `.DSN` scene files exist across both discs, so projects
reuse scenes — and several entries are explicitly marked `Doublon` (duplicate).

Seven entries have **no name at all**, just a raw filename — unfinished content
that shipped: `F20_FEU.DSN` (×2), `F21_AIR.DSN` (×2), `F22_EAU.DSN` (×3),
`F32LAB.DSN`, `H18ANGKR.DSN`.

Five are marked as duplicates: `Doublon Montagne du`, `Doublon Tout le iles`,
`Doublon Seconde A`, `Doublon inthe Usine`, `Doublon Montagne du Bien Mal`.

Two names are truncated mid-word (`Passage vers Pyramid`, `Cabine de projectio`,
`Cauchemard Gargouill`), implying a **20-character** field limit.

### Where the game starts

**[verified]** **Project 0, *Ile d'Angkor*, scene `H18ANGKR.DSN`.** Following
the `LINK` entries in `DREAMS.DAT` reaches **145 of the 150 projects from P0**
(see [level-map.md](level-map.md)), which is what settles it rather than the
index being zero. The decompiled path from the main menu's New Game to this
map — including which videos play on the way — is in
[boot-sequence.md](boot-sequence.md).

The map is a floating plateau of grass carrying a stone tower wrapped in
roots — Angkor, and specifically Ta Prohm. Its object names say so outright:
`H18RACIN` (*racine*, root), `H18TETA1`-`5` (*tete*, the Bayon face towers),
`H18_BRIK`, `H18_DALE` (*dalle*, paving), `H18_PELZ` (*pelouse*, lawn) and
`H18_C_BK`/`FT`/`LF`/`RT`, the four walls of a cube skybox.

It loads five things besides the scene: `F84.DAN` (the floating island, 15,000
units out past the rim), `CH0.DAN` (a creature standing on it), four copies of `F07BLEU.DAN`
(*bleu* — the blue gnomes), and `MINE.DAN`, whose mesh is internally named
`GRILLE` — a grate, so either the mine or the grating an explosion removes.
**[unverified]** which.

**`H03PAQUE.DSN` is not the first map**, though `LISTL0.TXT` — the boot
manifest — names it and nothing else. It is the always-resident scene, loaded
beside `XH_`, `MHE`, `CH0` and `HOLO`. *Ile de Paques* is Easter Island and it
has the heads to match (`H03TET01`-`06`), plus eight radiating `H03TRS`
walkways, but its sky texture `H03NUI01` is a **starfield** and `H03HNM01` a
comet: it is a night scene. **[verified]**

### Thematic grouping

The level names cluster into recognisable areas:

| Theme | Levels |
|---|---|
| **Angkor** | Ile d'Angkor, Ile d'Angkor 2, Interieur Angkor, Cauchemard Angkor (×2) |
| **Spider** (Araignee) | Araignee sous-sol, 1er/2eme etage, Exterieur Araignee, Seconde Araignee 1/2 |
| **Shark** (Requin) | Exterieur/Interieur Requin (×2 each), Arene des Requin, Lutin Requin (×2), Ride Requin Bombonne |
| **Hamam** (bathhouse) | Piscine Hamam (×2), Interieur Hamam, Ile du Hamam (×2), Hamam 2eme partie, Cauchemard Ile Hamam, Ile du Hamam Baleine |
| **Factory** (Usine) | Usine Regeneration, labyrinthe Usine, Usine Minotaure, Usine, Usine Fin |
| **Cinema** | Cinema, Cinema 2eme partie (×2), Passage du cinema, Passage Cinema 2, Cabine de projectio, Cabine 2eme Partie, Ile Projectionniste |
| **Army** (Armee) | Interieur Armee 1/2, Exterieur Armee 1/2, Hangard Armee, Second/Troisieme Hangard, Cellule Armee |
| **Arenas** (Arene) | Arene Naissance, Maturite (×2), Mort, Conclusion, des Requin |
| **Nightmares** (Cauchemard) | Chaman, Angkor (×2), Ile Hamam, Gargouill, du Lutin (×2), du Serval |
| **Elements** | Interieur Feu / Air / Eau, F20_FEU, F21_AIR, F22_EAU |
| **Hidden rooms** | Piece cachee 1-7 |
| **Rides** | Ride Baleine (×2), Ride Surf (×2), Ride Canyon Surf, Ride Bombonne, Ride sous eau, Ride Baleine Tornade |

The four arenas named **Naissance → Maturite → Mort → Conclusion** (birth →
maturity → death → conclusion) suggest the game's structural spine.

`Gaudi Seconde partie` and `Piece Gaudi` reference the architect Antoni Gaudí.

## Save system

`[SYSTEM]` holds the save/load UI strings, confirming PCGamingWiki's description:

```
Charger et rejouer une partie          load and replay a game
Sauvegarder la partie en cours...      save the current game
Choisir les options du jeu             game options
Quitter le jeu                         quit
Chargement en cours                    loading
Erreur sur le disque / Impossible de charger cette partie
Partie non prot�g�e   /  Partie prot�g�e          unprotected / protected slot
Sauvegarde effectu�e                    save complete
Partie prot�g�e impossible de sauvegarder sur cet emplacement
Erreur sur le disque / Impossible de sauver cette partie
Erreur sur le disque / Impossible de sauver l'�tat des parties
Erreur sur le disque / Disque plein     disk full
```

The **"protected slot"** concept is confirmed in the retail save menu:
`MENU_InitSaveSlotSelect` (`0x437aa2`) picks the default slot, and in save mode
(argument `1`) it takes the most recent slot whose status field at
`0x5DAB98 + 4*i` is zero, while load mode (argument `0`) takes the most recent
of all slots. The resource
strings include the matching "protected slot cannot be saved here" error.

## Save files

`DATA\GAME\` exists but is **empty on disc 1** and holds three files on disc 2:
**[verified]**

| File | Size | Contents |
|---|---|---|
| `GAME.DAT` | 300 | Begins with the ASCII string `Ile d'Angkor`, rest zero-filled |
| `GAME0.DAT` | 10,364 | Entirely zero-filled |
| `GAME0.ICO` | 8,192 | Icon |

### Save format **[verified]** (2026-09-26)

Traced from `GAME_SaveIndex` (`0x40f202`) / `GAME_LoadIndex` (`0x40f3aa`) and `GAME_SaveGame` (`0x40f542`) /
`GAME_LoadGame` (`0x40f94a`), all under the install root:

```
data\game\game.dat        slot index, 340 bytes
  +0x000  char[10][22]    slot names (the level shown in the menu)
  +0x0dc  i32[10]         status: non-zero = protected (MENU_InitSaveSlotSelect skips it when saving)
  +0x104  i32[10]         recency 1..n, 0 = empty (GAME_SortSaveIndex (0x40ef1f) sorts and renumbers)
  +0x12c  i32[10]         file number n of game<n>.dat, -1 = none

data\game\game<n>.dat     one save, 11,388 bytes
  +0x0000  0x2880 bytes   world-state block 0x5e2b08..0x5e5387
  +0x2880  i32            0x49da84 (index into eight 0x510-byte records at 0x5e3008)
  +0x2884  char[32]       current level name ("Project<n>")
  +0x28a4  f32            player health (+0x38)
  +0x28a8  f32            player magic (+0x3c)
  +0x28ac  0x3b8 bytes    inventory (owner pointer, 32 names, counts, 50.0 per slot, flags)
  +0x2c64  i32[3], i32[3] hotkey slot icons (x, y)

data\game\game<n>.ico     64x64 RGB 2-byte thumbnail, 0x2000 bytes (GAME_SaveThumbnail (0x40fd54))
```

Loading reads the same fields, then **re-reads the level record from
`DREAMS.DAT` by name** (`DDAT_LoadRecord` (`0x449bf9`)) into the working copy, reattaches the
inventory to the player, restores the hotkey icons and sets the pending-load
flag with a 15-frame fade. At most ten slots, file numbers 0-9.

The shipped files predate this layout: `GAME.DAT` is 300 bytes (the names
and two of the three arrays; slot 0 "Ile d'Angkor" has recency 1) and
`GAME0.DAT` 10,364 bytes, 4 short of the world block alone.
`DATA\REPLAY.BIN` (316 bytes) is a **demo recording** (`DEMO_SaveReplay` (`0x40edaf`) format: a
frame count, then records) of 3 frames in an older 104-byte record, where the
current recorder writes 112: 11 input words (all 2, released), player state,
position. **[verified]**

## Level file naming

Scene files use a one-letter area prefix plus a two-digit index:

```
E01GROTT.DSN  E02ARAI0.DSN  E03ARAI1.DSN   E = ?  (GROTT = grotte/cave, ARAI = araignee/spider)
F20_FEU.DSN   F21_AIR.DSN   F22_EAU.DSN    F = elemental rooms (fire, air, water)
H03PAQUE.DSN  H18ANGKR.DSN                 H = ? (ANGKR = Angkor)
L01 L12 L14   M01 M05                      L, M = ?
```

The same prefixes appear on the leftover JPEGs in `DATA\TGA\TEMP\` (`E09_0000`,
`E13_0001`, `F02_0002`, `H03_0000`, `L01_0003`, `M01_0003`), which are almost
certainly reference renders of those levels.

Within a `.DSN`, object names carry the level prefix too — `E01_ME1`, `E01_MN1`,
`E02_COL1`, `E03_CH` — see [file-formats.md](file-formats.md) for the scene
header layout.
