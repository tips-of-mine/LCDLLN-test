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
| `trellis_import.py` | Visages et pilosité **sculptés** (SDXL + TRELLIS.2) : projection, atlas, pièces .glb |
| `sculpt_project.py` | Champ de rayons autour de la tête, projection des visages, surfaces de poils, cuisson |
| `blender_scripts/fit_high.py` | Blender en arrière-plan : ajuste un maillage TRELLIS au gabarit du corps |
| `sculpted_parts.json` | Manifeste généré par `trellis_import.py`, repris par `build_character_appearance.py` |

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
- **Visage peint** (races procédurales) : la tête projette l'îlot « visage » de
  la texture de peau du corps ; elle suit donc la teinte de peau choisie.
- **Couleurs par teinte** : une seule texture neutre pour les cheveux et une pour
  les yeux ; la couleur est une teinte du matériau (`MaterialFlags::Tint` /
  `TintMasked`). Les teintes de peau sont aussi des teintes de la texture
  d'origine. Le tableau de textures du shader passe à 128 entrées pour les
  atlas des pièces sculptées.
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

## Visages et pilosité sculptés (SDXL + TRELLIS.2)

Pour les humains, visages, coiffures, barbes et moustaches ne sont plus
procéduraux : ils viennent de maillages générés puis ramenés aux contraintes du
jeu (squelette du corps, 3 LOD, budgets, teintes).

1. **Références** (ComfyUI, SDXL base) : bustes génériques de jeunes adultes
   (environ 20-25 ans), crâne rasé pour les visages, cheveux noirs pour les
   coiffures. Aucune image de la BD. Les visages plus âgés générés en essai sont
   gardés hors du dépôt pour de futurs PNJ.
2. **Maillage** (ComfyUI, TRELLIS.2 — licence MIT ; Hunyuan3D est exclu : sa
   licence ne s'applique pas dans l'Union européenne) : un `.glb` coloré par
   pièce. Seuls la forme et la couleur sont utilisées : la topologie TRELLIS
   (bords ouverts, arêtes non manifold) ne se décime pas proprement.
3. **Ajustement** (`blender_scripts/fit_high.py`, Blender en arrière-plan) :
   sommet du crâne, pointe du nez et largeur du front ramenés au gabarit du
   genre ; export d'un nuage `<pièce>_high.npz`.
4. **Import** (`trellis_import.py <dossier> humains`, sans Blender) :
   - visages : la tête procédurale (anneau du cou raccordé au corps, UV uniques)
     est portée sur la peau TRELLIS ; le crâne est commun aux trois visages d'un
     genre (et, chez l'homme, la mâchoire et la lèvre supérieure, pour que barbes
     et moustaches se posent sans jour) ; cou, nuque et tempes recolorés sur la
     peau du front ; atlas 2048 (3 visages) + normal map ; iris peints et teintables
     (matériau `MI_FaceEye`), peau teintée comme le corps (`MI_Face`) ;
   - pilosité : surface en grille sphérique sur la zone de poils TRELLIS, bord
     posé à 0,5 mm du cuir chevelu (aucun jour), contour fin dans l'alpha de
     l'atlas (découpe), albedo neutre teinté par la couleur choisie ;
   - barbes et moustaches : épaisseur bornée au-dessus de la peau, lèvres
     dégagées, rien sous la ligne de la mâchoire ;
   - coiffures attachées (queues, chignon, tresses) : calotte TRELLIS plaquée
     (10 mm max., oreilles dégagées) + queue / chignon / tresses de `hair.py`
     fixés sur la calotte, dans des cases d'atlas à texture de mèches ;
   - pièces dérivées déclarées dans `pieces.json` : même nuage, autre zone
     (bouc, moustache fine ou tombante) ou coupe (`"cut"` : carré tiré des
     cheveux longs) ;
   - écriture des pièces et de `sculpted_parts.json`, puis
     `build_character_appearance.py` régénère le catalogue.

Les nuages `*_high.npz` (plusieurs centaines de Mo) restent hors du dépôt, sur
le poste de génération.
