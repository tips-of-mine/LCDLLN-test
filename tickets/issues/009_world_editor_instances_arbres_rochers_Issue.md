# Issue: world-009

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/world_editor/ui/WorldMapEditDocument.h
- game/data/zones/demo_plains/layout_from_editor.json

## Note
Instances arbres/rochers

---

## Contenu du ticket (world-009)

# 009 — World Editor : placement d’instances (arbres, rochers, « prefabs »)

**Statut : livré**

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **008** (splat au sol) | Obstacles / décor discrets réutilisables | **010** (herbe) — souvent guidée par masque ou splat |

Le **layout** JSON (`objects` / `instances`) existe déjà côté parsing (002) ; ce ticket vise **la boucle complète** : placer dans l’UI WE, sauver, exporter, afficher dans le moteur (ou stub draw debug jusqu’au pipeline props).

---

## Objectif

Créer un **premier workflow auteur** pour peupler la zone : placer, déplacer, supprimer des **instances** référencées par id de mesh / de prefab (MVP : liste fixe ou fichier JSON de catalogue), avec **snap au terrain** (heightmap CPU ou sampling cohérent avec le rendu).

## Livrables attendus

1. **Modèle de données** : structure d’instance (position, rotation Y, échelle uniforme ou vec3, `asset_id` ou chemin mesh relatif) stockée dans le document carte / `layout.json` exporté.
2. **UI WE** : panneau liste + placement au clic sur le sol (raycast terrain) + gizmo minimal ou drag sur XZ.
3. **Sérialisation** : sauvegarde / chargement ; export bundle inchangé ou étendu pour inclure le layout enrichi.
4. **Rendu MVP** : soit dessin des instances dans la vue éditeur via le chemin mesh existant (`AssetRegistry`), soit document explicite « rendu jeu = ticket suivant » avec draw debug dans le WE.
5. **Filtres simples (optionnel MVP+)** : pas d’instance sous l’eau / pente max (utilise heightmap + normale si dispo).

## Critères d’acceptation (DoD)

- [x] Au moins **2 types** d’assets différents placables et visibles après sauvegarde + reload.
- [x] **Export** : le fichier layout consommé par `zone_builder` / jeu contient les instances avec coordonnées monde cohérentes avec `terrain.world_size` / origine documentée.
- [x] Supprimer une instance met à jour disque + UI sans corruption du JSON.
- [x] Pas de fuite mémoire évidente sur création / destruction répétée (sanity manual ou test léger).

## Fichiers concernés (indicatif)

- `engine/editor/WorldEditorImGui.cpp`, `WorldEditorSession.*`, `EditorMode.*`
- `engine/Engine.cpp` (pick terrain déjà partiellement là)
- Schéma JSON carte / `WorldMapIo.*`, export runtime bundle

## Dépendances

- **008** recommandé (repères visuels sol) ; **002** / **006** ✅ pour le format JSON existant.
