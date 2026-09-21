# Level map — projects to scenes

The complete mapping from the game's 150 **projects** to the 98 `.DSN` **scene**
files. **[verified]** — recovered by parsing the 150 project records in
`DREAMS.DAT` and joining them to the 150 `[PROJECT]` entries in `DREAMS.INI`.

A "project" is the engine's unit of progression (the thing the autosave records);
a scene is the geometry it loads. They are **not** one-to-one — scenes are reused
heavily, which is why 150 projects fit into 98 files.

## How the join works

`DREAMS.DAT` is self-indexing (see [file-formats.md](file-formats.md)):

```
0x000   u32[151]    offsets relative to 0x400
0x25C   420 x 00    padding
0x400   150 records "ProjectN\0" + variable body
```

Each record body names the assets that project needs. The first `.DSN` reference
in record *i* is the scene for project *i*. Joining on the index gives the table
below.

**[verified]** The 150 `.DSN` lines across `LISTL0.TXT`–`LISTL4.TXT` are exactly
the multiset of those first references — the manifests are the same data in load
order rather than project order. Counts are `LISTL0`=1, `LISTL1`=44, `LISTL2`=28,
`LISTL3`=43, `LISTL4`=34, over 799 total asset-path lines.

`DREAMS.DAT` carries one extra, secondary reference: `E01GROTT.DSN` in P136.

## The connectivity graph — how one level reaches another

**[verified]** A project record carries `LINK<n>` entries whose value is a
literal `"Project<n>"`. That is the game's level graph, and it is explicit:

- **239 link edges over 144 of the 150 projects.**
- **145 of 150 projects are reachable from Project 0** by following them, which
  is independent evidence that **P0 is where the game starts**.
- Edges run both ways: P134 and P62 both link back to P0.

Project 0, *Ile d'Angkor*, has exactly two exits:

| | destination | scene |
|---|---|---|
| `LINK0` | P134 *Grotte au Pics* | `F08_GPIC.DSN` |
| `LINK1` | P62 *Ile du Hamam* | `E13_ANGK.DSN` |

`OBJET<n>` entries place the scene and its actors, each with a position, and
`BOX<n>` gives 420 coordinate volumes across the game — the obvious candidate
for trigger volumes, **[unverified]** until the record encoding is settled.

### The flying landmarks are scale models of their destination

**[verified]** Project 0 places `F84.DAN` 15,000 units above the map — up is
negative Y and the skybox reaches -24,286 — and `CH0.DAN` 461 units above
that, inside the island's 4,542 x 4,406 footprint. A creature standing on a
floating island, which is what the game shows.

That island **is** its destination, modelled small. Strip the sky objects and
compare extents:

| | X | Y | Z |
|---|---:|---:|---:|
| `F84.DAN` | 4,542 | 1,856 | 4,406 |
| `E13_ANGK.DSN` (P62) | 5,370 | 2,252 | 5,204 |
| ratio | **1.18** | **1.21** | **1.18** |
| `F08_GPIC.DSN` (P134) | 6,696 | 7,544 | 6,471 |
| ratio | 1.47 | **4.07** | 1.47 |

One uniform scale on all three axes for `E13_ANGK`; a factor of four out
vertically for `F08_GPIC`. Rendered, both `F84` and `E13_ANGK` are a shallow
rocky bowl with a raised rim and pale stone inside. **So flying to the island
loads P62, *Ile du Hamam*** — and by elimination the other exit, into a
*grotte*, is the cave the statue's mouth opens onto.

`F89.DAN` in P62 is the same kind of object: 11,043 units across, parts named
`F37FACE`, `F37TETE`, `F37TOP`.

**Do not read the destination off the model's internal names.** `F84`'s parts
are `F19EA01`-`03` and `F89`'s are `F37*`, but **`F19` and `F37` are gaps in
the scene numbering** — the files run F18, F20 ... F36, F38. They are leftovers
from cut or renamed scenes, not pointers.

## Filename grammar

**[verified]** 97 of 98 scene names match `[A-Z][0-9]{2}[A-Z0-9_]{0,5}.DSN`. The
lone exception is `END.DSN`.

Prefixes observed: `E` (27), `F` (28), `H` (10), `L` (19), `M` (11), `O` (1),
`Y` (1), plus `END`.

**[verified]** The two digits are an internal scene ID, **not** the project
number — `H18ANGKR.DSN` is Project 0 while `E01GROTT.DSN` is Project 26. And the
prefix letter does not identify one stable world: `E29USINE.DSN` serves four
different factory projects, `H15ARENE.DSN` serves six arena projects.

