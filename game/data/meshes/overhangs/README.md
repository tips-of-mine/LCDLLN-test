# Catalogue de surplombs — `game/data/meshes/overhangs/`

> M100.41 — Overhang Cliff Tool (Phase 11 « Volumes 3D »).

## Statut MVP

Le fichier `catalog.json` liste 3 surplombs (`overhang_small_01`,
`overhang_medium_01`, `overhang_large_01`) avec leurs métadonnées (AABB,
wallAnchorPoint, wallNormalDirection, coverageRadius). Les **assets
`.gltf` et `thumbnails/*.png` ne sont pas livrés dans cette PR** —
ticket d'art dédié.

L'outil **Overhang** réutilise `MeshInsertDocument` + LCMI v1
(introduits par M100.40) ; pas de nouveau format binaire. Les instances
ont `insertCategory = "overhang"`.

Détection cliff automatique (raycast normal terrain) : reportée à
M100.17 (gizmo + raycast viewport). En MVP, l'utilisateur saisit
manuellement les coordonnées + valide la slope locale via un slider.

## Champs `catalog.json`

| Champ | Type | Description |
|---|---|---|
| `id` | string | Identifiant unique (clé de sélection). |
| `gltf` | string | Chemin content-relatif vers le mesh. |
| `displayName` | string | Nom affiché dans l'UI (FR). |
| `thumbnail` | string | PNG 128×128 content-relatif. |
| `aabbMin`/`aabbMax` | `[x,y,z]` | AABB pivot-relatif. |
| `wallAnchorPoint` | `[x,y,z]` | Point de contact mesh ↔ falaise (pivot-relatif). |
| `wallNormalDirection` | `[x,y,z]` | Vecteur sortant du mesh, dirigé vers le vide. Le tool ajuste yaw pour aligner avec la normale terrain. |
| `coverageRadius` | float | Rayon (m) de l'ombre projetée — utile pour SurfaceQuery / lighting. |
