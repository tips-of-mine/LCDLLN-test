# Personnalisation des personnages — chaîne de génération des assets

Génère les pièces de personnalisation des 5 races jouables (humains, elfes,
nains, orcs, démons) et des deux genres, ainsi que le catalogue lu par le client.

| Fichier | Rôle |
|---|---|
| `build_character_appearance.py` | Point d'entrée : corps de race, pièces .glb (3 LOD), textures, catalogue |
| `glb_io.py` | Lecture/écriture .glb ; recopie du squelette du corps dans chaque pièce |
| `geom.py` | Géométrie paramétrique (surfaces, tubes, Bézier) |
| `heads.py` | Têtes (3 visages par race et genre), yeux, oreilles |
| `hair.py` | Coiffures, barbes, moustaches, sourcils |
| `features.py` | Cornes, queues, ailes (démons), défenses (orcs) |
| `bodies.py` | Corps de race dérivés du corps humain (même squelette) |
| `textures.py` | Textures neutres (cheveux, yeux, corne, ivoire) et teintes |
| `validate_appearance.py` | Contrôle CI (sans Blender) |
| `render_check.py` | Rendus de contrôle sous Blender (module `bpy`) |
| `tests/` | Tests unitaires (géométrie, écriture .glb) |
| `appearance_report.json` | Rapport généré : triangles et boîte englobante par pièce |

## Régénérer

```
python tools/asset_pipeline/character_appearance/build_character_appearance.py
```

Dépendances : Python 3.11+, numpy, Pillow. Le script est déterministe et
réécrit tout : `game/data/models/characters/appearance/`, les corps
`game/data/models/characters/<race>/<Genre>_<Race>/`, les textures
`game/data/textures/characters/appearance/` et
`game/data/configuration/customization/appearance_catalog.json`.

## Principes

- **Squelette partagé** : chaque pièce contient une copie exacte du squelette du
  corps humain du même genre (mêmes os, même ordre, mêmes matrices de liaison
  inverses). Le moteur la dessine avec les matrices d'os du corps
  (`SkinnedRenderer::RecordParts`) : une pièce liée à `Head` suit la tête, la
  queue suit `pelvis`, les ailes `spine_03`.
- **Corps de race** : sommets du corps humain épaissis ou affinés autour des os
  (`bodies.py`), squelette inchangé — les animations UE5 s'appliquent sans
  retargeting. La taille de la race est réglée par `importScale` dans
  `game/data/races/races.json` (nain : 1,30 m).
- **Visage peint** : la tête projette l'îlot « visage » de la texture de peau du
  corps ; elle suit donc la teinte de peau choisie.
- **Couleurs par teinte** : une seule texture neutre pour les cheveux et une pour
  les yeux ; la couleur est une teinte du matériau (`MaterialFlags::Tint` /
  `TintMasked`). Les teintes de peau sont aussi des teintes de la texture
  d'origine. Aucune texture de peau supplémentaire : le tableau de 64 textures
  du shader est préservé.
- **Niveaux de détail** : chaque pièce est régénérée à 3 résolutions (pas de
  décimation). Budgets de triangles dans `BUDGETS` (contrôlés en CI).
- **Licence** : créations originales du projet (CC0), aucune image ni forme
  issue de la bande dessinée. Les corps de base sont ceux déjà présents dans le
  dépôt (Quaternius, CC0).

## Contrôle visuel

Blender 5.x en module Python (`pip install bpy`, Python 3.11) :

```
LCDLLN_RENDER='[{"race":"demons","gender":"male","choice":{"horns":1,"wings":1}}]' \
LCDLLN_RENDER_OUT=/tmp/rendu python -c "exec(open('tools/asset_pipeline/character_appearance/render_check.py').read())"
```
