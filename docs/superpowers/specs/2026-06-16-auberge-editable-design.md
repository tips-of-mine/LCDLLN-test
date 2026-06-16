# Design — Auberge éditable depuis l'éditeur de carte

**Date** : 2026-06-16
**Statut** : design validé (en attente de relecture utilisateur avant plan d'implémentation)
**Communication** : FR · code/identifiants EN · racine source = `src/`

## Objectif

Permettre, depuis l'éditeur de carte (`src/world_editor/`), de **créer et modifier la
forme** d'une auberge en assemblant des éléments existants, puis de propager
automatiquement le modèle dans la carte de démonstration côté client, là où
l'auberge n'existe aujourd'hui que comme une terrasse de props (`config.json`,
indices `world.scenery.310-322`) doublée d'un point de réapparition placeholder.

## Contexte technique (issu de l'audit du 2026-06-16)

### Constat architectural central : trois systèmes de structures non connectés

| Système | Producteur | Sortie | Lu par le client de jeu ? |
|---|---|---|---|
| `PlacementDocument` → `props.bin` | éditeur (`PlacementTool`) | `instances/zone_<id>/props.bin` | ❌ Non |
| `MeshInsertDocument` → `mesh_inserts.bin` | éditeur (volumes) | `instances/zone_<id>/mesh_inserts.bin` | ❌ Non |
| `world.scenery` | config / scripts | `config.json` | ✅ **Oui — seul rendu en jeu** |

- Côté client de jeu, le seul chargement de décor est `LoadScenery()` qui lit
  `world.scenery.count` + `world.scenery.<i>.*` depuis `config.json`
  (`src/client/app/Engine.cpp:12978-13002`).
- `LoadPropsBin` existe (`src/client/world/instances/PropInstances.h:96`) mais
  n'est **jamais appelée** dans le runtime du client de jeu. Les références
  `MeshInsert` dans `Engine.cpp` (`:9099`, `:9126-9128`) sont gardées par
  `m_worldEditorShell` → actives uniquement dans le binaire éditeur.

**Conséquence** : exporter vers `world.scenery` court-circuite le pont manquant
éditeur→jeu. C'est le levier retenu (cf. décision C).

### Deux autorités de respawn distinctes

- `respawn_points.txt` (`game/data/respawn/respawn_points.txt`, ligne
  `0 inn 88.0 1.5 100.0`) est **purement client** : chargé par
  `LoadRespawnMarkers()` (`src/client/app/Engine.cpp:12817-12848`) dans
  `m_respawnMarkers` (`Engine.h:1408`). Le rendu placeholder (label + anneau) a
  été **retiré volontairement** (`Engine.cpp:11641-11647`).
- Le serveur (`shardd`) calcule le respawn depuis la **base de données** :
  `GraveyardManager::Load(pool)` lit la table `graveyards`
  (`src/shardd/internals/globals/GraveyardManager.h:39-47`) ; la position de
  spawn perso vient de `characters.spawn_x/y/z` (`src/shardd/main_linux.cpp:205`).

→ Coupler l'auberge au respawn **client** ne touche pas le serveur ; coupler au
respawn **serveur** exige une migration DB + un redéploiement `shardd`.

### Assets réellement disponibles

`game/data/meshes/props/` contient un **kit modulaire complet** (~338 paires
`.gltf`+`.bin`, glTF 2.0 + textures PNG trim bois/métal/tissu) :
murs (`Wall_Plaster_*`, `Wall_UnevenBrick_*`), portes/cadres (`Door_*`,
`DoorFrame_*`), fenêtres/volets (`Window_*`, `WindowShutters_*`), toits
(`Roof_RoundTiles_*`, `Roof_Front_Brick*`, `Roof_Wooden_*`), planchers
(`Floor_*`), coins/surplombs/balcons, escaliers (`Stairs_*`), mobilier taverne
(`Table_Large`, `Chair_1`, `Bench`, `Stool`, `Bed_Twin*`, `Barrel*`, `Crate_*`,
`Chandelier`, `Lantern_Wall`, `Banner_*`, etc.).

