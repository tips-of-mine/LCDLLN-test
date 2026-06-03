# Issue: world-007

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- game/data/zones/demo_plains/
- docs/world_zone_demo_checklist.md

## Note
Zone demo validation+doc

---

## Contenu du ticket (world-007)

# 007 — Zone démo : validation in-game & documentation build

**Statut : livré** — `game/data/zones/demo_plains/` (artefacts minimum + `chunks/chunk_0_0/`), `docs/world_zone_demo_checklist.md`, README racine section World, script `Generate-DemoPlainsAssets.ps1`.

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **002** → **006** (pipeline fonctionnel) | Preuve par l’exemple + onboarding | — (boucle prod / gameplay) |

---

## Objectif

- Zone démo sous contrôle de version (`game/data/zones/demo_plains/`).
- Checklist reproductible (World Editor optionnel, client, logs).
- Liens README racine vers le pipeline + build `zone_builder` / `lcdlln_world_editor`.

## Livrables réalisés

1. **`demo_plains`** : `terrain_height.r16h`, `zone.meta`, `runtime_manifest.json`, `layout_from_editor.json`, `probes.bin`, `atmosphere.json`, `chunks/chunk_0_0/chunk.meta` + `instances.bin`.
2. **`docs/world_zone_demo_checklist.md`** : étapes numérotées, clés `paths.content`, `render.terrain.*`, option `world.zone_meta_path` / `world.probes_path` / `world.atmosphere_path`.
3. **`README.md`** racine : section World / zones + table des exécutables + liens docs.

## Critères d’acceptation (DoD)

- [x] Nouveau dev : checklist + doc → terrain démo visible en **≈ 20–30 min** (indicatif ; dépend de l’install toolchain).
- [x] Pas de régression CI (fichiers data + docs + script ; pas de changement CI requis).
- [x] `zone_id` **demo_plains** documenté ; `config.json` d’équipe inchangé (override documenté dans la checklist, pas imposé au dépôt).

## Fichiers concernés

- `game/data/zones/demo_plains/*`
- `tools/world/Generate-DemoPlainsAssets.ps1`
- `docs/world_zone_demo_checklist.md`
- `docs/world_editor_zone_pipeline.md` (§8 lien checklist)
- `README.md`

## Dépendances

- **001** ✅ ; **002–006** ✅ pour une démo alignée pipeline.
