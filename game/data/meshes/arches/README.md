# Catalogue d'arches naturelles — `game/data/meshes/arches/`

> M100.42 — Natural Arch Tool (Phase 11 « Volumes 3D »).

## Statut MVP

Le fichier `catalog.json` liste 3 arches (`arch_small_01`,
`arch_medium_01`, `arch_large_01`) avec leurs métadonnées (AABB,
archAnchorA, archAnchorB, archHeight). Les **assets `.gltf` et
`thumbnails/*.png` ne sont pas livrés dans cette PR** — ticket d'art
dédié.

L'outil **Arch** réutilise `MeshInsertDocument` + LCMI v1 (introduits
par M100.40) ; pas de nouveau format binaire. Les instances ont
`insertCategory = "arch"`.

## Workflow de placement

L'utilisateur fournit deux points monde (pieds A et B). Le tool calcule
automatiquement :

| Sortie | Formule |
|---|---|
| `worldPosition` | milieu (`pointA`, `pointB`) |
| `eulerRotationDeg.y` | `atan2(B.z − A.z, B.x − A.x)` |
| `uniformScale` | `span_monde / span_natif` |

Garde-fou : si le scale dérivé est hors `[minScaleRatio,
maxScaleRatio]`, le placement est refusé (arche écrasée ou étirée).

## Champs `catalog.json`

| Champ | Type | Description |
|---|---|---|
| `id` | string | Identifiant unique. |
| `gltf` | string | Chemin content-relatif vers le mesh. |
| `displayName` | string | Nom affiché. |
| `thumbnail` | string | PNG 128×128. |
| `aabbMin`/`aabbMax` | `[x,y,z]` | AABB pivot-relatif. |
| `archAnchorA` | `[x,y,z]` | Pied A (pivot-relatif, Y au sol modélisé). |
| `archAnchorB` | `[x,y,z]` | Pied B (pivot-relatif). |
| `archHeight` | float | Distance corde A-B → clé d'arc (m). Indicatif. |
