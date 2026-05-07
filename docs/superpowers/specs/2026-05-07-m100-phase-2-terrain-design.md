# M100 Phase 2 — Terrain : design de PR

> **Sortie de session de brainstorming** (2026-05-07).
> **Sujet** : design de la 2ᵉ PR M100 (Phase 2 Terrain, M100.5 → .6 → .7 → .8).
> **Suite de** : [`2026-05-06-m100-execution-design.md`](2026-05-06-m100-execution-design.md) (cadence et stratégie globales — déjà validées) et de la Phase 1 mergée (commit `94e756c`).

## 1. Contexte

Phase 1 (Fondations, M100.1–4) a livré le namespace `engine::editor::world::*`,
le `CommandStack`, l'`EditorCameraController` et l'extraction `zone_builder_lib`.
Phase 2 attaque le **terrain** : structure de données par chunk, sculpt brushes,
stamps + procédural, et chaîne LOD asynchrone.

L'audit ([`2026-05-06-m100-gap-analysis.md`](../audits/2026-05-06-m100-gap-analysis.md))
classe les 4 tickets `vide` avec un effort cumulé 2 L + 2 M ≈ 3500–4500 lignes
(code + tests + 1 PNG d'asset).

## 2. Périmètre

| Ticket | Effort audit | Rôle | Test round-trip |
|--------|--------------|------|-----------------|
| M100.5 Heightmap Data Structure | L | `TerrainChunk` 257² float, format binaire `TRRN`, loader `StreamCache`, mesh builder | **Oui** (`TerrainParityTests` byte-exact) |
| M100.6 Sculpting Brushes | L | Raise/Lower/Smooth/Flatten/Noise, delta sparse, undo via `CommandStack`, couture inter-chunks | Non |
| M100.7 Stamps & Procedural | M | `StampLibrary` PNG 16-bit + générateurs Mountain/Valley/Crater déterministes | Non |
| M100.8 LOD Regeneration | M | `TerrainLodChain` async, LOD0 sync immédiat, LODs N>0 différés | Oui (sortie bit-stable pour mêmes inputs) |

## 3. Décisions structurantes

| # | Question | Décision |
|---|---------|----------|
| 1 | Cohabitation avec le legacy R16 (`HeightmapLoader`, `TerrainRenderer` global, `TerrainEditingTools`) | **Couche au-dessus** : on ne touche pas au legacy, le nouveau code vit en parallèle. La cohabitation se résout en Phase 10 (polissage). |
| 2 | Granularité PR | **1 PR pour la phase entière**. Si la rédaction du plan révèle un découpage évident (M100.5+.8 = data, .6+.7 = UI), basculer en 2a/2b et l'annoncer dans le résumé PR. |
| 3 | Ordre d'implémentation | M100.5 → M100.8 → M100.6 → M100.7. Suit le DAG (`.5` foundation, `.8` ne dépend que de `.5`, `.6` consomme `.5`+`.2`, `.7` consomme `.6`). |
| 4 | Index buffer du `GeometryPass` | Modif ciblée intégrée à M100.5 (commit dédié dans la même série) : passer en index 32-bit pour indexer 257² = 66 049 sommets par chunk. Doit atterrir avec ou avant le premier drawcall mesh-terrain par chunk pour ne pas casser le legacy. |
| 5 | Coutures inter-chunks (M100.6) | Gather voisin en **lecture seule**, écriture des deux côtés dans **une seule `ICommand`** (atomique pour undo/redo). |
| 6 | Async LOD (M100.8) | Thread pool dédié géré par `TerrainLodWorker`. LOD0 calculé sync au commit du brushstroke. LODs N>0 différés. **Flag atomic de cancel** sur nouveau brushstroke pour stale jobs. |
| 7 | Lecture PNG 16-bit (M100.7) | `stb_image` (déjà dans le repo) en mode `STBI_grey`, 16 bpc. Si support 16 bpc absent, fallback `lodepng` (à valider à Task 1 du plan). |
| 8 | Round-trip parité | Tests obligatoires pour M100.5 (byte-exact writer ↔ reader) et M100.8 (regénération bit-stable pour mêmes inputs). Listés dans la section critères d'acceptance des specs. |

## 4. Architecture

```
engine/world/terrain/                ← data, partagé éditeur ↔ client
  TerrainChunk.{h,cpp}               (M100.5)
  TerrainChunkLoader.{h,cpp}         (M100.5)
  TerrainMeshBuilder.{h,cpp}         (M100.5)
  TerrainLodChain.{h,cpp}            (M100.8)
  TerrainLodWorker.{h,cpp}           (M100.8)
  tests/                             (4 fichiers de tests)

engine/editor/world/                 ← tools, éditeur seul
  TerrainDocument.{h,cpp}            (M100.5)
  TerrainBrush.{h,cpp}               (M100.6, kernels)
  TerrainSculptTool.{h,cpp}          (M100.6)
  TerrainSculptCommand.{h,cpp}       (M100.6)
  TerrainRaycast.{h,cpp}             (M100.6)
  TerrainStampTool.{h,cpp}           (M100.7)
  TerrainStampCommand.{h,cpp}        (M100.7)
  StampLibrary.{h,cpp}               (M100.7)
  ProceduralStampGenerators.{h,cpp}  (M100.7)
  tests/                             (2 fichiers de tests)

engine/render/GeometryPass.cpp       ← modif index 32-bit (commit dédié)
engine/world/StreamCache.{h,cpp}     ← modif LoadTerrainChunk
engine/world/StreamingScheduler.cpp  ← modif inclure terrain.bin
tools/zone_builder/lib/...           ← modif WriteTerrainChunk
assets/editor/stamps/                ← 1 PNG 16-bit minimal de test (mountain.png)
```

**Anti-duplication serveur :** `engine/world/terrain/**` exclu du target serveur
via la CMake (vérification : `grep -RIn "engine::world::terrain" engine/server/`
doit retourner 0). `engine::editor::world::*` était déjà exclu en Phase 1.

## 5. Tests prévus (TDD red → green, 1 commit par couleur)

| Ticket | Fichier de tests | Tests minimaux |
|--------|------------------|----------------|
| M100.5 | `engine/world/terrain/tests/TerrainChunkTests.cpp` | construction, validation header, MakeFlat, sample bilinear, gradient, MeshBuilder vertex count |
| M100.5 | `engine/world/terrain/tests/TerrainParityTests.cpp` | **round-trip byte-exact** : SaveTerrainBin → LoadTerrainBin → comparaison heights[] et header |
| M100.6 | `engine/editor/world/tests/TerrainSculptTests.cpp` | kernel Raise/Lower/Smooth/Flatten/Noise, delta sparse compact, undo/redo via CommandStack, seam multi-chunk (gather + écriture deux côtés en 1 Command) |
| M100.7 | `engine/editor/world/tests/TerrainStampTests.cpp` | charger PNG 16-bit, générateurs Mountain/Valley/Crater **déterministes** pour seed fixe, application non-destructive (preview), Apply commit à la stack |
| M100.8 | `engine/world/terrain/tests/TerrainLodChainTests.cpp` | LOD0 calculé sync, LODs N>0 file dans le worker, **cancel sur nouveau brushstroke**, sortie bit-stable pour mêmes inputs |

Estimation : ~25–35 tests neufs.

## 6. Risques détectés à l'audit

1. **Coexistence R16 legacy ↔ TRRN nouveau** — atténuation : couche au-dessus, zéro modif au TerrainRenderer global. Impact : duplication logique pendant la transition (assumée).
2. **Index 32-bit GeometryPass** — modif isolée dans un commit dédié. Risque mineur si le pipeline accepte déjà des index buffers 32-bit (à confirmer Task 1 du plan).
3. **Multi-chunk seam M100.6** — bug typique : delta écrit sur chunk A et oublié sur chunk B → discontinuité visible. Atténuation : test dédié `TerrainSculptTests::Seam` qui frappe à cheval sur 2 chunks et vérifie l'égalité aux bords.
4. **Async LOD M100.8** — bug typique : LOD calculé sur un état stale juste avant un nouveau brushstroke et appliqué après. Atténuation : ID de génération atomique incrémenté à chaque sculpt commit ; LOD jeté si génération mismatch à la fin du job.
5. **PNG 16-bit M100.7** — risque : `stb_image` ne supporte pas tous les sous-formats 16 bpc selon la version embarquée. Atténuation : tester en Task 1 ; fallback `lodepng` si nécessaire.
6. **Volume PR borderline (~3500–4500 lignes)** — atténuation : laisser au plan le soin de décider 1 PR vs 2 PRs (2a data / 2b UI) selon la profondeur réelle.

## 7. Déploiement

✅ Client/éditeur uniquement. **Pas de redéploiement serveur.** Aucun nouveau
opcode, aucun handler serveur, aucune migration DB. (Conforme au design global
§7 : Phase 2 est dans la liste « 30 tickets sans redéploiement serveur ».)

## 8. Hors scope

- Splat painting / SurfaceQuery (Phase 3, M100.9–12).
- Modification du `TerrainRenderer` global (R16) ou du `HeightmapLoader` legacy.
- Génération procédurale au-delà des 3 archétypes Mountain/Valley/Crater.
- Test end-to-end éditeur → chunk package → client en mode jeu (charnière M100.34, Phase 10).
- Optimisations perf au-delà des budgets explicitement écrits dans les specs.

## 9. Étapes suivantes

1. Lecture et validation de ce document par l'utilisateur.
2. Invocation de `superpowers:writing-plans` pour produire le **plan d'implémentation** Phase 2 sur le modèle du plan Phase 1 ([`2026-05-06-m100-phase-1-fondations.md`](../plans/2026-05-06-m100-phase-1-fondations.md)).
3. Exécution du plan via `superpowers:executing-plans` ou `superpowers:subagent-driven-development`.
