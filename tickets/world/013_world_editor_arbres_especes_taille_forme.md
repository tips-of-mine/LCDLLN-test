# 013 — World Editor : arbres (espèces, tailles, formes)

**Statut : livré**

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **009** (instances glTF génériques), **012** (validation client / pipeline zone) | Peupler la forêt de façon crédible et variée | *(rendu jeu / LOD / imposteurs, ou série M… hors `world/`)* |

Le ticket **009** pose le **placement** d’instances (catalogue MVP, snap sol, JSON + export). Ce ticket **spécialise** le cas **arbres** : plusieurs **espèces**, **échelles** cohérentes et **formes** (variété visuelle / biométrie), sans remplacer le schéma `layout_from_editor` : on l’**étend** ou on ajoute des champs documentés côté document d’édition.

---

## Objectif

Permettre à l’auteur de **positionner des arbres** dans le World Editor en choisissant :

1. **L’espèce** (plusieurs types d’arbres : conifère, feuillu large, jeune pousse, etc.) — chaque espèce = entrée de **catalogue** (glTF ou prefab id + métadonnées).
2. **La taille** : au minimum **échelle** par instance (uniforme ou par axe si le pipeline le supporte), idéalement **plage autorisée par espèce** (min / max) + option « variation aléatoire bornée » au placement pour éviter l’uniformité.
3. **La forme** : variété visuelle via **plusieurs meshes** par espèce (ex. A/B/C) ou **paramètre de silhouette** (si plus tard procédural / imposteur) ; en MVP glTF, **plusieurs assets** = plusieurs « formes » explicites.

## Livrables attendus

1. **Catalogue arbres** : fichier ou section JSON (ex. `tree_species[]`) avec pour chaque espèce : `id`, chemins `gltf` (ou plusieurs variantes **forme**), `scale_min` / `scale_max` (mètres ou facteur), tags optionnels (biome, saison).
2. **Modèle d’instance étendu** (rétrocompatible) : champs optionnels sur une instance layout, ex. `species_id`, `shape_variant` (index ou id), `uniform_scale` ou `scale_xyz` documenté ; les instances **009** existantes restent valides (défauts = comportement actuel).
3. **UI WE** : panneau ou mode « Arbres » — choix d’espèce, aperçu des formes disponibles, placement au clic (réutiliser le pick terrain), réglage taille (slider borné par espèce ou saisie + clamp).
4. **Sérialisation** : `WorldMapIo` / document carte + `layout_from_editor.json` (ou extension export) ; **zone_builder** / jeu : documenter ce qui est consommé **dès ce ticket** vs reporté (ex. scale non uniforme si non supporté côté chunk).
5. **Doc** : `docs/terrain_et_world_editor.md` ou `docs/world_editor_zone_pipeline.md` — tableau espèce → fichiers, règles de clamp monde / taille, lien vers ce ticket.

## Critères d’acceptation (DoD)

- [x] Au moins **3 espèces** d’arbres différentes sélectionnables dans le WE (chemins content distincts).
- [x] Pour une même espèce, au moins **2 formes** (deux glTF ou deux entrées `shape_variant`) utilisables au placement.
- [x] La **taille** respecte les bornes de l’espèce (clamp + persistance JSON) ; rechargement carte = même variété.
- [x] Export bundle / layout : pas de régression sur les instances **009** sans nouveaux champs.
- [x] Pas de crash si catalogue incomplet (fichier manquant → log + skip espèce documenté).

## Fichiers concernés (indicatif)

- `engine/editor/WorldMapEditDocument.h`, `WorldMapIo.cpp` (schéma JSON)
- `engine/editor/WorldEditorSession.*`, `WorldEditorImGui.cpp` (UI catalogue arbres)
- `engine/Engine.cpp` (pick / placement si logique partagée)
- `docs/world_zone_demo_checklist.md` (ligne de test optionnelle post-013)

## Dépendances

- **009** ✅ (instances + layout) ; **002** / **006** ✅ (JSON) ; **012** recommandé pour valider le flux jusqu’au client une fois les chunks / builder alignés sur les nouveaux champs.
