# Monde « Royaumes de la Lune Noire » — carte jouable complète

> Chantier « Royaumes » (2026-10). Remplace la zone de test `feyhin` par un
> monde continu de **5,6 km × 5,6 km** : 35 agglomérations, 12 régions,
> 10 lieux remarquables, routes, rivières, lacs, forêts et végétation,
> 5 villes de départ par race et ~560 PNJ d'ambiance qui vivent selon l'heure.

## 1. Vue d'ensemble

```
game/data/world/royaumes/world_master.json      ← SOURCE DE VÉRITÉ (éditée à la main)
                │
                ▼   tools/world_gen  (lcdlln_world_gen, déterministe : seed)
┌───────────────────────────────────────────────────────────────────────────┐
│ zones/royaumes/        terrain_height.r16h  terrain_splat.slap             │
│                        terrain_grass.grms   zone.json  spawners.json       │
│                        respawn_points.txt   runtime_manifest.json          │
│ instances/zone_royaumes/ scatter.bin (LCSC)  buildings.bin  water.bin      │
│ buildings/templates/maison.json   (13 modèles de maisons paramétriques)    │
│ world/royaumes/        places.json  world_map.png                          │
│ world/start_locations.json        quests/quest_poi.json (fusion)           │
└───────────────────────────────────────────────────────────────────────────┘
        │ client (Engine)                       │ serveur
        ▼                                       ▼
  terrain + décor instancié + bâtiments    shard : spawners (créatures + routines
  + eau + carte M + bannière de lieu         PNJ), respawns, PNJ non attaquables
  + dialogues PNJ d'ambiance (E)           master : point d'apparition par race
```

Tous les fichiers sous `zones/royaumes`, `instances/zone_royaumes`,
`world/royaumes` et `world/start_locations.json` sont **GÉNÉRÉS** : ne pas les
éditer, modifier `world_master.json` (ou le générateur) puis régénérer.

## 2. Régénérer le monde

La CI compile `lcdlln_world_gen` et exécute `world_gen_tests` (Linux). En local
(Windows, après un build) :

```powershell
.\build\pkg\world_gen\lcdlln_world_gen.exe --content game/data --world royaumes
# --dry-run : génère et affiche le rapport sans rien écrire
# --output <dir> : écrit ailleurs que dans --content (comparaison)
```

Le rapport liste instances de décor, bâtiments, km de routes, rivières/lacs,
spawners, respawns, et les **avertissements** (route impossible, mesh absent…).
La génération est **déterministe** : même master + même seed ⇒ fichiers
identiques octet pour octet (`world_gen_tests` le vérifie).

## 3. Le fichier maître (`world_master.json`)

| Bloc | Rôle |
|---|---|
| `seed` | graine unique de tout le monde (bruit, placement, population) |
| `scale` | `cell_size_m` 128 (1 case de la carte de lore = 128 m), grille 44 × 44, `origin_cell` N003_W009 = (0,0) monde, `height_scale_m` 400, `sea_level_m` 40 |
| `grid.rows` | carte de lore (1 caractère = 1 case, légende dans `grid.legend` : mer, plaine, forêt, désert, montagne, volcan, marais…) |
| `regions` | 12 régions (rectangles de cases) → noms de la carte et bannières |
| `cities` | 35 agglomérations : `kind` (capitale, grande_ville, ville, village, hameau, forteresse, camp), `race`, `state` (intact/ruine), `faction`, `start_for` |
| `pois` | 10 lieux remarquables (l'Oracle, monolithes, gouffres…) |
| `imports` | contenu historique repris tel quel au Hameau de Marn (zone feyhin : config, décor, bâtiments, respawns) |
| `extra_lakes` | lacs ajoutés à la main (Étang de Marn) |
| `zone_overrides` | clés recopiées dans `zones/royaumes/zone.json` (brouillard, distances de culling) |
| `interactables` | PNJ statiques (donneurs de quêtes) placés relativement au centre d'une ville |

## 4. Pipeline du générateur (`tools/world_gen/lib`)

1. **Terrain** (`Terrain.cpp`) — altitude par biome (bruit fbm + ridged,
   domain-warp pour casser les bords carrés de la grille), falloff océanique
   en bordure, villes **aplanies** (plateau lissé), splat 4 couches
   (herbe / terre / roche / neige ; sable près de la mer), masque d'herbe.
2. **Eau** — rivières par priority-flood + écoulement (les villes sont des
   « dômes » contournés, jamais creusées), lacs (CCW) éloignés des villes.
3. **Routes** (`Roads.cpp`) — A* sur grille 10 m, pente > 46° interdite,
   réutilisation des routes existantes encouragée, lissage Chaikin, profil
   en long ≤ 16 %. Graphe : chaque ville est reliée à ses voisines.
4. **Agglomérations** (`Settlements.cpp`) — place centrale, rues radiales +
   anneau, lots orientés (occupation par rectangles orientés), remplissage,
   forge, marché, auberge, enceinte / palissade avec portes, champs,
   cimetière, pavage. Maisons = 13 modèles paramétriques du kit modulaire
   (grille 2 m, étages 3 m, porte en -Z local tournée vers la rue).
5. **Végétation** (`Vegetation.cpp`) — grille jitterée par biome, classes de
   culling (grand / moyen / petit), exclusion routes / eau / bâtiments.
6. **Population** (`Population.cpp`) — PNJ d'ambiance par ville (villageois,
   gardes, marchands, aubergistes), voyageurs et patrouilles impériales sur
   les routes, faune par biome et par éloignement de la ville de départ la
   plus proche (≈ niveau), faune des quêtes d'accueil, bandits dans les ruines,
   respawns (cimetière + auberge par agglomération), points de départ.
