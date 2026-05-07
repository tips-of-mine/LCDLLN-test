# Terrain Chunk Runtime — design de PR

> **Sortie de session de brainstorming** (2026-05-07).
> **Sujet** : design de la PR « Terrain Chunk Runtime » qui résout les deux dettes techniques différées de Phase 2 (Task 11) et Phase 3a (Task 14) : drawcall mesh-terrain par chunk avec splat 8-layer dans `GeometryPass`, sans branche éditeur, production-ready.
> **Suite de** : Phase 1 + Phase 2 + Phase 3a M100 (mergées sur main).
> **Précède** : Phase 3b M100 (M100.11 PIVOT SurfaceQuery + M100.12 Collision Proxy).

## 1. Contexte

Phase 2 a posé `TerrainChunk` (heightmap 257² par chunk) + `TerrainMeshBuilder::BuildLod0Mesh`. Phase 3a a posé `SplatMap` (8-layer splat 257² par chunk) + `TerrainChunkPipeline` (pipeline Vulkan compilé, prêt à dessiner). Les deux phases ont **différé l'intégration end-to-end dans `GeometryPass`** parce que cela demande une infrastructure runtime GPU non triviale :

- Cache des chunks visibles + leurs ressources GPU (vertex/index buffers, splat-maps, descriptor sets)
- Loader d'asset PBR pour les 8 layers (8 × 3 = 24 textures à packer en 3 `VkImageView2DArray`)
- Upload mesh + splat avec respect du budget par frame (`GpuUploadQueue`)
- Descriptor pool dédié au splat set (1 alloc par chunk visible)
- Boot wiring de la pipeline dans `Engine::Init`
- Politique d'éviction (cache LRU avec budget GPU configurable)

Cette PR livre cette infrastructure **production-ready** (vs minimaliste). L'objectif est d'observer le rendu terrain par chunk dès le merge, sur un budget GPU réaliste avec eviction.

## 2. Périmètre

| Composant | Rôle | Localisation |
|-----------|------|--------------|
| `TerrainChunkRenderer` | Orchestrateur, entry point appelé par `GeometryPass` | `engine/render/terrain_chunk/` |
| `ChunkRuntime` | Cache LRU des chunks visibles + budget GPU global | `engine/render/terrain_chunk/` |
| `TerrainMeshGpuCache` | Upload mesh (vertex+index VkBuffer) + cache LRU | `engine/render/terrain_chunk/` |
| `SplatMapGpuCache` | Upload splat-maps (2× VkImage RGBA8 257² par chunk) + cache LRU | `engine/render/terrain_chunk/` |
| `LayerArrayLoader` | 24 textures PBR → 3 `VkImageView2DArray` partagées + 5 samplers (1× boot) | `engine/render/terrain_chunk/` |
| `DescriptorSetPool` | Pool dédié au splat set (alloc par chunk visible, max 49 = 7×7 Visible ring) | `engine/render/terrain_chunk/` |

## 3. Décisions structurantes

| # | Question | Décision |
|---|---------|----------|
| 1 | Localisation du nouveau sous-système | `engine/render/terrain_chunk/` — runtime GPU, distinct de `engine/world/` (world model) et `engine/render/terrain/` (legacy R16/SLAP qui sert `demo_plains`). |
| 2 | Lecture disque des `TerrainChunk` + `SplatMap` | Réutiliser `StreamCache::LoadTerrainChunk` + `LoadSplatMap` (déjà sync, déjà cache LRU côté CPU). Sync à la première demande GPU. |
| 3 | Upload GPU | Sync à la première lecture du chunk (lazy loading). Le budget par frame est tracké via `GpuUploadQueue::Enqueue(TerrainHlod, sizeBytes)` pour respecter `streaming.upload_budget_mb`. Async streaming est un follow-up perf si nécessaire. |
| 4 | Cache LRU policy | Budget GPU configurable via `editor.world.terrain.gpu_budget_mb` (default 256 MB). Tracking par chunk : `meshBytes + 2*splatBytes + descriptorSet`. Eviction LRU au-delà du budget : on jette le plus ancien chunk non-visible (== pas dans `Active` ni `Visible` ring). |
| 5 | Loader d'asset PBR | `LayerArrayLoader` charge 8 layers × 3 maps via `AssetRegistry::LoadTexture(relativePath)` (déjà cache asset) puis blit vers 3 `VkImage2DArray` (8 layers each). Si `.texr` absent → fallback placeholder PNG `assets/terrain/placeholders/<name>.png` (8 PNG 4×4 colorés livrés en Phase 3a). Boot-time, ~24 textures, négligeable en termes de chargement. |
| 6 | Cohabitation avec le legacy | `TerrainChunkRenderer::Render` skippe les chunks sans `terrain.bin` + `splat.bin` (les chunks `demo_plains` héritent du legacy `TerrainRenderer` qui continue à les dessiner). Aucun override, aucune régression. |
| 7 | Sans branche éditeur (critère M100.5/.9) | `TerrainChunkRenderer` est instancié dans `Engine` et utilisé en mode jeu normal autant qu'en mode éditeur. Le code drawcall ne checke pas `m_editorEnabled` — la parité éditeur ↔ client est garantie par le format binaire identique. |
| 8 | Tests CI | Unit tests CPU sur les policies de cache (LRU eviction, budget tracking) + résolution de path PBR (fallback placeholders). Pas de test runtime Vulkan (CI sans GPU). Smoke test interactif post-merge obligatoire. |

