# Game content

Derived from `DATA\LANG\FRANCAIS\DREAMS.INI`, the game's text resource file.
**[verified]** — this is a plain-text file shipped on both discs.

The companion `INIT.TXT` says only: *"Placer le fichier DREAMS.INI dans
DATA\LANG\FRANCAIS\"*. `FRANCAIS` is the **only** language directory present, in
an English-labelled release.

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

`[DIALOG]` contains a single entry: `Encore`.

Either this file is dead weight the engine never reads, or the shipped game
displays placeholder text. **[unverified]** which.

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

Items 0-14 are abilities and spells; 15-29 are quest objects. The split lines up
with the game's action/puzzle hybrid genre.

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

The **"protected slot"** concept is notable — slots can be locked against
overwriting. This is a richer save system than "autosave only" implies.

## Save files

`DATA\GAME\` exists but is **empty on disc 1** and holds three files on disc 2:
**[verified]**

| File | Size | Contents |
|---|---|---|
| `GAME.DAT` | 300 | Begins with the ASCII string `Ile d'Angkor`, rest zero-filled |
| `GAME0.DAT` | 10,364 | Entirely zero-filled |
| `GAME0.ICO` | 8,192 | Icon |

`GAME.DAT` holding the name of `Project0` and nothing else reads as a **save-slot
index** — one fixed-size record per slot, storing the level name to display in
the load menu. `GAME0.DAT` is an empty slot-0 save (10,364 bytes of state), and
`GAME0.ICO` is presumably its thumbnail. `SETUP.INI` creates `DATA\GAME` during
install, so this is where saves land. **[unverified]** interpretation.

`DATA\REPLAY.BIN` (316 bytes) is copied by even the minimum install. It is an
array of little-endian u32s beginning `3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
0x0B7E, 0, 0, ...` — plausibly attract-mode/demo replay input. **[unverified]**

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
