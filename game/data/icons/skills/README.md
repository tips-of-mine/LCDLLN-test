# icons/skills/

🇫🇷 FRANÇAIS
## Pourquoi ce dossier existe
Icônes des **compétences par-classe** (arbre de compétences, Grimoire, barre
d'action). Distinctes des icônes fonctionnelles de `icons/ui/` (navigation/état) :
ici c'est du **contenu gameplay**.

## Organisation
Un sous-dossier par classe ; chaque fichier porte le nom exact du champ `iconFile`
de la compétence dans `game/data/gameplay/class_skills/<classId>.json` :

```
icons/skills/<classId>/<iconFile>
```

Exemple : `icons/skills/guerrier/Guerrier_Unitaire_Niv08_Estoc-Funeste.png`.
Le chemin est construit par `ClassSkillCatalog` (`src/client/gameplay/ClassSkillCatalog.cpp`).
24 classes × 180 = 4320 icônes (180 par sous-dossier de classe).
`branches/` contient les bannières d'en-tête des trois colonnes de l'arbre.

## Fabrication (ne pas éditer les PNG à la main)
Les icônes sont **composées** par `tools/skills/compose_skill_icons.py` :

1. **Motif central** : un motif peint par *compétence de base* (18 par classe,
   3 branches × 6), partagé par toutes ses déclinaisons (« Estoc », « Estoc Funeste »,
   « Estoc Maitrise 50 »…). Sources détourées en 512 px dans
   `tools/skills/icon_motifs/<classId>/<branche>_<slug>.png` (Git LFS).
2. **Cadre** : bordure de la couleur du palier — niveaux 1-10 bronze, 11-20 argent,
   21-30 or, 31-40 violet, 41-50 orange, 51-60 cyan.
3. **Points de rang** : 1 à 10 points selon le rang dans le palier.

```
python3 tools/skills/compose_skill_icons.py --class guerrier   # une classe
python3 tools/skills/compose_skill_icons.py --all              # toutes
```

Contrôle en CI (`.github/workflows/skill-icons.yml`) :
`tools/skills/validate_skill_icons.py` vérifie que chaque `iconFile` et chaque motif
existent, puis les tests de `tools/skills/tests/`.

Les motifs ont été générés avec SDXL base 1.0 (licence CreativeML Open RAIL++-M,
usage commercial autorisé), style pictural, sans reproduction d'éléments de la BD.

## Format
- **PNG** RGBA 128×128, coins transparents.
- Stockés dans **Git LFS** (`.gitattributes`) ; la CI `build-windows` fait le
  checkout avec `lfs: true` pour les empaqueter dans le client.
- Icône absente : l'interface retombe sur le libellé texte.

---

🇬🇧 ENGLISH
## Why this folder exists
Per-class **skill icons** (skill tree, Grimoire, action bar). Distinct from the
functional icons in `icons/ui/` (navigation/status): this is **gameplay content**.

## Layout
One subfolder per class; each file is named exactly after the skill's `iconFile`
field in `game/data/gameplay/class_skills/<classId>.json`:

```
icons/skills/<classId>/<iconFile>
```

24 classes × 180 = 4320 icons (180 per class subfolder). `branches/` holds the
three skill-tree column banners.

## How they are made (do not hand-edit the PNGs)
Icons are **composed** by `tools/skills/compose_skill_icons.py` from one painted
motif per *base skill* (18 per class, shared by all its variants), stored in
`tools/skills/icon_motifs/<classId>/` (Git LFS), plus a tier-coloured frame
(bronze, silver, gold, purple, orange, cyan per 10 levels) and 1–10 rank dots.
CI (`skill-icons.yml`) runs `validate_skill_icons.py` and the tool tests.

## Format
- **PNG** RGBA 128×128, transparent corners, stored in **Git LFS**.
- Missing icon: the UI falls back to the text label.
