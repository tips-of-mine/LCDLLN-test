# Auberge éditable — entité « Building » dans l'éditeur de carte

**Date** : 2026-06-17
**Statut** : design validé (décisions A-E tranchées), implémentation en cours (PR unique).
**Objectif** : pouvoir composer un visuel d'auberge dans l'éditeur monde à partir
d'éléments existants (murs, toits, portes, mobilier), le persister, le voir en jeu,
puis remplacer l'auberge actuelle de la carte de démo (Feyhin).

> **Révision 2026-06-17 (bibliothèque de types + références)** — Sur demande
> utilisateur, le modèle évolue d'un Building « inline » vers une **bibliothèque
> de TYPES** : un fichier JSON par type dans `game/data/buildings/templates/`
> (`tavern.json`, `house.json`…), chacun contenant plusieurs **variantes**
> (créations), chaque variante = une grappe de pièces. La carte ne stocke que des
> **références** (`BuildingPlacement` : type + variante + transform monde) dans
> `buildings.bin` ; le jeu résout chaque référence contre la bibliothèque
> (`BuildingTemplateLibrary`) pour afficher. Modifier une variante met à jour tous
> les bâtiments de ce type posés. Les sections §3-§5 ci-dessous décrivant le
> modèle inline d'origine sont **supersédées** par ce modèle là où elles diffèrent
> (struct `BuildingInstance` à pièces inline → `BuildingTemplate`/`BuildingVariant`
> en bibliothèque + `BuildingPlacement` en carte). Le format `BuildingPart`, le
> piège de rendu (ground-snap) et `BuildPropFromMeshMatrix` restent valables.

---

## 1. Contexte (audit du 2026-06-17)

- L'auberge actuelle = **13 props posés à la main** dans
  `game/data/zones/feyhin/scenery.json` (indices 310-322), rendus côté client par
  `Engine::LoadScenery` → `BuildPropFromMesh`. Non éditable depuis l'éditeur.
- L'éditeur écrit un **pipeline binaire** (`instances/.../props.bin`,
  `mesh_inserts.bin`…) que **le client n'a jamais lu** (la seule occurrence de
  `props.bin` sous `src/client/` est le header de format `PropInstances.h`, aucun
  call-site de chargement runtime). Rupture éditeur→jeu.
- Kit modulaire complet présent sur disque : `game/data/meshes/props/` (murs
  `Wall_*`, portes `Door_*`/`DoorFrame_*`, fenêtres `Window_*`/`WindowShutters_*`,
  toits `Roof_*`, sols `Floor_*`, balcons `Balcony_*`, mobilier taverne). **Aucun
  mesh « auberge » monolithique** ; le kit `human_village_kit_01.json` référence
  des `.glb` absents.
- `AssetBrowserPanel` est un **placeholder vide** (`AssetBrowserPanel.cpp:17`).
  `OutlinerPanel` + `InspectorPanel` savent déjà lister et éditer le transform
  d'une entité sélectionnée via `EditorSceneModel` + `SetEntityTransformCommand`.

## 2. Décisions validées

| # | Décision | Choix |
|---|----------|-------|
| A | Nature de l'auberge | **Preset multi-meshes** : grappe d'éléments existants regroupée logiquement, ajustable pièce par pièce. |
| B | Pont de données | **Le client lit le pipeline binaire de zone** (vrai fix), via les `buildings.bin` (et, à terme, `props.bin`). |
| C | Regroupement | **Nouveau `BuildingDocument`** structuré (guid + CRUD + callbacks), format `buildings.bin`, sur le patron de `MeshInsertDocument`. |
| D | Auberge existante | **Migrer** les 13 props 310-322 en un Building éditable, les **retirer** de `scenery.json`, pointer le respawn « inn » (88,100) dessus. |
| E | Asset browser | **`catalog.json` généré** (catégorie + chemin + vignette) consommé par `AssetBrowserPanel`. |

### Décision interne D1 — pièces inline (pas de référence à props.bin)

Un `BuildingInstance` **possède ses pièces en propre** (`parts` inline, chemin
glTF en string comme `MeshInsertInstance`). Il ne référence **pas** d'`instanceId`
de `props.bin`. Conséquences :

- Déplacer / dupliquer / supprimer l'auberge = une opération sur le transform de
  groupe ; aucune référence fantôme.
- Le chemin glTF est porté **en clair** dans `buildings.bin` → le client n'a pas
  besoin de résoudre le hash `assetId` (problème qui bloque la lecture brute de
  `props.bin`, où seul le hash FNV-1a est stocké).
- `props.bin` reste réservé aux props **isolés** (non groupés) ; sa lecture
  runtime (résolution hash→chemin via le manifest de la décision E) est un
  chantier ultérieur, hors périmètre auberge.

