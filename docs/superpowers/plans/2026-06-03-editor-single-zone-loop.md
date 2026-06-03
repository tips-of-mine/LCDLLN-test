# Éditeur monde — Boucle d'édition d'une zone — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rendre l'éditeur monde réellement utilisable sur une zone : créer → voir → éditer → sélectionner/inspecter → sauver → recharger, identique, avec les chunks comme source de vérité unique.

**Architecture:** `TerrainDocument` (chunks `terrain.bin`, format `TRRN`) = source de vérité. Le `r16h`/`HeightmapData` reste un **cache GPU** reconstruit depuis les chunks et affiché par `TerrainRenderer`. Le prérequis transversal du Bloc A est d'**aligner résolution et échelle** entre la grille de chunks (257²/chunk à 1 m) et le `r16h` d'édition, afin que la synchro chunk→GPU soit 1:1. Les panneaux (Outliner/Inspector/Console) lisent un `EditorSceneModel` agrégé + un `EditorSelection` partagé. Un seul pipeline d'outils (le moderne `WorldEditorShell`).

**Tech Stack:** C++17, Vulkan, ImGui, CMake. Tests : framework de tests interne `src/world_editor/tests/` exécuté via ctest sur la CI `build-linux`. **Aucune compilation locale** : la CI GitHub Actions est le compilateur ; itérer par push.

**Référence spec :** `docs/superpowers/specs/2026-06-03-editor-single-zone-loop-design.md`

**Cible binaire :** `lcdlln_world_editor.exe` (`--world-editor`). Mode `--editor` embarqué hors périmètre.

**Déploiement :** ✅ client/éditeur uniquement — aucun redéploiement serveur.

---

## Structure des fichiers