7. **Sorties** (`Output.cpp`) — écrit tous les fichiers + carte PNG 1024².

## 5. Points de départ par race

`world/start_locations.json` (généré) est lu par le **master**
(`CharacterCreateHandler`, module `src/shared/world/StartLocations`) à la
**création** d'un personnage. Ensuite, la dernière position sauvegardée prime.

| Race | Ville de départ | Région | PNJ d'accueil (quêtes) |
|---|---|---|---|
| Humains | Hameau de Marn (0, 0) | Terres de l'Empereur | l'ancien Marn, capitaine Rendal (contenu historique) |
| Elfes | Whispers Hill | Forêt elfique | Iaelith (gardienne), Sylvanor (éclaireur) |
| Orcs | Leviathan (kraal) | Territoires orcques | Grokhaar (chef de guerre), Shazka (chamane) |
| Nains | Ghrunke Dash | Royaume des nains de l'Est | Thrain (forgeron), Brunhild (capitaine des portes) |
| Démons | Faille de Tsaroth | Terres désolées de Tsaroth | Maltheus (héraut), Ysviel (liseuse de braises) |

Repli si la race est inconnue : `default_race` (humains), puis
`character_creation.default_spawn.*`.

**Chaîne d'accueil** (elfes / orcs / nains / démons, `quests/quest_definitions.json`) :
`<race>_accueil` (parler au second PNJ) → `<race>_chasse` (6 bêtes autour de la
ville, archétypes 120/130/140/150) → `<race>_epreuve` (2 bêtes d'élite,
121/131/141/151). Textes : `quest_texts.fr.json` ; dialogues : `dialogues/*.json`.

## 6. Monde vivant — PNJ d'ambiance

Un PNJ d'ambiance est un mob du shard dont l'archétype porte
`"behavior": { "kind": "ambient", … }` (plage 200-259 dans
`creatures/archetypes.json`) et dont le spawner porte une **routine** :

```json
"routine": { "kind": "tour", "schedule": "day", "path": [[x,z], …],
             "pauseMinSec": 3, "pauseMaxSec": 14, "pauseChance": 0.4,
             "wanderRadiusMeters": 0, "pingPong": false, "face": [x,z] }
```

| `kind` | Comportement |
|---|---|
| `tour` | boucle porte → rues → porte, pauses aléatoires (villageois) |
| `patrol` | ronde en boucle (ou aller-retour `pingPong`) — gardes, patrouilles de route |
| `post` | reste à son poste (petite errance), regarde `face` — marchands, aubergistes, sentinelles |
| `wander` | errance libre dans `wanderRadiusMeters` |

`schedule` : `day` (6 h → 21 h), `night` (20 h → 6 h, relève au crépuscule),
`always`. Hors horaire, le PNJ **rentre chez lui** (premier point du chemin)
puis disparaît ; il réapparaît à l'ouverture. L'heure est celle de
`engine::world::WorldClock` (clés `game.worldclock.*`, mêmes défauts que le master).

Règles serveur (`ServerApp::UpdateAmbientMob`) : non attaquable
(`behavior.attackable`, défaut false pour ambient), aucune aggro, vitesse
`behavior.moveSpeed` (m/s), **s'arrête quand un joueur est à moins de 2,6 m**
(il « écoute »). Module pur testé : `src/shardd/gameplay/ambient/AmbientRoutine`.

