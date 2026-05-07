# M100 Phase 3a — Splat Map + Painting : design de PR

> **Sortie de session de brainstorming** (2026-05-07).
> **Sujet** : design de la 3ᵉ PR M100 (Phase 3a, M100.9 → M100.10).
> **Suite de** : [`2026-05-06-m100-execution-design.md`](2026-05-06-m100-execution-design.md) (cadence et stratégie globales — déjà validées) et de la Phase 2 (M100.5-8) mergée sur main (PRs #471 + #472).

## 1. Contexte

Phase 1 (Fondations, M100.1-4) et Phase 2 (Terrain data + sculpt + stamps + LODs, M100.5-8) sont mergées sur main. Phase 3 démarre par Phase 3a (M100.9 + M100.10) — **splat data + paint brushes**. Phase 3b suivra avec M100.11 (PIVOT SurfaceQuery) et M100.12 (collision proxy).

Phase 3a apporte aussi **la résolution de la dette technique Task 11 reportée de Phase 2** : le critère M100.5 « la passe Geometry consomme le mesh terrain par chunk » devient observable parce que M100.9 introduit le pipeline Vulkan terrain_chunk avec le shader 8-layers.

## 2. Périmètre

| Ticket | Effort audit | Rôle | Test round-trip |
|--------|--------------|------|-----------------|
| M100.9 Splat Map System | partiel L | `SplatMap` 8 layers 257² par chunk, format binaire `splat.bin` magic SLAT, `LayerPalette.json`, shaders GLSL `terrain.vert`/`terrain.frag`, **drawcall Vulkan terrain_chunk dans `GeometryPass`** | **Oui** (`SplatMapTests` byte-exact) |
| M100.10 Splat Painting Brushes | partiel L | `SplatPaintTool` (raccourci `P`), brosse manuelle + auto-rules pente/altitude, undo coalescé, couture multi-chunks | Non |

## 3. Décisions structurantes

| # | Question | Décision validée |
|---|---------|------------------|
| 1 | Coexistence du splat legacy `TerrainSplatting` (4 layers RGBA8 1024², magic SLAP) avec le nouveau `engine::world::terrain::SplatMap` (8 layers 257², magic SLAT) | **Couche au-dessus** : SLAP reste intact pour le mode démo single-zone (`zones/demo_plains/terrain_splat.slap`). SLAT vit en parallèle pour le mode chunked. Pas de migration `demo_plains` dans cette PR. |
| 2 | Drawcall mesh-terrain par chunk (dette Task 11 de Phase 2) | M100.9 l'impose (critère « un chunk uniforme layer 0 rendu avec dirt » + « aucune branche `m_editorEnabled` dans `terrain.frag`/`GeometryPass` »). Phase 3a livre ce drawcall. |
| 3 | Pipeline GLSL terrain_chunk | Nouveau pipeline Vulkan dans `engine_core`, shaders compilés via `engine/render/ShaderCompiler.cpp` existant. Descriptor set 2 = `splatMap` + 3 `texture2DArray` (albedo / normal / ARM). Index buffer UINT32 (déjà utilisé partout dans `GeometryPass`). |
| 4 | Assets PBR des 8 layers (`tex/terrain/{dirt,grass_dry,grass_wet,mud,sand,rock,snow,lava_cooled}_albedo/normal/arm.texr`) | **Placeholders colorés générés au build, PAS fallback gris uniforme.** Un mini-tool `tools/gen_terrain_placeholders` (~50 lignes) génère 8 PNG 4×4 à couleurs unies distinctes (dirt=brun, grass=vert, rock=gris, etc.) dans `assets/terrain/placeholders/` au moment du build (target CMake custom). Permet de **valider visuellement le blend 8-layers** dès le merge. Remplaçables transparents quand les vrais PBR arrivent. |
| 5 | Test `Test_Shader_8LayersBlendVisualBaseline` (screenshot vs baseline PNG) listé dans la spec M100.9 | **Stub** : la CI Linux/Windows release n'a pas de GPU Vulkan. On déclare le test target mais le screenshot est stubbé avec un commentaire `// TODO: réactiver quand harnais GPU CI dispo`. |
| 6 | Granularité PR | **1 PR pour Phase 3a** (M100.9 + M100.10). Volume estimé ~4000-5000 lignes (au-dessus du seuil 3000 mais Phase 2 a fait 10K — reste raisonnable). Découpage interne logique dans le plan d'implémentation. |
| 7 | Couture inter-chunks (M100.10) | Pattern identique à M100.6 (sculpt seam) : la rangée bord d'un chunk = rangée bord du chunk voisin. Le brush écrit explicitement les deux cellules dans la même `ICommand`. |
| 8 | Invariant somme=255 par cellule | Maintenu par `SplatPaintCommand::Execute` après chaque write (renormalisation proportionnelle des autres layers). Validé au `Load` de `splat.bin` (rejet sinon). |

## 4. Architecture

```
engine/world/terrain/                       ← data partagée éditeur ↔ client
  SplatMap.{h,cpp}                          (M100.9 — struct + Save/Load + MakeUniform)
  LayerPalette.{h,cpp}                      (M100.9 — JSON parse + cache textures)
  tests/SplatMapTests.cpp                   (round-trip byte-exact, MakeUniform, palette JSON)

engine/render/                              ← rendu Vulkan
  TerrainChunkPipeline.{h,cpp}              (M100.9 — pipeline + descriptor sets)
  GeometryPass.cpp                          (M100.9 — modif : drawcall terrain par chunk visible)
  shaders/terrain.vert                      (M100.9 — vertex shader)
  shaders/terrain.frag                      (M100.9 — fragment 8-layer blend)

engine/editor/world/                        ← tools, éditeur seul
  SplatPaintTool.{h,cpp}                    (M100.10 — outil + dispatch input)
  SplatPaintCommand.{h,cpp}                 (M100.10 — ICommand delta sparse + somme=255)
  SplatRules.{h,cpp}                        (M100.10 — auto-rules pente/altitude)
  TerrainDocument.cpp                       (M100.9 — modif : EnsureSplatLoaded + Save split splat)
  panels/ToolPropertiesPanel.cpp            (M100.10 — UI palette splat paint)
  WorldEditorShell.cpp                      (M100.10 — raccourci P + ActiveTool::SplatPaint)
  tests/SplatPaintTests.cpp                 (manual + auto-rules + somme=255 + seam + history)

engine/world/                               ← data common
  StreamCache.{h,cpp}                       (M100.9 — modif : LoadSplatMap, ChunkSegment::Splat)
  ChunkPackageLayout.{h,cpp}                (M100.9 — modif : ajouter ChunkSegment::Splat dans le load order, kChunkMetaHasSplat flag)

tools/zone_builder/lib/                     ← writer offline
  ChunkPackageWriter.{h,cpp}                (M100.9 — modif : WriteSplatMap)
  Public/zone_builder/ChunkPackageWriter.h  (M100.9 — modif : forward-declare SplatMap, signature WriteSplatMap)
  CMakeLists.txt                            (M100.9 — ajouter SplatMap.cpp + LayerPalette.cpp aux sources dupliquées)

tools/gen_terrain_placeholders/             ← mini-tool nouveau (Décision 4)
  main.cpp                                  (génère 8 PNG 4×4 colorés)
  CMakeLists.txt                            (target build-time)

assets/terrain/                             ← assets nouveaux
  layer_palette.json                        (8 entrées, surfaceType par défaut "Dirt" jusqu'à M100.11)

CMakeLists.txt (racine)                     ← entrées sources + 2 nouveaux test targets
                                              + custom command pour gen_terrain_placeholders
```

**Anti-duplication serveur :** `engine/world/terrain/SplatMap.cpp` + `LayerPalette.cpp` exclues du target serveur (vérification : `grep -RIn "engine::world::terrain::SplatMap" engine/server/` doit retourner 0). `engine/render/TerrainChunkPipeline.cpp` + shaders : déjà exclus côté Vulkan (le serveur n'a pas Vulkan).

## 5. Tests prévus (TDD red→green, framework REQUIRE maison)

| Ticket | Fichier de tests | Tests minimaux |
|--------|------------------|----------------|
| M100.9 | `engine/world/terrain/tests/SplatMapTests.cpp` | `MakeUniform_SumIs255`, `SaveLoad_Roundtrip` byte-exact, `Load_RejectsBadMagic`, `Load_RejectsSumNot255`, `LayerPalette_LoadJson`, `Parity_EditorWritesClientReadsIdentical` |
| M100.10 | `engine/editor/world/tests/SplatPaintTests.cpp` | `ManualBrush_PreservesSum255`, `AutoRules_PaintsOnlyMatchingCells`, `Falloff_RadialMonotone`, `CrossChunk_PreservesSeam`, `Stroke_OneHistoryEntry` |

Estimation : ~10-12 tests neufs.

## 6. Risques détectés à l'audit

1. **Pipeline Vulkan terrain_chunk** — premier vrai pipeline mesh-terrain par chunk. Beaucoup de boilerplate (descriptor set layout + 4 binding samplers + push constant ou UBO pour tilingScale[8] + render pass setup). Risque : bugs runtime non détectables par la CI sans GPU. **Atténuation** : copier-coller le squelette d'un pipeline existant (ex. `DeferredPipeline.cpp`), revue de cohérence avec `GeometryPass.cpp` ; smoke test interactif post-merge sur ta machine.
2. **Coexistence SLAP/SLAT** — 2 codepaths splat pendant la transition. **Atténuation** : aucune écriture sur SLAP depuis le nouveau code, séparation stricte des chemins. Migration `demo_plains` → SLAT dans un ticket ciblé futur.
3. **Couture inter-chunks (M100.10)** — bug typique : peinture sur cellule de bord oubliée côté chunk voisin → ligne de couture visible. **Atténuation** : test dédié `CrossChunk_PreservesSeam` qui frappe à cheval sur 2 chunks et vérifie l'égalité aux bords.
4. **Invariant somme=255** — facile à casser avec une saturation uint8_t mal gérée. **Atténuation** : `SplatPaintCommand::Execute` renormalise systématiquement après chaque write (test `ManualBrush_PreservesSum255` couvre).
5. **Placeholders au build** — la custom command CMake doit tourner sur Linux + Windows. **Atténuation** : mini-tool C++ standalone (pas de dépendance Python, déjà stb_image dans le repo pour l'écriture PNG si besoin — mais 4×4 PNG canonique est trivial à écrire à la main en < 50 lignes).

## 7. Déploiement

✅ Client/éditeur uniquement. **Pas de redéploiement serveur.** Aucun nouveau opcode, aucun handler serveur, aucune migration DB. Phase 3a est dans la liste « 30 tickets sans redéploiement serveur » du design global §7.

## 8. Hors scope

- Migration `demo_plains` du format SLAP vers SLAT chunked (ticket ciblé futur).
- Génération des vrais assets PBR des 8 layers (asset work séparé).
- `SurfaceQuery` runtime (M100.11, Phase 3b — PIVOT).
- Collision proxies (M100.12, Phase 3b).
- Tampons / stencil par PNG dans le splat paint (futur).
- Test screenshot vs baseline (déféré à un ticket CI-GPU futur).

## 9. Étapes suivantes

1. Lecture et validation de ce document par l'utilisateur.
2. Invocation de `superpowers:writing-plans` pour produire le **plan d'implémentation** Phase 3a sur le modèle du plan Phase 2.
3. Exécution du plan : inline pour les modifs trivialcs (CMake, ChunkPackageLayout, StreamCache, TerrainDocument), subagent pour les blocs lourds (`TerrainChunkPipeline` Vulkan, `SplatPaintTool`).