**Bloc A — cycle de vie terrain (modifie l'existant) :**
- `src/world_editor/ui/WorldEditorSession.cpp` — `ActionNewMap`, `ActionSaveCurrentMap`, `ActionLoadMapByZoneId`.
- `src/world_editor/terrain/TerrainDocument.{h,cpp}` — helpers d'initialisation/itération de zone.
- `src/client/app/Engine.cpp` — `RebuildWorldEditorTerrainGpu`, `SyncWorldEditorHeightmapFromDocument`, hook de sauvegarde, tick.
- `src/world_editor/tests/TerrainDocumentRoundTripTests.cpp` — **créé**.

**Bloc B — scène + sélection (crée) :**
- `src/world_editor/scene/EditorSceneModel.{h,cpp}` — **créé**.
- `src/world_editor/scene/EditorSelection.{h,cpp}` — **créé**.
- `src/world_editor/core/WorldEditorShell.{h,cpp}` — possède `EditorSelection`, expose `EditorSceneModel`.
- `src/world_editor/tests/EditorSceneModelTests.cpp`, `EditorSelectionTests.cpp` — **créés**.

**Bloc C/D/E — panneaux réels (remplacent les placeholders) :**
- `src/world_editor/panels/OutlinerPanel.{h,cpp}`, `InspectorPanel.{h,cpp}`, `ConsolePanel.{h,cpp}`.
- `src/world_editor/console/EditorLogSink.{h,cpp}` — **créé** (sink Log → ring buffer).
- `src/world_editor/inspector/InspectorCommands.{h,cpp}` — **créé** (commandes d'édition de propriétés).

**Bloc G/H — nettoyage + menu + config :**
- `src/client/app/Engine.cpp` — suppression de la branche pinceau legacy.
- `src/world_editor/ui/WorldEditorImGui.cpp` — menu Fichier.
- `src/shared/core/Config.{h,cpp}` — `SaveToFile`.

**Bloc F — viewport-dans-panneau (en dernier) :**
- `src/world_editor/render/EditorViewportRenderTarget.{h,cpp}`, `src/world_editor/panels/ScenePanel.{h,cpp}`, passe FrameGraph dans `Engine.cpp`.

> **CMake :** chaque nouveau `.cpp` doit être ajouté à la cible de l'éditeur **et**, s'il est partagé côté serveur, à la liste `server_app` (`src/CMakeLists.txt`). Ici tout est éditeur/client : ajouter à la cible `world_editor` / `engine_core` selon le pattern existant du dossier.

---

## BLOC A — Cycle de vie du terrain fiable

État actuel (lu dans le code) :
- `ActionNewMap` (`WorldEditorSession.cpp:305`) écrit `height.r16h` (`sz×sz`, plat à 32768), `splat.slap` (1024²), `grass.grms`, le JSON ; **n'initialise aucun chunk**. `m_doc.terrainWorldSizeM = kZoneSize (10000)`.
- `RebuildWorldEditorTerrainGpu` (`Engine.cpp:11549`) reconstruit `m_terrain` depuis le `r16h` avec `worldSizeOverride = terrainWorldSizeM`. Retour anticipé si `hmRel` vide.
- `SyncWorldEditorHeightmapFromDocument` (`Engine.cpp:11708`) copie les chunks chargés → `HeightmapData` GPU, gardé par `m_terrain.IsValid()` etc.
- Save terrain via `m_terrainSaveHook` (`Engine.cpp:1245`) → `SaveHeightmap/SaveSplatMap/SaveGrassMask` (r16h), **pas** les chunks.

**Décision d'alignement (A0)** : pour le sous-projet 1, on édite une zone à une **empreinte de chunks** `N×N` (config `editor.world.edit_chunks_per_axis`, défaut **4** → 1024 m, équilibre RAM/visibilité). Le `r16h` d'édition est dimensionné **exactement** à `N*256+1` par axe et `terrainWorldSizeM = N*256`. Ainsi 1 texel r16h = 1 sommet de chunk = 1 m → synchro chunk→GPU strictement 1:1. (Le passage à la pleine zone 20×20 relève du sous-projet 3.)

### Task A1 : Helper `TerrainDocument::InitFlatZone`

**Files:**
- Modify: `src/world_editor/terrain/TerrainDocument.h` (déclaration), `src/world_editor/terrain/TerrainDocument.cpp` (définition)
- Test: `src/world_editor/tests/TerrainDocumentRoundTripTests.cpp` (créé)

- [ ] **Step 1 — Test qui échoue** : créer `TerrainDocumentRoundTripTests.cpp` :

```cpp
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "test_framework.h" // suivre l'include utilisé par les tests existants du dossier
using namespace engine::editor::world;

TEST_CASE("InitFlatZone alloue NxN chunks plats a la hauteur demandee")
{
    TerrainDocument doc;
    doc.InitFlatZone(/*chunksPerAxis=*/4, /*flatHeightMeters=*/0.0f);
    REQUIRE(doc.LoadedChunkCount() == 16u);
    auto c = doc.Find(engine::world::GlobalChunkCoord{0, 0});
    REQUIRE(c != nullptr);
    REQUIRE(c->resolutionX == 257u);
    for (float h : c->heights) { REQUIRE(h == 0.0f); }
    REQUIRE(doc.HasDirtyChunks()); // tout neuf = dirty -> sera persiste
}
```

- [ ] **Step 2 — Vérifier l'échec** : pousser ; CI `build-linux` doit échouer à la compilation (`InitFlatZone` non déclaré). (Pas de build local.)

- [ ] **Step 3 — Implémenter** dans `TerrainDocument.h` (section publique) :

```cpp
/// Initialise une zone neuve : alloue `chunksPerAxis * chunksPerAxis`
/// chunks plats (257x257, 1 m/cellule) à `flatHeightMeters`, indexés de
/// (0,0) à (chunksPerAxis-1, chunksPerAxis-1). Tous marqués dirty pour
/// qu'un `SaveDirtyToDisk` ultérieur les persiste. Vide d'abord le cache.
/// \param chunksPerAxis nombre de chunks par axe (empreinte d'édition).
/// \param flatHeightMeters hauteur uniforme initiale, en mètres.
void InitFlatZone(int chunksPerAxis, float flatHeightMeters);
```

Et dans `TerrainDocument.cpp` (s'appuyer sur la construction réelle de `TerrainChunk` ; lire `TerrainChunk.h` pour `resolutionX/Z`, `cellSizeMeters`, `heights`, `RecomputeBounds`) :

```cpp
void TerrainDocument::InitFlatZone(int chunksPerAxis, float flatHeightMeters)
{
    m_chunks.clear();
    m_splats.clear();
    for (int iz = 0; iz < chunksPerAxis; ++iz)
    for (int ix = 0; ix < chunksPerAxis; ++ix)
    {
        auto chunk = std::make_shared<engine::world::terrain::TerrainChunk>();
        chunk->resolutionX = 257u;
        chunk->resolutionZ = 257u;
        chunk->cellSizeMeters = 1.0f;
        chunk->heights.assign(257u * 257u, flatHeightMeters);
        chunk->RecomputeBounds();
        const engine::world::GlobalChunkCoord coord{ix, iz};
        m_chunks[PackCoord(coord)] = ChunkSlot{std::move(chunk), /*dirty=*/true};
    }
}
```

- [ ] **Step 4 — Vérifier le passage** : push ; CI verte sur ce test.
- [ ] **Step 5 — Commit** : `git commit -m "feat(editor): TerrainDocument::InitFlatZone (chunks plats NxN)"`

### Task A2 : `ActionNewMap` initialise les chunks + r16h aligné

**Files:** Modify `src/world_editor/ui/WorldEditorSession.cpp:305-405`

- [ ] **Step 1** — Ajouter une dépendance `TerrainDocument&` accessible à la session **ou** déclencher l'init via le Shell. *Note d'exécution* : la session n'a pas le `TerrainDocument` (il est dans le Shell). Choix : faire porter l'init des chunks par le **Shell** au moment du `ConsumeTerrainGpuReloadRequest` dans `Engine`, et l'alignement r16h dans `ActionNewMap`. Donc ici :
  - Lire `editor.world.edit_chunks_per_axis` (défaut 4) → `N`.
  - Dimensionner le r16h à `N*256+1` au lieu de `sz` ; fixer `m_doc.terrainWorldSizeM = N*256`.
  - Conserver `WriteFlatHeightmapR16h(hmAbs, N*256+1, N*256+1, 32768u, err)`.
- [ ] **Step 2** — Marquer un flag `m_doc`/session « zone neuve à initialiser en chunks » consommé par le Shell (voir A3).
- [ ] **Step 3** — Build CI (compile). Pas de test unitaire ici (touche I/O + config) → couvert par le test d'intégration manuel (§ critère d'acceptation).
- [ ] **Step 4 — Commit** : `git commit -m "feat(editor): ActionNewMap aligne r16h sur la grille de chunks (N*256+1)"`

### Task A3 : Shell initialise les chunks d'une zone neuve + sauvegarde chunks

**Files:** Modify `src/world_editor/core/WorldEditorShell.{h,cpp}`, `src/client/app/Engine.cpp` (point de consommation du reload)

- [ ] **Step 1** — Ajouter `WorldEditorShell::InitNewZoneTerrain(int chunksPerAxis)` qui appelle `m_terrainDoc.InitFlatZone(chunksPerAxis, 0.0f)` puis `m_terrainDoc.SaveDirtyToDisk(cfg)` (écrit les `terrain.bin`). Documenter (`///`).
- [ ] **Step 2** — Dans `Engine`, là où `ConsumeTerrainGpuReloadRequest()` est traité (avant/au `RebuildWorldEditorTerrainGpu`), si la zone est « neuve » appeler `m_worldEditorShell->InitNewZoneTerrain(N)` **avant** le rebuild GPU, sinon (chargement) charger les chunks existants via `EnsureLoaded` sur l'empreinte `N×N`.
- [ ] **Step 3** — `ActionSaveCurrentMap` : garantir que `MutableTerrainDocument().SaveDirtyToDisk(cfg)` + `SaveDirtySplatToDisk(cfg)` sont appelés (chunks autoritaires) en plus du hook r16h. *Note* : faire ceci côté Shell/Engine, pas dans la session (qui n'a pas le doc).
- [ ] **Step 4 — Commit** : `git commit -m "feat(editor): init + persistance des chunks d'une zone (source de verite)"`

### Task A4 : Synchro chunk→GPU robuste (retirer les gardes silencieuses)

**Files:** Modify `src/client/app/Engine.cpp:11708-11800` (`SyncWorldEditorHeightmapFromDocument`) et le tick `7568`

- [ ] **Step 1** — Lire intégralement `SyncWorldEditorHeightmapFromDocument` (mapping chunk-coord → texel r16h). Avec l'alignement A0 (`r16h = N*256+1`, 1 m/texel), écrire le mapping 1:1 : pour le chunk `(ix,iz)`, le sommet `(x,z)` va au texel `(ix*256 + x, iz*256 + z)`. Gérer le recouvrement des bords (257 vs 256).
- [ ] **Step 2** — Si une précondition manque (`!m_terrain.IsValid()`), **ne pas** abandonner le flag : le laisser armé pour re-tenter au prochain tick (différer au lieu d'abandonner). Documenter.
- [ ] **Step 3** — Build CI.
- [ ] **Step 4 — Commit** : `git commit -m "fix(editor): synchro chunk->heightmap 1:1 + differe si GPU pas pret"`

### Task A5 : Test d'aller-retour disque des chunks

**Files:** Modify `src/world_editor/tests/TerrainDocumentRoundTripTests.cpp`

- [ ] **Step 1 — Test** : `InitFlatZone` → modifier quelques hauteurs d'un chunk → `MarkDirty` → `SaveDirtyToDisk(tmpCfg)` → nouveau `TerrainDocument` → `EnsureLoaded` les mêmes coords → comparer `heights` octet-à-octet. (Utiliser un `Config` pointant `paths.content` vers un dossier temporaire ; suivre le pattern des tests existants pour fabriquer un Config de test.)
- [ ] **Step 2 — Échec attendu** si A1/A3 incomplets.
- [ ] **Step 3 — Vert** après A1/A3.
- [ ] **Step 4 — Commit** : `git commit -m "test(editor): round-trip disque des chunks terrain"`

**➡️ Fin du Bloc A = PR #1.** Critère : dans l'exe, créer une zone → sculpter → voir l'effet → sauver → recharger → relief identique. CI verte. Pousser pour build GitHub validable.

---

## BLOC B — Modèle de scène + sélection

### Task B1 : `EditorSelection`

**Files:** Create `src/world_editor/scene/EditorSelection.{h,cpp}`, Test `src/world_editor/tests/EditorSelectionTests.cpp`

- [ ] **Step 1 — Test** :

```cpp
#include "src/world_editor/scene/EditorSelection.h"
using namespace engine::editor::scene;

TEST_CASE("EditorSelection notifie au changement")
{
    EditorSelection sel; int calls = 0; EntityId last{};
    sel.SetOnChanged([&](EntityId id){ ++calls; last = id; });
    sel.Select(EntityId{EntityKind::MeshInsert, 7});
    REQUIRE(calls == 1);
    REQUIRE(last.index == 7u);
    REQUIRE(sel.Current().kind == EntityKind::MeshInsert);
    sel.Clear();
    REQUIRE(calls == 2);
    REQUIRE(sel.Current().kind == EntityKind::None);
}
```

- [ ] **Step 2 — Échec** (compile).
- [ ] **Step 3 — Implémenter** : `EntityKind { None, Terrain, Water, MeshInsert, DungeonPortal, LayoutInstance }`, `struct EntityId { EntityKind kind; uint32_t index; }`, classe `EditorSelection` avec `Select/Clear/Current/SetOnChanged` (callback `std::function<void(EntityId)>`). Documenter chaque méthode (`///`).
- [ ] **Step 4 — Vert.**
- [ ] **Step 5 — Commit** : `git commit -m "feat(editor): EditorSelection (etat de selection partage)"`

### Task B2 : `EditorSceneModel`

**Files:** Create `src/world_editor/scene/EditorSceneModel.{h,cpp}`, Test `EditorSceneModelTests.cpp`

- [ ] **Step 1 — Test** : construire un `EditorSceneModel` à partir de documents factices (water/mesh inserts/dungeon/layout) et vérifier que `Entities()` renvoie le bon nombre par `EntityKind` et des libellés non vides. *Note d'exécution* : lire les API réelles de `WaterDocument`, `MeshInsertDocument`, `DungeonPortalDocument`, `WorldMapEditDocument.layoutInstances` pour les accesseurs.
- [ ] **Step 2 — Échec.**
- [ ] **Step 3 — Implémenter** : `EditorSceneModel` agrège (par référence) les documents et expose `struct SceneEntity { EntityId id; std::string label; bool hasTransform; Transform transform; }` + `const std::vector<SceneEntity>& Entities() const` + `Rebuild()` (reconstruit la liste depuis les documents). Pas de possession des données.
- [ ] **Step 4 — Vert.**
- [ ] **Step 5 — Commit** : `git commit -m "feat(editor): EditorSceneModel (vue agregee des entites de zone)"`

### Task B3 : Picking viewport → sélection

**Files:** Modify `src/world_editor/core/WorldEditorShell.{h,cpp}` (possède `EditorSelection` + `EditorSceneModel`), point d'entrée clic viewport (`Engine.cpp` ou ScenePanel)

- [ ] **Step 1** — Brancher : clic dans la vue 3D **sans outil d'édition actif** → raycast (`TerrainRaycast` + tests d'intersection des bornes d'entités) → `EditorSelection::Select(nearest)`. Documenter.
- [ ] **Step 2** — Build CI (logique de picking testée indirectement ; intersection AABB peut avoir un test unitaire dédié si extrait dans une fonction pure).
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): picking viewport -> selection d'entite"`

**➡️ Fin du Bloc B = PR #2.**

---

## BLOC C/D/E — Panneaux réels

### Task C1 : OutlinerPanel réel

**Files:** Modify `src/world_editor/panels/OutlinerPanel.{h,cpp}`

- [ ] **Step 1** — Remplacer le placeholder : itérer `EditorSceneModel::Entities()`, grouper par `EntityKind` (ImGui tree nodes), `Selectable` par entité → `EditorSelection::Select`. Surbrillance = `EditorSelection::Current()`. Bascule de visibilité (drapeau local par entité). Documenter.
- [ ] **Step 2** — Build CI.
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): OutlinerPanel reel (liste + selection + visibilite)"`

### Task D1 : InspectorCommands (édition de transform via CommandStack)

**Files:** Create `src/world_editor/inspector/InspectorCommands.{h,cpp}`, Test `src/world_editor/tests/InspectorCommandsTests.cpp`

- [ ] **Step 1 — Test** : commande `SetEntityTransformCommand` (ancien/nouveau transform) : `Execute` applique le nouveau, `Undo` restaure l'ancien, sur une cible factice. Vérifier `TryMerge` (drag continu) cohérent.
- [ ] **Step 2 — Échec.**
- [ ] **Step 3 — Implémenter** `SetEntityTransformCommand : public ICommand` (suivre l'interface `CommandStack.h:ICommand` : `Execute/Undo/TryMerge`). Applique via un foncteur d'écriture fourni par l'appelant (découplage du document concret).
- [ ] **Step 4 — Vert.**
- [ ] **Step 5 — Commit** : `git commit -m "feat(editor): SetEntityTransformCommand (undo/redo transform)"`

### Task D2 : InspectorPanel réel

**Files:** Modify `src/world_editor/panels/InspectorPanel.{h,cpp}`

- [ ] **Step 1** — Afficher les propriétés de `EditorSelection::Current()` via `EditorSceneModel`. Champs transform éditables → émettent `SetEntityTransformCommand` sur `CommandStack`. Cas `Terrain` : métadonnées de zone (lecture). Documenter.
- [ ] **Step 2** — Build CI.
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): InspectorPanel reel (proprietes + edition undoable)"`

### Task E1 : EditorLogSink

**Files:** Create `src/world_editor/console/EditorLogSink.{h,cpp}`, Test `src/world_editor/tests/EditorLogSinkTests.cpp`

- [ ] **Step 1 — Test** : pousser N entrées (> capacité) → le ring buffer ne garde que les `capacity` dernières, dans l'ordre ; filtrage par niveau renvoie le bon sous-ensemble ; thread-safe (push concurrent ne crash pas — test simple).
- [ ] **Step 2 — Échec.**
- [ ] **Step 3 — Implémenter** : ring buffer borné protégé par mutex, `Push(level, msg)`, `Snapshot(levelFilter)`. *Note* : lire `src/shared/core/Log*` pour le mécanisme de sink (callback global ?). Brancher l'enregistrement du sink ailleurs (E2).
- [ ] **Step 4 — Vert.**
- [ ] **Step 5 — Commit** : `git commit -m "feat(editor): EditorLogSink (ring buffer thread-safe)"`

### Task E2 : ConsolePanel réel + branchement du sink

**Files:** Modify `src/world_editor/panels/ConsolePanel.{h,cpp}`, branchement dans `WorldEditorShell`/`Engine`

- [ ] **Step 1** — Enregistrer un `EditorLogSink` sur le système Log au boot éditeur. `ConsolePanel` affiche `Snapshot`, filtres par niveau, auto-scroll, bouton clear. Documenter.
- [ ] **Step 2** — Build CI.
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): ConsolePanel reel (logs filtres)"`

**➡️ Fin C/D/E = PR #3.**

---

## BLOC G/H — Nettoyage legacy + menu Fichier + Config::SaveToFile

### Task G1 : Retirer la branche pinceau legacy

**Files:** Modify `src/client/app/Engine.cpp` (~ligne 9058-9155, garde `m_worldEditorSession` legacy + `m_worldEditorTerrainTools.ApplyBrush`)

- [ ] **Step 1** — Lire la zone exacte. Supprimer **uniquement** le chemin d'édition pinceau obsolète, **conserver** `m_worldEditorTerrainTools` comme uploader GPU (`FlushHeightmap`) utilisé par A4. Vérifier qu'aucun appel restant ne référence le code retiré.
- [ ] **Step 2** — Build CI (le linker confirmera l'absence de référence pendante).
- [ ] **Step 3 — Commit** : `git commit -m "refactor(editor): retire le chemin pinceau legacy (1 seul pipeline)"`

### Task H1 : Config::SaveToFile

**Files:** Modify `src/shared/core/Config.{h,cpp}`, Test `src/world_editor/tests/...` ou test config existant

- [ ] **Step 1 — Test** : `Config` en mémoire → `SetX` → `SaveToFile(tmp)` → `LoadFromFile(tmp)` → valeurs égales (round-trip JSON).
- [ ] **Step 2 — Échec.**
- [ ] **Step 3 — Implémenter** `bool Config::SaveToFile(const std::string& path) const` (sérialise l'arbre de config courant en JSON ; suivre le format de lecture existant). Documenter.
- [ ] **Step 4 — Vert.**
- [ ] **Step 5 — Commit** : `git commit -m "feat(core): Config::SaveToFile (persistance config editeur)"`

### Task H2 : Menu Fichier câblé

**Files:** Modify `src/world_editor/ui/WorldEditorImGui.cpp` (menu), `WorldEditorShell`

- [ ] **Step 1** — Nouveau / Ouvrir / Enregistrer / Enregistrer sous → actions des blocs A. « Ouvrir » liste les zones (`RefreshAvailableMaps`). À la fermeture, `Config::SaveToFile` persiste préférences + layout docks. Documenter chaque handler.
- [ ] **Step 2** — Build CI.
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): menu Fichier (Nouveau/Ouvrir/Enregistrer) + persistance layout"`

**➡️ Fin G/H = PR #4.**

---

## BLOC F — Viewport 3D dans le panneau (le plus risqué, en dernier)

⚠️ **Ne pas modifier** `frontFace`/`cullMode` du terrain (garde anti-régression « terrain invisible », CLAUDE.md). Repli : conserver le rendu plein écran si instable.

### Task F1 : EditorViewportRenderTarget complet

**Files:** Modify `src/world_editor/render/EditorViewportRenderTarget.{h,cpp}`

- [ ] **Step 1** — Lire l'état actuel + la passe `SceneColor_LDR`. Allouer image + sampler + descriptor ImGui (`ImGui_ImplVulkan_AddTexture`). Redimensionnable. Documenter (contraintes thread/timing : alloc avant `ImGui_ImplVulkan_Init`, free au resize).
- [ ] **Step 2** — Build CI.
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): EditorViewportRenderTarget (render-to-texture dock)"`

### Task F2 : Passe FrameGraph SceneColor → render target + ScenePanel

**Files:** Modify `src/client/app/Engine.cpp` (passe de copie), `src/world_editor/panels/ScenePanel.{h,cpp}`

- [ ] **Step 1** — Ajouter une passe copiant `SceneColor_LDR` → image du render target ; `ScenePanel` affiche la texture via `ImGui::Image`. Caméra/picking en coordonnées **locales au panneau**. Repli plein écran si le render target invalide.
- [ ] **Step 2** — Build CI + vérif manuelle (la 3D apparaît dans le dock).
- [ ] **Step 3 — Commit** : `git commit -m "feat(editor): viewport 3D dans le dock ScenePanel (+ repli plein ecran)"`

**➡️ Fin F = PR #5.**

---

## Auto-revue (réalisée)

- **Couverture spec** : Bloc A (A1-A5) ↔ §4 Bloc A ; B (B1-B3) ↔ Bloc B ; C/D/E ↔ Blocs C/D/E ; G/H ↔ Blocs G/H ; F ↔ Bloc F ; tests ↔ Bloc I (répartis par tâche). ✔
- **Placeholders** : les « Note d'exécution » signalent les endroits où le code exact dépend d'API à lire in situ (TerrainChunk, WaterDocument, Log, FrameGraph) — volontaire car aucune compilation locale ; ce ne sont pas des TODO de plan mais des points d'ancrage de lecture. Les interfaces nouvelles (InitFlatZone, EditorSelection, EditorSceneModel, SetEntityTransformCommand, EditorLogSink, Config::SaveToFile) sont définies concrètement.
- **Cohérence des types** : `EntityId`/`EntityKind` définis en B1, réutilisés identiquement en B2/C1/D2 ; `InitFlatZone(int,float)` cohérent A1↔A3 ; `SaveDirtyToDisk(cfg)` API réelle (TerrainDocument.h:53). ✔
- **Risques** : Bloc A = alignement résolution/échelle (cœur) ; Bloc F = render-to-texture (isolé en dernier, repli). ✔

## Notes d'exécution autonome

- **CI = compilateur** : chaque tâche se termine par un push ; corriger les erreurs CI avant la tâche suivante. Durée CI Windows ≈ 30 min.
- **Ordre des PR** : A → B → C/D/E → G/H → F. CI verte avant d'enchaîner. Lock-step non requis (client uniquement).
- **Convention** : commentaires FR, identifiants EN, PascalCase pour le nouveau code, doc `///` systématique sur toute fonction éditeur ajoutée/modifiée.