Côté client : plaque « Nom - Rôle » sans barre de vie, jamais ciblé au Tab,
« [E] » à portée de parole ; **E** ouvre le dialogue `behavior.dialogueId`
(`dialogues/amb_*.json`) et/ou envoie `behavior.talkTarget` (ex. `vendor:1` ⇒
boutique). La conversation se rompt si le PNJ s'éloigne (sortie d'AoI).

## 7. Interface joueur

- **Carte du monde — touche M** (`WorldMapImGuiRenderer`) : image générée,
  villes colorées par peuple (liseré doré = ville de départ, épais = la vôtre),
  ruines barrées, lieux remarquables (losanges), noms de régions, flèche du
  joueur orientée. Molette = zoom (×1 à ×8) autour du curseur, glisser =
  déplacer, « Centrer » / « Vue d'ensemble ». Survol = fiche du lieu (type,
  peuple, région, description de lore).
- **Bannière d'entrée** : « Whispers Hill — Village - Forêt elfique » au
  passage d'une ville (hystérésis 6 m / 14 m) ou d'une région ; accueil à
  l'arrivée en jeu. Module pur testé : `src/client/world/places/WorldPlaces`.

## 8. Formats binaires

**`scatter.bin` « LCSC » v1** (`src/client/world/instances/ScatterInstances.h`) :
en-tête (magic, version, nombre de meshes, nombre d'instances), table de
chaînes (chemins de mesh), puis instances de 32 octets :
`meshIndex u16, flags u8 (1 solide, 2 pièce de bâtiment, 4 occulteur),
cullClass u8, x f32, yOffset f32, z f32, yaw f32, scale f32,
collisionRadius f32, tilt f32`. Y = hauteur du terrain + `yOffset`
(résolu au chargement). Extension : additive et versionnée.

Rendu : `ScatterField` (grille 32 m + frustum + distance par classe) →
`GeometryPass::RecordInstanced` (lots triés par mesh / matériau / fondu,
3 tampons d'instances host-visible en anneau). Les bâtiments (`buildings.bin`)
passent par le même chemin instancié. Collisions : `CompositeWorldCollider`
indexé sur grille uniforme 16 m.

## 9. Réglages

| Clé (`config.json` / `zone.json`) | Défaut | Effet |
|---|---|---|
| `world.active_zone` | `royaumes` | zone chargée (client ET shard) |
| `world.scatter.cull_large_m` | 150 | distance d'affichage arbres / grands rochers |
| `world.scatter.cull_medium_m` | 80 | buissons, rochers moyens, tonneaux |
| `world.scatter.cull_small_m` | 42 | fleurs, fougères, petits props |
| `world.props.cull_distance_m` | 140 | props historiques (scenery.json) |
| `world.fog.start_m` / `end_m` | 60 / 185 | brouillard masquant la coupe de distance |
| `game.worldclock.timescale_real_min_per_day` | 60 | durée d'une journée de jeu (min réelles) |
| `client.remote_avatars.max_skinned_draws` | 32 | avatars animés dessinés par frame (plafond 48) |
| `client.remote_avatars.render_distance_m` | 60 | distance d'affichage des personnages |

Revenir à l'ancienne zone de test : `world.active_zone = "feyhin"` (client et
`deploy/docker/config/shard.config.json`).

## 10. Limites connues / suite

- Le serveur n'a ni heightfield ni navmesh : les PNJ suivent les graphes de
  rues générés ; le client recale Y visuellement sur le terrain.
- **Horloge et commandes admin** : le shard calcule l'heure des routines depuis
  `game.worldclock.*` (mêmes valeurs que le master). Les commandes runtime
  `/settime`, `/pausetime`, `/settimescale` ne modifient que le master : les PNJ
  d'ambiance gardent alors l'horaire « config » jusqu'au redémarrage du shard
  (propagation master → shard non implémentée : demanderait un nouveau message).
- **Avatars animés** : au plus `client.remote_avatars.max_skinned_draws` (32,
  plafond 48) personnages distants dessinés par frame — uniquement ceux dans le
  champ de la caméra et à moins de `render_distance_m` (60 m), les plus proches
  d'abord ; les autres gardent leur plaque de nom.
- Elfes et démons empruntent la silhouette humaine (pas de mesh dédié).
- L'île d'Hishtarland n'est pas reliée par route (avertissement attendu du
  générateur) — liaison maritime à concevoir.
- Les quêtes au-delà de la chaîne d'accueil restent à écrire par région.
