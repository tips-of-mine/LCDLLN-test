# Issue: world-002

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/world_editor/ui/WorldMapIo.cpp
- src/world_editor/ui/WorldMapEditDocument.h

## Note
world_map.edit JSON parse/save roundtrip

---

## Contenu du ticket (world-002)

# 002 — `WorldMapEditDocument` / JSON édition : champs, parsing, versionnement

**Statut : livré (code)** — parsing `textures` / `objects`, plafond 4096, refus `version` > support.

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **001** (spec doc) | Fichier `map.lcdlln_edit.json` fiable | **003** (champs optionnels terrain), **004** (export qui duplique les listes) |

---

## Objectif

Rendre le format **`map.lcdlln_edit.json`** **symétrique** : ce qui est sauvegardé est relu à l’identique, avec erreurs explicites et politique de **version**.

## Implémentation (réalisée)

- `LoadEditDocumentJson` : parse `textures` et `objects` (tableaux JSON de strings uniquement) ; clé absente ou `null` → vecteurs vides.
- Plafond **4096** entrées par tableau.
- Si `version` > `WorldMapEditDocument::kFormatVersion` → erreur `version document non supportée: …`.
- Doc : `docs/world_editor_zone_pipeline.md` §4.

## Critères d’acceptation (DoD)

- [x] Round-trip save/load listes `textures` / `objects`.
- [x] Tableau avec éléments non-string → erreur claire.
- [x] `version` trop élevée → erreur claire.
- [x] Pas de régression `zone_id`, `size`, `seed`, `heightmap`.

## Fichiers modifiés

- `engine/editor/WorldMapIo.cpp`
- `docs/world_editor_zone_pipeline.md`

## Dépendances

- **001** ✅
