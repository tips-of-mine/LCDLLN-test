# Monde « Royaumes de la Lune Noire » — monde continu en cellules de 10 km

> Chantier « Royaumes » (2026-10). Le continent du lore (carte de 42 × 46
> cases N023→S018 × W019→E026 depuis le lore v1.0.52, 42 × 50 avec les marges de
> mer du master) devient un **monde continu de ~420 km × 460 km** découpé en
> **cellules de 10 km × 10 km** (1 case du lore = 1 cellule = 1 zone moteur).
> Seules les cellules listées dans `generated_cells` sont générées et jouables ;
> on a commencé par les **5 cellules des villes de départ par race** ; depuis la v1.0.61
> les départs sont **par faction** (section 6).
>
> Les placements tirés des albums (villes, lieux, POI, états) et les corrections du
> lore qui les accompagnent sont tracés dans [`LORE_BD.md`](LORE_BD.md).
>
> **Voir aussi** : index [`README.md`](README.md) ; contenu du monde au v1.0.108 (régions,
> niveaux, peuples, cases jouables, routes, secrets) : [§11](#11-contenu-du-monde-après-la-branche-carte-v1072--v10108) ;
> éditeur : [`EDITEUR_LORE.md`](EDITEUR_LORE.md) ; scripts et CI : [`OUTILS_LORE.md`](OUTILS_LORE.md) ;
> seconde carte : [`ENFERS.md`](ENFERS.md) ; synthèse de la branche :
> [`CHANGELOG_CARTE_v1.0.72-v1.0.108.md`](CHANGELOG_CARTE_v1.0.72-v1.0.108.md).

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
| Carte | Toutes les cellules partagent la carte `kCellWorldMapId` : une seule grille d'intérêt (100 m, creuse, ±270 km depuis le lore v1.0.52). Un joueur voit, combat, ramasse, parle à travers une frontière de cellule comme au milieu d'une cellule (`SameMap`). |
| Région | `zoneId` d'un joueur = id de SA cellule, recalculé à chaque mouvement (`UpdateClientRegion`) : présence (amis, web), quêtes `zone:<id>`, canal /zone, /who zone. Aucun message réseau (monde continu). |
| Activation | `RegionActivity`, recalculée 1 fois/s : une cellule est **active** si un joueur s'y trouve ou à moins de `world.region_activation_margin_m` (1000 m) de son bord. Les spawners d'une cellule inactive ne sont **pas évalués** ; leurs mobs sont retirés (hors combat), comme avant à l'échelle du spawner. |
| Chargement | Spawners, événements, récolte : uniquement `zones/cell_*` en monde en cellules ; uniquement les autres dossiers sinon (`ZoneDirBelongsToWorld`). Cimetières/auberges : `respawn_points.txt` de chaque cellule générée, le plus proche gagne. |
| Mur invisible | Un Input dans une cellule non générée est refusé et le client recalé (`ForcePosition`, raison « téléport », ≤ 1 / 500 ms). |
| Entrée en monde | Position hors cellule générée, ou fichier personnage d'une zone historique (repère local feyhin) → point de départ de la race (`characters.race_str`, sinon race par défaut) + `ForcePosition`. |
| Création (master) | Ville de départ de la race lue dans `world/royaumes/start_locations.json`, fusionné avec `world/enfers/start_locations.json` (démons aux Enfers) ; en zone historique, repli `character_creation.default_spawn.*`. |
| Seconde carte | Les Enfers (`docs/world/ENFERS.md`) : carte `kEnfersMapId`, cellules `enfers_*` (zone ids 2 000 000+), grille d'intérêt et régions propres ; portails `world/<monde>/portals.json` ; passage = `TransferToMap` (ZoneChange puis ForcePosition). |
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
`world_master.json` : les anciennes villes de départ et leurs 8 voisines, puis
(v1.0.61) les 7 hameaux de départ par faction et leurs voisines hors mer (104 cellules listées),
plus — `connect_start_cities: true` — les cellules traversées par les routes qui
relient les villes de départ (arbre couvrant de plus courts chemins sur le réseau
routier calculé par le générateur — pas sur les routes du lore, v1.0.105 —, + la cellule voisine quand la route passe à moins de `corridor_margin_m`
= 300 m de la frontière) : **180 cellules** depuis la v1.0.61 (117 avant), on va à pied d'un royaume à l'autre en
suivant la route. **Depuis la v1.0.119 : 428 cellules listées dans `generated_cells`, 497 cellules
jouables avec les corridors** (v1.0.106 : 412 et 481 ; +16 cases des routes Farvhenn – Vestmhor et Shaddrek –
Klehrwath, LORE_BD §16.46 ; détail : §11.7). Le bundle Docker ne garde des cellules que les `.json` / `.txt`
lus par le master et le shard (exclus dès la copie) ; les
données de rendu restent dans le paquet client. En local (Windows) :

```powershell
.\lcdlln_world_gen.exe --content game/data --world royaumes
# --cells N003_W009,N011_E008 : seulement ces cellules (sinon generated_cells du master) ;
#                               les sorties globales (places, carte, départs) ne sont alors PAS réécrites
# --output <dir>              : écrire ailleurs que dans --content
# --dry-run                   : générer en mémoire, sans rien écrire
# --jobs N                    : cellules générées en même temps (défaut : nombre de cœurs, 4 au plus)
```

Durée (v1.0.112, 2 cœurs) : ≈ 12 s pour la couche macro, puis ≈ 5,3 s par cellule
(≈ 9,4 s avant l'accélération) ; résultat identique quel que soit le nombre de cœurs ou
de `--jobs`. **Déterministe** : mêmes entrées ⇒ fichiers identiques octet pour octet
(testé). En fin de génération, `[world_gen] temps par etape` donne le temps cumulé de
chaque étape (repère pour de futures optimisations).

**Accélération sans changer une donnée** (v1.0.112, demande du propriétaire : « un
résultat de qualité ») — fichiers produits identiques au bit près, vérifié sur des
cellules des Royaumes et sur les 9 cellules des Enfers, et par `world_gen_tests`
(`TestAccelerationV1112`) :

| Changement | Gain mesuré | Pourquoi le résultat ne change pas |
|---|---|---|
| Cache par fil des mailles du bruit de gradient (`Random.h`, `GradNoise2`) | ≈ 1,5× sur tout le générateur | mêmes indices de gradient (entiers), mêmes calculs flottants |
| Grille des biomes du splat (`Output.cpp`, `SplatBiomeGrid`) | ≈ 1,7× sur l'écriture des textures du sol | biome d'un nœud voisin réutilisé seulement si la case du lore est certaine (majorant rigoureux de la pente de la déformation, `kMacroBiomeWarpLipschitz`), sinon calcul direct |
| Plusieurs cellules en parallèle (`--jobs`) | sur les machines de la CI à 4 cœurs : recouvre les étapes non parallèles (routes, forêt, végétation, écriture) | chaque cellule ne lit que la couche macro ; bilans fusionnés dans l'ordre des cellules |

Écarté : calculer les biomes sur une grille approchée (≈ 3× plus rapide) — les textures
du sol auraient changé de quelques mètres aux frontières de biome.

Les sorties ne sont **pas versionnées** (`.gitignore`) : ~40 Mo par cellule.
Seuls `world/royaumes/world_master.json` (source) et
`buildings/templates/maison.json` (bibliothèque) sont dans git.

### Sorties par cellule `<c>` = `cell_nXXX_eYYY`

| Fichier | Contenu |
|---|---|
| `zones/<c>/terrain_height.r16h` | heightmap 2049² (≈ 4,9 m), 10 km, altitude max 1 200 m |
| `zones/<c>/terrain_splat.slap` / `terrain_grass.grms` | sol 2048² (herbe/terre/roche/neige), masque d'herbe |
| `zones/<c>/terrain_forest.frms` | densité forestière 1024² (FORET.1) : arbres et sous-bois des forêts posés par le client |
| `zones/<c>/zone.json` | cadrage (`terrain.origin_x/z` = coin min GLOBAL), PNJ statiques, réglages client |
| `zones/<c>/spawners.json` | PNJ d'ambiance (routines, agendas de métier — cf. `docs/architecture/etape4_simulation.md` §4.4) + faune, `zoneId` = id de la cellule, positions globales |
| `zones/<c>/respawn_points.txt` | cimetières / auberges |
| `zones/<c>/runtime_manifest.json` | `zone_id` = nom, `zone_numeric_id` = id serveur |
| `instances/zone_<c>/scatter.bin` | décor instancié **LCSC v2** (table de chunks 20 × 20 de 500 m) |
| `instances/zone_<c>/buildings.bin` | bâtiments (LCBD) |
| `instances/zone_<c>/water.bin` | mer (si la cellule en a), lacs, étangs, rivières |

### Sorties globales (`world/royaumes/`)

`places.json` (villes du lore, lieux procéduraux, régions, cellules générées —
coordonnées globales), `world_map.png` (carte du continent 2048², relief ombré,
rivières, routes, cellules générées encadrées), `map_features.bin` (FIX.12 :
routes, chemins, rues et emprises des bâtiments en vectoriel pour la carte zoomée,
format `src/shared/world/MapFeatures.h`), `start_locations.json`
(départs par race + `generated_cells`, lus par le master et le shard),
`quest_poi.json`, `portals.json` (portails reliés à une autre carte, lus par le shard).

La seconde carte se génère de la même façon : `--world enfers` (9 cellules,
~2 min ; sorties `zones/enfers_*`, `world/enfers/`). La CI génère les deux.

## 5. Pipeline du générateur (`tools/world_gen/lib`)

**Couche macro** (une fois, tout le continent) — garantit des cellules
voisines raccord au mètre près :

1. `MacroHeight` / `MacroBiome` (`Terrain.cpp`) : fonctions **pures** de la
   position globale. Profil par code du lore (base, collines, crêtes, dunes)
   interpolé à position déformée (domain warp) ; à l'intérieur d'une case :
   pays plats / vallonnés (≈ 7 km), chaînons de collines, vallons, plancher
   souple au-dessus de la mer. Bruit de **gradient** (pas de terrasses).
   **Reliefs locaux** (`reliefs` du master, v1.0.72) ajoutés à `MacroHeight`,
   ancrés sur le centre de la case d'une ville : `colline` (ville perchée, ex.
   Vaerdhun : plateau sommital, flanc jusqu'au pied) et `crevasse` (cul-de-sac,
   ex. Vharholdt : massif entaillé d'une fente à fond plat ouverte vers une case
   voisine, fermé aux routes, mine au fond si `mine` est renseigné). Depuis la
   v1.0.77, `massif` est ancré sur une case (`cell`, décalage `offset_x_m` /
   `offset_z_m`) et non sur une ville : dôme lobé de rayon `foot_radius_m`
   (≤ 7 km) haut de `height_m` (≤ 600 m), découpé par un bruit en crêtes ; il ne
   s'applique qu'aux terres déjà émergées, ce qui garde la côte et pose des
   falaises au rivage ; le code de la case (forêt…) ne change pas (pics et
   crêtes boisés de l'Île de la Trêve, LORE_BD §16.6). Depuis la v1.0.107,
   `gouffre` **creuse** une fosse sous la ville `city` posée au fond (position
   manuelle admise : le gouffre suit la ville) : fond plat à une altitude absolue
   (point le plus bas du pourtour − `depth_m`), rayon `floor_radius_m` (contour
   lobé), parois abruptes sur `wall_m`, galerie de sortie `gallery` (fente dont le
   sol monte régulièrement du fond à l'entrée cachée) ; appliqué en dernier, jamais
   de relèvement, fermé aux routes, sans lieu procédural ni faune ; torches (décor)
   au fond et dans la galerie, lumières et ambiance « sombre » écrites en données
   dans le `zone.json` de la cellule (`world.point_lights`, `world.ambiance_zones`,
   pas encore lues par le client). Ex. : Gouffre des Revenus et Thurvhal, Deathtrap
   Canyon (LORE_BD §16.36).
   **Ville secrète** (`secret: true` d'une ville du master, v1.0.107) : absente de
   la carte publique (`cities` de `places.json`, carte du continent, rues et
   bâtiments de `map_features.bin` ; recopiée dans `secret_places.json`, réservé au
   serveur, v1.0.110), jamais reliée
   par les routes (ni réseau du générateur, ni ancre de case d'une route du lore),
   sans effet sur les cellules voisines ; cimetière et auberge générés comme ailleurs.
   **Lieu secret** (`secret: true` d'un POI du master, v1.0.110) : absent de
   `places.json`, recopié dans `world/<monde>/secret_places.json` (annuaire des MJ du
   shard, retiré du paquet client par la CI) ; interdit pour un portail relié et une
   extrémité de route du lore (LORE_BD §16.37).
   Depuis la v1.0.120, `falaise` pose une **côte en falaise de craie** (type falaises de
   Douvres) sur une liste de cases (`cells`, sans ville) et leurs voisines côtières
   (codes `,` et `_`, où tombe souvent le rivage réel) : grille de distance signée à la
   côte (pas de 24 m, calculée une fois, à la première demande, sur le relief sans la
   falaise) ; masque « sous la mer » lissé (filtre majoritaire de 408 m : le marais côtier
   piqueté devient une côte nette), mer reliée au large (codes ` ` et `-`). Côté mer, les
   îlots du marais sont noyés à 1,5 m sous la mer ; côté terre, plage et mares comblées au
   niveau du rivage sur `beach_m`, puis paroi de `height_m` (± 15 %) sur `wall_m`, plateau
   au-dessus ; extinction sur `fade_m` hors des cases. Paroi blanche : couche « neige » du
   splat (la seule couche claire du client) et liseré blanc sur la carte du monde
   (LORE_BD §16.47).
2. Grille grossière 250 m du relief ; **sites des villes** (meilleur
   emplacement plat et sec de la case ; centre de la case pour une ville de
   départ ou ancrée sur un relief ; **position manuelle** `position`
   `{ "x_m", "z_m" }` depuis la v1.0.95 : mètres depuis le centre de la case,
   X est, Z sud, au plus 4300 m, ville posée exactement là et plateau aplani
   autour, recopiée du lore par `tools/lore/sync_positions.py`, LORE_BD §16.24) ; lacs du lore (codes `L` forêt avec lac,
   `l` aride avec lac, `p` plaine avec lac depuis le lore v1.0.52) et lacs manuels.
3. **Rivières** : descente sur la grille grossière remplie (priority-flood),
   méandres, profil de surface non croissant. **Cours tracés à la main**
   (`river_courses` du master, lore v1.0.55) : la rivière suit les centres des
   cases listées (codes rivière ; la dernière peut être la case d'embouchure),
   contourne les autres villes, prend de grands méandres (~7 km) puis descend
   vers la mer ; elle remplace la rivière automatique de sa composante.
   `island_city` : le lit se divise en deux bras qui entourent la ville (île
   fluviale de Sysigie). Un cours qui part de la case d'une ville perchée sur une
   `colline` naît au pied de la colline (sans méandre sur le flanc). POI : au centre
   de leur case, ou décalés (`offset_x_m`, `offset_z_m`, v1.0.72) ; plusieurs POI
   d'une même case sont écartés d'un quart de tour autour de la ville de la case.
   Un POI à `position` manuelle (v1.0.95, même format que les villes) est posé
   exactement là, sans écartement ; ses `offset_*` sont alors ignorés (v1.0.106 :
   les 4 balises de route replacées au bord des routes du lore, LORE_BD §16.35).
4. **Routes** (`Roads.cpp`) — depuis la v1.0.105, **le générateur suit les routes du
   lore** (`lore_roads` du master, recopiées de la collection `routes` du lore par
   `tools/lore/sync_routes.py`, voir LORE_BD §16.34) :
   - chaque route du lore (`road`, `imperial_road`, `path` ; les routes maritimes ne
     sont pas tracées, aucun ponton) passe par chaque case de ses `waypoints` : la
     ville du lore de la case (la route y est coupée en tronçons ville à ville), sinon
     le point praticable le plus proche du centre ; un pas en diagonale entre deux
     cellules jouables passe par le coin commun si une voisine commune n'est pas
     jouable ; entre deux cases, A* 250 m sur le terrain (pente, eau, relief, gués),
     pénalisé hors des cellules jouables et des cases de la route ; un détour de plus
     de 3 × est refusé (tronçon coupé, avertissement) ;
   - embranchement (extrémité sans ville ni POI) : départ du point le plus proche d'une
     route du lore déjà tracée dans la case ;
   - largeurs : voie impériale 7 m (grande route), route 5 m (7 m entre grandes
     villes), sentier 3,2 m (`Road::path`, pas de voyageurs) ;
   - le **réseau du générateur** (Kruskal + 2 plus proches voisines, comme avant) est
     toujours calculé (`g.generatorRoads`) : il fixe les **corridors des villes de
     départ** (cellules jouables inchangées) ; une de ses routes n'est gardée en jeu
     que si elle fait partie de ces corridors et que le lore ne relie pas déjà ses deux
     villes à pied (détour ≤ 1,5 ×). Sans `lore_roads` (Enfers), seul ce réseau existe ;
   - pour tous : simplification RDP, méandres ±110 m (nuls près des cases imposées),
     lissage ; profil en long **global** (pente ≤ 16 %) → chaque cellule aplanit sa
     portion au même profil.

**Par cellule** :

5. `Places.cpp` — **lieux procéduraux** selon le biome : hameaux, fermes,
   camps de bûcherons, repaires de bandits, ruines, mines (> 700 m des bords),
   puis étangs.
6. Heightmap 2049² ; cuvettes sous la mer **isolées** comblées (sinon la
   nappe de la mer y apparaît en pleine terre, EAU.5) ; plateaux urbains ;
   lits de rivière (berges jamais sous l'eau) ; lacs et étangs creusés selon
   leur polygone, niveau jamais au-dessus du bas de la rive, bord de la nappe
   enfoui sous la rive, digue si besoin, fond jamais sous la mer (EAU.5).
7. Routes découpées + **chemins locaux** reliant chaque lieu au réseau.
8. `Settlements.cpp` : rues, lots, auberge, forge, marché, remparts/palissades,
   champs, cimetière, accessoires de métier (bûcherons, mine, bandits).
   **Cimetières** (v1.0.112, section `graveyards` du master) : toujours exactement
   **666** aux Royaumes (`target`, génération en échec sinon) ; une cellule sans
   cimetière en reçoit un **isolé**, sans ville, au bord d'une route
   (`fill_empty_cells` ; aucun dans une cellule de pleine mer) ; les hameaux de `without_graveyard` n'en ont pas (auberge
   seule) pour garder le total (LORE_BD §16.38-16.39).
   Champs clôturés hors les murs (`FieldsFor`) : 2 à 4 petits champs (16 × 10 m)
   par hameau ou village, 4 à 6 grandes parcelles (24 × 14 m à 32 × 18 m) par
   ville, grande ville, forteresse ou capitale ; aucun pour les camps, les
   ruines et les villes démoniaques.
   État d'une ville (`cities[].state` du master) : `intact`, `ruine` (ruines,
   pillards, ni auberge ni marché), `camp`, ou `reconstruction` (ville relevée
   après un sac : environ 51 % de maisons habitées, 31 % de ruines et 18 % de
   chantiers entourés d'étais et de matériaux, trois brèches avec gravats dans
   le rempart ou la palissade ; auberge, marché, forge, pavage et PNJ comme une
   ville intacte — ex. Kendrhir après le tome 1).
9. `Vegetation.cpp` : treillis **global** jitterisé (aucun motif répété),
   couverture nuancée (bois dans les plaines, clairières dans les forêts).
   **Forêts et lisières (FORET.1)** : leurs arbres ne sont plus stockés dans
   `scatter.bin`. `BuildForestMask` écrit une densité par texel de ~10 m (pleine
   forêt 1, lisière ≈ 0,38, nulle sur l'eau, les routes, le bâti, les champs, les
   pentes > 34° et la neige) et le client pose lui-même, autour du joueur, un arbre
   tous les 4 à 5 m en pleine forêt (~8 m en lisière) et le sous-bois (fougères,
   buissons), à des positions déterministes (`src/client/world/instances/ForestMask.h`,
   reconstruites tous les `world.forest.rebuild_step_m` m). Troncs solides côté
   client ; le serveur ne lit pas le décor. Rayon : `world.forest.filler_radius_m`,
   par défaut `world.scatter.cull_large_m` × facteur du préréglage + 30 m (réglages
   dans `zone_overrides` du master). Coût mesuré (Whispers Hill, forêt) : décor
   22,3 → 11,8 Mo + 1 Mo de densité.
10. `Population.cpp` : PNJ d'ambiance, voyageurs/patrouilles sur les routes,
    faune par biome et distance aux villes de départ, quêtes d'accueil.

## 6. Points de départ par faction (v1.0.61)

Décision du propriétaire : les départs sont **par faction**, plus par race. `start_for`
(nom inchangé) porte les identifiants de faction de jeu (`game/data/races/factions.json`,
`characters.faction_str`). Détail, justifications et correspondance avec le lore :
[`LORE_BD.md`](LORE_BD.md) section 14.

| Faction de jeu | Ville de départ | Cellule |
|---|---|---|
| `legions_imperiales` (réservé, absent de `factions.json`) | Haeldorf, près de Lhynn | `cell_n002_w010` |
| `lumiere` | Orhendal, près d'Altenberg | `cell_n005_w011` |
| `justice` | Ordhrann, près de Syzygie | `cell_n009_w010` |
| `dragons` | Drakhenhal, près du Palais des Maîtres des Dragons | `cell_s004_w004` |
| `dzorak` | Grakhul, près de Ghulvathan | `cell_n008_e012` |
| `naine` | Durnhalde, près de Ghrunke Dash | `cell_s007_e017` |
| `elfe` | Syllaërin, près de Northind (deux cases à l'est depuis la v1.0.72) | `cell_n007_w003` |
| `legion` | Citadelle de la Légion — **seconde carte « Les Enfers »** (`docs/world/ENFERS.md`) | `enfers_n000_e000` |

`lune_noire` (palais lunaire), Archimages (pyramide de Methraton) : départs hors carte,
portés par le lore seulement ; `serpent` : à définir ; Hors-La-Loi : aucun départ.
Une faction sans départ suit le **repli par race** de `start_locations.json` (section
`races` : première ville de départ du master de la race, humains → Haeldorf) puis
`default_race`. Format : `start_factions` (liste) + `factions` (prioritaires) + `races`.
Le master (création) et le shard (relocalisation hors monde) appellent
`StartLocationTable::ResolveFor(faction, race)`.

Le Hameau de Marn (ancien contenu feyhin, translaté), Whispers Hill, Ghulvathan et
Ghrunke Dash ne sont plus des départs ; leurs cellules restent générées.

Le camp de la Légion de la Faille de Tsaroth (`ville_tsaroth`, `S009_E011`) a été
retiré du continent avec ses PNJ (Maltheus, Ysviel, déplacés à la Citadelle) ;
la cellule `S009_E011`, ses voisines et les 7 cellules de la route qui y menait
restent générées (listées explicitement dans `generated_cells` : les corridors
sont inchangés). Les 5 Portes des enfers (`poi_porte_1..5`) sont reliées aux
5 portails des Enfers ; leurs cellules sont générées (sans leurs voisines).

Chaîne d'accueil (elfes / orcs / nains / démons, faune autour de la ville de départ selon
la race de la ville) : `<race>_accueil` →
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
- Routes (v1.0.105) : seules les routes tracées dans le lore existent en jeu (plus
  les routes du générateur des corridors de départ). Une liaison sans tracé au lore
  n'a pas de route : la tracer dans l'éditeur puis lancer `tools/lore/sync_routes.py`.
  v1.0.106 (LORE_BD §16.35) : les liaisons perdues au v1.0.105 sont tracées au lore
  (11 routes créées, dont Orkh et Marn), chaque ville et hameau jouable est relié à
  pied aux départs (sauf l'Île de la Trêve, réseau propre), aucune route du lore ne
  passe plus par une case non générée entre deux zones jouables (68 cases ouvertes,
  481 cellules jouables ; la Via Magna est jouable de Lhynn à Moork). Exception : les
  5 cases de haut-fond de la baie de l'Île de la Trêve restent sans route. Le Hameau
  de Marn, sans fiche au lore, est rattaché par la table `EXTREMITES` du script.
  Routes maritimes et pontons non tracés.
- Ville secrète (v1.0.107, Thurvhal) : hors de `places.json` `cities`, donc sans
  bannière d'entrée, sans radar ni ambiance sonore de ville ; depuis v1.0.110, les villes
  et lieux secrets (dont les 11 grottes des dragons et le Puits des Lianes) ne sont plus du
  tout dans les données du client (`secret_places.json`, serveur seulement) : mécanisme de
  découverte à faire. Torches du gouffre : mesh
  réel, mais leur lumière et l'assombrissement du fond (`world.point_lights`,
  `world.ambiance_zones` du zone.json) ne sont pas encore rendus : le client n'a
  qu'un fichier global de lumières ponctuelles (`lights/dynamic_lights.json`, 64
  au plus) et pas de réglage d'ambiance par zone. La galerie est une fente à ciel
  ouvert (la heightmap ne fait pas de tunnel).
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

## 11. Contenu du monde après la branche carte (v1.0.72 → v1.0.108)

Synthèse de l'état du monde au lore **v1.0.108** ; l'historique, les motifs et les preuves de
chaque lot sont dans [`LORE_BD.md`](LORE_BD.md) §16 (table des matières en tête de la section),
le résumé par version dans [`CHANGELOG_CARTE_v1.0.72-v1.0.108.md`](CHANGELOG_CARTE_v1.0.72-v1.0.108.md).

**Réel en jeu ou lore seulement ?** Le générateur ne lit que le master
(`game/data/world/royaumes/world_master.json`) : grille, `generated_cells`, `cities`, `pois`,
`reliefs`, `river_courses`, `lore_roads`, `regions` (boîtes). Tout le reste (niveaux, quêtes,
raids, PNJ nommés, créatures, factions, peuples) est une **donnée de conception** du lore
(`legacy/world-builder-html/lune-noire-data.json`), sans effet en jeu tant qu'un système ne la lit pas.

### 11.1 Régions du hors-série et provinces (v1.0.92 – v1.0.102)

`[DÉCISION]` v1.0.92 : les **régions de référence** sont celles de la carte p.4 du hors-série
« L'Empire de la Négation » (`type: region_hs`) ; une région peut être découpée en **provinces**
(`type: province`, `parent_region`) ; les anciennes régions du lore qui ne sont pas des régions
HS sont devenues des provinces (identifiant, nom, notes et niveaux inchangés). Case → région :
`cells[].region_id` ; case → province : `cells[].province`. Îlots corrigés en v1.0.101 – v1.0.102.

| Région HS (`region_id`) | Nom au lore (nom HS) | Cases | Niveau de réf. |
|---|---|---:|---|
| `terres_des_gigantomorphes` | Terres des Gigantomorphes | 249 | 30-56 |
| `royaume_des_nains_de_lest` | Royaume des nains de l'est (Royaume des Nains de l'Est) | 217 | 10-30 |
| `territoires_orcques` | Territoires orcques (Territoires orques) | 154 | 18-45 |
| `terres_de_lempereur` | Terres de l'empereur (Terres de l'Empereur Wismerhill Ier) | 149 | 10-30 |
| `via_magna` | Via Magna | 134 | 10-30 |
| `royaumes_des_nains_du_sud` | Royaumes des nains du sud (Royaume des Nains du Sud) | 127 | 10-30 |
| `baronnie_de_moorks` | Baronnie de Moorks (Baronnie de Moork) | 80 | 18-45 |
| `region_ile_de_la_treve` | Île de la Trêve (région propre, hors HS, §11.3) | 54 | 45-60 |
| `royaume_drach` | Royaume Drach | 51 | 30-56 |
| `foret_elfique` | Forêt elfique | 40 | 10-30 |
| `syzygie` | Syzygie | 37 | 10-30 |
| `region_hishtarland` | Royaume d'Hishtarland (Hishtarland) | 19 | 25-40 |

| Province | Région parente | Cases | Niveau de réf. |
|---|---|---:|---|
| `region_marches` Territoires des Marches | Territoires orcques | 56 | 1-15 |
| `val_de_southport` Val de Suthvarn | Royaumes des nains du sud | 42 | 18-28 |
| `foret_des_lutins` Forêt des Lutins | Via Magna | 35 | 14-24 |
| `region_premiere_province_empire` 1ère Province de l'Empire | Terres de l'empereur | 27 | 10-22 |
| `confins_du_midi` Confins du Midi | Royaumes des nains du sud | 21 | 22-34 |
| `region_marche_est` Marche de l'Est | Baronnie de Moorks | 18 | 15-28 |
| `desert_des_sels` Désert des Sels | Royaume des nains de l'est | 12 | 24-36 |
| `terres_desolees` Terres désolées de Tsaroth | Royaumes des nains du sud | 6 | 20-40 |
| `marais_maudits_t11` Marais Maudits (de Thenhart) | Terres des Gigantomorphes | 5 | 40-56 |
| `region_northind` Province de Northind | Terres de l'empereur | 4 | 12-24 |
| `region_terres_nord_t00` Terres du Nord — Château en Ruine | Terres des Gigantomorphes | 2 | 1-15 |
| `desert_central` Désert de l'Oracle | Via Magna | 0 (étendue portée par la boîte du master) | 10-25 |

Régions **sans case** sur le continent : Les Enfers (`region_enfers_t00`, 1-10, seconde carte),
La Grande Plaine (45-55, champ de bataille du T10), Empire du Jade, Empire Tharque, Terra Secunda
(contenu d'extension, sans niveau). Les **boîtes `regions` du master** (rectangles utilisés par le
générateur et les tests) n'ont pas été recalées sur ces régions : point ouvert.

### 11.2 Niveaux de référence et niveaux par faction (v1.0.100, v1.0.103)

| Donnée | Où | Règle |
|---|---|---|
| Niveau de référence d'une zone | `regions[].niveau_ref` `{min, max}` | valeurs ci-dessus (`[GD]` à valider sauf les décisions citées) ; toutes les cases terrestres ont un niveau. |
| Carré de départ d'une faction | `game_design.depart.region` | **1-10** (les 3 × 3 cases autour de la ville de départ). |
| Niveau par faction | `regions[].niveau_par_faction` `{faction: {min, max}}` | rempli pour 24 fiches × 12 factions jouables (`[GD]`, `rule_niveaux_par_faction`). |
| Niveau conseillé d'un raid | `raids[].niveau_ref` | 36 raids renseignés, 2 à `null` (Empire du Jade). |

Règle `[GD]` des niveaux par faction (v1.0.103) : (1) Île de la Trêve, même niveau pour toutes ;
(2) région ou province qui contient le départ de la faction : niveau de référence ; (3) sans
maître, maître allié, neutre ou relation inconnue : référence ; (4) maître **ennemi** :
min = max(réf. min + 5, **20**), max = min(70, max(réf. max + 5, min + 5)). Relations : blocs
de `game_design.faction_relations`.

Règle `[GD]` des niveaux de raid : boss connu → `[niveau max − 5, niveau max]` ; sinon zone du
raid selon la difficulté (f = 0,25 / 0,5 / 0,75 / 1), +5 à partir de 3 équipes ; planchers des
raids de fin d'histoire : Héroïque 45, Légendaire 50, Mythique 55, Apocalyptique 60.

**Aucun code du jeu ni du serveur ne lit ces niveaux** ; le calque « Niveaux » de l'éditeur les
affiche ([`EDITEUR_LORE.md`](EDITEUR_LORE.md) §5). Les zones nettement plus fortes que leur
voisinage sont **balisées** (notes « Balisage du danger », lieux-balises ; v1.0.85).

### 11.3 Île de la Trêve (v1.0.74, v1.0.77)

| Point | Valeur |
|---|---|
| Emprise | N016-N023 × W009-W015 : fer à cheval de forêt (20 cases `T`) autour d'une baie ouverte à l'est, îlot central ; 34 cases de haut-fond générées pour la côte (54 cases jouables) |
| Région | `region_ile_de_la_treve`, commune à **toutes les factions** (Légion comprise), **niveau 45-60** pour toutes |
| Villes `[GD]` | Brenhaval (`town_brinehaven`, N017_W010, port franc, neutre), Duzhmoroth (`town_duskmoor`, N022_W010, Lune Noire), Lhantarn (`town_lanternhold`, N021_W014, Empire), Thrival (`town_truceholm`, N019_W010, îlot) |
| Liaisons | routes du lore 000214 à 000220 ; lignes maritimes (lore seulement) Brenhaval ↔ Haevrin ↔ Ostrevan et navette Brenhaval ↔ Thrival |
| Relief | 11 `massif` du master (crêtes, falaises, vallées encaissées) ; pas de route dans la baie (5 cases de haut-fond) |
| Contenu `[GD]` | Hwakhari (peuple vivant des temples incas, 11 POI temple / sanctuaire) ; 3 raids simples (45-60) et 1 raid multi-équipe (Dévoreur des Brumes, 60) ; quêtes entre factions |
| En jeu | relief, villes, POI, routes ; temples sans modèle 3D, raids sans instance, bateaux non joués |

### 11.4 Peuples lointains (v1.0.82)

Douze peuples **hors carte** (`factions`, `type: peuple_lointain`, noms `[GD]`), placés par
climat ; chacun a une **direction** (flèche du calque « Peuples lointains ») et un **comptoir**
sur le continent. Aucun n'est jouable ni présent en jeu (lore seulement).

| Peuple | Direction | Comptoir |
|---|---|---|
| Les Nordiques (`faction_nordiques`) | nord | Comptoir du Nord, Brenhaval (N017_W010) |
| Les Vharnoctes (`faction_vharnoct`) | nord-nord-est | Comptoir du Nord, Brenhaval |
| Le Saelvhyr (`faction_saelvhyr`) | nord-ouest | Comptoir du Nord, Brenhaval |
| L'Empire Minghol (`faction_empire_minghol`) | nord-est | Comptoir de l'Est, Ryvaldh (N006_E023) |
| L'archipel d'Akhazumi (`faction_akhazumi`) | est-nord-est | Comptoir de l'Est, Ryvaldh |
| L'Empire de Tsin (`faction_empire_tsin`) | est | Comptoir de l'Est, Ryvaldh |
| Le Kharzhad (`faction_kharzhad`) | sud-est | Comptoir de l'Est, Ryvaldh |
| Les émirats du Sheydhar (`faction_sheydhar`) | sud-sud-est | Comptoir du Sud, Hishtarland (S011_W006) |
| Le royaume de Nebkhara (`faction_nebkhara`) | sud | Comptoir du Sud, Hishtarland |
| Le royaume de Nkhaloa (`faction_nkhaloa`) | sud-sud-ouest | Comptoir du Sud, Hishtarland |
| Azteca (`faction_azteca`) | ouest-sud-ouest | Comptoir de l'Ouest, Altenberg (N005_W012) |
| Les nations tahokhanes (`faction_tahokhan`) | ouest-nord-ouest | Comptoir de l'Ouest, Altenberg |

### 11.5 Ophidiens (v1.0.82)

> **Secret joueurs** : les cases des entrées ci-dessous sont une donnée de conception ; ne pas
> les publier (calque 🔒 « Entrées ophidiennes » de l'éditeur, désactivé à chaque ouverture).

`faction_ophidiens` : **peuple ennemi souterrain**, hostile à toutes les factions et aux douze
peuples (hommes-serpents télépathes, Empereur, Impératrice pondeuse, Prince Serpent, montures
sauriennes, cités souterraines, écloserie ; `regle_ophidiens`). Pas d'Ophidia Prime en surface
(elle est sur Terra Secunda). Créatures 55-62, boss de raid 66-68 (lore seulement, aucun spawner).

| Entrée cachée (`entree_cachee`) | Case | Milieu | En jeu |
|---|---|---|---|
| Puits des Lianes (`lieu_ophidiens_puits_des_lianes`) | N010_E010 | jungle de Leviathan | POI réel (cellule générée) |
| Bouche des Roseaux (`lieu_ophidiens_bouche_des_roseaux`) | N010_W003 | Marais maudits de Thenhart | lore seul (case fermée) |
| Faille des Mues (`lieu_ophidiens_faille_des_mues`), entrée principale | S011_E015 | montagnes du sud-est | lore seul (case fermée) |

### 11.6 Dragons cachés de Xygga (v1.0.81)

> **Secret joueurs** : cette sous-section donne les cases exactes des grottes. Doc de conception
> interne ; ne pas la recopier dans un document destiné aux joueurs ni la montrer en partage
> d'écran (calque 🔒 « Dragons et grottes de Xygga » de l'éditeur).

`regle_dragons_de_xygga` : les dragons vivent dans le monde des Royaumes, chacun caché dans une
grotte difficile à trouver ; Xygga dans un volcan, **10 premiers-nés** ; **pas de noms** pour
l'instant ; genre de Xygga non tranché. Niveaux `[GD]` : premiers-nés 66-70, Xygga 70 (endormi :
rencontre narrative, combat à décider). Grottes à plus de 1,5 km de toute route ; les cases
sensibles et leurs 8 voisines ne sont jamais ouvertes par les lots d'ouverture de cases.

| Dragon | Case | Grotte (`entree_cachee`) | En jeu |
|---|---|---|---|
| Xygga, Grand Dragon rouge | S008_E012 | Grotte du volcan endormi | POI réel |
| doré enflammé | N002_E004 | Grotte de la falaise de grès | lore seul |
| rouge à cornes | S005_E001 | Grotte de la vire | lore seul |
| jaune-vert épineux (tête de tricératops) | N011_W005 | Grotte des racines du marais | lore seul |
| petit rouge épineux | S007_E021 | Grotte de la crevasse de l'est | lore seul |
| blanc à cornes noires | S006_W001 | Grotte du surplomb | lore seul |
| noir aux ailes brunes | N010_E012 | Grotte des ronces | POI réel |
| hydre | N013_E017 | Grotte de la cascade du marais | lore seul |
| dragon de glace | N021_E016 | Grotte du glacier | lore seul |
| dragon bleu (distinct de celui de Pilou) | N008_W015 | Grotte marine de la côte ouest | lore seul |
| oriental doré-orangé | N006_E007 | Grotte derrière la cascade | lore seul |

« POI réel » : repère, nom et aplanissement dans une cellule générée ; aucune grotte, aucun
dragon modélisé. L'Entrée des Abysses (S008_W005, cachée, sans dragon ni Ophidien) est un lieu
ordinaire du lore.

### 11.7 Cases jouables

| Version | `generated_cells` (master) | Cellules jouables (master + corridors) |
|---|---:|---:|
| v1.0.71 (avant la branche) | — | 191 |
| v1.0.74 (Île de la Trêve) | — | 245 |
| v1.0.104 (routes de Vozmarkh et de Rhivkhar) | — | 413 |
| v1.0.106 (passages des routes du lore) | 412 | **481** |

- Une cellule est jouable si elle est dans `generated_cells` **ou** dans un corridor des villes
  de départ (`AddStartCorridors`, calculé sur le réseau du générateur, inchangé par les routes du
  lore). Ailleurs : **mur invisible** (serveur et client).
- Méthode des lots d'ouverture (v1.0.88, v1.0.91, v1.0.104, v1.0.106) : cases du tracé des routes
  à ouvrir, case de la ville d'arrivée, une case de marge (terre) autour de chaque ville devenue
  jouable, puis **trous intérieurs** (case bordée sur au moins 3 côtés par des cases jouables) ;
  jamais une case de grotte de dragon, d'entrée ophidienne ni leurs 8 voisines ; chaque case vide
  reçoit un lieu `[GD]` avec un POI réel.
- Ajouter une case : l'inscrire dans `generated_cells` (nouvelle ligne en fin de liste) ; la CI la
  génère au build suivant ([§4](#4-générer-le-monde)).

### 11.8 Routes : le générateur suit le lore (v1.0.105, v1.0.106)

- **Source** : collection `routes` du lore (tracée dans l'éditeur, mode 〰 Tracer), recopiée par
  `tools/lore/sync_routes.py` dans `lore_roads` du master : **165 routes terrestres** (`road`,
  `imperial_road` — la Via Magna —, `path`) ; les 8 routes maritimes ne sont pas tracées.
- **Tracé en jeu** (§5, étape 4) : chaque route passe par chaque case de ses `waypoints`, coupée
  aux villes traversées ; A* entre deux cases ; pas en diagonale par le coin commun quand une
  voisine n'est pas jouable ; embranchements raccordés à leur route hôte.
- **Réseau du générateur** : toujours calculé pour les corridors de départ ; **10 de ses routes**
  sont gardées en jeu (corridors sans équivalent au lore).
- **Garanties v1.0.106** : chaque ville et hameau jouable est relié à pied aux 7 départs (sauf
  les 4 villes de l'Île de la Trêve, réseau propre) ; aucune route du lore ne sort des cellules
  jouables pour y revenir ; 43 routes mènent à une zone fermée et s'arrêtent au mur invisible ;
  la Via Magna est jouable de Lhynn à Moork.
- **Encore coupées** : Farvhenn – Vestmhor (000106), Shaddrek – Klehrwath (000071). Sans route :
  la baie de l'Île de la Trêve, Hishtarland et Dyprokh (îles).

### 11.9 Gouffre des Revenus et Thurvhal (v1.0.107)

> **Secret joueurs** : le gouffre ne se découvre qu'en mourant dans le canyon.

| Point | Valeur |
|---|---|
| Lieu | Deathtrap Canyon, case **S008_E010** ; gouffre à 2,3 km à l'est de la gorge que suit la route 000074, sans route ni sentier |
| Master | relief `gouffre_des_revenus` (`type: gouffre`) ancré sur `ville_thurvhal` (`secret: true`, position 2300 m est, 200 m nord) : fond plat ≈ 410 m (85 m sous le point le plus bas du pourtour, ≈ 146 m sous le terrain environnant), rayon du fond 380 m, parois 45 m, galerie de la Fente (pente ≈ 15 %) vers une entrée cachée par des éboulis |
| Lore `[GD]` | Gouffre des Revenus (`gouffre_poi`), Fente des Revenus, village de Thurvhal, peuple oublié neutre des **Ghaurdes** (`faction_ghaurdes`), PNJ Mhaldra et Kerreth, 3 quêtes (30-38) |
| Réapparition | le cimetière neutre de Thurvhal (`respawn_points.txt` de `cell_s008_e010`) est le plus proche pour 85,6 % de S008_E010 et 54,7 % de S008_E011 : un joueur mort dans le canyon se réveille au fond. Aucune modification du shard (règle « cimetière le plus proche en X, Z »). |
| Secret | ville absente de `places.json` `cities`, de la carte et de `map_features.bin` ; recopiée dans `secret_places.json` (v1.0.110 : réservé au serveur, retiré du paquet client) |
| À faire côté client | lumière des 36 torches (`world.point_lights` du zone.json), fond sombre (`world.ambiance_zones`), mécanisme de « lieu découvert », bannière et radar de la ville secrète |

### 11.10 Autres reliefs locaux du master (v1.0.72, v1.0.77)

| Relief | Type | Ancrage | Effet |
|---|---|---|---|
| `colline_verdansur` | `colline` | Vaerdhun (`town_0119`, N010_W015) | colline de 130 m d'où partent deux rivières |
| `crevasse_wardholt` | `crevasse` | Vharholdt (`hamlet_wardholt`) | fente en cul-de-sac, mine au fond, fermée aux routes |
| `massif_n0xx_w0xx` (×11) | `massif` | cases de l'Île de la Trêve | crêtes boisées, falaises au rivage |
| `gouffre_des_revenus` | `gouffre` | Thurvhal (S008_E010) | §11.9 |

Une ville ancrée sur un relief ne peut pas recevoir de position manuelle, sauf un gouffre.
