# Catalogue de grottes — `game/data/meshes/caves/`

> M100.40 — Mesh Insert Foundation + Cave Tool (Phase 11 « Volumes 3D »).

## Statut MVP

Le fichier `catalog.json` liste 3 grottes (`cave_small_01`, `cave_medium_01`,
`cave_large_01`) avec leurs métadonnées (AABB, entrancePoint, volume
intérieur). Les **assets `.gltf` et `thumbnails/*.png` ne sont pas livrés
dans cette PR** — ils seront produits par un ticket d'art dédié.

Conséquence pour l'éditeur :
- L'outil **Cave** charge le catalogue et permet la sélection + placement.
- Le `MeshInsertDocument` persiste correctement les instances dans
  `instances/mesh_inserts.bin` (format LCMI v1).
- Le **rendu visuel** des grottes n'est pas câblé tant que `tinygltf` (ou
  équivalent) n'est pas intégré et que le runtime client (`MeshInsertRuntime`,
  `MeshInsertReader`) n'est pas livré.
- La logique de camouflage splat « Rocher » est, elle, fonctionnelle
  immédiatement (modifie `splat.bin` via `TerrainDocument`).

## Convention de fichiers

Quand le ticket d'art livre les assets :

- `meshes/caves/<id>.gltf` (+ `.bin` pour les buffers si gltf séparé).
- `meshes/caves/thumbnails/<id>.png` 128×128 RGBA.
- Coordonnées modélisées en mètres, origine au point d'ancrage (le
  `entrancePoint` du catalog est exprimé dans ce repère pivot-relatif).

## Champs `catalog.json`

| Champ | Type | Description |
|---|---|---|
| `id` | string | Identifiant unique (clé de sélection). |
| `gltf` | string | Chemin content-relatif vers le mesh. |
| `displayName` | string | Nom affiché dans l'UI (FR). |
| `thumbnail` | string | Chemin content-relatif vers le PNG 128×128. |
| `aabbMin`/`aabbMax` | `[x,y,z]` | AABB pivot-relatif (mètres). |
| `entrancePoint` | `[x,y,z]` | Point de contact entrée ↔ terrain. Le snap au sol ajuste `worldPosition.y = terrain_y - entrancePoint.y`. |
| `interiorAabbMin`/`interiorAabbMax` | `[x,y,z]` | Sous-AABB du volume jouable. Consommé par `SurfaceQueryService` (M100.11) pour détecter quand le joueur entre dans la grotte. |
