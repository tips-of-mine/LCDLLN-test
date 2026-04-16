# 006 — Pont World Editor → `zone_builder` (workflow ou automatisation)

**Statut : livré** — `layout_from_editor.json` écrit à l’export runtime ; scripts `tools/world/export_zone_with_chunks.{ps1,sh}` ; doc §5.1 (commandes Windows / Linux).

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **004** (export WE), **005** (builder stable) | Une seule procédure « heightmap + chunks » | **007** (démo + checklist sur ce flux) |

Sans **005**, les arguments et dossiers du builder bougent ; sans **004**, il manque des fichiers sous `zones/<id>/` avant/après builder.

---

## Objectif

Une personne (ou le pipeline CI) peut produire **`zones/<zone_id>/`** avec **terrain_height** (WE) **et** `chunks/` (builder) sans deviner les chemins.

## Livrables réalisés

1. **`layout_from_editor.json`** minimal (`version`, `instances: []`) généré par **`ExportRuntimeBundle`** sous `zones/<id>/` (nom validé ticket).
2. **Scripts** `tools/world/export_zone_with_chunks.ps1` + `export_zone_with_chunks.sh` : `ZONE_ID`, `LCDLLN_BUILD_DIR`, `ZONE_BUILDER` optionnel ; pas de chemin absolu codé en dur dans le dépôt.
3. **Section §5.1** dans `docs/world_editor_zone_pipeline.md` (commandes copier-coller Windows + Linux + équivalent manuel `zone_builder`).

## Critères d’acceptation (DoD)

- [x] Après la séquence documentée, `zones/<id>/` contient au minimum ce qui est décrit pour le flux (export WE + `zone_builder`).
- [x] `zone_builder` exit 0 sur le layout stub fourni (instances vides).
- [x] Aucun chemin absolu codé en dur vers un poste développeur (AGENTS).

## Fichiers concernés

- `engine/editor/WorldMapIo.cpp` / `WorldMapIo.h`
- `tools/world/export_zone_with_chunks.ps1`
- `tools/world/export_zone_with_chunks.sh`
- `docs/world_editor_zone_pipeline.md`

## Dépendances

- **001** ✅ ; **004** ✅ ; **005** ✅.
