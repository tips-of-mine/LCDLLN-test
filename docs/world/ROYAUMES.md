# Monde « Royaumes de la Lune Noire » — monde continu en cellules de 10 km

> Chantier « Royaumes » (2026-10). Le continent du lore (carte de 33 × 44
> cases) devient un **monde continu de ~330 km × 440 km** découpé en
> **cellules de 10 km × 10 km** (1 case du lore = 1 cellule = 1 zone moteur).
> Seules les cellules listées dans `generated_cells` sont générées et jouables ;
> on commence par les **5 cellules des villes de départ par race**.

## 0. Avancement (une PR par étape)

| Étape | Contenu | État |
|---|---|---|
| 1 | Convention commune (`WorldCells`), générateur à l'échelle 10 km (2049², couche macro + cellules, lieux procéduraux), binaires générés par la CI | **cette étape** — n'active rien en jeu |
| 2 | Serveur : index de positions unique, simulation par cellule occupée, zones `cell_*` chargées, départs par race, ancien contenu feyhin migré | à faire |
| 3 | Client : streaming des cellules voisines (terrain 2049², décor par chunks, bâtiments, eau), origine flottante, carte M du continent, mur invisible | à faire |
| 4 | Bascule : `world.active_zone` → monde « royaumes », CI qui génère avant l'empaquetage | à faire |

Tant que les étapes 2 et 3 ne sont pas livrées, **ne pas déployer** les
sorties du générateur (le serveur actuel refuserait des spawners en
coordonnées globales). La zone active reste `feyhin`.

## 1. Convention des cellules (`src/shared/world/WorldCells.h`)