## 3. Modèle de données

Header partagé `src/client/world/instances/Buildings.h` (header-only, inline,
sur le patron de `PropInstances.h` — compilé dans `engine_core`, donc visible
client + éditeur sans duplication de symboles).

```cpp
namespace engine::world::instances
{
    struct BuildingPart
    {
        std::string        gltfRelativePath; // ex: "meshes/props/Wall_Plaster_Straight.gltf"
        engine::math::Vec3 localPosition{};  // offset local (m) par rapport à l'origine du bâtiment
        engine::math::Vec3 localEulerDeg{};  // rotation XYZ locale (degrés)
        float              localScale = 1.0f; // échelle uniforme locale
    };

    struct BuildingInstance
    {
        uint64_t                 guid = 0u;       // 0 = sentinelle invalide
        std::string              displayName;     // libre, pour Outliner ("Auberge")
        engine::math::Vec3       worldPosition{}; // origine du groupe (m)
        float                    worldYawDeg = 0; // yaw du groupe (degrés)
        float                    worldScale  = 1; // échelle uniforme du groupe
        std::vector<BuildingPart> parts;
    };
}
```

Rotation par pièce en **Euler degrés + échelle uniforme** (et non quaternion) pour
rester cohérent avec `MeshInsertInstance` et `EntityTransform` de l'éditeur, et
simplifier l'UI (sliders). Le groupe n'a qu'un **yaw** (les auberges se tournent
au sol) + échelle uniforme — suffisant pour le placement, extensible plus tard.

### Format binaire `buildings.bin` — magic « LCBD » v1

Sur le patron `LCMI` (`MeshInsertIo`). Little-endian.

```
Header 16 octets : [magic(4)="LCBD"][version(4)=1][buildingCount(4)][reserved(4)]
Par building :
  guid (u64)
  displayName (u16 len + UTF-8)
  worldPosition (3× f32)
  worldYawDeg (f32)
  worldScale (f32)
  partCount (u32)
  Par part :
    gltfRelativePath (u16 len + UTF-8)
    localPosition (3× f32)
    localEulerDeg (3× f32)
    localScale (f32)
```

Chemin disque : `instances/zone_<zoneId>/buildings.bin`, écriture toujours
namespacée + fallback lecture legacy via `zone_paths::ResolveInstancesFileForRead`.

## 4. Composants

### 4.1 Côté partagé / éditeur
- **`Buildings.h`** — struct + `SaveBuildingsBin`/`LoadBuildingsBin` (inline).
- **`BuildingDocument` (.h/.cpp)** — `src/world_editor/buildings/` — CRUD (Add /
  Remove / Update / GetByGuid / All), guid monotone, dirty flag, callbacks
  `onAdded/onUpdated/onRemoved`, `SetZoneId`, `SaveToDisk`/`LoadFromDisk`. Calque
  exact de `MeshInsertDocument`.
- **`BuildingDocumentIo`** — sérialisation via `Buildings.h` (header-only) ; pas
  de .cpp dédié si l'inline suffit (comme `props.bin`).
- **`AssetCatalog` (.h/.cpp)** — `src/world_editor/assets/` — parse
  `game/data/meshes/props/catalog.json` : `{ id, category, gltfRelativePath,
  displayName, thumbnailPath }`. Catégories : Wall, Door, Window, Roof, Floor,
  Balcony, Furniture, Light, Misc.
- **Générateur de catalogue** — petit outil/commande qui scanne
  `game/data/meshes/props/**/*.gltf`, dérive la catégorie par préfixe de nom, et
  (ré)écrit `catalog.json`. Maintient le manifest à jour sans saisie manuelle.
- **`AssetBrowserPanel`** (réécriture) — charge le catalogue, liste par catégorie
  avec filtre/recherche, expose un callback « asset sélectionné » → alimente
  l'outil actif.
- **`BuildingTool` (.h/.cpp)** — `src/world_editor/buildings/` — sur le patron
  `CaveTool` : démarrer/charger un bâtiment, ajouter une pièce (asset sélectionné
  + raycast viewport → `BuildingPart` en espace local), éditer transform de
  groupe et des pièces, retirer une pièce. Commandes undo/redo
  (`AddBuildingCommand`, `AddPartCommand`, `RemovePartCommand`, …) sur le patron
  `PlacePropsCommand`.
- **`EditorSceneModel`** — ajouter le type d'entité **Building** (et, en option,
  ses pièces enfants) à l'agrégation Outliner ; `InspectorPanel` édite le
  transform de groupe via `SetEntityTransformCommand`.
- **Export** — `ZoneExportInputs.buildings` + `WorldEditorExporter::SaveZone`
  écrit `instances/buildings.bin` ; `WorldEditorShell::SaveZoneDocuments` sauve
  le `BuildingDocument`.

