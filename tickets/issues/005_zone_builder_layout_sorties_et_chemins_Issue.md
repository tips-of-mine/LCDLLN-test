# Issue: world-005

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- tools/zone_builder/main.cpp
- game/data/zones/_templates/layout_minimal.json

## Note
Zone builder layout sorties/chemins

---

## Contenu du ticket (world-005)

# 005 — `zone_builder` : layout, sorties chunk & alignement `paths.content`

**Statut : livré** — `--help`, exemple `layout_minimal.json`, validations (layout / glTF / chunk / positions XZ), `--output` avec préfixe `zones/` via `FileSystem::ResolveContentPath`, `--zone-id` optionnel, doc §3.

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **001** (chemins), **004** (contrat dossier zone) | Outil fiable et prévisible | **006** (appelle `zone_builder` avec des args stables), **007** (repro build) |

---

## Objectif

`zone_builder` exécutable **reproductible** : chemins relatifs à `paths.content`, codes de retour stricts, `--help` complet, sortie alignable sur **`zones/<zone_id>/`** du jeu.

## Contexte (livraisons existantes)

- `main.cpp` : modes layout vs legacy.
- Sortie layout : `WriteChunkedZoneOutputs` → `zone.meta`, `probes.bin`, `atmosphere.json`, `chunks/chunk_x_z/{chunk.meta,instances.bin}`.
- Défaut `--output` = `<cwd>/build/zone_0` si omis.
- Instance avec position XZ hors **[0, kZoneSize)** : **rejet** au parse layout ; indices chunk hors grille → erreur au build chunk.

## Livrables

1. **`--help`** : deux modes, exemples relatifs au content, mention `config.json`.
2. **Exemple** : `game/data/zones/_templates/layout_minimal.json`.
3. **Validation** : layout introuvable → exit 1 ; glTF manquant → exit 1 ; chunk legacy négatif → exit 1 ; positions layout hors zone → exit 1.
4. **`--output`** : si commence par `zones/`, résolution via `ResolveContentPath` + config ; sinon relatif/absolu au cwd (documenté §3.1).
5. **`--zone-id`** (optionnel) : cohérence avec le dossier `zones/<id>/` résolu.

## Critères d’acceptation (DoD)

- [x] `zone_builder --help` documenté (console + doc).
- [x] Run layout sur l’exemple : fichiers créés listés dans la doc (§3.3 inchangé structurellement).
- [x] Instance `positionX` (et Z) hors **[0, kZoneSize)** : **rejet** + doc.
- [x] Pas de nouvelle dépendance (AGENTS).

## Fichiers concernés

- `tools/zone_builder/main.cpp`
- `tools/zone_builder/LayoutImporter.cpp`
- `tools/zone_builder/ChunkPackageWriter.cpp`
- `tools/zone_builder/GltfImporter.cpp`
- `game/data/zones/_templates/layout_minimal.json`
- `docs/world_editor_zone_pipeline.md`

## Dépendances

- **001** ✅ ; **004** recommandé pour vocabulaire « bundle zone ».
