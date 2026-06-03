# Issue: world-003

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/world_editor/ui/WorldMapEditDocument.h
- docs/world_editor_zone_pipeline.md

## Note
Heightmap vs zone 10km override

---

## Contenu du ticket (world-003)

# 003 — Heightmap : résolution vs zone 10 km & `terrain.world_size`

**Statut : livré (code + doc)** — override `terrain_world_size_m` dans le document JSON, appliqué au `TerrainRenderer` du World Editor ; nouvelle carte alignée sur `kZoneSize`.

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **002** | Échelle terrain WE ↔ zone 10 km | **004** (manifeste / export), **007** |

---

## Objectif

Aligner l’**étendue monde du terrain** en World Editor sur **`kZoneSize`** (10 km) sans imposer cette valeur au client jeu hors document.

## Réalisé

- `WorldMapEditDocument` : `hasTerrainWorldSizeM`, `terrainWorldSizeM` ; JSON `terrain_world_size_m` (nombre ou `null`).
- `LoadEditDocumentJson` / `SaveEditDocumentJson` ; parse avec plage `]0, 1e7]` m.
- `ActionNewMap` : `terrain_world_size_m = kZoneSize`, `hasTerrainWorldSizeM = true`.
- `TerrainRenderer::Init(..., std::optional<float> terrainWorldSizeMetersOverride)` — dernier paramètre ; jeu inchangé (défaut `nullopt`).
- `Engine::RebuildWorldEditorTerrainGpu` : passe l’override depuis le document.
- `runtime_manifest.json` : clé `terrain_world_size_m`.
- Doc : `docs/world_editor_zone_pipeline.md` §4.1 (limite `m_vertStepWorld` / 1024 notée).

## Critères d’acceptation (DoD)

- [x] Nouvelle carte WE → JSON contient `terrain_world_size_m` à 10 000 (tant que `kZoneSize` = 10 000).
- [x] Recharger JSON avec override → rebuild utilise la même étendue (log `terrain.world_size overridden`).
- [x] Client jeu : `m_terrain.Init` sans dernier argument → comportement config inchangé.

## Fichiers modifiés

- `engine/editor/WorldMapEditDocument.h`
- `engine/editor/WorldMapIo.cpp`
- `engine/editor/WorldEditorSession.cpp`
- `engine/render/terrain/TerrainRenderer.{h,cpp}`
- `engine/Engine.cpp`
- `docs/world_editor_zone_pipeline.md`

## Dépendances

- **001** ✅ ; **002** ✅