## 4. Architecture

```
engine/render/terrain_chunk/                  ← nouveau sous-système
  TerrainChunkRenderer.h                       Entry point public, appelé par GeometryPass
  TerrainChunkRenderer.cpp                     Orchestrateur des autres caches + pipeline
  ChunkRuntime.h/.cpp                          Cache LRU global + tracking budget GPU
  TerrainMeshGpuCache.h/.cpp                   Mesh GPU upload + cache LRU
  SplatMapGpuCache.h/.cpp                      Splat-maps GPU upload + cache LRU
  LayerArrayLoader.h/.cpp                      24 PBR → 3 VkImageView2DArray (1× boot)
  DescriptorSetPool.h/.cpp                     Pool descriptor set splat (max 49)
  tests/
    ChunkRuntimeTests.cpp                      4 cas : hit/miss, eviction, ordre Active>Visible
    TerrainMeshGpuCacheTests.cpp               4 cas : insert/lookup/LRU/budget
    SplatMapGpuCacheTests.cpp                  3 cas : insert/lookup/LRU
    LayerArrayLoaderTests.cpp                  4 cas : fallback placeholder, path resolution

engine/Engine.cpp                              ← modif : init TerrainChunkRenderer après DeferredPipeline
engine/render/GeometryPass.cpp                 ← modif : drawcall par chunk visible

CMakeLists.txt                                 ← modif : add sources + 4 nouveaux test targets
config.json                                    ← modif : ajout editor.world.terrain.gpu_budget_mb
```

