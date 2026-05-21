# Exigences techniques FBX — assets de personnages

Contraintes pour qu'un FBX soit intégrable dans le système de customisation.
La validation automatique partielle est faite par
`tools/asset_pipeline/validate_fbx.py` ; les points marqués *(SDK)* nécessitent
le SDK FBX et restent à automatiser.

## Format & fichier

- Extension `.fbx`, **FBX binaire** (pas ASCII) *(SDK)*.
- Taille **≤ 50 Mo** par fichier (sinon découper / optimiser).
- Nom conforme à `docs/CONVENTIONS_NAMING.md` (snake_case, sans espaces).
- Unités : **mètres**. Échelle 1.0 (pas de scale appliqué au niveau du noeud
  racine de l'export).
- Axe : Y up, Z forward (cohérent avec le reste du pipeline).

## Squelette (mesh skinnés : corps, têtes, cheveux, traits)

- Squelette **`humanoid_base`** avec les noms de bones exacts
  (cf. CONVENTIONS_NAMING.md). Un mesh attaché doit partager ces noms pour
  hériter du scaling d'os et des sockets *(SDK)*.
- Skinning : **4 influences max par vertex**, poids **normalisés** (somme = 1)
  *(SDK)*.
- Pas de bone hors hiérarchie `root` ; pas de bones dupliqués *(SDK)*.
- Bind pose en T-pose (ou A-pose documentée), cohérente entre tous les modules
  d'une même race.

## Géométrie

- **Triangulé** (pas de n-gons) *(SDK)*.
- Normals présentes et cohérentes ; tangentes calculables.
- **Une seule UV map** (UV0) par mesh, dans [0,1], sans chevauchement nuisible
  *(SDK)*.
- Aucun vertex non assigné à un bone (mesh skinnés) *(SDK)*.
- Modules conçus pour se raccorder proprement (tête ↔ corps, cheveux ↔ tête)
  sans interpénétration excessive.

## Matériaux & textures

- Textures **séparées** du FBX (PNG dans `textures/characters/...`), référencées
  par la config de race, pas embarquées.
- Convention PBR : `diffuse` + `normal` + `orm` (cf. CONVENTIONS_NAMING.md).
- Yeux émissifs : flag `emissive` géré côté config (`eyeColors[].emissive`).

## Morph targets (blend shapes)

- Nommés exactement comme dans `morphTargets` du JSON de race
  (`faceWidth`, `jawWidth`, `noseSize`, `cheekbones`, `eyeSize`, `lipThickness`,
  `bodyMass`, …) *(SDK)*.
- Plage de poids cohérente avec les bornes `min`/`max` de la config.

## Traits raciaux & « none »

- Chaque catégorie de trait optionnel (cornes, queue, défenses…) fournit un
  `none.fbx` (mesh vide / placeholder) pour l'option « aucun ».

## Workflow

1. Exporter dans `tools/asset_pipeline/inbox/characters/<race>/<type>/`.
2. `python3 tools/asset_pipeline/validate_fbx.py <fichier>`.
3. `python3 tools/asset_pipeline/process_character_assets.py --race <race>`.
4. Re-valider la sortie : `validate_fbx.py --dir game/data/models/characters/<race>`.

> Note conversion : le moteur consomme aussi du glTF/glb (cf.
> `tools/asset_pipeline/fbx_to_gltf.ps1`, `convert_race_meshes.py`). Le présent
> document couvre les **exigences d'entrée FBX** ; l'étape de conversion vers le
> format runtime est gérée par ces outils.
