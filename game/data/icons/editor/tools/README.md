# Icônes d'outils éditeur monde — `game/data/icons/editor/tools/`

> M100.35 — Toolbar à icônes + Outils macros terrain.

Ce dossier est **réservé** aux icônes des boutons de la barre d'outils du
World Editor (`lcdlln_world_editor.exe`).

## Statut actuel

**Aucun fichier PNG n'est livré dans cette PR.** La toolbar M100.35 utilise
des **placeholders procéduraux** : un carré coloré (couleur définie par
`ToolbarIconAtlas::Get(tool)`) + une lettre centrale (`S` pour Sculpt,
`T` pour Stamp, `P` pour Splat Paint, `L` pour Lake, `R` pour River, `M`
pour Mountain Range, `V` pour Valley Chain, `X` pour Désélection).

Le test `editor_toolbar_tests::Test_Toolbar_MissingIcon_FallsBackToPlaceholder`
valide explicitement que l'absence de PNG ne provoque pas de crash.

## Convention pour le futur ticket art

Quand un ticket d'art remplacera les placeholders par les icônes finales,
chaque fichier doit respecter :

- **Nom** : `<tool_slug>.png`, exactement un par bouton.
  - `sculpt.png`         — TerrainSculptTool (M100.6)
  - `stamp.png`          — TerrainStampTool (M100.7)
  - `splat.png`          — SplatPaintTool (M100.10)
  - `lake.png`           — LakeTool (M100.13)
  - `river.png`          — RiverTool (M100.13)
  - `mountain_range.png` — MountainRangeTool (M100.35)
  - `valley_chain.png`   — ValleyChainTool (M100.35)
  - `deselect.png`       — bouton X (None)
- **Dimensions** : 32 × 32 px exactement.
- **Format** : PNG, RGBA8, alpha utilisé pour le détourage.
- **Style** : monochrome `#E6E6E6` sur fond transparent (la couleur de fond
  est appliquée dynamiquement par `EditorToolbar::Render` selon l'outil
  actif).
- **Pas de bordure** intégrée : la bordure est dessinée à la frame par
  `ImDrawList::AddRect` (ambre quand actif, gris sinon).

## Comment activer le chargement PNG

Le chemin de fichier `game/data/icons/editor/tools/<slug>.png` est testé par
`ToolbarIconAtlas::Get` (extension future). Le ticket d'art devra :

1. Ajouter le décodage PNG via `TexturePreviewCache` (déjà utilisé pour
   les vignettes splat).
2. Étendre `ToolIconStyle` avec un champ `ImTextureID iconTexture` quand
   un PNG est disponible.
3. Côté `EditorToolbar::Render`, remplacer la branche `AddText(letter)`
   par une `AddImage(iconTexture, ...)` si non-nulle.

Le contrat actuel (couleurs / tooltips / enabled) reste valide.