**Anti-duplication serveur** : `engine/render/terrain_chunk/**/*.cpp` exclus du target serveur (déjà via Vulkan dependency — le serveur n'a pas Vulkan). Vérification : `grep -RIn "engine::render::terrain_chunk" engine/server/` doit retourner 0.

## 5. Lifecycle

1. **`Engine::Init`** :
   - `m_terrainChunkRenderer = make_unique<TerrainChunkRenderer>();`
   - `m_terrainChunkRenderer->Init(device, physDev, renderPass, cameraSetLayout, config, paths.content)` :
     - `m_pipeline.Init(...)` (déjà fait Phase 3a Task 13, ré-utilisé)
     - `m_layerArrays.LoadAll(config, paths.content)` : charge 24 PBR + crée 3 VkImage2DArray + 5 samplers (1 nearest pour splatMap, 4 linear pour les arrays). Fallback placeholder si .texr absent.
     - `m_descriptorPool.Init(device, maxSets=49)` (taille Visible ring 7×7).
     - `m_meshCache.Init(device, budget = gpu_budget_mb / 2)` (~128 MB pour mesh).
     - `m_splatCache.Init(device, budget = gpu_budget_mb / 2)` (~128 MB pour splat-maps).

2. **`GeometryPass::Record(cmd)`** (chaque frame) :
   - Pour chaque chunk dans `world.GetActiveAndVisibleChunks()` :
     - Si `chunk` n'a pas de `TerrainMeshGpu` en cache : `m_meshCache.GetOrUpload(coord)` lit `terrain.bin` via `StreamCache::LoadTerrainChunk` puis `BuildLod0Mesh` puis `vkCreateBuffer` + `StagingAllocator::Upload`. Si fichier absent → skip ce chunk (legacy le dessine).
     - Idem `m_splatCache.GetOrUpload(coord)` pour `splat.bin`.
     - `m_descriptorPool.AllocateAndWriteSplatSet(splat0View, splat1View, m_layerArrays)` → splatSet.
     - `m_pipeline.RecordChunkDraw(cmd, cameraSet, splatSet, mesh, chunkOriginXYZ)`.

3. **`Engine::EndFrame`** :
   - `m_terrainChunkRenderer->Tick()` : si budget dépassé, evict LRU non-visible chunks.

4. **`Engine::Shutdown`** :
   - `m_terrainChunkRenderer->Shutdown(device)` libère toutes les ressources Vulkan dans l'ordre inverse.

## 6. Risques détectés

| Risque | Atténuation |
|--------|-------------|
| Bugs Vulkan runtime non détectables en CI | Copier-coller `DeferredPipeline.cpp` pattern. Smoke test interactif post-merge obligatoire. Logs verbose au boot pour validation visuelle. |
| Régression rendu legacy `TerrainRenderer` | Skip strict si `terrain.bin`+`splat.bin` absents. Tests d'integration optionnels. Demo_plains doit continuer à fonctionner. |
| Boot-time long (24 textures PBR) | `AssetRegistry` cache déjà. Placeholders 4×4 = quelques Ko chacun. Total < 1 ms. |
| Descriptor pool insuffisant | Sized pour Visible ring 7×7 = 49 max. Budget LRU restreint à 49 chunks résidents simultanés. Si Visible ring change, resizer dynamiquement. |
| Memory leak Vulkan si Shutdown mal séquencé | Test unit `ChunkRuntimeTests::Test_ShutdownReleasesAllResources` (mock VkDevice avec compteur des creates/destroys). |
| Race condition à l'eviction (chunk evicted pendant frame) | Eviction uniquement à `Engine::EndFrame` (entre frames). Pas de free pendant `GeometryPass::Record`. |

## 7. Tests prévus (TDD red→green)

| Fichier | Cas |
|---------|-----|
| `ChunkRuntimeTests.cpp` | Test_CacheHit_NoUpload · Test_CacheMiss_TriggersLoad · Test_LruEviction_UnderBudget · Test_VisibleRing_NeverEvicted |
| `TerrainMeshGpuCacheTests.cpp` | Test_Insert_TracksBudget · Test_Lookup_HitMiss · Test_LruEviction · Test_BudgetExceeded_EvictsOldest |
| `SplatMapGpuCacheTests.cpp` | Test_TwoVkImagesPerChunk · Test_Insert_TracksBudget · Test_LruEviction |
| `LayerArrayLoaderTests.cpp` | Test_LoadAll_8LayersTimes3Maps · Test_FallbackToPlaceholder_When_TexrAbsent · Test_PathResolution_RelativeToContentRoot · Test_BootTime_Under500ms |

Total : ~15 cas. Mock `VkDevice` léger pour les tests CPU (compteur des creates/destroys, pas de vrai upload GPU).

## 8. Critères d'acceptation

- [ ] Un chunk avec `terrain.bin` + `splat.bin` est rendu avec son blend 8-layer dans `GeometryPass`, **sans branche `m_editorEnabled`** dans le code rendu.
- [ ] Un chunk sans ces fichiers est skip, le legacy continue à le dessiner (`demo_plains` non régressé).
- [ ] Les 8 placeholders 4×4 colorés produisent un blend visible (chaque layer a sa couleur distincte).
- [ ] Le budget GPU `editor.world.terrain.gpu_budget_mb` est respecté : eviction LRU des chunks non-visibles quand le budget est atteint.
- [ ] Les tests unitaires CPU passent (15 cas).
- [ ] Pas de leak Vulkan au `Shutdown` (vérifié par counters dans tests).
- [ ] Le serveur ne dépend pas de `engine::render::terrain_chunk::*`.

## 9. Hors scope

- Async streaming des chunks (loading sync à la première demande pour cette PR ; async = follow-up perf).
- Compression des splat-maps GPU (BC7/ASTC ; format RGBA8 brut pour cette PR).
- Texture PBR réelles : on utilise les placeholders 4×4 livrés en Phase 3a. Vrais assets PBR = asset work séparé (artiste).
- Test screenshot visuel CI : déféré à un harnais GPU CI futur.
- Migration `demo_plains` du legacy SLAP vers SLAT chunked : ticket ciblé futur.

## 10. Déploiement

✅ Client/éditeur uniquement. Aucun serveur, aucun nouveau opcode, aucune migration DB.

## 11. Étapes suivantes

1. Lecture et validation de ce document par l'utilisateur.
2. Invocation de `superpowers:writing-plans` pour produire le plan d'implémentation détaillé.
3. Exécution mixte : inline pour les composants pure-CPU (caches LRU, budget tracking, tests) + subagent dédié pour le code Vulkan boilerplate (`LayerArrayLoader`, `DescriptorSetPool`, intégration `GeometryPass`).
