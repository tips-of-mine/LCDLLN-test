# Issue: world-001

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/world_editor/ui/WorldMapEditDocument.h
- docs/world_editor_zone_pipeline.md

## Note
Spec pipeline monde documentee+implementee

---

## Contenu du ticket (world-001)

# 001 — Spec pipeline World Editor + zone_builder & contraintes monde

**Statut : livré (documentation)** — la spec détaillée est dans le dépôt, pas seulement dans ce ticket.

## Référence principale

→ **`docs/world_editor_zone_pipeline.md`**

Ce document couvre : constantes monde (`kZoneSize`, `kChunkSize`, `kSpatialCellSizeMeters`), chemins World Editor, contenu de l’export runtime, CLI et sorties `zone_builder`, format JSON d’édition, chaîne recommandée et lien vers `docs/terrain_et_world_editor.md`.

## Objectif du ticket (rappel)

Une **source unique** pour l’équipe : où sont les fichiers, quels invariants numériques, comment l’éditeur et l’outil offline s’articulent avec le runtime.

## Critères d’acceptation (001)

- [x] Un lecteur sait où écrire / lire les artefacts sans ouvrir le C++ en premier recours.
- [x] Les valeurs actuelles des constantes monde sont documentées (10 km, 500 m chunk, 100 m cellules).
- [x] Lien vers le rendu terrain : `docs/terrain_et_world_editor.md`.

## Suivi

- Implémentations code / correctifs : tickets **002** et suivants (`tickets/world/README.md`).
- Toute évolution de spec : **éditer d’abord** `docs/world_editor_zone_pipeline.md`, puis ajuster les tickets concernés.
