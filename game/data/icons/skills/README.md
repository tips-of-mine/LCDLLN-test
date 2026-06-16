# assets/icons/skills/

🇫🇷 FRANÇAIS
## Pourquoi ce dossier existe
Icônes des **compétences par-classe** (arbre de compétences, Grimoire, barre
d'action). Distinctes des icônes fonctionnelles de `icons/ui/` (navigation/état) :
ici c'est du **contenu gameplay**.

## Organisation
Un sous-dossier par classe, fichier nommé d'après l'`id` de la compétence
(tel qu'il figure dans `game/data/gameplay/class_skills/<classId>.json`) :

```
icons/skills/<classId>/<skillId>.png
```

Exemples :
- `icons/skills/pretre_jugement/pretre_jugement_single_t1.png`
- `icons/skills/pretre_jugement/pretre_jugement_aoe_t1.png`
- `icons/skills/guerrier/guerrier_def_t5.png`

Le nom de fichier **doit** correspondre exactement à l'`id` (le mapping est
automatique, aucune édition des JSON nécessaire). 24 classes × 180 = 4320 icônes
attendues au total (180 par sous-dossier de classe).

## Format
- **PNG**, carré, fond **transparent**.
- Taille recommandée : **64×64** (ou 128×128 pour plus de netteté).
- Chemin chargé au runtime, relatif à `paths.content` (= `game/data`) :
  `icons/skills/<classId>/<skillId>.png`.

## Note
Déposer les fichiers ne suffit pas à les afficher : le rendu (chargement de la
texture + `ImGui::Image` dans l'arbre / le Grimoire / la barre d'action) doit
être câblé côté client. Tant qu'une icône est absente, l'UI retombe sur le
libellé texte.

---

🇬🇧 ENGLISH
## Why this folder exists
Per-class **skill icons** (skill tree, Grimoire, action bar). Distinct from the
functional icons in `icons/ui/` (navigation/status): this is **gameplay content**.

## Layout
One subfolder per class, file named after the skill `id` (as found in
`game/data/gameplay/class_skills/<classId>.json`):

```
icons/skills/<classId>/<skillId>.png
```

The filename **must** match the `id` exactly (automatic mapping, no JSON edits).
24 classes × 180 = 4320 icons total (180 per class subfolder).

## Format
- **PNG**, square, **transparent** background.
- Recommended size: **64×64** (or 128×128 for sharper rendering).
- Runtime path, relative to `paths.content` (= `game/data`):
  `icons/skills/<classId>/<skillId>.png`.
