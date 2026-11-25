# EAU.6 — Vagues de la mer, jonction mer / sable, lacs calmes

## Demande (25/11/2026)

> Au bord de mer, il n'y a pas de vague. Il faut des vagues pour les mers
> seulement, pas dans les lacs. La liaison mer / sable doit être améliorée.
> Ne pas changer le visuel de la mer.

Décisions du propriétaire : vraie surface en relief + vagues déferlantes (Q1-B),
houle fixe et calme réglable (Q2-A), lacs avec rides légères sans ressac (Q3-B),
sable sec / mouillé calculé par le shader de terrain sans changement de format
(Q4-A), la nage suit la vague (Q5-B), suppression de l'ancienne copie des
shaders (Q6-A), son du ressac (Q7).

## Cause

La houle d'EAU.2 n'existait que dans l'**éclairage** (`water.frag`, normales) :
la mer d'une cellule est un carré de 4 sommets (10 km), impossible à soulever.
De nuit (ciel sombre, reflets sombres) et au loin (`distFade`), elle devenait
invisible. Aucune vague n'arrivait sur la plage. Côté sable, le générateur
verse le sable des côtes dans la couche « terre » du splat et force cette
couche au bord de l'eau : la plage se lisait comme de la terre brune, coupée
net par l'eau.

## Ce qui change

### Mer en relief autour du joueur (`SeaGrid`)

- `src/client/world/water/SeaGrid.{h,cpp}` : grille de 512 m × 512 m, un sommet
  tous les 4 m, recentrée sur le joueur par pas de 64 m (la grille ne « glisse »
  pas), limitée aux nappes `isOcean` rectangulaires (cellules générées et mer
  de repli). Les carreaux entièrement à plus d'1 m au-dessus de la mer (sous la
  terre) sont sautés.
- Chaque sommet porte la **profondeur d'eau** (niveau de la mer − sol, lu dans
  `CellHeightFields`) et un **fondu de bord** (0 au bord de la grille : raccord
  sans marche avec la mer plate qui l'entoure). Sol pas encore chargé : traité
  comme le large, nouvel essai toutes les ~0,5 s.
- `WaterMeshGpu::BuildDrawInfos` découpe chaque nappe de mer autour du carré de
  la grille (`SubtractSeaRect`, jusqu'à 4 rectangles) : aucune zone dessinée deux
  fois. La grille est le dernier draw, avec les paramètres de la mer.
- Sommet GPU : 28 → 36 octets (`sea[2]` = profondeur, fondu). Lacs, rivières et
  mer hors grille : `{100, 0}` → jamais soulevés.
- `WaterMeshGpu::Rebuild` : barrière avant copie (les frames précédentes ont fini
  de lire le buffer) et vidage de la queue avant toute réallocation (capacité
  +50 %), la grille étant renvoyée à chaque recentrage.

### Houle et vague de bord (`water.vert`, `SeaSwell`)

- Houle : les 4 trains d'EAU.2 (21, 13, 8, 5 m, mêmes directions et phases que
  l'éclairage), élévation maximale `render.water.sea_swell_m` (0,35 m : mer
  calme). Hauteur seulement (pas de déplacement horizontal) pour que la nage
  lise exactement la même surface.
- La houle s'éteint en eau peu profonde (de 5 m à 0,2 m de fond).
- **Jet de rive** : toutes les 9 s, une vague naît vers 2,5 m de fond, court
  vers le rivage (~3 s) et l'eau monte jusqu'à 0,30 m sur la plage avant de se
  retirer ; la hauteur varie lentement le long du rivage.
- `SeaSwell.{h,cpp}` est le **double exact** de `water.vert` (règle de parité ;
  `sea_swell_tests` relit les constantes de `water.vert` et `water.frag`).

### Écume et lisibilité (`water.frag`)

- Bande d'écume du déferlement calée sur la montée de l'eau, écume résiduelle
  qui se retire, découpées par du bruit.
- La houle de l'éclairage s'éteint aussi en eau peu profonde sur la grille.
- De nuit uniquement : léger éclat sur les crêtes (clair de lune) et écume moins
  sombre (plancher 0,16 au lieu de 0,08).
- **Couleur de la mer inchangée** : aucune constante de couleur, d'absorption,
  de diffusion ou de reflet n'a bougé ; à l'amplitude par défaut, l'éclairage de
  la houle est identique à EAU.2.

### Lacs calmes

Les lignes de ressac qui défilaient et l'écume qui dérivait ont disparu des
lacs ; restent le liseré fixe au bord et les rides légères dues au vent.
Rivières inchangées.

### Sable sec et sable mouillé (`terrain.frag`)

Seulement dans une cellule qui a la mer (`TerrainRenderer::SetCoast`, posé à
chaque frame depuis l'eau de la cellule) — jamais au bord d'un lac, ni dans
l'éditeur, ni aux Enfers :

- sable sec : la couche « terre » des plages basses (bande de 1,5 à 3 m
  au-dessus de la mer, bord irrégulier) et peu pentues prend une teinte sable
  clair, en gardant le grain de la texture ; la bande est étroite pour ne pas
  teinter les rues des villes portuaires ;
- sable mouillé : sous la limite atteinte par les vagues (0,30 m + 0,15 m de
  bande de séchage, bord irrégulier), sol plus sombre et plus lisse (reflets).

UBO du terrain : `coastParams` ajouté en fin de bloc (224 → 240 octets).

### Nage

`TerrainCollider::QueryWater` : pour la mer, la surface = niveau + houle + vague
de bord (`SeaSurfaceOffset`, même horloge que le rendu : `Engine::WaterTimeSeconds`,
posée à chaque frame par `SetWaterTime`). Lacs et rivières restent plats.
Purement client : le serveur ne simule pas la nage.

### Son du ressac

`amb_surf` (3 variantes synthétisées, `tools/audio_gen/sfx_ambience.py`) joué à
chaque arrivée d'une vague au rivage (`SeaSurfBreakIndex`), au volume de
l'ambiance `amb_coast` (mer à moins de ~30 m).

## Réglage

| Clé | Défaut | Effet |
|---|---|---|
| `render.water.sea_swell_m` | 0,35 | élévation maximale de la houle (m, 0 à 1,5) — rendu et nage |

## Limites connues

- Les vagues en relief ne couvrent que 256 m autour du joueur ; au-delà, la mer
  reste plate et la houle n'est rendue que par l'éclairage (comme avant).
- L'horloge des vagues est propre à chaque client : deux joueurs ne voient pas
  la même vague au même instant (sans effet sur le jeu, la nage étant locale).
- Une nappe `isOcean` non rectangulaire (outil Côte de l'éditeur) n'a pas de
  grille : elle reste plate, comme avant.

## Tests

- `sea_swell_tests` : houle bornée et animée, extinction au bord, jet de rive
  limité à la zone de bord, une vague toutes les 9 s, **parité** des constantes
  avec `water.vert` / `water.frag`.
- `sea_grid_tests` : rectangles de mer, découpe sans recouvrement, grille en
  pleine mer (sommets, fondu, sens des triangles), grille limitée à la mer,
  carreaux secs sautés, sol inconnu.
- `water_mesh_gpu_tests` : sommet à 9 floats, nappe découpée + grille en dernier.
- `tools/audio_gen/tests` : sons `amb_surf_*` (durée, niveau, catalogue).
- Contrôle manuel : bord de mer à Lhynn de jour et de nuit ; un lac et une
  rivière sans vague ; nage au bord de l'eau.