**[unverified]** Suffixes are truncated French mnemonics: `GROTT`≈grotte (cave),
`ARAI`≈araignée (spider), `GAUD`≈Gaudí, `USINE` (factory), `CINE` (cinema),
`ARENE` (arena), `FEU` (fire), `AIR`, `EAU` (water), `CASC`≈cascade (waterfall),
`TORT`≈torture, `SABA`, `REQI`≈requin (shark).

## The map

Project titles are `DREAMS.INI`'s own French labels, reproduced verbatim
including their truncation and typos.

| Scene | Project(s) |
|---|---|
| `E01GROTT.DSN` | P26 Grotte du chaman; P136 (secondary ref) |
| `E02ARAI0.DSN` | P7 Araignee sous-sol |
| `E03ARAI1.DSN` | P8 Araignee 1er etage |
| `E04ARAI2.DSN` | P9 Araignee 2eme etage; P115 Doublon Seconde A |
| `E05GAUD2.DSN` | P24 Gaudi Seconde partie |
| `E05GAUDI.DSN` | P112 Piece Gaudi; P118 Cauchemard du Serval |
| `E08FRISS.DSN` | P27 Grotte Frisson |
| `E08_END.DSN` | P75 Puit Connaissance |
| `E09COL.DSN` | P2 Montagne du Bien Mal |
| `E10_PIEC.DSN` | P48 Piece d'Eau |
| `E11_ANGK.DSN` | P39 Interieur Hamam; P70 Hamam 2eme partie |
| `E12_ANGK.DSN` | P30 Piscine Hamam; P123 Piscine Hamam 2 |
| `E13_ANGK.DSN` | P62 Ile du Hamam; P74 Cauchemard Ile Hamam; P77 Ile du Hamam Baleine; P137 Ile du Hamam 2 |
| `E14_CERV.DSN` | P25 Ile des deux rochers |
| `E15_RIDE.DSN` | P45 Exterieur Armee 2 |
| `E16_RIDE.DSN` | P54 Exterieur Armee |
| `E17_TR.DSN` | P40 Doublon Montagne du |
| `E18.DSN` | P57 Interieur Armee 2 |
| `E19_GARD.DSN` | P94 Piece du gardien |
| `E20_RIDE.DSN` | P76 Ride sous eau |
| `E21_RIDE.DSN` | P58 Ride Surf |
| `E23_RIDE.DSN` | P68 Ride Surf 2 |
| `E25_RIDE.DSN` | P84 Ride Canyon Surf |
| `E29USINE.DSN` | P14 Usine Regeneration; P71 Usine Minotaure; P140 Usine; P141 Usine Fin |
| `E30_CERV.DSN` | P92 Cauchemard Gargouill |
| `E98ARAI1.DSN` | P72 Seconde Araignee 1 |
| `E99ARAI2.DSN` | P73 Seconde Araignee 2 |
| `END.DSN` | P95 (INI label reads `H18ANGKR.DSN`) |
| `F01_BAT.DSN` | P4 Batiment Sous Sol 1; P81 Batiment Sous Sol 2; P82 Batiment Sous Sol 3 |
| `F02_CASC.DSN` | P17 Cascade; P60 Cascade Scribe; P136 (primary ref) |
| `F03_CINE.DSN` | P13 Cinema; P51 Cinema 2eme partie; P131 Cinema 2eme Partie |
| `F04_ARB2.DSN` | P117 Cauchemard du Lutin |
| `F04_ARBR.DSN` | P22 Scene de l'arbre |
| `F05CAB.DSN` | P33 Cabine de projectio; P89 Cabine 2eme Partie; P132 Cabine 2 eme Partie |
| `F06_ROC.DSN` | P32 Pic des pretres |
| `F07_GROT.DSN` | P31 Grotte aux colonnes |
| `F08_GPIC.DSN` | P134 Grotte au Pics |
| `F09COUL2.DSN` | P100 Piece cachee 2 |
| `F10COUL3.DSN` | P101 Piece cachee 3 |
| `F11COUL4.DSN` | P102 Piece cachee 4 |
| `F12COUL5.DSN` | P103 Piece cachee 5 |
| `F13COUL6.DSN` | P104 Piece cachee 6 |
| `F14COUL7.DSN` | P105 Piece cachee 7 |
| `F15SOUFF.DSN` | P34 Piece Soufflerie |
| `F16FAC.DSN` | P35 Ile de la soufflerie |
| `F17ASC.DSN` | P106 Cauchemard du Lutin |
| `F18HANG.DSN` | P88 Hangard Armee; P91 Second Hangard Armee; P129, P130 Troisieme Hangard |
| `F20_FEU.DSN` | P96 Interieur Feu; P79, P119 (raw path) |
| `F21_AIR.DSN` | P97 Interieur Air; P80, P120 (raw path) |
| `F22_EAU.DSN` | P98 Interieur Eau; P121, P122, P148 (raw path) |
| `F31CIEL.DSN` | P5 Ride Baleine; P52 Ride Baleine 2 |
| `F32LAB.DSN` | P83 (raw path) |
| `F33BATMO.DSN` | P61 Arene des Requin |
| `F34COUL1.DSN` | P99 Piece cachee 1 |
| `F36ANGK.DSN` | P20 Cauchemard Angkor; P108 Ile d'Angkor 2 |
| `F38ARENE.DSN` | P113 Double Noir |
| `H03PAQUE.DSN` | P10 Ile du Chaman; P15 Cauchemard Chaman |
| `H04GLACE.DSN` | P11 Statue de Glace |
| `H05ROCHE.DSN` | P53 Exterieur Araignee |
| `H06BATMO.DSN` | P18 Exterieur Batiment 2; P145 Exterieur Batiment |
| `H10MARIN.DSN` | P16 Exterieur Requin; P142 Lutin Requin; P144 Exterieur Requin 2; P149 Lutin Requin 2 |
| `H15ARENE.DSN` | P23 Arene Naissance; P28 Arene Maturite; P43 Arene Mort; P44 (duplicate); P116 Arene Conclusion; P146 Arene Maturite 2 |
| `H18ANGKR.DSN` | P0 Ile d'Angkor |
| `H18PUIT.DSN` | P3 Puit Connaissance |
| `L01LAVE.DSN` | P1 Passage Naissance |
| `L02MORT.DSN` | P19 Passade de la mort |
| `L03_REQI.DSN` | P29 Interieur Requin; P143 Interieur Requin 2 |
| `L07_TUN2.DSN` | P46 Interieur Angkor |
| `L07_TUNE.DSN` | P59 Cauchemard Angkor |
| `L08_CANY.DSN` | P49 Ile du canyon; P87 Ile du canyon 2 |
| `L09AFRI2.DSN` | P93 Ile Projectionniste |
| `L09_AFRI.DSN` | P47 Ile Afrique; P114 Ile Afrique 2; P147 Ile du gardien 2 |
| `L10_TORT.DSN` | P63 Piece de la torture |
| `L11_SABA.DSN` | P64 Piece du tigre; P139 Piece du tigre 2 |
| `L12_TORN.DSN` | P135 Ride Baleine Tornade |
| `L13_USI2.DSN` | P65 labyrinthe Usine |
| `L14_PETI.DSN` | P110 Puit des rêves; P126, P127, P128 Interieur puit |
| `L15_EAU.DSN` | P67 Passage eau |
| `L16_BOMB.DSN` | P85 Ride Bombonne; P124 Ride Requin Bombonne |
| `L17_TUAF.DSN` | P50 Passage du cinema; P133 Passage Cinema 2 |
| `L18VACHE.DSN` | P78 Retour sur terre |
| `L19_SANG.DSN` | P109 Passage vers Pyramid |
| `M01TORN.DSN` | P12 Piece de la tornade |
| `M02POD.DSN` | P42 Pod de teleportation |
| `M03DORM.DSN` | P6 Piece de Bill; P111 Piece de Bill 2; P125 (duplicate) |
| `M04GEOLE.DSN` | P36 Piece geole |
| `M05AUTEL.DSN` | P37 Piece Autel |
| `M06GLACE.DSN` | P38 Piece de glace |
| `M07LACM.DSN` | P41 Lac Magique |
| `M08ENT.DSN` | P69 Ile des colonnes; P90 Ile des colonnes 2 |
| `M09CELUL.DSN` | P107 Cellule Armee |
| `M12STAT.DSN` | P66 Ile de la Statue; P86 Ile de la Statue 2 |
| `M13TUNEL.DSN` | P21 Interieur Armee |
| `O01EAU01.DSN` | P55 Passage sous cascade; P138 Sirene cascade |
| `Y01_RAY.DSN` | P56 Passage Maturite |

## Size outliers

**[verified]** Scene sizes run 105,155 B to 2,562,695 B.

| File | Size | `countB` | `countA` |
|---|---:|---:|---:|
| `L14_PETI.DSN` | 105,155 | 1 | 38 |
| `F31CIEL.DSN` | 302,145 | 4 | 131 |
| `E25_RIDE.DSN` | 2,562,695 | 32 | 999 |

## Duplicates

**[verified]** `H03PAQUE.DSN`, `H15ARENE.DSN` and `L09_AFRI.DSN` are byte-identical
across the two discs. On disc 2, `END.DSN` and `F01_BAT.DSN` are byte-identical to
each other — two names for one scene.