### 4.2 Côté client (jeu)
- **`Engine::LoadBuildings`** — lit `instances/zone_<id>/buildings.bin`, et pour
  chaque pièce calcule la matrice monde
  `T(worldPos) · Ry(worldYaw) · S(worldScale) · T(localPos) · R(localEuler) · S(localScale)`,
  puis rend la pièce **sans re-snap au sol** (l'origine du bâtiment est snappée
  une seule fois ; les pièces conservent leur Y local pour pouvoir empiler
  toit/étage).
- **`Engine::BuildPropFromMeshMatrix`** (nouveau, ou overload) — variante de
  `BuildPropFromMesh` qui **bake une `Mat4` explicite** dans les sommets (même
  contournement du bug de buffer d'instance partagé) **sans** le `lift = groundY
  - minY`. `LoadScenery`/`LoadInteractableProps` conservent le comportement
  ground-snap actuel.
- Appel de `LoadBuildings` dans la séquence de chargement de zone, après
  `LoadScenery`.

## 5. Migration de l'auberge existante (décision D)

1. Convertir les 13 entrées `scenery.json` 310-322 en un `BuildingInstance`
   « Auberge » : origine du groupe = (88, 0, 100) ; chaque prop → `BuildingPart`
   avec `localPosition = (x - 88, 0, z - 100)`, `localEulerDeg.y = yaw`,
   `localScale = scale`. Produit `game/data/zones/feyhin/instances/zone_feyhin/buildings.bin`
   (ou via un export éditeur).
2. Retirer les indices 310-322 de `scenery.json` (et recompter `world.scenery.count`).
3. Le respawn « inn » (88,100, `respawn_points.txt`) pointe déjà sur cette origine.
4. Vérifier en jeu : l'auberge s'affiche identiquement, désormais depuis
   `buildings.bin`, et devient éditable dans l'éditeur.

Un petit utilitaire de migration (offline ou test-outil) génère le `buildings.bin`
depuis le JSON pour éviter une saisie binaire manuelle.

## 6. Découpage en phases (1 PR unique, commits par phase)

- **P1 — Fondation** : `Buildings.h` (format + Save/Load inline) + `BuildingDocument`
  (.h/.cpp) + tests unitaires (round-trip LCBD, CRUD) + wiring CMake (engine_core +
  `building_tests`). *Compile et testable en CI.*
- **P2 — Rendu client** : `BuildPropFromMeshMatrix` (bake sans ground-snap) +
  `Engine::LoadBuildings` + appel dans la séquence de zone.
- **P3 — Export** : `ZoneExportInputs.buildings` + `SaveZone` + `BuildingDocument`
  dans `WorldEditorShell::SaveZoneDocuments`.
- **P4 — Asset browser** : format `catalog.json` + générateur + `AssetCatalog`
  (.h/.cpp) + réécriture `AssetBrowserPanel` + tests parsing.
- **P5 — Outil de composition** : `BuildingTool` + commandes undo/redo + intégration
  Outliner (type Building) + Inspector (transform de groupe).
- **P6 — Migration + preset** : preset auberge JSON + utilitaire de migration
  scenery 310-322 → `buildings.bin`, retrait de `scenery.json`, validation en jeu.

## 7. Déploiement

**✅ Client + éditeur + données uniquement — redéploiement serveur PAS nécessaire.**
Aucun nouvel opcode, aucune migration DB, aucun handler serveur, aucune clé
`config.json` lue par le serveur. Le contenu de zone (décor/auberge) est chargé
côté client ; l'auberge est du décor sans collision serveur autoritaire dans ce
périmètre. (À reconsidérer si, plus tard, l'auberge porte de la collision ou de
l'interaction validée serveur.)

## 8. Risques / points d'attention

- **Ground-snap par pièce** : le chemin de rendu actuel plaque chaque mesh au sol
  (`Engine.cpp:13214`). Le renderer de bâtiment DOIT utiliser une bake de matrice
  explicite sans ce lift, sinon toits/étages s'effondrent au sol. (Risque P2.)
- **Bug buffer d'instance partagé** : le contournement existant (cuisson de la
  transformation dans les sommets) doit être conservé dans la variante matrice.
- **`server_app` ne linke pas `engine_core`** : code purement client/éditeur ici,
  donc non concerné — mais si un .cpp partagé devait servir au serveur, l'ajouter
  AUSSI à la liste `server_app`.
- **Convention winding Vulkan** : on ne touche à aucun pipeline `frontFace` ; les
  meshes du kit `props/` sont chargés par le même chemin que le décor existant
  (déjà correct).
- **Naming** : nouveau code/fichiers/dossiers en PascalCase (`BuildingDocument`,
  `Buildings.h`, dossier `buildings/`).