**Manque** : aucun mesh « auberge » assemblé dédié ; le kit
`game/data/assets/structures/kits/human_village_kit_01.json` référence des
`.glb` **inexistants** (inutilisable en l'état) ; les `catalog.json`
arches/grottes/surplombs/donjons sont des stubs sans `.gltf` livrés.

### État des panneaux éditeur

- **AssetBrowserPanel** : placeholder pur (`src/world_editor/panels/AssetBrowserPanel.cpp:15-20`).
- **ToolPropertiesPanel** : fonctionnel terrain/eau/macro ; volumes 3D à UI
  complète mais sans preview mesh glTF (`:927`, `:1022`, `:1111`).
- **InspectorPanel** : fonctionnel, édite Position (DragFloat3), Rotation **Y
  seule**, Échelle, via `SetEntityTransformCommand` undoable
  (`src/world_editor/panels/InspectorPanel.cpp:65-72`).
- **OutlinerPanel** : fonctionnel (groupes par `EntityKind`, sélection), sans
  suppression/renommage (`src/world_editor/panels/OutlinerPanel.cpp:14-70`).
- **SelectionTool** : géométrie pure (rect/lasso), non câblée au viewport
  (`src/world_editor/SelectionTool.cpp:20-69`).

## Décisions de design (validées)

| Réf | Décision |
|---|---|
| **A** | Auberge = **preset multi-meshes groupé** (assemblage de props modulaires existants). |
| **B** | Persistance éditeur = **preset réutilisable JSON** (`game/data/assets/structures/presets/`) + champ `groupId` sur les instances. |
| **C** | Pont vers le jeu = **export vers `world.scenery`** de `config.json` (le client le rend déjà ; aucun nouveau loader). |
| **D** | Respawn = **couplé source-unique** via un **marqueur spawn** du preset → écrit dans `respawn_points.txt` (autorité **client**). Couplage DB serveur **différé** (T6). |
| **E** | Édition = **bloc + accès aux éléments** (transform de groupe global, mais chaque élément reste sélectionnable/éditable). |
| **Périmètre v1** | **Vrai bâtiment** : murs + portes + fenêtres + toit + plancher + mobilier, composé depuis le kit `props/`. |

## Architecture cible

### Flux global

```
Éditeur (preset + groupId)  ──export──►  config.json/world.scenery  ──LoadScenery()──►  rendu en jeu
                                    └────►  respawn_points.txt (ancre spawn « inn »)
```

Aucun nouveau loader client : `LoadScenery()` rend déjà `world.scenery`.

### Modèle de données

- **`BuildingPreset` (JSON)** dans `game/data/assets/structures/presets/auberge_*.json` :
  - liste d'éléments `{ meshPath, offset (x,y,z) relatif au pivot, yaw, scale }` ;
  - un **point d'ancrage spawn** relatif (alimente le respawn « inn ») ;
  - un **pivot** de groupe.
  - Format inspiré de `human_village_kit_01.json`, mais référençant de **vrais**
    meshes de `game/data/meshes/props/`.
- **`groupId`** ajouté à `PropInstance` (`src/client/world/instances/PropInstances.h:25-32`)
  pour matérialiser le bloc dans la zone et autoriser l'accès élément-par-élément.
  Impact sérialisation `props.bin` : ce fichier n'étant pas lu par le jeu (cf.
  constat central), l'ajout d'un champ y est sans risque runtime ; la version du
  format (`kPropsVersion`) sera bumpée et la lecture restera rétro-compatible.
- **Export** : aplatit le groupe en N entrées `world.scenery.<i>` (mesh +
  position absolue = pivot ⊕ offset, yaw, scale) et écrit/maj la ligne `inn` de
  `respawn_points.txt` depuis l'ancre spawn.

### Réutilisation (pas de duplication)

- `PlacementTool` tel quel (mode `Single` + snap `Grid`,
  `src/world_editor/PlacementTool.h:19-20`) pour poser les éléments.
- `OutlinerPanel` / `InspectorPanel` / `SetEntityTransformCommand` existants pour
  l'accès élément-par-élément et l'undo.
- Pas de `BuildingDocument` séparé : le groupe vit comme un ensemble de
  `PropInstance` partageant un `groupId`, plus un preset JSON réutilisable.

## Périmètre v1

Composer une auberge fermée (murs plaster/brique, portes, fenêtres, toit tuiles,
plancher, mobilier taverne) à partir du kit `props/`. La terrasse actuelle
(`config.json:552-566`) est remplacée par / intégrée à l'auberge.

## Découpage en tickets

1. **T1 — AssetBrowserPanel fonctionnel** : scan de `game/data/meshes/props/`,
   liste filtrable, sélection → asset actif du `PlacementTool`. *Client/éditeur.*
2. **T2 — Groupe + BuildingPreset** : champ `groupId` (+ bump version `props.bin`),
   format preset JSON, commandes grouper / charger / déplacer le groupe. *Éditeur.*
3. **T3 — Inspector rotation XYZ + suppression Outliner** : compléments d'édition
   (aujourd'hui rotation Y seule). *Éditeur.*
4. **T4 — Export `world.scenery` + `respawn_points.txt`** : aplatissement du
   groupe vers `config.json` + écriture de l'ancre spawn « inn ». *Client/données.*
5. **T5 — Preset auberge de référence** : composer la vraie auberge, la poser dans
   la zone démo à (88,100), remplacer la terrasse placeholder. *Données.*
6. **T6 (différé, optionnel) — Couplage respawn serveur DB** : migration
   `graveyards` + relecture `shardd`. *Serveur.*

Ordre de réalisation : T1 → T2 → T3 → T4 → T5 (T6 indépendant, plus tard).

## Déploiement

- **T1 → T5** : ✅ client + éditeur + données — **pas de redéploiement serveur**.
- **T6** : ⚠️ **redéploiement serveur `shardd` + migration DB** requis (couplage
  respawn complet côté serveur).

## Risques / points de vigilance

- **Cohérence ancre spawn** : tant que T6 n'est pas fait, le respawn serveur réel
  (DB) reste découplé du respawn client (`respawn_points.txt`). En v1, le joueur
  réapparaît selon l'autorité serveur ; l'ancre client n'est cohérente que si la
  DB pointe déjà au même endroit. À documenter pour éviter la confusion.
- **`config.json` qui gonfle** : l'export ajoute N entrées `world.scenery` par
  auberge. Acceptable pour la démo ; à surveiller si multiplication des bâtiments.
- **Pas de preview mesh dans le viewport éditeur** (limite connue de
  `ToolPropertiesPanel`) : le placement reste « à l'aveugle » tant qu'un rendu
  glTF des instances n'est pas câblé. Hors périmètre v1, à prévoir en suivi.
- **Convention de nommage** : nouveau code/fichiers/dossiers en PascalCase ; docs
  en kebab-case (convention repo).
