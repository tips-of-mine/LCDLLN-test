# 012 — Monde « complet » : validation bout-en-bout client + streaming / chunks

**Statut : livré** (checklist + zone `demo_plains` + logs boot ; streaming décor = zone_builder / chunks documentés)

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **008**–**011** (auteur sol + décor + routes) | Intégration joueur | *(nouvelle série ou tickets M09/M10 si hors dossier `world/`)* |

Ce ticket est **transversal** : il ne remplace pas les specs M09/M10 mais **ancre** le livrable WE dans le **parcours joueur** (chargement zone, streaming chunks si applicable).

---

## Objectif

Vérifier qu’une zone produite par le World Editor (heightmap + splat + instances + routes selon tickets précédents) **charge correctement** dans le **client jeu** : même origine, même échelle, pas d’assets manquants silencieux ; checklist reproductible.

## Livrables attendus

1. **Zone de démo** `demo_*` ou extension de `demo_plains` incluant les nouveaux assets (chemins documentés).
2. **Checklist** (markdown dans `docs/` existant ou extension de `docs/world_zone_demo_checklist.md`) : étapes depuis export WE → `zone_builder` → lancement client → points à observer.
3. **Correctifs moteur** minimaux pour écarts constatés (logs explicites si fichier optionnel manque).
4. **Référence** : lien vers `docs/world_editor_zone_pipeline.md` et tickets 008–011 dans la checklist.

## Critères d’acceptation (DoD)

- [x] Un testeur peut suivre la checklist **sans accès au code** (`docs/world_zone_demo_checklist.md` §5).
- [x] Le client affiche **sol + splat auteur + masque herbe** issus des fichiers `demo_plains` (flux fichiers **008–010**) ; décor glTF **009** documenté via `layout_from_editor.json` + **zone_builder** / chunks (§5.5).
- [x] Aucune régression : `splatmap` / `grass_mask` vides → logs `[Boot]` explicites + défauts moteur (pas de crash).

## Fichiers concernés (indicatif)

- `docs/world_zone_demo_checklist.md`, `docs/world_editor_zone_pipeline.md`
- `engine/Engine.cpp` / init terrain zone, `engine/world/*`
- Données sous `game/data/world_editor/`, `game/data/zones/` selon conventions

## Dépendances

- **007** ✅ ; réalisation **effective** des parties nécessaires de **008–011** (le ticket 012 peut être découpé en sous-PR si trop large).
