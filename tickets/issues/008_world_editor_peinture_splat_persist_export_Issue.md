# Issue: world-008

**Status:** Closed

_Verifie automatiquement le 2026-06-03 (analyse de code approfondie, reorganisation tickets)._

## Preuves d'implementation
- src/world_editor/ui/WorldMapEditDocument.h
- game/data/zones/demo_plains/terrain_splat.slap

## Note
Peinture splat persist/export

---

## Contenu du ticket (world-008)

# 008 — World Editor : peinture splat (herbe / sable / roc…), persistance et export

**Statut : livré**

## Chaînement

| Précédent | Ce ticket | Suivant |
|-----------|-----------|---------|
| **007** (démo + flux export validé) | Données sol « auteur » autres que la heightmap | **009** (instances) — le splat guide souvent le placement |

Sans **007**, le cycle sauvegarde / export zone reste fragile à valider ; ce ticket suppose `TerrainSplatting` et les shaders terrain déjà opérationnels côté moteur (voir `docs/terrain_et_world_editor.md`, ticket M34.x).

---

## Objectif

Permettre à un auteur de **peindre les couches matériaux** (splat) directement dans le World Editor, d’**enregistrer** la splatmap avec le document de carte, et d’**inclure** ce fichier dans le bundle exporté (`zones/<id>/` ou équivalent documenté) pour que le client jeu et le builder voient les mêmes couches.

## Livrables attendus

1. **UI éditeur** : mode brosse splat (au moins : rayon, opacité / force, choix de couche ou index de matériau aligné sur `TerrainSplatting`).
2. **Persistance** : chemin relatif content de la splatmap dans le JSON carte / session WE ; écriture disque sous `world_editor/maps/<zone>/` (ou convention actuelle du doc monde).
3. **Rechargement GPU** : après sauvegarde ou swap de fichier, `RequestTerrainGpuReload` (ou équivalent) recharge splat + heightmap sans redémarrer l’exe.
4. **Export** : le fichier splat est copié / référencé dans l’export runtime documenté (`docs/world_editor_zone_pipeline.md` § inventaire si besoin).
5. **Doc courte** : paragraphe dans `docs/terrain_et_world_editor.md` ou `docs/world_editor_zone_pipeline.md` (chemins, format texture, limites résolution).

## Critères d’acceptation (DoD)

- [x] Peindre au moins **2 couches** distinctes visibles en rendu dans le WE après « Nouvelle carte » ou chargement d’une carte existante.
- [x] **Sauvegarder** puis **rouvrir** la même zone : la peinture est identique (pas de perte de précision hors spec volontaire).
- [x] Après **export** du bundle, le fichier splat est présent à l’emplacement documenté et réutilisable par le jeu (chargement terrain côté client si déjà câblé).
- [x] Aucun chemin absolu codé en dur vers un poste développeur (AGENTS).

## Fichiers concernés (indicatif)

- `engine/editor/WorldEditorImGui.cpp` / `WorldEditorSession` / I/O carte
- `engine/render/terrain/TerrainSplatting.*`, `TerrainRenderer.*`, `TerrainEditingTools.*`
- `engine/editor/WorldMapIo.*` ou équivalent export bundle

## Dépendances

- **007** ✅ (flux zone / démo) ; alignement conceptuel **M34.2** / **M34.4** (splat + outils) si périmètre chevauché — coordonner pour éviter doublon de spec.