| Élément | Règle |
|---|---|
| Taille | 10 000 m (= `engine::world::kZoneSize`) |
| Repère | **global**, en mètres : X = est, Z = sud (nord = -Z), Y = altitude |
| Origine | centre de `cell_n000_e000` (l'Oracle) |
| Cellule (n, e) | x ∈ [e·10 000 − 5 000, e·10 000 + 5 000[, z ∈ [−n·10 000 − 5 000, −n·10 000 + 5 000[ |
| Nom disque | `cell_n003_w009` (minuscules, 3 chiffres ; `s`/`w` pour les négatifs) |
| Id du lore | `N003_W009` |
| Id de zone serveur | `1 000 000 + (n+500)·1000 + (e+500)` (jamais en collision avec les zones historiques 0, 1, 2) |
| Coordonnées locales | `ToLocal` : centre de la cellule = (0,0), plage [−5000, 5000[ (celle de la `CellGrid` serveur) |

La cellule d'un point se **déduit de sa position** : client et serveur la
calculent chacun de leur côté — pas de champ réseau, pas de colonne en base.

## 2. Générer le monde

La CI compile `lcdlln_world_gen` (paquet Windows « éditeur + carte » compris)
et exécute `world_gen_tests`. En local (Windows) :

```powershell
.\lcdlln_world_gen.exe --content game/data --world royaumes
# --cells N003_W009,N011_E008 : seulement ces cellules (sinon generated_cells du master) ;
#                               les sorties globales (places, carte, départs) ne sont alors PAS réécrites
# --output <dir>              : écrire ailleurs que dans --content
# --dry-run                   : générer en mémoire, sans rien écrire
```

Durée : ~1 min pour les 5 cellules sur 2 cœurs (parallélisé, résultat
identique quel que soit le nombre de cœurs). **Déterministe** : mêmes entrées
⇒ fichiers identiques octet pour octet (testé).

Les sorties ne sont **pas versionnées** (`.gitignore`) : ~40 Mo par cellule.
Seuls `world/royaumes/world_master.json` (source) et
`buildings/templates/maison.json` (bibliothèque) sont dans git.

### Sorties par cellule `<c>` = `cell_nXXX_eYYY`

| Fichier | Contenu |
|---|---|
| `zones/<c>/terrain_height.r16h` | heightmap 2049² (≈ 4,9 m), 10 km, altitude max 1 200 m |
| `zones/<c>/terrain_splat.slap` / `terrain_grass.grms` | sol 2048² (herbe/terre/roche/neige), masque d'herbe |
| `zones/<c>/zone.json` | cadrage (`terrain.origin_x/z` = coin min GLOBAL), PNJ statiques, réglages client |
| `zones/<c>/spawners.json` | PNJ d'ambiance (routines) + faune, `zoneId` = id de la cellule, positions globales |
| `zones/<c>/respawn_points.txt` | cimetières / auberges |
| `zones/<c>/runtime_manifest.json` | `zone_id` = nom, `zone_numeric_id` = id serveur |
| `instances/zone_<c>/scatter.bin` | décor instancié **LCSC v2** (table de chunks 20 × 20 de 500 m) |
| `instances/zone_<c>/buildings.bin` | bâtiments (LCBD) |
| `instances/zone_<c>/water.bin` | mer (si la cellule en a), lacs, étangs, rivières |

### Sorties globales (`world/royaumes/`)

`places.json` (villes du lore, lieux procéduraux, régions, cellules générées —
coordonnées globales), `world_map.png` (carte du continent 2048², relief ombré,
rivières, routes, cellules générées encadrées), `start_locations.json`,
`quest_poi.json`.

## 3. Pipeline du générateur (`tools/world_gen/lib`)

**Couche macro** (une fois, tout le continent) — garantit des cellules
voisines raccord au mètre près :

1. `MacroHeight` / `MacroBiome` (`Terrain.cpp`) : fonctions **pures** de la
   position globale. Profil par code du lore (base, collines, crêtes, dunes)
   interpolé à position déformée (domain warp) ; à l'intérieur d'une case :
   pays plats / vallonnés (≈ 7 km), chaînons de collines, vallons, plancher
   souple au-dessus de la mer. Bruit de **gradient** (pas de terrasses).
2. Grille grossière 250 m du relief ; **sites des villes** (meilleur
   emplacement plat et sec de la case) ; lacs du lore et lacs manuels.
3. **Rivières** : descente sur la grille grossière remplie (priority-flood),
   méandres, profil de surface non croissant.
4. **Routes** (`Roads.cpp`) : Kruskal + 2 plus proches voisines, A* 250 m,
   simplification RDP (plus d'escalier), méandres ±110 m, lissage ; profil en
   long **global** (pente ≤ 16 %) → chaque cellule aplanit sa portion au même profil.

**Par cellule** :

5. `Places.cpp` — **lieux procéduraux** selon le biome : hameaux, fermes,
   camps de bûcherons, repaires de bandits, ruines, mines (> 700 m des bords),
   puis étangs.
6. Heightmap 2049², plateaux urbains, lits de rivière (berges jamais sous
   l'eau), lacs.
7. Routes découpées + **chemins locaux** reliant chaque lieu au réseau.
8. `Settlements.cpp` : rues, lots, auberge, forge, marché, remparts/palissades,
   champs, cimetière, accessoires de métier (bûcherons, mine, bandits).
9. `Vegetation.cpp` : treillis **global** jitterisé (aucun motif répété),
   couverture nuancée (bois dans les plaines, clairières dans les forêts).
10. `Population.cpp` : PNJ d'ambiance, voyageurs/patrouilles sur les routes,
    faune par biome et distance aux villes de départ, quêtes d'accueil.

## 4. Points de départ par race

| Race | Ville | Cellule |
|---|---|---|
| Humains | Hameau de Marn (ancien contenu feyhin, translaté) | `cell_n003_w009` |
| Elfes | Whispers Hill | `cell_n011_e008` |
| Orcs | Leviathan | `cell_n009_e011` |
| Nains | Ghrunke Dash | `cell_s007_e016` |
| Démons | Faille de Tsaroth | `cell_s009_e011` |

Chaîne d'accueil (elfes / orcs / nains / démons) : `<race>_accueil` →
`<race>_chasse` → `<race>_epreuve` (`quests/quest_definitions.json`).

## 5. Contenu historique (ex-zone feyhin)

Le hameau de Marn reprend l'ancienne zone de test : `imports` du master
(zone.json, scenery, buildings, respawns, **spawners**, **POI de quêtes**
`quests/quest_poi_feyhin.json`) en coordonnées **locales**, translatées sur
le centre du hameau dans sa cellule. `quests/quest_poi.json` (racine) reste
celui de feyhin tant que le client n'est pas passé aux cellules.

## 6. Monde vivant — PNJ d'ambiance

Routines `tour` / `patrol` / `post` / `wander`, horaires `day` / `night` /
`always` (retour à la maison hors horaire), arrêt près d'un joueur, non
attaquables. Module pur : `src/shardd/gameplay/ambient/AmbientRoutine`.
Côté client : plaque « Nom - Rôle », E pour parler / commercer, jamais ciblé au Tab.

## 7. Format `scatter.bin` LCSC v2

v1 + table de chunks en fin de fichier : `originX f32, originZ f32,
chunkSizeM f32, cols u16, rows u16`, puis `cols × rows × (first u32, count u32)`.
Les instances d'un chunk sont contiguës. Écrit en v1 si aucune table (rétro-compatible).

## 8. Limites connues / suite

- Horloge : `/settime`, `/pausetime`, `/settimescale` ne sont pas propagées
  au shard (les PNJ gardent l'horaire « config »).
- Avatars animés : au plus `client.remote_avatars.max_skinned_draws` (32,
  plafond 48) personnages distants dessinés, dans le champ de la caméra.
- Elfes et démons empruntent la silhouette humaine.
- L'île d'Hishtarland n'a pas de route (avertissement attendu).
- Les cellules non générées seront bordées d'un mur invisible (étape 3).
