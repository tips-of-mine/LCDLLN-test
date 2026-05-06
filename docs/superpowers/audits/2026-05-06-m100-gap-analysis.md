# M100 — Audit & Gap Analysis

> Audit fichier-par-fichier des 34 tickets M100 contre le code existant.
> Sortie de l'étape 1 du plan d'exécution
> [docs/superpowers/specs/2026-05-06-m100-execution-design.md](../specs/2026-05-06-m100-execution-design.md).
>
> **Date :** 2026-05-06 · **Repo :** LCDLLN-test · **Branche :** claude/upbeat-hermann-17ef28
>
> Les 34 specs ont été produits dans le commit `2050118` sans aucun code C++.
> L'éditeur de monde existe déjà côté code ; cet audit recense pour chaque
> ticket ce qui est déjà fait, ce qui manque, et l'effort résiduel.
>
> Les 10 sections de Phase 2 sont produites par des sous-agents en parallèle,
> consolidées en main session, complétées par le tableau récap (Section 1),
> les flags transverses (Section 3) et les risques (Section 4).

## 1. Tableau récapitulatif

| Ticket  | Phase | Verdict | Effort | Bloque       | Notes |
|---------|-------|---------|--------|--------------|-------|
| M100.1  | 1     | partiel | L      | —            | refactor shell, namespace `engine::editor::world` à créer |
| M100.2  | 1     | vide    | M      | —            | CommandStack absent, bloque tous les Tools undoables |
| M100.3  | 1     | partiel | S      | —            | sources `tools/zone_builder/*` à extraire en `lib/` |
| M100.4  | 1     | vide    | M      | —            | EditorCameraController absent |
| M100.5  | 2     | vide    | L      | round-trip   | TerrainChunk 257² float absent (legacy R16 global) |
| M100.6  | 2     | vide    | L      | —            | sculpt brushes existent mais sur heightmap globale |
| M100.7  | 2     | vide    | M      | —            | aucun stamp PNG ni générateur procédural |
| M100.8  | 2     | vide    | M      | —            | TerrainLodChain async absent |
| M100.9  | 3     | partiel | L      | round-trip   | TerrainSplatting 4 layers existe (vs 8 spec, magic SLAP vs SLAT) |
| M100.10 | 3     | partiel | L      | —            | PaintSplat existe sans Command/Undo |
| M100.11 | 3     | vide    | L      | **PIVOT**    | SurfaceQueryService inexistant — 6 tickets aval bloqués |
| M100.12 | 3     | vide    | XL     | round-trip   | aucune passe `EditorOverlayPass`, aucun proxy collision |
| M100.13 | 4     | vide    | L      | —            | WaterSurfaces (lacs/rivières) inexistant |
| M100.14 | 4     | vide    | L      | —            | WaterPass FrameGraph absent (legacy WaterRenderer M37) |
| M100.15 | 4     | vide    | M      | —            | bloqué par M100.11 + M100.13 absents |
| M100.16 | 4     | vide    | L      | round-trip   | HazardVolumes/HazardSimulator absents |
| M100.17 | 5     | vide    | L      | —            | PlacementTool/PropInstances absents |
| M100.18 | 5     | vide    | XL     | —            | FoliageLibrary 12 catégories + Poisson disk + GPU culling foliage |
| M100.19 | 5     | vide    | L      | —            | Forest/Field tools (bloqué par M100.18) |
| M100.20 | 5     | vide    | M      | —            | WindParams/WindZones/WindSystem absents |
| M100.21 | 5     | vide    | S      | perf budget  | EntityInfluenceCollector — budget < 0.5 ms / 100k |
| M100.22 | 6     | vide    | XL     | —            | VolumetricFogPass froxel 160×88×64 absent |
| M100.23 | 6     | vide    | L      | —            | AtmosphereZones polygonales absentes |
| M100.24 | 6     | vide    | L      | —            | SunCurve data-driven (DayNightCycle keyframes-only) |
| M100.25 | 7     | vide    | M      | **réseau**   | SeasonClock + opcode 49 (suggéré) `kOpSeasonBroadcast` |
| M100.26 | 7     | vide    | L      | **réseau**   | WeatherSystem M38.2 à refactorer ; opcode 50 (suggéré) |
| M100.27 | 7     | vide    | L      | round-trip   | ShadeMap `shade.bin` 64² R8 par chunk |
| M100.28 | 7     | vide    | M      | round-trip   | Zones polygonales typées (≠ ZoneTransitions M22) |
| M100.29 | 8     | partiel | L      | —            | route ad-hoc 2D + JSON existe (à migrer en spline 3D bin) |
| M100.30 | 8     | vide    | L      | —            | bloqué par M100.29 + M100.17 |
| M100.31 | 8     | vide    | M      | —            | Hamlet generator (bloqué par M100.17) |
| M100.32 | 9     | vide    | L      | **réseau**   | opcodes 51/52/53 (suggérés) — décalage vs M100.25/26 |
| M100.33 | 10    | vide    | L      | —            | bloqué par M100.11/.16/.26/.27 absents |
| M100.34 | 10    | vide    | XL     | round-trip   | test round-trip global (12 writers + 12 loaders) |

**Synthèse cumulée :** 0 done · 5 partiels · 29 vides. Effort total : 2 S + 9 M + 19 L + 4 XL ≈ ~30 000–40 000 lignes C++ + tests + assets + shaders + JSON.

## 2. Tickets par phase

### Phase 1 — Fondations

#### M100.1 — World Editor Bootstrap

**Phase** : 1
**Dépendances** : M43.4, M19.4
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/WorldEditorShell.h` | absent | namespace `engine::editor::world` inexistant |
| `engine/editor/world/WorldEditorShell.cpp` | absent | — |
| `engine/editor/world/IPanel.h` | absent | aucune interface IPanel actuellement |
| `engine/editor/world/panels/ScenePanel.h/.cpp` | absent | — |
| `engine/editor/world/panels/InspectorPanel.h/.cpp` | absent | — |
| `engine/editor/world/panels/AssetBrowserPanel.h/.cpp` | absent | — |
| `engine/editor/world/panels/OutlinerPanel.h/.cpp` | absent | — |
| `engine/editor/world/panels/ConsolePanel.h/.cpp` | absent | — |
| `engine/editor/world/panels/ToolPropertiesPanel.h/.cpp` | absent | — |
| `engine/core/LogCategory.h` (modif : add `EditorWorld`) | partiel | catégorie absente du repo (grep zéro résultat) ; le projet utilise `engine/core/Log.h` |
| `engine/Engine.cpp` (modif : flag `--editor-world`) | partiel | l'éditeur existe via `--world-editor` (tiret inverse) ; `editor.world.enabled` non implémenté |
| `engine/editor/EditorMode.h/.cpp` (accesseur `IsWorldEditorWorld()`) | partiel | fichiers existent mais sans accesseur dédié |
| `config.json` (section `editor.world`) | absent | grep `editor.world` / `editor_world` → 0 |
| `CMakeLists.txt` (entrées WorldEditorShell + panels) | absent | — |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/WorldEditorShellTests.cpp` | absent | seuls existent `engine/editor/tests/{TexturePreviewCacheTests,WorldEditorSessionTests}.cpp` |

**Verdict** : ☐ done · ☑ partiel · ☐ vide
**Effort estimé** : L
**Risques** : `WorldEditorImGui.cpp` (existant) implémente déjà un menu bar et un dockspace (`WorldEditorDockSpaceV2`) — risque de doublon/conflit conceptuel avec `WorldEditorShell`. Flag CLI à harmoniser (`--world-editor` vs `--editor-world`).

#### M100.2 — Command Stack & Undo/Redo

