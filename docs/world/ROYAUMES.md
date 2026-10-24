# Monde « Royaumes de la Lune Noire » — monde continu en cellules de 10 km

> Chantier « Royaumes » (2026-10). Le continent du lore (carte de 33 × 44
> cases) devient un **monde continu de ~330 km × 440 km** découpé en
> **cellules de 10 km × 10 km** (1 case du lore = 1 cellule = 1 zone moteur).
> Seules les cellules listées dans `generated_cells` sont générées et jouables ;
> on commence par les **5 cellules des villes de départ par race**.

## 0. Avancement (une PR par étape)

| Étape | Contenu | État |
|---|---|---|
| 1 | Convention commune (`WorldCells`), générateur à l'échelle 10 km (2049², couche macro + cellules, lieux procéduraux), binaires générés par la CI | fait (PR #1017) |
| 2 | Serveur : une carte / une grille, régions actives par cellule, zones `cell_*` chargées, mur invisible serveur, départs par race (master + shard), anciens personnages feyhin replacés | fait (PR #1018) |
| 3 | Client : streaming des cellules (terrain 2049², décor, bâtiments, collisions), repère de rendu centré sur la cellule du joueur, eau, PNJ, carte M et POI en global, mur invisible | fait (PR #1019) |
| 4 | Bascule : `world.active_zone` = `royaumes` (client `config.json`, master, shard), génération du monde par la CI avant l'empaquetage (client Windows, bundle Docker) | **cette étape** |

Depuis l'étape 4, le monde actif est `royaumes` partout. Client, master et
shard doivent être déployés **ensemble**, avec le `game/data` généré par la CI du
même commit (le shard et le master refusent de démarrer sans
`world/royaumes/start_locations.json`). Retour arrière : remettre `feyhin` dans
les trois configs (les cellules générées sont alors ignorées).

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

## 2. Serveur : une seule carte, des régions de 10 km

Activé par `world.active_zone = "royaumes"` sur **le shard ET le master**
(même valeur des deux côtés).

| Point | Règle |
|---|---|
| Carte | Toutes les cellules partagent la carte `kCellWorldMapId` : une seule grille d'intérêt (100 m, creuse, ±210 km). Un joueur voit, combat, ramasse, parle à travers une frontière de cellule comme au milieu d'une cellule (`SameMap`). |
| Région | `zoneId` d'un joueur = id de SA cellule, recalculé à chaque mouvement (`UpdateClientRegion`) : présence (amis, web), quêtes `zone:<id>`, canal /zone, /who zone. Aucun message réseau (monde continu). |
| Activation | `RegionActivity`, recalculée 1 fois/s : une cellule est **active** si un joueur s'y trouve ou à moins de `world.region_activation_margin_m` (1000 m) de son bord. Les spawners d'une cellule inactive ne sont **pas évalués** ; leurs mobs sont retirés (hors combat), comme avant à l'échelle du spawner. |
| Chargement | Spawners, événements, récolte : uniquement `zones/cell_*` en monde en cellules ; uniquement les autres dossiers sinon (`ZoneDirBelongsToWorld`). Cimetières/auberges : `respawn_points.txt` de chaque cellule générée, le plus proche gagne. |
| Mur invisible | Un Input dans une cellule non générée est refusé et le client recalé (`ForcePosition`, raison « téléport », ≤ 1 / 500 ms). |
| Entrée en monde | Position hors cellule générée, ou fichier personnage d'une zone historique (repère local feyhin) → point de départ de la race (`characters.race_str`, sinon race par défaut) + `ForcePosition`. |
| Création (master) | Ville de départ de la race lue dans `world/royaumes/start_locations.json` ; en zone historique, repli `character_creation.default_spawn.*`. |
| Boot | Monde en cellules sans `world/royaumes/start_locations.json` (avec `generated_cells`) = **refus de démarrer** : lancer le générateur avant. |

Le canal « groupe / guilde » (encore un relais provisoire) diffuse à toute la carte.

## 3. Client : streaming et repère de rendu

Activé par le même `world.active_zone = "royaumes"` (client, shard, master).

| Point | Règle |
|---|---|
| Repère de rendu | Le moteur travaille en coordonnées de RENDU : origine = centre de la cellule du joueur (`RenderFrame`, double). Positions toujours à moins de ~15 km de (0,0) → précision du float conservée. |
| Rebase | Quand le joueur est entré de ≥ 50 m dans une autre cellule générée, l'origine saute au centre de la nouvelle (multiple exact de 10 km) : joueur, caméra, entités distantes, PNJ et leurs props (matrice décalée), marqueurs, eau, POI de quêtes décalés ; terrains et décor des cellules suivent par leur décalage (rien à recharger) ; historique TAA invalidé. |
| Réseau | Le shard et le master restent en GLOBAL. Conversion aux frontières : snapshots (`UIModelBinding::SetWorldOrigin`), Input, sauvegarde de position, EnterWorld, ForcePosition (qui recharge la zone d'arrivée si besoin). |
| Hauteurs CPU | Heightmap 2049² de chaque cellule à moins de `world.cells.height_radius_m` (6 km) : collision, pose au sol des PNJ / entités / décor (`CellHeightFields`). |
| Tuiles de rendu | À moins de `world.cells.render_radius_m` (2,5 km) du bord : terrain GPU (un `TerrainRenderer` par cellule, le premier efface le GBuffer), décor instancié et bâtiments en coordonnées LOCALES à la cellule, collisions de la cellule (`CellWorldCollider`). Déchargées au-delà de `world.cells.unload_radius_m` (3,5 km), destruction GPU différée (attente du GPU). Une cellule dont le zone.json, la heightmap ou le terrain manque n'est plus retentée de la session. Patchs de terrain dessinés jusqu'à `world.cells.terrain_draw_distance_m` (2,5 km). |
| Joints | Normal map calculée avec l'échantillon de la cellule voisine au-delà de chaque bord (`HeightmapBorders`) ; pavage des textures du sol en coordonnées globales (`TerrainRenderer::SetTextureWorldOrigin`) : pas de couture d'éclairage, pas de saut au rebase. |
| Chargement en tâche de fond | `CellAssetLoader` (un thread) lit et décode tout ce qui est disque et CPU : zone.json, heightmap, normal map (Sobel), splat, masque d'herbe, `scatter.bin`, `buildings.bin`, bibliothèque de bâtiments. Le main thread n'intègre qu'**une tuile par frame** (terrain : envois GPU, ~100 ms sous lavapipe ; décor et bâtiments ensuite par tranches, `ContinueCellPlacement`) ; 2 demandes en vol au plus, la plus proche d'abord ; un résultat devenu inutile est jeté. Boot et téléportation restent synchrones (`EnterCellWorldAt`). |
| Contenu léger | PNJ / coffres (`world.interactables` du zone.json) et marqueurs de respawn chargés une fois par cellule ; eau (`water.bin`) de toutes les cellules générées dès le boot (la passe d'eau est choisie au premier FrameGraph). |
| Mur invisible | Le personnage ne peut pas entrer dans une cellule non générée (le shard refuse aussi). |
| Carte M, bannière | `world/royaumes/places.json` (global) ; la position du joueur est convertie. Cellules générées : liste `cells` de places.json. |
| POI de quêtes | `world/royaumes/quest_poi.json` (global), ramenés au repère de rendu. |
| Éditeurs | `--world-editor` / `--editor` n'entrent jamais en mode cellules : avec `active_zone = royaumes`, ils ouvrent la zone historique `editor.zone` (défaut `feyhin`). Les cellules se régénèrent avec `tools/world_gen`. |
| Boot | Avant l'EnterWorld, la cellule de `world.cells.boot_x` / `boot_z` (défaut : centre de la première cellule générée) est chargée — fond de l'écran de connexion. |

Corrections du terrain faites à cette étape (profitent aussi aux zones historiques) :
pas de la heightmap = taille / (N − 1) (au lieu de / 1024), patchs de LOD ≥ 1 qui
étaient étirés 2^N fois (relief faux et décor « enterré » à quelques dizaines de
mètres), lecture de la heightmap et de sa normal map au centre des texels, seuils
de LOD proportionnels au pas de la heightmap.

**Test sans serveur (Linux, Xvfb + lavapipe)** : `LCDLLN_LINUX_VISIBLE_WINDOW=1`
ouvre une vraie fenêtre ; `LCDLLN_DEBUG_AUTOWALK="dx,dz"` fait marcher le
personnage (et lève l'écran de connexion absent sous Linux) — utile pour vérifier
le streaming et le rebase par captures d'écran.

## 4. Générer le monde

La CI compile `lcdlln_world_gen` (paquet Windows « éditeur + carte » compris)
et exécute `world_gen_tests`. Elle **génère aussi le monde avant l'empaquetage** :
job « Client (jeu) » (`Invoke-WorldGen` de `.github/scripts/package-windows.ps1`,
avant `Copy-GameData`) et build Linux (étape « Générer le monde », avant le bundle
Docker du master et du shard). Elle échoue si `start_locations.json` ou ses
`generated_cells` manquent. Les cellules générées sont celles de
`world_master.json` : les 5 villes de départ et leurs 8 voisines (45 cellules),
plus — `connect_start_cities: true` — les cellules traversées par les routes qui
relient les villes de départ (arbre couvrant de plus courts chemins sur le réseau
routier, + la cellule voisine quand la route passe à moins de `corridor_margin_m`
= 300 m de la frontière) : **116 cellules**, on va à pied d'un royaume à l'autre en
suivant la route. Le bundle Docker ne garde des cellules que les `.json` / `.txt`
lus par le master et le shard (exclus dès la copie) ; les
données de rendu restent dans le paquet client. En local (Windows) :

```powershell
.\lcdlln_world_gen.exe --content game/data --world royaumes
# --cells N003_W009,N011_E008 : seulement ces cellules (sinon generated_cells du master) ;
#                               les sorties globales (places, carte, départs) ne sont alors PAS réécrites
# --output <dir>              : écrire ailleurs que dans --content
# --dry-run                   : générer en mémoire, sans rien écrire
```

Durée : ~23 min pour les 116 cellules sur 2 cœurs, ~1 min pour 5 (parallélisé, résultat
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
rivières, routes, cellules générées encadrées), `start_locations.json`
(départs par race + `generated_cells`, lus par le master et le shard),
`quest_poi.json`.

## 5. Pipeline du générateur (`tools/world_gen/lib`)

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

## 6. Points de départ par race

| Race | Ville | Cellule |
|---|---|---|
| Humains | Hameau de Marn (ancien contenu feyhin, translaté) | `cell_n003_w009` |
| Elfes | Whispers Hill | `cell_n011_e008` |
| Orcs | Leviathan | `cell_n009_e011` |
| Nains | Ghrunke Dash | `cell_s007_e016` |
| Démons | Faille de Tsaroth | `cell_s009_e011` |

Chaîne d'accueil (elfes / orcs / nains / démons) : `<race>_accueil` →
`<race>_chasse` → `<race>_epreuve` (`quests/quest_definitions.json`).

## 7. Contenu historique (ex-zone feyhin)

Le hameau de Marn reprend l'ancienne zone de test : `imports` du master
(zone.json, scenery, buildings, respawns, **spawners**, **POI de quêtes**
`quests/quest_poi_feyhin.json`) en coordonnées **locales**, translatées sur
le centre du hameau dans sa cellule. `quests/quest_poi.json` (racine) reste
celui de feyhin (zone historique) ; dans le monde en cellules, le client lit
`world/royaumes/quest_poi.json` (global).

## 8. Monde vivant — PNJ d'ambiance

Routines `tour` / `patrol` / `post` / `wander`, horaires `day` / `night` /
`always` (retour à la maison hors horaire), arrêt près d'un joueur, non
attaquables. Module pur : `src/shardd/gameplay/ambient/AmbientRoutine`.
Côté client : plaque « Nom - Rôle », E pour parler / commercer, jamais ciblé au Tab.

## 9. Format `scatter.bin` LCSC v2

v1 + table de chunks en fin de fichier : `originX f32, originZ f32,
chunkSizeM f32, cols u16, rows u16`, puis `cols × rows × (first u32, count u32)`.
Les instances d'un chunk sont contiguës. Écrit en v1 si aucune table (rétro-compatible).

## 9 bis. Carte à découvrir

- La carte du monde (touche M) est voilée : seules les cases explorées sont
  visibles, villes et lieux compris. Case = 125 m (80 × 80 par cellule) ; une
  case est découverte quand le joueur passe à moins de 300 m de son centre
  (`src/shared/world/MapDiscovery.h`).
- Persistance PAR PERSONNAGE côté master : table `character_map_discovery`
  (migration 0076), un masque de 800 octets par cellule visitée, fusion OU.
- Protocole : 208 (page de masques, client → master), 209 (réponse paginée),
  210 (cases nouvellement révélées, envoyées toutes les 10 s et à la sortie).
  Le personnage est celui de la connexion (annoncé à l'EnterWorld).
- Sans base (no-DB) ou sans master : découverte locale à la session seulement.

## 10. Limites connues / suite

- Horloge : `/settime`, `/pausetime`, `/settimescale` ne sont pas propagées
  au shard (les PNJ gardent l'horaire « config »).
- Avatars animés : au plus `client.remote_avatars.max_skinned_draws` (32,
  plafond 48) personnages distants dessinés, dans le champ de la caméra.
- Elfes et démons empruntent la silhouette humaine.
- L'île d'Hishtarland n'a pas de route (avertissement attendu).
- Les cellules non générées sont bordées d'un mur invisible (serveur et client).
- Intégration d'une tuile au main thread : terrain (envois GPU, ~100 ms sous
  lavapipe, bien moins sur un vrai GPU) en une frame, puis décor et bâtiments
  posés par tranches (20 000 instances / 150 bâtiments par frame). Un modèle de
  décor jamais vu attend le GPU une fois (création de ses matériaux). Cache de
  pipelines partagé entre les terrains des cellules.
- Mer : chaque cellule côtière porte une nappe « ocean » (10 km, niveau
  `sea_level_m`) dessinée comme les lacs ; la section globale `OceanSectionData`
  (vent, turbidité) n'est pas encore exploitée. Les cellules côtières sont
  celles des liaisons qui longent la mer.
- Nage : poussée d'Archimède (`waterBuoyancyAccel`) — sans « plonger », le
  nageur remonte et reste en surface ; la caméra reste au-dessus de l'eau en
  nage de surface (`client.camera.water_clamp_margin`, `water_dive_depth_m`).
  Pas encore d'effet « sous l'eau » quand on plonge.
- Joints de cellules : relief et éclairage raccordés (la normal map lit la ligne
  voisine de la cellule d'à côté, pavage des sols en coordonnées globales) ; le
  mélange des sols (splat par cellule) peut encore marquer très légèrement la frontière.
- `scatter.bin` v2 (chunks de 500 m) : la table de chunks n'est pas encore
  exploitée, le client charge la cellule entière.
- Les anciens personnages de feyhin reprennent à la ville de départ de leur race
  (leur position locale n'a pas d'équivalent exact dans le repère global).
- Paquet client : ~40 Mo de données par cellule générée (~4,6 Go pour les 116).
- Liaisons : un couloir d'une cellule (deux près des frontières) le long de la
  route ; s'éloigner de la route mène vite au mur invisible. Une ville de départ
  sans route (île) reste isolée (avertissement du générateur).
- Ajouter des cellules jouables : les ajouter à `generated_cells` de
  `world_master.json` ; la CI les génère au prochain build.