**Phase** : 1
**Dépendances** : M100.1
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/CommandStack.h` | absent | grep `CommandStack\|ICommand` sous `engine/` → 0 |
| `engine/editor/world/CommandStack.cpp` | absent | — |
| `engine/editor/world/panels/HistoryPanel.h/.cpp` | absent | — |
| `engine/editor/world/WorldEditorShell.h/.cpp` (modif Ctrl+Z/Y) | absent | dépend de M100.1 (lui-même absent) |
| `config.json` (`editor.world.undo.capacity`, `editor.world.undo.maxBytes`) | absent | — |
| `CMakeLists.txt` (entrées CommandStack + HistoryPanel) | absent | — |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/CommandStackTests.cpp` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Aucun blocage technique propre, mais bloqué par M100.1 (le shell n'existe pas encore pour héberger `m_commandStack` et brancher les raccourcis).

#### M100.3 — Zone Builder Library Extraction

**Phase** : 1
**Dépendances** : M100.1
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `tools/zone_builder/lib/CMakeLists.txt` | absent | dossier `tools/zone_builder/lib/` inexistant |
| `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` | partiel | source originelle existe en `tools/zone_builder/ChunkPackageWriter.h` (non re-exportée) |
| `tools/zone_builder/lib/Public/zone_builder/LayoutImporter.h` | partiel | source en `tools/zone_builder/LayoutImporter.h` |
| `tools/zone_builder/lib/Public/zone_builder/JsonDocument.h` | partiel | source en `tools/zone_builder/JsonDocument.h` |
| `tools/zone_builder/lib/Public/zone_builder/GltfImporter.h` | partiel | source en `tools/zone_builder/GltfImporter.h` |
| `tools/zone_builder/CMakeLists.txt` (modif : link `zone_builder_lib`) | absent | actuel : `add_executable(zone_builder ...)` compile les `.cpp` directement |
| `tools/zone_builder/main.cpp` (modif : `#include <zone_builder/...>`) | partiel | fichier existe, includes actuels relatifs |
| `CMakeLists.txt` racine (`add_subdirectory(tools/zone_builder/lib)`) | absent | grep `zone_builder_lib` → 0 hors tickets |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `tools/zone_builder/lib/tests/RoundtripTests.cpp` | absent | — |
| `scripts/test_zone_builder_baseline.sh` | absent | non vu |

**Verdict** : ☐ done · ☑ partiel · ☐ vide
**Effort estimé** : S
**Risques** : Risque mineur de régression sur la baseline `zone_0/` lors du déplacement des `.cpp` vers le target statique ; nécessite un test bit-à-bit pour valider la non-régression.

#### M100.4 — Editor Camera Modes

**Phase** : 1
**Dépendances** : M100.1
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/EditorCameraController.h` | absent | grep `EditorCameraController\|EditorCameraMode` → 0 dans le code |
| `engine/editor/world/EditorCameraController.cpp` | absent | — |
| `engine/editor/world/panels/ScenePanel.h/.cpp` (modif) | absent | ScenePanel pas encore créé (M100.1) |
| `engine/editor/world/WorldEditorShell.cpp` (modif Numpad 1/3/7) | absent | shell pas créé |
| `config.json` (`editor.world.camera.lastMode`, `editor.world.camera.fpsSpeed`) | absent | — |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/EditorCameraControllerTests.cpp` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Bloqué par M100.1 (panneau Scene). Une caméra FPS WASD existe déjà côté gameplay/runtime mais pas de contrôleur éditeur dédié multi-mode — pas de réutilisation directe pour Orbital/TopDown.

### Phase 2 — Terrain

#### M100.5 — Heightmap Data Structure

**Phase** : 2
**Dépendances** : M100.3, M100.4
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/terrain/TerrainChunk.h` | absent | Répertoire `engine/world/terrain/` inexistant. |
| `engine/world/terrain/TerrainChunk.cpp` | absent | — |
| `engine/world/terrain/TerrainChunkLoader.h/.cpp` | absent | — |
| `engine/world/terrain/TerrainMeshBuilder.h/.cpp` | absent | Pas de `TerrainMeshBuilder` au sens M100 ; existe un `TerrainMesh` global non-chunkifié dans `engine/render/terrain/`. |
| `engine/editor/world/TerrainDocument.h/.cpp` | absent | Répertoire `engine/editor/world/` inexistant. |
| `engine/world/StreamCache.cpp` (modif `LoadTerrainChunk`) | partiel | Fichier existe, aucune méthode `LoadTerrainChunk` ni référence à `terrain.bin`. |
| `engine/world/StreamingScheduler.cpp` (modif) | partiel | Fichier existe, pas de `terrain.bin` dans le set requis. |
| `engine/render/GeometryPass.cpp` (modif) | partiel | Fichier existe, pas de drawcall mesh-terrain par chunk. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif) | partiel | Fichier existe sous `tools/zone_builder/ChunkPackageWriter.{h,cpp}` (pas dans `lib/`). Pas de `WriteTerrainChunk`. |
| `engine/render/terrain/HeightmapLoader.h` | partiel | Présent mais format R16 single-terrain (`HAMP`, `0x504D4148`), incompatible avec layout `TRRN` 257² float spécifié. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/terrain/tests/TerrainChunkTests.cpp` | absent | — |
| `engine/world/terrain/tests/TerrainParityTests.cpp` | absent | Test round-trip éditeur ↔ client manquant. |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Le `HeightmapLoader` legacy R16 + `TerrainRenderer` global cohabite avec le nouveau modèle par chunk → coexistence pendant la transition (mode "couche au-dessus"). Index 32-bit obligatoire (257² > 65k) à câbler dans `GeometryPass`. Format binaire `OutputVersionHeader` à respecter.

#### M100.6 — Terrain Sculpting Brushes

**Phase** : 2
**Dépendances** : M100.2, M100.5
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/TerrainSculptTool.h/.cpp` | absent | Répertoire inexistant. |
| `engine/editor/world/TerrainSculptCommand.h/.cpp` | absent | Dépend de `ICommand`/`CommandStack` (M100.2) qui n'existent pas. |
| `engine/editor/world/TerrainBrush.h/.cpp` | partiel | Existe `engine/render/terrain/TerrainEditingTools.{h,cpp}` avec `BrushOp` (Raise/Lower/Smooth/Flatten) + `BrushParams` mais : (1) pas de `Noise` (Simplex2D), (2) opère sur le heightmap global R16, pas sur `TerrainChunk` 257² float, (3) pas de `TerrainBrushMode` enum. |
| `engine/editor/world/TerrainRaycast.h/.cpp` | absent | Pas de raycast caméra → heightmap dédié pour M100. |
| `engine/editor/world/panels/ToolPropertiesPanel.cpp` (modif) | absent | Aucun `ToolPropertiesPanel` dans le repo. |
| `engine/editor/world/WorldEditorShell.cpp` (modif) | absent | Aucun `WorldEditorShell`. Existe un `WorldEditorSession`/`WorldEditorImGui` qui couvre une partie. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/TerrainSculptTests.cpp` | absent | Aucun test sculpt M100. |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Bloqué par M100.2 (CommandStack) ET M100.5 (TerrainChunk). Les brosses existantes n'ont pas de Noise, pas de delta sparse, pas de merge `mergeKey`, pas de coutures inter-chunks. Réutilisation possible côté kernels (smoothstep/falloff) mais pipeline de delta + multi-chunk seam à écrire from scratch.

#### M100.7 — Terrain Stamps & Procedural Generators

**Phase** : 2
**Dépendances** : M100.6
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/TerrainStampTool.h/.cpp` | absent | — |
| `engine/editor/world/TerrainStampCommand.h/.cpp` | absent | Dépend de M100.2 + M100.6. |
| `engine/editor/world/StampLibrary.h/.cpp` | absent | Aucune lib stamp PNG. |
| `engine/editor/world/ProceduralStampGenerators.h/.cpp` | absent | Pas de Mountain/Valley/Crater. |
| `engine/editor/world/panels/ToolPropertiesPanel.cpp` (modif) | absent | Voir M100.6. |
| `engine/editor/world/WorldEditorShell.cpp` (modif raccourci `N`) | absent | Voir M100.6. |
| Répertoire `assets/editor/stamps/` | absent | Aucun PNG 16-bit stamp dans le repo. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/TerrainStampTests.cpp` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Cascade de dépendances (M100.2 → M100.5 → M100.6 → M100.7). Lecture PNG 16-bit grayscale (stb_image en mode 16-bit ou libpng) à confirmer côté chaîne d'inclusion. Preview live (overlay ambre) sans modifier le `TerrainChunk` tant qu'`Apply` non cliqué.

#### M100.8 — Terrain LOD Regeneration

**Phase** : 2
**Dépendances** : M100.5
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/terrain/TerrainLodChain.h/.cpp` | absent | Répertoire inexistant. À ne pas confondre avec `kTerrainLodCount` du `TerrainMesh` global existant (système distinct). |
| `engine/world/terrain/TerrainLodWorker.h/.cpp` | absent | Pas de thread pool LOD éditeur. |
| `engine/world/terrain/tests/TerrainLodTests.cpp` | absent | — |
| `engine/world/terrain/TerrainChunk.h` (modif `struct TerrainLod`) | absent | Le fichier parent n'existe pas (M100.5). |
| `engine/world/StreamCache.cpp` (modif `terrain_lods.bin`) | partiel | Fichier existe, aucune référence à `terrain_lods.bin`. |
| `engine/editor/world/TerrainDocument.cpp` (modif `OnCommit` enqueue) | absent | TerrainDocument n'existe pas. |
| `engine/world/terrain/TerrainMeshBuilder.cpp` (modif `BuildLodMesh` + skirt) | absent | TerrainMeshBuilder n'existe pas. |
| `engine/render/LodConfig.cpp` | partiel | Existe (`engine/world/LodConfig.{h,cpp}`), modification éventuelle non requise selon spec. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/terrain/tests/TerrainLodTests.cpp` (Test_GenerateLodChain_BoxFilterMatch, Test_SaveLoadLods_Roundtrip, Test_ParityWithClient_Identical, Test_LodWorker_AsyncDoesNotBlockMain) | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Bloqué tant que M100.5 n'a pas posé `TerrainChunk` + `TerrainMeshBuilder`. Contrat « main thread jamais bloqué > 1 ms » + worker thread pool demande un design async propre. Skirt géométrique (2 m sous bord) dans le mesh builder sans la persister. Risque d'ambiguïté entre l'ancien système `kTerrainLodCount = 5` et le nouveau `lodCount = 3` persisté.

### Phase 3 — Splat / Surfaces / Collision

#### M100.9 — Splat Map System

**Phase** : 3
**Dépendances** : M100.5
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/terrain/SplatMap.h/.cpp` | absent | dossier `engine/world/terrain/` inexistant |
| `engine/world/terrain/LayerPalette.h/.cpp` | absent | aucun `LayerPalette` dans le code |
| `engine/render/shaders/terrain.vert` | absent | dossier `engine/render/shaders/` inexistant |
| `engine/render/shaders/terrain.frag` | absent | idem |
| `engine/world/terrain/tests/SplatMapTests.cpp` | absent | aucune cible `splat_map_tests` |
| `assets/terrain/layer_palette.json` | absent | aucun JSON palette dans `assets/` |
| `engine/world/StreamCache.h/.cpp` (modif `LoadSplatMap`) | partiel | StreamCache existe mais aucune méthode `LoadSplatMap` |
| `engine/world/terrain/TerrainMeshBuilder.cpp` | absent | classe inexistante (le terrain utilise `TerrainMesh.cpp`) |
| `engine/render/GeometryPass.cpp` (drawcall terrain 8 layers) | partiel | GeometryPass.cpp existe ; pas de pipeline 8 layers |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (`WriteSplatMap`) | partiel | à confirmer ; `tools/` présent mais pas de `WriteSplatMap` |

Existant connexe (à 4 layers, RGBA8, pas conforme au format spec 257×257×8) : `engine/render/terrain/TerrainSplatting.{h,cpp}`, `engine/render/terrain/SplatSampling.{h,cpp}`, `engine/render/terrain/TerrainEditingTools.cpp` (offre `SaveSplatMap` mais format différent, magic `0x50414C53` "SLAP" vs spec `0x54414C53` "SLAT").

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_SplatMap_MakeUniform_SumIs255` | absent | — |
| `Test_SaveLoad_Roundtrip` | absent | — |
| `Test_LayerPalette_LoadJson` | absent | — |
| `Test_Parity_EditorWritesClientReadsIdentical` | absent | — |
| `Test_Shader_8LayersBlendVisualBaseline` | absent | — |

**Verdict** : ☐ done · ☑ partiel · ☐ vide
**Effort estimé** : L
**Risques** : Conflit format avec l'existant 4 layers (RGBA `TerrainSplatting`, magic SLAP `0x50414C53`) vs spec 8 layers + magic SLAT `0x54414C53`. Le ticket exige 257×257 ; le code actuel est 1024×1024 RGBA. Migration requise (réécrire shader, format binaire et invariant somme=255). Vérifier compat `splat.bin` historique ou bumper version + migration.

#### M100.10 — Splat Painting Brushes

**Phase** : 3
**Dépendances** : M100.2, M100.9
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/SplatPaintTool.h/.cpp` | absent | dossier `engine/editor/world/` inexistant |
| `engine/editor/world/SplatPaintCommand.h/.cpp` | absent | aucun `ICommand` dans la base |
| `engine/editor/world/SplatRules.h/.cpp` | absent | — |
| `engine/editor/world/tests/SplatPaintTests.cpp` | absent | — |
| `engine/editor/world/panels/ToolPropertiesPanel.cpp` | absent | aucun panneau `ToolPropertiesPanel` |
| `engine/editor/world/WorldEditorShell.cpp` | absent | shell inexistant — actuellement `WorldEditorSession`/`WorldEditorImGui` |
| `engine/editor/world/TerrainDocument.cpp` | absent | il existe `WorldMapEditDocument.h` mais pas `TerrainDocument` |

Existant connexe : `TerrainEditingTools::PaintSplat` — peinture splat directe, **sans Command/Undo**, sans auto-rules, sans coutures inter-chunks, et sur 4 layers (RGBA) au lieu de 8.

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_ManualBrush_PreservesSum255` | absent | — |
| `Test_AutoRules_PaintsOnlyMatchingCells` | absent | — |
| `Test_Falloff_RadialMonotone` | absent | — |
| `Test_CrossChunk_PreservesSeam` | absent | — |
| `Test_Stroke_OneHistoryEntry` | absent | — |

**Verdict** : ☐ done · ☑ partiel · ☐ vide
**Effort estimé** : L
**Risques** : Dépend de M100.2 (CommandStack) qui n'existe pas (aucun `ICommand`). Dépend de M100.9 (8 layers). La logique de coutures inter-chunks suppose que le streaming chunk est en place (cf. M100.5). Refactor du `TerrainEditingTools::PaintSplat` existant en commande undoable.

#### M100.11 — Surface Material System & SurfaceQuery (client)

**Phase** : 3
**Dépendances** : M100.9
**Pivot/réseau** : PIVOT M100.11

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/surface/SurfaceType.h` | absent | dossier `engine/world/surface/` inexistant |
| `engine/world/surface/SurfaceTable.h/.cpp` | absent | aucun loader JSON `surface_table` |
| `engine/world/surface/SurfaceQueryService.h/.cpp` | absent | aucun symbole `SurfaceQuery*` dans le code |
| `engine/editor/world/panels/SurfaceTablePanel.h/.cpp` | absent | dossier `panels/` inexistant |
| `engine/world/surface/tests/SurfaceQueryTests.cpp` | absent | aucune cible `surface_query_tests` |
| `assets/gameplay/surface_table.json` | absent | dossier `assets/gameplay/` inexistant |
| `engine/world/terrain/LayerPalette.h/.cpp` (modif `GetSurfaceTypeForLayer`) | absent | dépend de M100.9 |
| `engine/gameplay/CharacterController.h/.cpp` (intégration) | partiel | controller existe et expose bien `WaterQuery` etc., aucun hook surface — vitesse appliquée sans `SurfaceQueryService` |
| `engine/world/StreamCache.cpp` | existe | rien à modifier d'après spec, mais `LoadSplatMap` à ajouter via M100.9 |
| Diff CMake : split `engine_core` / `engine_core_server` | absent | actuellement `engine_core` unique, target serveur lie engine_core direct |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_SurfaceTable_LoadJson_Has13Entries` | absent | — |
| `Test_SurfaceTable_BaseSpeed_DirtIs1p0` | absent | — |
| `Test_Query_DominantLayerWins` | absent | — |
| `Test_Query_DefaultModifiersAreNeutral` | absent | — |
| `Test_CharacterController_AppliesBaseSpeed` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** :
- **Contrat enum `SurfaceType`** : aucune définition existante (grep `SurfaceType` → uniquement docs/tickets, zéro hit code). Pas de conflit sur le nom ni l'ordre — création ex-nihilo, gel d'ordre à respecter (Dirt=0…Bridge=12).
- **`surface_table.json`** : pas de fichier homonyme existant.
- **`SurfaceQuery::At(...)`** : la spec définit `SurfaceQueryService::Query(Vec3)` ; la formulation du prompt directeur (`SurfaceQuery::At(...)`) diffère de la spec ticket. Cohérence à figer côté implem (au moins un alias `At`/`Query`) sinon les 6 tickets aval (M100.15, M100.16, M100.19, M100.26, M100.27, M100.33) appelleront un nom différent du code.
- **Anti-duplication serveur** : M100.11 introduit le split CMake `engine_core` / `engine_core_server`. À ce jour, `server_app` lie `engine_core` directement. Toucher la CMake racine impactera **tous** les tickets aval et le build serveur Linux.
- **Hook gameplay** : `CharacterController` actuel applique vitesse via `m_cfg` sans hook surface. La spec autorise un stub paramétrable mais l'intégration finale (M100.26 modifiers météo, M100.33 footsteps) suppose que `Query` retourne déjà `(SurfaceType, SurfaceModifiers)` dès M100.11, modifiers neutres.
- **Dépendance M100.9** : `SurfaceQueryService` lit la splat-map via `StreamCache::LoadSplatMap` qui n'existe pas. Sans M100.9 livré, M100.11 ne peut pas aboutir.
- **Couplage water existant** : `CharacterController` a déjà sa propre logique water (`WaterQuery`, `surfaceY`, `waterSurfaceBreachingDepth`). À l'arrivée de `SurfaceType::ShallowWater`/`DeepWater`, il faudra définir laquelle prime (water existante reste autoritaire pour la flottabilité ; `SurfaceType` pour vitesse/audio).

#### M100.12 — Collision Proxy System

**Phase** : 3
**Dépendances** : M100.1
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/collision/CollisionProxy.h/.cpp` | absent | dossier `engine/world/collision/` inexistant |
| `engine/world/collision/AutoFitProxy.h/.cpp` | absent | aucun helper V-HACD |
| `engine/editor/world/panels/CollisionEditorPanel.h/.cpp` | absent | — |
| `engine/render/EditorOverlayPass.h/.cpp` | absent | grep `EditorOverlayPass` → 0 hit code |
| `engine/render/shaders/editor_overlay.vert/.frag` | absent | dossier shaders/ inexistant |
| `engine/world/collision/tests/CollisionProxyTests.cpp` | absent | aucune cible `collision_proxy_tests` |
| `engine/render/FrameGraph.cpp` (modif) | existe | présent, à modifier pour enregistrer la passe overlay |
| `engine/editor/world/panels/AssetBrowserPanel.cpp` | absent | dossier panels/ inexistant |
| `engine/editor/world/PlacementOverlapInfo.h/.cpp` | absent | — |

Aucun symbole `CollisionProxy`/`physics`/`solveur` côté gameplay.

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_CollisionProxy_RoundtripCapsule` | absent | — |
| `Test_CollisionProxy_RoundtripConvexHull` | absent | — |
| `Test_CollisionProxy_RoundtripTriMesh` | absent | — |
| `Test_AutoFit_TallSlim_PicksCapsule` | absent | — |
| `Test_AutoFit_Compact_PicksConvexHull` | absent | — |
| `Test_AutoFit_StaticComplex_PicksTriMesh` | absent | — |
| `Test_OverlayPass_NotRegisteredWhenNoEditor` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : XL
**Risques** : Aucun moteur physique runtime visible côté client (le critère « le solveur runtime consomme les `.collision.bin` » suppose un solveur à brancher — non audité, peut-être hors phase). Dépendance V-HACD (Volumetric Hierarchical Approximate Convex Decomposition — décomposition automatique d'un mesh complexe en convex hulls) à wrapper (lib externe non vue dans `external/` à confirmer). `EditorOverlayPass` doit s'enregistrer **uniquement** si éditeur actif : intégration `FrameGraph` non triviale. Effort XL car couvre : 3 sérializers + auto-fit V-HACD + UI panel + passe Vulkan dédiée + intégration FrameGraph + tests round-trip.

### Phase 4 — Hydrologie & Hazards

#### M100.13 — Water Surfaces (Lakes & Rivers)

**Phase** : 4
**Dépendances** : M100.5, M100.6
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/water/WaterSurfaces.h/.cpp` | absent | Dossier `engine/world/water/` inexistant. |
| `engine/world/water/WaterMeshBuilder.h/.cpp` | absent | Aucune triangulation Delaunay 2D / ribbon. |
| `engine/editor/world/LakeTool.h/.cpp` | absent | Dossier `engine/editor/world/` inexistant. |
| `engine/editor/world/RiverTool.h/.cpp` | absent | Idem. |
| `engine/editor/world/WaterDocument.h/.cpp` | absent | Idem. |
| `engine/world/StreamCache.cpp` (modif `LoadWater`) | partiel | Fichier existe (`engine/world/StreamCache.cpp`) mais aucun symbole `LoadWater` / référence à `water.bin`. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif `WriteWater`) | partiel | Fichier existe (`tools/zone_builder/ChunkPackageWriter.cpp`, chemin légèrement différent du spec) mais aucun `WriteWater`. |
| Constantes `kWaterMagic = 0x52544157`, `kWaterVersion = 1` | absent | Aucune occurrence. |
| Structures `LakeInstance`, `RiverInstance`, `RiverNode`, `WaterScene` | absent | — |
| `SaveWaterBin` / `LoadWaterBin` | absent | — |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/water/tests/WaterSurfacesTests.cpp` | absent | Dossier inexistant. |
| `Test_LakeMesh_Triangulation_AllVerticesInside` | absent | — |
| `Test_River_RibbonHas2NPlus2Vertices` | absent | — |
| `Test_SaveLoad_Roundtrip` | absent | — |
| `Test_FlowDirection_AlignsWithSlope` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Delaunay 2D contraint au polygone CCW, robustesse aux polygones concaves/auto-intersectants ; ribbon rivière qui doit suivre la heightmap (couplage M100.5) ; format binaire à versionner avec `OutputVersionHeader` ; intégration `StreamCache` + `ChunkPackageWriter` qui n'ont actuellement aucun chemin water.

#### M100.14 — Water Render Pass

**Phase** : 4
**Dépendances** : M100.13
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/render/WaterPass.h/.cpp` | absent | Aucun fichier `WaterPass*`. Existe `engine/render/WaterRenderer.{h,cpp}` (M37 — plan d'eau plat unique avec reflection/refraction RT, foam, caustics) mais pas une passe FrameGraph qui consomme `WaterScene`. |
| `engine/render/shaders/water.vert` | absent | Dossier `engine/render/shaders/` ne contient aucun `water.*`. |
| `engine/render/shaders/water.frag` | absent | Idem. |
| `engine/render/FrameGraph.cpp` (insérer `WaterPass` après `DecalPass`) | partiel | Fichier existe ; aucune occurrence de `WaterPass` ou `WaterRenderer` dedans. |
| `engine/Engine.cpp` (config `WaterParams` depuis `WaterScene`) | partiel | `WaterParams` existe (struct M37) mais aucune `WaterScene` à brancher (M100.13 absent). |
| Constant buffer GLSL `WaterParams` (set=1 binding=0, layout M100.14) | absent | Le `WaterParams`/`WaterPushConstants` actuels (M37) ont une signature différente. |
| SSR raymarch (max 32 steps, fallback skybox) | absent | `WaterRenderer` actuel fait reflection RT par caméra mirrorée Y, pas de SSR. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/render/tests/WaterPassTests.cpp` | absent | — |
| `Test_WaterPass_RegistersInFrameGraph` | absent | — |
| `Test_WaterPass_ReadsSceneColorAfterDecals` | absent | — |
| `Test_WaterPass_GoldenImage` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : SSR (Screen-Space Reflections — réflexions calculées par raymarch sur le buffer de profondeur) mince à écrire (raymarch + fallback skybox) ; recâblage du `FrameGraph` après `Decals` et avant `VolumetricFog`/`TAA` ; refonte du `WaterParams` UBO pour matcher la spec (flowDirection, bottomColor, turbidity) — incompatible avec celui de `WaterRenderer` M37 ; décision à prendre : remplacer `WaterRenderer` ou faire vivre les deux ; couplage strict M100.13.

#### M100.15 — Water Surface Hook (Wading & Swimming)

**Phase** : 4
**Dépendances** : M100.11, M100.13
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/water/WaterSampler.h/.cpp` | absent | Dossier inexistant. |
| `engine/world/surface/SurfaceQueryService.h/.cpp` (modif) | absent | Dossier `engine/world/surface/` inexistant ; M100.11 (dépendance dure) non implémenté. |
| `engine/gameplay/CharacterController.h/.cpp` (modif `m_swimming`, transitions hystérésis) | partiel | Fichier existe et expose déjà `swimUpPressed`/`swimDownPressed`/paramètres « Swimming » (vestiges M37/UnderwaterPass), mais aucune logique `m_swimming` activée par `SurfaceType::DeepWater`, aucun `splash_water_enter/exit`, aucune hystérésis 1.0/0.7 m. |
| `engine/world/water/WaterSurfaces.h` (ajout `WaterSample { float surfaceY; }`) | absent | Le header n'existe pas. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/surface/tests/WaterHookTests.cpp` | absent | — |
| `Test_Lake_PointInside_ReturnsSurfaceY` | absent | — |
| `Test_River_ProjectionOnSegment` | absent | — |
| `Test_DepthBelow1m_IsShallowWater` | absent | — |
| `Test_DepthAbove1m_IsDeepWater_EntersSwimMode` | absent | — |
| `Test_Hysteresis_ExitAt0p7m` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Bloqué par deux dépendances mortes (M100.11 absent → pas de `SurfaceQueryService`, M100.13 absent → pas de `WaterScene` à sampler). Point-in-polygon 2D (lac) et projection sur spline (rivière). Hystérésis bidirectionnelle 1.0/0.7 m doit être testée explicitement (oscillation à la frontière). Conflit potentiel avec les paramètres swim M37 résiduels.

#### M100.16 — Hazard Volume System

**Phase** : 4
**Dépendances** : M100.11
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/hazard/HazardVolumes.h/.cpp` | absent | Dossier `engine/world/hazard/` inexistant. |
| `engine/world/hazard/HazardSimulator.h/.cpp` | absent | Idem. |
| `engine/editor/world/HazardTool.h/.cpp` | absent | Dossier `engine/editor/world/` inexistant. |
| `engine/editor/world/HazardDocument.h/.cpp` | absent | Idem. |
| `engine/gameplay/CharacterController.h/.cpp` (hooks `OnHazardEnter/Update/Exit`) | partiel | Fichier existe, aucun hook hazard. |
| `engine/render/ThirdPersonCamera.cpp` (compensation `SetGroundOffset`) | absent | Aucun fichier `ThirdPersonCamera*` localisé sur ce périmètre. |
| `engine/world/StreamCache.cpp` (modif `LoadHazards`) | partiel | Fichier existe, aucun symbole hazard. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif `WriteHazards`) | partiel | Fichier existe (chemin réel `tools/zone_builder/ChunkPackageWriter.cpp`), aucun `WriteHazards`. |
| Constantes `kHazardsMagic = 0x5A415748`, `kHazardsVersion = 1` | absent | — |
| Layout binaire `instances/hazards.bin` | absent | — |
| Anti-duplication serveur (exclusion `engine_core_server`) | absent | Rien à exclure tant que le code n'existe pas. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/hazard/tests/HazardTests.cpp` | absent | Dossier inexistant. |
| `Test_Hazards_RoundtripBin` | absent | Test round-trip exigé. |
| `Test_HazardSimulator_SinkRate` | absent | — |
| `Test_HazardSimulator_MashEscape` | absent | — |
| `Test_HazardSimulator_LateralEscape` | absent | — |
| `Test_HazardSimulator_LavaKills3s` | absent | — |
| `Test_HazardSimulator_DeathOnMaxDepth` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : 4 types × 4 modes d'évasion (combinatoire de tests) ; tests temporels exacts (LavaKills3s, mash 10/5s) sensibles au tick sim ; compensation caméra tierce (`SetGroundOffset`) ; exclusion CMake stricte des `.cpp` de la cible serveur (`engine_core_server`) à câbler dès le départ ; round-trip binaire avec champs union box/cylindre. Bloqué par M100.11 absent.

### Phase 5 — Placement & Végétation

#### M100.17 — Easy Placement Tool

**Phase** : 5
**Dépendances** : M100.2, M100.5, M100.12
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/PlacementTool.h/.cpp` | absent | Répertoire `engine/editor/world/` inexistant. |
| `engine/editor/world/PlacementCommand.h/.cpp` | absent | Aucun `ICommand` / `CommandStack` repérable dans le repo. |
| `engine/editor/world/PlacementGhost.h/.cpp` | absent | — |
| `engine/editor/world/PlacementSnapping.h/.cpp` | absent | — |
| `engine/world/instances/PropInstances.h/.cpp` | absent | Aucun dossier `engine/world/instances/`. |
| `engine/editor/world/panels/AssetBrowserPanel.cpp` (modif) | absent | Aucun `AssetBrowserPanel` trouvé. |
| `engine/editor/world/panels/ToolPropertiesPanel.cpp` (modif) | absent | Aucun `ToolPropertiesPanel` trouvé. |
| `engine/world/StreamCache.cpp` (modif `LoadProps`) | partiel | `engine/world/StreamCache.{h,cpp}` existe mais aucune entrée `LoadProps`. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif `WriteProps`) | absent | Aucun match `WriteProps`. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/PlacementTests.cpp` (Single, DragLine, Scatter, RandomRotation, GroundSnap, FaceSnap, Perf Ghost <5ms, Perf Scatter <50ms, Roundtrip props.bin) | absent | Aucun fichier de tests placement. |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Dépend de M100.2 (CommandStack) et M100.12 (Collision Proxies) absents — risque de blocage en cascade. Format `props.bin` versionné à concevoir avec parité éditeur↔client (round-trip + `StreamCache::LoadProps`). Budgets perf ghost <5 ms / commit <16 ms / scatter 50 props <50 ms à valider sur perf-tests CI.

#### M100.18 — Vegetation Library & Density Painting

**Phase** : 5
**Dépendances** : M100.5, M100.17
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/foliage/FoliageLibrary.h/.cpp` | absent | Pas de répertoire `engine/world/foliage/`. |
| `engine/world/foliage/FoliageInstances.h/.cpp` | absent | — |
| `engine/world/foliage/PoissonDiskSampler.h/.cpp` | absent | Aucun match `Poisson`. |
| `engine/editor/world/FoliagePaintTool.h/.cpp` | absent | — |
| `engine/editor/world/FoliagePaintCommand.h/.cpp` | absent | — |
| `engine/editor/world/FoliageDocument.h/.cpp` | absent | — |
| `assets/vegetation/library.json` | absent | Catalogue 12 catégories à créer. |
| `engine/render/GpuDrivenCullingPass.cpp` (modif bucket "foliage") | partiel | Le pass existe (`engine/render/GpuDrivenCullingPass.{h,cpp}`) mais aucun bucket foliage. |
| `engine/world/StreamCache.cpp` (modif `LoadFoliage`) | partiel | Fichier existe, fonction absente. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif `WriteFoliage`) | absent | — |
| `engine/editor/TreeSpeciesCatalog.{h,cpp}` (existant lié) | existe | Pré-amorce : catalogue d'espèces déjà code-level — à articuler avec la library JSON sans dupliquer. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/foliage/tests/FoliageTests.cpp` (LoadsAllCategories, PoissonDisk MinRadius, RuleFilter Slope, Roundtrip, Perf 100k) | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : XL
**Risques** : Volume code important (catalogue, sampler Poisson 2D, doc/state, command, sérialisation, brush, intégration GPU-culling). Risque de duplication avec `TreeSpeciesCatalog` existant — à réutiliser ou migrer. Budget perf 100 k instances @ 60 fps dépend du GPU-driven culling déjà bouclé. Format binaire `instances/foliage.bin` versionné, parité éditeur↔client à tester (round-trip).

#### M100.19 — Procedural Forest & Field Tools

**Phase** : 5
**Dépendances** : M100.11, M100.18
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/ForestTool.h/.cpp` | absent | — |
| `engine/editor/world/FieldTool.h/.cpp` | absent | — |
| `engine/editor/world/ForestRecipe.h/.cpp` | absent | — |
| `engine/editor/world/ForestCommand.h/.cpp` | absent | — |
| `engine/editor/world/FieldCommand.h/.cpp` | absent | — |
| `engine/editor/world/FoliageDocument.h/.cpp` (modif `BulkPaste`) | absent | Document même pas créé en M100.18. |
| `engine/editor/world/TerrainDocument.h/.cpp` (modif `PaintSplatRectangle`) | absent | TerrainDocument introuvable. |
| `assets/terrain/layer_palette.json` (ajout layers wheat/corn) | absent | À vérifier mais aucun match côté assets. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/editor/world/tests/ForestFieldTests.cpp` (DensityMatchesRecipe, RulesFilterSlope, DeterministicWithSeed, RegularSpacing, TagsSplatLayer, SurfaceQueryReturnsWheatField) | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Dépendance dure sur M100.18 (FoliageDocument, FoliageInstances) et M100.11 (Surface table avec tags `WheatField`/`CornField`) — tous deux absents : verrou bloquant. Triangulation polygone, weighted-asset sampling et auto-tag splat (poids 200/255 + redistribution) sont non triviaux. Test `Test_Field_SurfaceQueryReturnsWheatField` croise éditeur+client via `SurfaceQuery`.

#### M100.20 — Vegetation Wind Animation

**Phase** : 5
**Dépendances** : M100.18
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/wind/WindParams.h` | absent | Pas de répertoire `engine/world/wind/`. |
| `engine/world/wind/WindZones.h/.cpp` | absent | — |
| `engine/world/wind/WindSystem.h/.cpp` | absent | — |
| `engine/editor/world/WindZoneTool.h/.cpp` | absent | — |
| `engine/render/shaders/foliage.vert` | absent | Pas de répertoire `engine/render/shaders/` ; aucun shader `.vert` détecté pour foliage. |
| `engine/render/GeometryPass.cpp` (modif bind WindParams) | partiel | `GeometryPass` existe (`engine/render/GeometryPass.{h,cpp}`), aucun binding wind. |
| `engine/Engine.cpp` (update timeSeconds) | partiel | Engine existe, sans hook wind. |
| `engine/editor/world/panels/AtmospherePanel.h/.cpp` (sous-panneau Wind) | absent | Pas d'`AtmospherePanel`. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/wind/tests/WindTests.cpp` (SwayProportionalToHeight, LocalZoneOverridesGlobal, RoundtripWindZonesBin, AtmosphereJsonPreservesWindOnReload) | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Dépend de M100.18 (`foliage.vert`, pipeline foliage) inexistant — bloqué. Constant buffer `WindParams` partagé C++/GLSL doit rester en strict miroir (taille, alignement, ordre). Format binaire `wind_zones.bin` versionné, parité éditeur↔client à tester. Atmosphère JSON et reload live (sliders → uniform).

#### M100.21 — Vegetation Player Interaction Shader

**Phase** : 5
**Dépendances** : M100.20
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/foliage/EntityInfluenceCollector.h/.cpp` | absent | Aucun `EntityInfluence*` dans le repo. |
| `engine/render/shaders/foliage.vert` (modif loop d'influences) | absent | Shader inexistant (cf. M100.20). |
| `engine/render/GpuDrivenCullingPass.cpp` (modif bind SSBO Entities) | partiel | Pass existe, aucun SSBO Entities. |
| `engine/Engine.cpp` (modif frame setup `Update`) | partiel | Engine existe, hook absent. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/foliage/tests/EntityInfluenceTests.cpp` (FiltersByRange30m, TruncatesAt32, FlexionMonotoneWithDistance, Perf <0.5ms 100k) | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : S
**Risques** : Budget perf strict — **< 0.5 ms additionnel sur 100 k instances foliage visibles** (mid-range RTX 2060) : test bloquant, à mesurer avec un harnais GPU-timing. Dépend de l'ensemble de la chaîne foliage (M100.18) + shader vent (M100.20) qui sont vides. SSBO 32 entités avec relaxation 0.5 s côté collector ; risque de rejet de loop GLSL non déroulé sur certains drivers — prévoir variante `count` faible.

### Phase 6 — Atmosphère & Brouillard

#### M100.22 — Volumetric Fog Volumes

**Phase** : 6
**Dépendances** : M100.4
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/render/VolumetricFogPass.h/.cpp` | absent | Aucun fichier `VolumetricFog*`. Aucune occurrence de `VolumetricFogPass` ou `FogParams`. |
| `engine/render/shaders/fog_inject.comp` | absent | Le dossier `engine/render/shaders/` n'existe pas. Tous les shaders sont sous `game/data/shaders/` et il n'y a aucun shader `fog_*`. |
| `engine/render/shaders/fog_scatter.comp` | absent | Idem. |
| `engine/render/shaders/fog_apply.frag` | absent | Idem. |
| `engine/world/atmosphere/FogVolumes.h/.cpp` | absent | Le dossier `engine/world/atmosphere/` n'existe pas. |
| `engine/editor/world/VolumetricFogVolumeTool.h/.cpp` | absent | Le dossier `engine/editor/world/` n'existe pas. |
| `engine/world/atmosphere/tests/FogVolumesTests.cpp` | absent | Aucun test fog. |
| `engine/render/FrameGraph.cpp` (modif : insère `VolumetricFogPass`) | absent | `FrameGraph.cpp` ne contient aucune référence à `Fog`/`fog`. |
| `engine/world/StreamCache.cpp` (modif : `LoadFogVolumes`) | absent | Aucune mention de fog dans `StreamCache.cpp`. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif : `WriteFogVolumes`) | absent | `ChunkPackageWriter.cpp` ne contient pas de `WriteFogVolumes`. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_FogVolumes_RoundtripBin` | absent | — |
| `Test_VolumetricFog_RegistersInFrameGraph` | absent | — |
| `Test_VolumetricFog_GoldenImage` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : XL
**Risques** : passe froxel 160×88×64 lourde (3 sous-passes compute/frag), nouveau dossier shaders, intégration FrameGraph entre `Water` et `TAA`, format binaire versionné, parité éditeur↔client + golden image. Performance ciblée < 1.5 ms.

#### M100.23 — Distance Fog & Height Fog Tuning

**Phase** : 6
**Dépendances** : M100.22
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/atmosphere/AtmosphereZones.h/.cpp` | absent | Aucun fichier `AtmosphereZone*`. |
| `engine/editor/world/AtmosphereZoneTool.h/.cpp` | absent | Pas d'outil polygonal. Le panneau Atmosphere existant (`WorldEditorImGui.cpp` lignes 1108-1192) n'expose que `timeOfDay`/`timeScale`. |
| `engine/render/shaders/fog_apply.frag` (modifié) | absent | Le fichier d'origine n'existe pas (cf. M100.22). |
| `engine/world/AtmosphereSettings.h/.cpp` (existant, modif) | partiel | `AtmosphereSettings` est défini comme **struct** dans `engine/world/ProbeData.h` (lignes 36-41) avec uniquement `sunDirection`, `sunColor`, `ambientColor`. Pas de fichier `.cpp` dédié, pas de champ `distanceFog`/`heightFog`/`version`. |
| `engine/render/VolumetricFogPass.cpp` (modif) | absent | La passe à étendre n'existe pas (cf. M100.22). |
| `engine/editor/world/panels/AtmospherePanel.cpp` (UI 3 sous-panneaux + courbe) | absent | Le panneau actuel est inline dans `WorldEditorImGui.cpp` (ligne 1103+). |
| `engine/world/StreamCache.cpp` (modif : `LoadAtmosphereZones`) | absent | Aucune référence à `AtmosphereZones`. |
| `atmosphere.json` v2 (migration v1→v2) | absent | `game/data/atmosphere.json` est en `"version": 1`. Aucun code de migration. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_AtmosphereJson_V1ToV2Migration` | absent | — |
| `Test_DistanceFog_DensityIncreaseWithDepth` | absent | — |
| `Test_HeightFog_RespectsBaseYAndFalloff` | absent | — |
| `Test_AtmosphereZones_RoundtripBin` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : dépend de M100.22 (passe fog inexistante à étendre). Migration JSON v1→v2 + ajout d'un binaire `atmosphere_zones.bin` polygonal. Refactor probable de `AtmosphereSettings` (sortir de `ProbeData.h` vers `engine/world/atmosphere/`). Transition adoucie 5 m sur les bords de polygones.

#### M100.24 — Sun, Sky & Probes Editor

**Phase** : 6
**Dépendances** : M100.23
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/atmosphere/SunCurve.h/.cpp` | absent | Aucun symbole `SunCurve` ni courbe ToD×ToY 24×4. Le `DayNightCycle` actuel utilise des keyframes hardcodées en interne. |
| `engine/editor/world/SunSkyPanel.h/.cpp` | absent | Aucun fichier ; le panneau Atmosphere actuel (`WorldEditorImGui.cpp`) n'a ni slider `dayOfYear`, ni azimuth/elevation override, ni sun lux, ni courbe éditable. |
| `engine/editor/world/ProbeBakeTrigger.h/.cpp` | absent | Pas d'outil placement/bake côté éditeur. `ProbeData.h/.cpp` définit `ProbeRecord`/`ProbeSet`/`LoadProbeSet` (lecture seule, MVP M11.4) mais aucun bouton "Bake all" ni "Place probe". |
| `engine/world/AtmosphereSettings.h/.cpp` (modif : courbe v3) | partiel | Struct minimaliste dans `ProbeData.h` (cf. M100.23) — aucun support v3, aucune matrice 24×4. |
| `engine/render/DayNightCycle.cpp` (modif : évalue depuis `SunCurve`) | partiel | `DayNightCycle.h/.cpp` existe et calcule sun direction/color/ambient/sky gradient via mécanique céleste simplifiée + keyframes internes, mais **ne consomme pas** une courbe data-driven et n'a pas de paramètre `dayOfYear`. |
| `engine/world/ProbeData.h/.cpp` (rien) | existe | Présent (lignes 1-54), inchangé selon spec. |
| `engine/editor/world/panels/AtmospherePanel.cpp` (inclut SunSkyPanel) | absent | Pas de fichier panneau dédié (cf. M100.23). |
| `atmosphere.json` v3 (migration v2→v3) | absent | Toujours en v1 sur disque ; aucun bump prévu côté code. |
| `engine/world/atmosphere/tests/SunCurveTests.cpp` | absent | — |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_SunCurve_Interpolation` | absent | — |
| `Test_AtmosphereJson_V2ToV3Migration` | absent | — |
| `Test_DayNightCycle_UsesCurve` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : refactor `DayNightCycle` pour consommer une courbe externe au lieu de keyframes internes (rupture de l'API existante éditeur ↔ runtime). Migration JSON v1→v2→v3 en chaîne (les trois sont absentes). Bake IBL probes : déclencheur + workflow non spécifié finement, à intégrer au pipeline existant. UI courbe 24×4 cellules éditables non triviale en ImGui.

### Phase 7 — Saisons / Météo / Thermal

#### M100.25 — Season System & Time-of-Year

**Phase** : 7
**Dépendances** : M100.24
**Pivot/réseau** : redéploiement serveur requis

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/season/Season.h` | absent | Aucun fichier `Season*` dans `engine/`. Grep `Season::` ne trouve que des docs et tickets. |
| `engine/world/season/SeasonClock.h/.cpp` | absent | Aucun service client. Le sous-dossier `engine/world/season/` n'existe pas. |
| `engine/network/SeasonBroadcastPayloads.h/.cpp` | absent | `engine/network/` ne contient aucun `Season*Payloads*`. |
| `engine/server/SeasonBroadcaster.h/.cpp` | absent | Pas dans `engine/server/`. |
| `engine/network/ProtocolV1Constants.h` (modif : ajout `kOpSeasonBroadcast`) | partiel | Fichier existe ; opcodes alloués 1–48, aucun `kOpSeason*`. |
| `engine/render/shaders/foliage.vert/.frag` (tint saisonnier) | absent | Pas de shader `foliage.*`. |
| `engine/editor/world/panels/AtmospherePanel.cpp` | absent | Le sous-dossier `engine/editor/world/panels/` n'existe pas. |
| `engine/server/MasterServer.cpp` (boot `SeasonBroadcaster`) | partiel | Fichier équivalent `main_server.cpp` / `ServerApp.cpp` existe, aucun init saison. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_SeasonClock_CyclesAcross4Seasons` | absent | — |
| `Test_SeasonClock_BlendTAtTransitionWindow` | absent | — |
| `Test_SeasonBroadcast_Roundtrip` | absent | — |
| `Test_Editor_PreviewOverrideVisible` | absent | — |
| `Test_Server_NoSurfaceModifierLogic` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Wire-breaking — l'opcode `kOpSeasonBroadcast` doit être alloué dans `ProtocolV1Constants.h`. Opcodes actuels alloués : 1–4 (auth/register), 7–8 (heartbeat/error), 10–13 (shard register/heartbeat), 14–18 (shard ticket), 19–20 (server list), 21–24 (forgot/reset password), 25–26 (verify email), 27–32 (terms), 33–34 (char create), 35–36 (username avail), 37–38 (resend verif), 39–40 (char list), 41–42 (char delete), 43–44 (save position), 45–46 (chat), 47–48 (enter world). **Premier slot libre suggéré : 49** (`kOpSeasonBroadcast` = 49u). Pas de `kProtocolVersion` constant trouvé. **Redéploiement master obligatoire**, sinon BAD_REQUEST. M100.24 (Sun/Sky) absent aussi → ordre de PR à respecter.

#### M100.26 — Weather System & Dynamic Surface Modifiers

**Phase** : 7
**Dépendances** : M100.11, M100.25
**Pivot/réseau** : redéploiement serveur requis

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/weather/WeatherType.h` | absent | Pas de sous-dossier `engine/world/weather/`. **Attention** : un fichier `engine/render/WeatherSystem.h` **existe** mais c'est le système particules M38.2 (`enum class WeatherState { Clear, Rain, Snow, Fog, Storm }`, pas de `Drought`, pas d'opcode, pas de `SurfaceModifiers`). Risque de collision de namespace. |
| `engine/world/weather/WeatherSystem.h/.cpp` | partiel | Le `WeatherSystem` M38.2 existe à `engine/render/WeatherSystem.{h,cpp}` mais n'a ni `OnNetworkBroadcast`, ni `ComputeModifiers(SurfaceType)`, ni `OverridePreview`. Reuse possible mais nécessite refactor + déplacement, ou nouveau service distinct dans `engine/world/weather/`. |
| `engine/world/weather/WeatherModifiers.h/.cpp` | absent | Aucun loader JSON `weather_modifiers.json`. |
| `engine/network/WeatherBroadcastPayloads.h/.cpp` | absent | — |
| `engine/server/WeatherBroadcaster.h/.cpp` | absent | — |
| `engine/editor/world/panels/WeatherPanel.h/.cpp` | absent | Le sous-dossier `engine/editor/world/panels/` n'existe pas. |
| `assets/gameplay/weather_modifiers.json` | absent | — |
| `engine/network/ProtocolV1Constants.h` (modif `kOpWeatherBroadcast`) | partiel | Fichier existe, opcode à allouer. |
| `engine/world/surface/SurfaceQueryService.cpp` (branche `WeatherSystem`) | absent | **Le sous-dossier `engine/world/surface/` n'existe pas du tout** (M100.11 entièrement vide). |
| `engine/render/VolumetricFogPass.cpp` | absent | Aucun `VolumetricFogPass` dans `engine/render/`. Le fog actuel est probablement géré ailleurs (DayNightCycle existant). |
| `engine/server/MasterServer.cpp` (boot `WeatherBroadcaster`) | partiel | équivalent `ServerApp.cpp` / `main_server.cpp` présent. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_WeatherSystem_AppliesModifiersUnderRain` | absent | — |
| `Test_WeatherSystem_SnowOverridesOutdoorSurface` | absent | — |
| `Test_WeatherBroadcast_Roundtrip` | absent | — |
| `Test_Server_NoModifierComputation` | absent | — |
| `Test_FogPass_DensityIncreasesUnderStorm` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : (1) **Wire-breaking** — opcode `kOpWeatherBroadcast` à allouer ; en supposant que M100.25 prenne 49, **slot suggéré : 50** (`kOpWeatherBroadcast` = 50u). Redéploiement master obligatoire. (2) **Bloquant amont** : M100.11 (SurfaceQueryService, SurfaceType, SurfaceModifiers) totalement absent. (3) **Dépendance M100.25** absente. (4) **Collision potentielle** avec l'existant `engine/render/WeatherSystem` (M38.2) : il faut soit étendre, soit déplacer vers `engine/world/weather/` avec dépréciation propre du chemin render. (5) `VolumetricFogPass` mentionné mais inexistant — modification requise sur le pipeline fog actuel (DayNightCycle ?).

#### M100.27 — Shade Map & Thermal Map (`ThermalQuery`)

**Phase** : 7
**Dépendances** : M100.18, M100.25, M100.26
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/thermal/ShadeMap.h/.cpp` | absent | Pas de sous-dossier `engine/world/thermal/`. |
| `engine/world/thermal/ShadeMapBuilder.h/.cpp` | absent | — |
| `engine/world/thermal/ThermalMap.h/.cpp` | absent | — |
| `engine/world/thermal/ThermalQueryService.h/.cpp` | absent | Grep `ShadeMap|ThermalMap|ThermalQuery|shade\.bin` → seulement docs/tickets. |
| `engine/editor/world/panels/ThermalDebugPanel.h/.cpp` | absent | — |
| `engine/editor/world/WorldEditorExporter.cpp` (modif export) | absent | Le présent `engine/editor/world/` ne contient pas d'exporter — c'est `WorldMapIo.cpp` ou `WorldEditorSession.cpp` qu'il faudrait étendre. |
| `engine/world/StreamCache.cpp` (`LoadShadeMap`) | partiel | `StreamCache.{cpp,h}` existe dans `engine/world/`, à étendre pour charger `shade.bin`. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (`WriteShadeMap`) | partiel | À confirmer existant côté toolchain (référencé par CMake spec). |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_ShadeMap_Roundtrip` | absent | — |
| `Test_ShadeMapBuilder_DenseCanopy_HighShade` | absent | — |
| `Test_ComputeTemperature_SummerNoonOpenAt0m_Is22Plus8` | absent | — |
| `Test_ComputeTemperature_WinterNightDenseAt500m_Is_neg5` | absent | — |
| `Test_ThermalQuery_OnSeasonChange_RebuildsMap` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Round-trip binaire `shade.bin` (R8 64×64 par chunk + `OutputVersionHeader` 24 octets) listé dans INDEX → test indispensable et strict. Bloqué amont par M100.11 (PIVOT — `ThermalQuery` se construit au-dessus de `SurfaceQueryService`), M100.18 (canopée arborée), M100.25 (Season) et M100.26 (WeatherType) tous **absents**. Pas de réseau propre, mais la frame de rebuild thermal map dépend de signaux saison/météo qui n'existent pas encore. Nécessite worker thread (raycast vertical 16 samples × 64×64 cellules = 65 536 raycasts par chunk).

#### M100.28 — Gameplay Zones & Weather Zones

**Phase** : 7
**Dépendances** : M100.26
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/zones/Zones.h/.cpp` | absent | Pas de sous-dossier `engine/world/zones/`. **Attention** : `engine/server/ZoneTransitions.{h,cpp}` existe (transitions inter-zones par sourceZoneId/targetZoneId — M22.x), c'est un **autre concept** (pas polygonal, pas typé SafeZone/PvP/etc.). Aucune collision logique mais attention à la nomenclature `ZoneType`. |
| `engine/editor/world/ZoneTool.h/.cpp` | absent | — |
| `engine/editor/world/panels/ZonesPanel.h/.cpp` | absent | — |
| `engine/world/StreamCache.cpp` (modif `LoadZones`) | partiel | `StreamCache.{cpp,h}` existe, à étendre. |
| `engine/world/weather/WeatherSystem.cpp` (hook override) | absent | M100.26 vide (cf. ci-dessus). |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (`WriteZones`) | partiel | À confirmer ; `engine/world/ChunkPackageLayout.{cpp,h}` existe côté engine. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_Zones_Roundtrip` | absent | — |
| `Test_WeatherOverride_TransitionAtBorder` | absent | — |
| `Test_QuestTrigger_FieldsPreserved` | absent | — |
| `Test_ZonesPanel_LayerVisibility` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Round-trip `zones.bin` listé explicitement dans INDEX (`instances/zones.bin` | M100.28 | M100.26, M100.34) → test obligatoire (magic `0x534E4F5A` "ZONS", v1, layout polygonal variable). Bloqué amont par M100.26 (WeatherSystem) absent → la fonctionnalité WeatherOverride (qui est la valeur métier) ne peut pas être livrée sans M100.26. Risque de confusion nominale avec `ZoneTransitions` côté serveur (concept différent).

### Phase 8 — Routes / Ponts / Structures

#### M100.29 — Spline Tool & Roads

**Phase** : 8
**Dépendances** : M100.10, M100.17
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/SplineTool.h/.cpp` | absent | Pas de dossier `engine/editor/world/`. Existe : un panneau « Routes » dans `WorldEditorImGui.cpp` (mode 4) qui pousse une polyligne 2D XZ via `WorldEditorSession::RouteDraftPoints/RouteStripWidthM` — pas de spline 3D, pas de Catmull-Rom, pas de gizmo width par point, pas d'auto-fit Y, raccourci L absent. |
| `engine/editor/world/SplineCommand.h/.cpp` | absent | Aucune `ICommand` ; l'application route est un side-effect direct dans `Engine::Update` (`ConsumeRouteApplyDraftRequest` ligne 3702). Pas d'undo. |
| `engine/world/spline/SplineInstances.h/.cpp` | absent | Dossier `engine/world/spline/` inexistant. Aucune sérialisation `splines.bin`, aucun `kSplinesMagic`/`kSplinesVersion`, aucun enum `SplineType`. Persistance actuelle : routes JSON dans `WorldMapIo.cpp` (`SerializeRoutesJson` / `ParseJsonRoutes`) — format incompatible avec le binaire spec. |
| `engine/world/spline/SplineSampler.h/.cpp` | absent | Aucun échantillonneur, pas de tangentes ni d'offsets latéraux. |
| `engine/world/spline/tests/SplineTests.cpp` | absent | Aucun test. |
| `engine/world/StreamCache.cpp` (mod : `LoadSplines`) | absent | `StreamCache.cpp` existe mais ne référence aucune spline. |
| `engine/editor/world/TerrainDocument.cpp` (mod : `PaintSplatAlongSpline`) | partiel | Pas de `TerrainDocument`. Existe `TerrainEditingTools::PaintSplatAlongPolyline(points, widthM, layer, BrushParams)` qui peint sur SLAP avec largeur uniforme — sert de base pour la peinture mais pas de width par point ni de feather distinct. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (mod : `WriteSplines`) | absent | `tools/zone_builder/ChunkPackageWriter.cpp` n'a aucune référence spline. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_Spline_CatmullRom_Continuity` | absent | — |
| `Test_Spline_GroundAutoFit` | absent | — |
| `Test_PaintAlongSpline_PreservesSum255` | absent | — |
| `Test_SurfaceQuery_OverRoad_ReturnsRoad` | absent | Bloquant : aucun `SurfaceQueryService` dans le repo. |
| `Test_Roundtrip_SplinesBin` | absent | — |

**Verdict** : ☐ done · ☑ partiel · ☐ vide
**Effort estimé** : L
**Risques** : Le système route actuel (JSON polyligne 2D + peinture splat à largeur uniforme) doit être migré ou rendu compatible avec le format binaire `splines.bin` v1. Dépendance dure sur `SurfaceQueryService` (M100.11 absent) pour le critère SurfaceType::Road. Catmull-Rom centripète + gizmo width par point + UX (raccourci L, double-click pour fermer, sélection point + Suppr) à intégrer from scratch.

#### M100.30 — Bridges & Modular Walls

**Phase** : 8
**Dépendances** : M100.17, M100.29
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/BridgeTool.h/.cpp` | absent | Aucune occurrence. |
| `engine/editor/world/WallTool.h/.cpp` | absent | Aucune occurrence. |
| `engine/world/structures/Kits.h/.cpp` | absent | Dossier `engine/world/structures/` inexistant. |
| `engine/world/structures/BridgeSurfaces.h/.cpp` | absent | Pas de `BridgeWalkable`. |
| `engine/world/structures/tests/StructuresTests.cpp` | absent | — |
| `engine/world/spline/SplineInstances.h/.cpp` (mod : bump v2) | absent | Le fichier source n'existe pas (cf. M100.29). |
| `engine/world/surface/SurfaceQueryService.cpp` (mod : override Bridge) | absent | Dossier `engine/world/surface/` inexistant ; aucun `SurfaceQueryService`. |
| `engine/world/instances/PropInstances.cpp` (mod : paste atomique) | absent | Dossier `engine/world/instances/` inexistant ; aucun `PropInstances`. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_Bridge_GeneratesCorrectSegmentCount` | absent | — |
| `Test_Wall_PostSpacingApplied` | absent | — |
| `Test_Wall_DetectsCornersOver70deg` | absent | — |
| `Test_Bridge_SurfaceQueryReturnsBridge` | absent | — |
| `Test_SplinesBin_V1ToV2Migration` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : Bloqué tant que M100.29 (splines binaires v1) et M100.17 (PropInstances/PlacementTool) ne sont pas livrés — ce ticket bump v1→v2 et écrit dans `PropInstances`. La détection de coin ≥ 70° et le `BridgeWalkable` lookup pour `SurfaceQueryService` impliquent que M100.11 soit aussi en place. Charge mesh modulaires (kits) et instancing GPU à intégrer.

#### M100.31 — Hamlet Generator

**Phase** : 8
**Dépendances** : M100.17
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/HamletGeneratorTool.h/.cpp` | absent | Aucune occurrence (grep `Hamlet*` vide). |
| `engine/editor/world/HamletCommand.h/.cpp` | absent | — |
| `engine/world/structures/HamletKitLibrary.h/.cpp` | absent | Dossier `engine/world/structures/` inexistant. |
| `engine/editor/world/tests/HamletTests.cpp` | absent | — |
| Dépendance implicite : `PropInstances` + `PlacementTool` (M100.17) | absent | Aucun `PlacementTool` ni `PropInstances`/`PropDocument` dans le code. Le générateur n'a rien sur quoi pousser ses poses. |
| Format kit `assets/structures/kits/<name>.json` | absent | Aucun dossier `assets/structures/kits/`. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_Hamlet_DeterministicWithSeed` | absent | — |
| `Test_Hamlet_RespectsMinSpacing` | absent | — |
| `Test_Hamlet_SnapsToRoadWhenAvailable` | absent | Dépend de splines (M100.29) pour la projection sur route. |
| `Test_Hamlet_OneHistoryEntry` | absent | — |
| `Test_HamletKit_LoadsHumanKit` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : M
**Risques** : Strictement bloqué sur M100.17 (PlacementTool / PropInstances / système ICommand) : sans paste atomique ni historique, le critère « Ctrl+Z annule en bloc » est inatteignable. Le critère « snap à la route » dépend aussi de M100.29 (lookup spline). Poisson-disk + triangulation polygone + tirage pondéré seedé sont du code algorithmique standard, mais nécessitent les meshes/kits assets (huit races à fournir) — le périmètre asset pourrait masquer de l'effort tooling caché.

### Phase 9 — Objets interactifs

#### M100.32 — Interactive Props (Doors, Windows, Trapdoors, Simple Chests)

**Phase** : 9
**Dépendances** : M100.12, M100.17
**Pivot/réseau** : redéploiement serveur requis, test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/world/interactive/InteractiveTypes.h` | absent | Dossier `engine/world/interactive/` inexistant. |
| `engine/world/interactive/InteractiveInstances.h/.cpp` | absent | Sérialisation `interactives.bin` à créer (magic `INCT`, version 1). |
| `engine/world/interactive/InteractiveSimulator.h/.cpp` | absent | Animation locale + prompt + audio, strict client. |
| `engine/network/InteractivePayloads.h/.cpp` | absent | Aucun symbole `Interactive*` dans `engine/network/`. |
| `engine/server/InteractiveStateRelay.h/.cpp` | absent | Aucun handler master interactif. |
| `engine/editor/world/InteractivePanel.h/.cpp` | absent | Sous-dossier `engine/editor/world/` inexistant. |
| `engine/network/ProtocolV1Constants.h` (modif) | partiel | Fichier existe ; opcodes 1–48 alloués. **Aucun** opcode `kOpInteractive*` défini. |
| `engine/world/StreamCache.cpp` (modif) | partiel | Fichier existe ; aucune référence à `LoadInteractives` ou `interactives.bin`. |
| `engine/server/MasterServer.cpp` (modif) | absent | **Chemin spec invalide** : pas de `MasterServer.cpp` sous `engine/server/`. Le démarrage du relais devra cibler le binaire master réel. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif) | partiel | Fichier existe sous `tools/zone_builder/ChunkPackageWriter.cpp` (pas dans `lib/` — chemin spec décalé) ; aucun `WriteInteractives`. |

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `engine/world/interactive/tests/InteractiveTests.cpp` | absent | Aucun fichier de test interactif. |
| `Test_Interactives_Roundtrip` | absent | — |
| `Test_Simulator_AnimatesDoorHinge` | absent | — |
| `Test_Simulator_AnimatesSliding` | absent | — |
| `Test_Simulator_AnimatesTrapdoor` | absent | — |
| `Test_Simulator_AnimatesChest` | absent | — |
| `Test_Network_StateChangeRoundtrip` | absent | — |
| `Test_Server_NoGameplayValidation` | absent | — |
| `Test_Server_InitialSyncOnConnect` | absent | — |
| `Test_Client_RemoteAnimationLatencyCompensation` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** :
- **Allocation opcodes** : opcodes 1–48 alloués dans `ProtocolV1Constants.h`. Si M100.25 prend 49 et M100.26 prend 50, alors `kOpInteractiveStateChange / Broadcast / Sync` doivent prendre **51/52/53**.
- **Wire-breaking** : trois nouveaux opcodes UDP gameplay → bump `kProtocolVersion` (non présent dans le fichier — confirmer où il vit) et **redéploiement master en lock-step** ; un client neuf parlant à un master ancien recevra BAD_REQUEST sur les trois opcodes.
- **Chemins spec décalés** : `engine/server/MasterServer.cpp` et `tools/zone_builder/lib/` n'existent pas tels quels — cibles d'intégration à reprécifier avant implémentation.
- **Couplage inter-tickets** : dépend de M100.12 (proxies de collision portes ouvertes/fermées) et M100.17 (PlacementTool) eux-mêmes vides.
- **Persistance master volontairement RAM-only** : à la coupure serveur, l'état repart de `initialState`. À documenter pour ops.
- **Anti-duplication** : `InteractiveSimulator` doit rester hors de `engine_core_server` (animation/audio/prompt strict client) — vigilance CMake.

### Phase 10 — Polissage final

#### M100.33 — Footstep Audio Surface Hook & Playtest Mode (F5)

**Phase** : 10
**Dépendances** : M100.11, M100.16, M100.26, M100.27
**Pivot/réseau** : —

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/audio/FootstepAudioSurfaceHook.h/.cpp` | absent | Aucun fichier `Footstep*Hook*` ni `FootstepAudio*`. `engine/audio/` ne contient que `AudioEngine.{h,cpp}` et `MaMenuMusic.{h,cpp}`. |
| `engine/editor/world/PlaytestMode.h/.cpp` | absent | Le répertoire `engine/editor/world/` n'existe pas. Aucun fichier `Playtest*`. |
| `engine/editor/world/PlaytestHUDOverlay.h/.cpp` | absent | Idem ; aucun HUD overlay debug runtime côté éditeur. |
| `engine/editor/world/tests/PlaytestTests.cpp` | absent | Aucun test playtest. |
| `engine/gameplay/CharacterController.cpp` (modif) | partiel | Fichier existe mais aucun hook footstep dispatché ; recherche `footstep|Footstep` dans `engine/gameplay/*.cpp` → 0 matches. |
| `engine/editor/world/WorldEditorShell.cpp` (modif) | absent | Pas de `WorldEditorShell*`. L'éditeur passe par `engine/editor/WorldEditorImGui.cpp`. Aucun branchement F5 → playtest. |
| `engine/editor/world/EditorCameraController.cpp` (modif) | absent | Aucun `EditorCamera*`. La caméra éditeur réside vraisemblablement dans `WorldEditorSession`/`Engine` mais sans API `Suspend()`/`Resume()`. |

Pré-requis indirects : `SurfaceQuery` service M100.11 absent, `surface_table.json` non instancié. Note : `AudioEngine::RegisterFootstepSoundsFromManifest` existe et expose des sound ids `_footstep_layer_<n>` (M43.5), mais c'est un mapping splat-layer brut, sans branchement `(SurfaceType, SurfaceModifiers)` ni pitch shift météo M100.26.

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_FootstepHook_GrassPlaysStepGrass` | absent | — |
| `Test_FootstepHook_RainOnRockPitchShift` | absent | — |
| `Test_PlaytestMode_TogglePreservesEditorState` | absent | — |
| `Test_PlaytestMode_UsesProductionCharacterController` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : L
**Risques** : (1) M100.11 (SurfaceQuery + surfaceTable) absent : ce ticket ne peut être implémenté tant que la table de surface n'existe pas — risque de stub. (2) M100.26 (modifiers météo) absent : pitch shift et `slippery` non disponibles. (3) `WorldEditorShell` n'existe pas dans la codebase actuelle — refactor préalable nécessaire ou adaptation des chemins. (4) Anti-duplication exigée : intégrer `CharacterController` + `ThirdPersonCamera` de prod dans le mode éditeur sans fork demande un découplage propre des inputs/cameras.

#### M100.34 — Selection, Layers, Minimap & Save/Load Zone

**Phase** : 10
**Dépendances** : M100.5, M100.9, M100.16, M100.27, M100.28, M100.32
**Pivot/réseau** : test round-trip

**Fichiers spec vs. code existant**

| Fichier exigé par le spec | Statut | Note |
|---------------------------|--------|------|
| `engine/editor/world/SelectionTool.h/.cpp` | absent | Aucun `Selection*` dans tout le repo. |
| `engine/editor/world/LayersDocument.h/.cpp` | absent | Aucun `Layer*`. Pas de système de calques. |
| `engine/editor/world/panels/MinimapPanel.h/.cpp` | absent | Aucun `Minimap*`. Pas de répertoire `panels/`. |
| `engine/editor/world/WorldEditorExporter.h/.cpp` | absent | Aucun orchestrateur `Save Zone`. |
| `engine/editor/world/DeleteCommand.h/.cpp` | absent | Aucun `ICommand` / `DeleteCommand`. |
| `engine/editor/world/tests/ExportZoneTests.cpp` | absent | Aucun test round-trip global ; `WriteFullZone\|SaveZone\|FullRoundtrip` ne matche que dans la spec. |
| `engine/editor/world/panels/OutlinerPanel.cpp` (modif) | absent | Aucun `Outliner*`. |
| `engine/world/instances/PropInstances.h` (modif) | absent | Le répertoire `engine/world/instances/` n'existe pas. |
| `engine/world/hazard/HazardVolumes.h` (modif) | absent | Aucun `Hazard*`. |
| `engine/world/zones/Zones.h` (modif) | absent | Aucun `Zones.h` éditeur. |
| `engine/editor/world/WorldEditorShell.cpp` (modif) | absent | Idem M100.33. |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` (modif) | partiel | `tools/zone_builder/ChunkPackageWriter.{h,cpp}` existe (heightmap/splat baking M100.5/.9), mais il n'y a pas de sous-dossier `lib/`, et aucune API agrégée `WriteFullZone(...)` qui sequence les writers de props/foliage/hazards/interactives/zones/water/fog/atmosphere/splines/wind/shade. |

Pré-requis dépendants tous absents : aucune trace de M100.5 (heightmap), M100.9 (splat — `TerrainSplatting`/`SplatSampling` existent mais format binaire chunk non confirmé), M100.16 (hazards), M100.27 (shade map), M100.28 (gameplay zones — seul `ZoneTransitions` côté server), M100.32 (interactives). `engine/world/` contient `ChunkPackageLayout`, `PakReader`, `StreamCache`, `WorldModel` — la fondation existe mais ne couvre pas les ~12 catégories à exporter.

**Tests mentionnés**

| Test exigé | Statut | Note |
|------------|--------|------|
| `Test_Selection_Rect_SelectsContainedProps` | absent | — |
| `Test_Selection_Lasso_SelectsContainedProps` | absent | — |
| `Test_Layer_Visibility_HidesPropsInOutliner` | absent | — |
| `Test_Layer_Lock_BlocksEdits` | absent | — |
| `Test_Minimap_RendersCurrentChunk` | absent | — |
| `Test_DeleteCommand_RoundTrip` | absent | — |
| `Test_ExportZone_FullRoundtrip` | absent | Test charnière M100 — non implémenté. |
| `Test_ExportZone_ConsumedByClient` | absent | — |

**Verdict** : ☐ done · ☐ partiel · ☑ vide
**Effort estimé** : XL
**Risques** : (1) Ticket terminal qui dépend de 6 tickets eux-mêmes vides ou très partiels — il ne peut littéralement pas être implémenté avant que toutes ces structures existent et aient un format binaire stable. (2) Bump de version + migration v1→v2 (`layerIndex`) sur 3+ structures simultanément. (3) Test round-trip global = test de cohérence end-to-end de M100 entier : le moindre champ non sérialisé fera échouer le test. (4) Anti-duplication client/éditeur via `zone_builder_lib` : un sous-dossier `lib/` doit être extrait de `tools/zone_builder/`. (5) Volumétrie : ~12 writers + 12 loaders + 12 comparateurs + UI Outliner/Minimap/Selection/Layers + DeleteCommand undo, le tout bien au-delà de 3000 lignes nettes.

## 3. Flags transverses

### 3.1 Pivot M100.11 — SurfaceQuery

**Verdict** : ☑ vide · **Effort** : L · **Status** : aucun symbole `SurfaceQuery*` / `SurfaceType` / `SurfaceTable*` dans le code (création ex-nihilo).

**Tickets aval impactés** (consommateurs directs du contrat) :

- **M100.15** (Water Surface Hook) — appelle `SurfaceQueryService` pour décider Wading vs Swimming.
- **M100.16** (Hazard Volume System) — résout les types `SurfaceType::Lava/Mud/Quicksand/DeepWater` pour appliquer modifiers + ticks de dégât.
- **M100.19** (Procedural Forest & Field) — tags splat `WheatField`/`CornField` doivent retourner les `SurfaceType` correspondants.
- **M100.26** (Weather System) — `ComputeModifiers(SurfaceType)` est l'API critique pour appliquer `slippery`, `wet`, `snowed`.
- **M100.27** (Shade & Thermal) — `ThermalQuery` se construit *au-dessus* de `SurfaceQuery` ; le contrat doit être stable.
- **M100.33** (Footstep Audio + Playtest) — sélection sample audio par `SurfaceType` + pitch shift par `SurfaceModifiers`.

**Risques contractuels figés à l'audit** :

1. **Nom de méthode** : la spec ticket utilise `SurfaceQueryService::Query(Vec3)`, le prompt directeur utilise `SurfaceQuery::At(...)`. **Décision à prendre en phase 3 build** : un seul nom officiel. Recommandation : implémenter `SurfaceQueryService::Query(Vec3 worldPos) → (SurfaceType, SurfaceModifiers)` et bannir `::At` partout, ou prévoir un alias inline.
2. **Ordre figé de l'enum** `SurfaceType` : `Dirt=0, Grass=1, Rock=2, Snow=3, Sand=4, Mud=5, ShallowWater=6, DeepWater=7, Lava=8, WheatField=9, CornField=10, Road=11, Bridge=12`. À matcher exactement au moment du build M100.11 sinon `surface_table.json` désynchronisé.
3. **Diff CMake structurant** : split `engine_core` (full) / `engine_core_server` (sous-ensemble sans Vulkan/shaders/splat/SurfaceQueryService). Touche le build Linux serveur — risque de régression si la liste de sources strictement client est mal calibrée. À tester sur CI Linux dès la phase 3 build.
4. **Couplage water existant** : `CharacterController::WaterQuery` reste autoritaire pour la flottabilité ; `SurfaceQueryService` autoritaire pour vitesse + audio + thermal. Précaution à graver dans le code de M100.11 (commentaire) et dans M100.15.

### 3.2 Redéploiement serveur (wire-breaking)

| Ticket  | Opcode spec                  | Slot suggéré | Verdict | Notes                                                                |
|---------|------------------------------|--------------|---------|----------------------------------------------------------------------|
| M100.25 | `kOpSeasonBroadcast`         | **49**       | ☑ vide  | Premier libre après 1–48 (cf. `engine/network/ProtocolV1Constants.h`) |
| M100.26 | `kOpWeatherBroadcast`        | **50**       | ☑ vide  | Suivant logique. Bloqué amont par M100.11 (SurfaceModifiers).         |
| M100.32 | `kOpInteractiveStateChange`  | **51**       | ☑ vide  | Spec ajoute aussi `Broadcast` (52) et `Sync` (53). 3 opcodes au total. |
| M100.32 | `kOpInteractiveStateBroadcast` | **52**     | ☑ vide  | Idem.                                                                  |
| M100.32 | `kOpInteractiveStateSync`    | **53**       | ☑ vide  | Idem.                                                                  |

**Total : 5 nouveaux opcodes (49→53)**, à allouer en lock-step entre les phases 7 (M100.25/.26) et 9 (M100.32). Pas de `kProtocolVersion` constante détectée (l'évolution est tracée par non-réaffectation des valeurs, cf. commentaire en tête de `ProtocolV1Constants.h`).

**Règle CLAUDE.md** : pour chaque PR phase 7 et phase 9, mention obligatoire `**Déploiement** : ⚠️ redéploiement serveur master requis — opcode <X>` dans le résumé chat ET la description GitHub.

### 3.3 Tests round-trip parité éditeur ↔ client

| Ticket  | Format binaire concerné            | Test mentionné dans la spec        | Statut audit |
|---------|------------------------------------|------------------------------------|--------------|
| M100.5  | `terrain.bin` (heightmap 257² float)| `TerrainParityTests.cpp`           | absent       |
| M100.8  | `terrain_lods.bin`                 | `TerrainLodTests.cpp` (`Test_ParityWithClient_Identical`) | absent |
| M100.9  | `splat.bin` (8 layers, 257², somme=255)| `Test_Parity_EditorWritesClientReadsIdentical` | absent |
| M100.12 | `*.collision.bin` (capsule/convex/trimesh) | `Test_CollisionProxy_Roundtrip*` (3 variantes) | absent |
| M100.16 | `instances/hazards.bin`            | `Test_Hazards_RoundtripBin`        | absent       |
| M100.27 | `shade.bin` (R8 64² par chunk)     | `Test_ShadeMap_Roundtrip`          | absent       |
| M100.28 | `instances/zones.bin`              | `Test_Zones_Roundtrip`             | absent       |
| M100.32 | `instances/interactives.bin` + payloads wire | `Test_Interactives_Roundtrip` + `Test_Network_StateChangeRoundtrip` | absent |
| M100.34 | **Tous** (test charnière)          | `Test_ExportZone_FullRoundtrip` + `Test_ExportZone_ConsumedByClient` | absent |

Aucun test round-trip M100 n'existe à ce jour. Tous sont à écrire dans la PR de leur ticket respectif. Le test charnière de M100.34 est end-to-end : il sequence l'écriture de tous les writers et vérifie que le client peut consommer le zip complet — c'est le test final qui valide que le mode B « couche au-dessus » a effectivement préservé la parité.

## 4. Risques détectés à l'audit

Liste agrégée des risques non triviaux remontés par les sous-agents, par ordre de ticket. Ces risques alimenteront le plan de build par phase pour décider de l'ordre d'attaque et des PRs séquentielles intra-phase.

- **M100.1** — `WorldEditorImGui.cpp` (existant) implémente déjà un menu bar et un dockspace (`WorldEditorDockSpaceV2`) ; risque de doublon/conflit conceptuel avec le futur `WorldEditorShell`. Flag CLI à harmoniser (`--world-editor` actuel vs `--editor-world` spec).
- **M100.2** — Pas de blocage technique propre, mais bloqué par M100.1 (le shell n'existe pas pour héberger `m_commandStack` et brancher Ctrl+Z/Y).
- **M100.3** — Risque mineur de régression sur la baseline `zone_0/` lors de l'extraction `tools/zone_builder/` → `tools/zone_builder/lib/` ; nécessite un test bit-à-bit pour valider la non-régression.
- **M100.4** — Bloqué par M100.1 (panneau Scene). Pas de réutilisation directe de la caméra FPS gameplay pour Orbital/TopDown.
- **M100.5** — Coexistence `HeightmapLoader` legacy R16 (magic `HAMP`) avec le nouveau `TerrainChunk` 257² float (magic `TRRN`). Index 32-bit obligatoire (257² > 65k) à câbler dans `GeometryPass`.
- **M100.6** — Bloqué par M100.2 + M100.5. Brosses existantes (`TerrainEditingTools`) sans Noise, sans delta sparse, sans coutures inter-chunks.
- **M100.7** — Cascade de dépendances M100.2 → .5 → .6 → .7. Lecture PNG 16-bit grayscale à confirmer côté chaîne stb_image/libpng.
- **M100.8** — Contrat « main thread jamais bloqué > 1 ms » + worker thread pool. Skirt géométrique (2 m) à intégrer sans persistance. Risque d'ambiguïté `kTerrainLodCount = 5` (legacy) vs `lodCount = 3` (nouveau).
- **M100.9** — Conflit format avec l'existant 4 layers RGBA `TerrainSplatting` (magic `SLAP 0x50414C53`) vs spec 8 layers (magic `SLAT 0x54414C53`). Résolution 257² (spec) vs 1024² (actuel). Migration ou bumpversion.
- **M100.10** — Refactor du `TerrainEditingTools::PaintSplat` existant en commande undoable. Coutures inter-chunks supposent que le streaming chunk soit en place (M100.5).
- **M100.11 (PIVOT)** — Voir section 3.1 ci-dessus. Création ex-nihilo de tout le sous-système. Diff CMake structurant.
- **M100.12** — Aucun moteur physique runtime visible côté client. Dépendance V-HACD à wrapper (lib externe non vue dans `external/`). `EditorOverlayPass` doit s'enregistrer **uniquement** si éditeur actif.
- **M100.13** — Delaunay 2D contraint au polygone CCW, robustesse aux polygones concaves/auto-intersectants. Ribbon rivière qui doit suivre la heightmap.
- **M100.14** — SSR mince à écrire (raymarch + fallback skybox). Décision à prendre : remplacer le `WaterRenderer` M37 (plan plat unique) ou faire vivre les deux pendant la transition.
- **M100.15** — Bloqué par M100.11 + M100.13. Hystérésis bidirectionnelle 1.0/0.7 m à tester explicitement (oscillation à la frontière). Conflit potentiel avec les paramètres swim M37 résiduels.
- **M100.16** — 4 types × 4 modes d'évasion (combinatoire de tests). Tests temporels exacts (LavaKills3s, mash 10/5s) sensibles au tick sim.
- **M100.17** — Format `props.bin` versionné à concevoir avec parité. Budgets perf ghost <5 ms / commit <16 ms / scatter 50 props <50 ms à valider sur perf-tests CI.
- **M100.18** — Volume code XL. Risque de duplication avec `engine/editor/TreeSpeciesCatalog` existant — à réutiliser ou migrer. Budget perf 100k instances @ 60 fps dépend du GPU-driven culling déjà bouclé.
- **M100.19** — Verrou bloquant : dépend de M100.18 + M100.11. Triangulation polygone, weighted-asset sampling et auto-tag splat (poids 200/255 + redistribution).
- **M100.20** — Constant buffer `WindParams` partagé C++/GLSL en strict miroir. Dépend de M100.18 (`foliage.vert`).
- **M100.21** — **Budget perf strict < 0.5 ms additionnel sur 100k instances foliage visibles** (mid-range RTX 2060). Test bloquant à mesurer avec un harnais GPU-timing. Loop GLSL non déroulé risque de rejet sur certains drivers.
- **M100.22** — Passe froxel 160×88×64 lourde (3 sous-passes compute/frag). Performance ciblée < 1.5 ms.
- **M100.23** — Migration JSON v1→v2 + ajout d'un binaire `atmosphere_zones.bin` polygonal. Refactor probable de `AtmosphereSettings` (sortir de `ProbeData.h`).
- **M100.24** — Refactor `DayNightCycle` pour consommer une courbe externe (rupture de l'API existante éditeur ↔ runtime). Migration JSON v1→v2→v3 en chaîne.
- **M100.25** — Wire-breaking ; opcode 49 suggéré. M100.24 (Sun/Sky) absent → ordre de PR à respecter.
- **M100.26** — Wire-breaking ; opcode 50 suggéré. **Bloquant amont M100.11 absent**. Collision potentielle avec `engine/render/WeatherSystem` (M38.2) — refactor ou déplacement vers `engine/world/weather/`. `VolumetricFogPass` mentionné mais inexistant.
- **M100.27** — Round-trip binaire `shade.bin` strict. Bloqué amont par M100.18 + M100.25 + M100.26 tous absents. 65 536 raycasts par chunk en worker thread.
- **M100.28** — Round-trip `zones.bin` listé dans INDEX. Bloqué par M100.26 absent. Risque de confusion nominale avec `ZoneTransitions` côté serveur.
- **M100.29** — Système route actuel (JSON polyligne 2D + peinture splat à largeur uniforme) à migrer vers le format binaire spline 3D versionné. Dépendance dure sur `SurfaceQueryService` (M100.11 absent).
- **M100.30** — Bloqué par M100.29 + M100.17. Bump v1→v2 splines, `BridgeWalkable` lookup pour `SurfaceQueryService`.
- **M100.31** — Strictement bloqué sur M100.17. Le critère « snap à la route » dépend aussi de M100.29. Effort tooling caché possible (8 races à fournir en kits).
- **M100.32** — Wire-breaking 3 opcodes (51/52/53 suggérés). Chemins spec décalés (`engine/server/MasterServer.cpp` et `tools/zone_builder/lib/` n'existent pas). Persistance master volontairement RAM-only (à documenter pour ops).
- **M100.33** — Bloqué par M100.11 + M100.16 + M100.26 + M100.27. `WorldEditorShell` n'existe pas — refactor préalable. Anti-duplication exigée : intégrer `CharacterController` + `ThirdPersonCamera` de prod sans fork.
- **M100.34** — Ticket terminal dépendant de 6 tickets vides. Le moindre champ non sérialisé fait échouer le test round-trip global. Volume XL : ~12 writers + 12 loaders + 12 comparateurs + UI complète.

## 5. Synthèse pour la suite

**Cadence de build recommandée par les dépendances détectées :**

1. **Phase 1** (M100.1–4) — fondation impérative ; bloque tout le reste.
2. **Phase 2** (M100.5–8) — terrain ; nécessaire pour Phase 3, 4, 5, 7 (shade), 10.
3. **Phase 3** (M100.9–12) — **inclut le PIVOT M100.11** ; bloque 6 tickets aval.
4. **Phase 4** (M100.13–16) — hydrologie + hazards ; M100.15/16 bloqués par M100.11 (PIVOT) ; OK une fois Phase 3 livrée.
5. **Phase 5** (M100.17–21) — placement + vegetation ; M100.18 = XL ; **risque de découper en 5a (M100.17–18) + 5b (M100.19–21)** au moment du build.
6. **Phase 6** (M100.22–24) — atmosphère + fog ; pas de dépendance phase 5 ; peut tourner en parallèle avec phase 5 pour la rédaction du plan, mais merge en série.
7. **Phase 7** (M100.25–28) — ⚠️ wire-breaking ; bloqué par M100.18 + M100.11 + M100.24.
8. **Phase 8** (M100.29–31) — routes ; bloqué par M100.17.
9. **Phase 9** (M100.32) — ⚠️ wire-breaking ; bloqué par M100.12 + M100.17.
10. **Phase 10** (M100.33–34) — polissage ; **M100.34 dépend de tout** (test charnière).

**Hand-off vers `writing-plans`** : utiliser ce document comme entrée pour produire les 10 plans de build par phase, en commençant par la Phase 1.
