# CHAR-MODEL.15 — Modèle **Humain**

## Dépendances
- CHAR-MODEL.14 (`humanoid_v1.skel`, sockets, clips placeholder)

## Cadrage

Livrer le **modèle Humain** : `.skinmesh` skinné sur `humanoid_v1`,
+ **manifest race** `humain.race.json` qui décrit les paramètres
visuels par défaut, le code couleur placeholder, les bornes morphologiques
(utilisées par CHAR-MODEL.25).

L'Humain est la race **étalon** : taille = 1.0×, masse = 1.0×, épaules =
1.0×. Toutes les autres races se mesurent en delta par rapport à lui.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel       # CHAR-MODEL.14 livré
ls tools/skinmesh_builder/                    # CHAR-MODEL.1 livré
ls game/data/models/ 2>/dev/null || mkdir -p game/data/models/humain
grep -n '"id": "humains"' game/data/races/races.json
```

---

## Spécification technique

### Mesh source

Modèle glTF placeholder `tools/model_placeholders/humain/humain.gltf` :
- géométrie low-poly (≤ 8 000 triangles, suffisant pour validation),
- skin sur `humanoid_v1` (mêmes noms d'os),
- 1 submesh `body`, 1 submesh `head`, 1 submesh `hands`, 1 submesh
  `clothes_placeholder` (pour autoriser la `MaterialOverride` race en
  CHAR-MODEL.23),
- UVs cohérentes (la texture finale arrivera plus tard).

### Manifest race `humain.race.json`

```json
{
  "id": "humains",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/humain/humain.skinmesh",
  "scale": {
    "global":   1.00,
    "shoulders":1.00,
    "limbs":    1.00,
    "torso":    1.00
  },
  "morphologyBounds": {
    "globalScale":   [0.92, 1.08],
    "corpulence":    [0.85, 1.20],
    "limbsLength":   [0.95, 1.05]
  },
  "colorCode": "#C68642",
  "submeshMaterialSlots": {
    "body":     0,
    "head":     0,
    "hands":    0,
    "clothes_placeholder": 1
  }
}
```

### Construction

1. Préparer `humain.gltf` (Blender / autre, hors repo si nécessaire,
   committer le fichier `.gltf` placeholder dans
   `tools/model_placeholders/humain/`).
2. `skinmesh_builder --input humain.gltf --output
   game/data/models/humain/humain.skinmesh --validate`.
3. Vérifier que le `.skinmesh` a ≤ 4 influences/sommet, indices d'os
   < 60 (taille `humanoid_v1`).
4. Écrire `humain.race.json`.

---

## Liste des fichiers

**Créés :**
- `game/data/models/humain/humain.skinmesh`
- `game/data/models/humain/humain.race.json`
- `tools/model_placeholders/humain/humain.gltf` (+ textures placeholder
  si présentes)
- `tools/model_placeholders/humain/SOURCES.md` (procédure regen)

**Modifiés :** *(rien dans `engine/`)*

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0 sur `humain.skinmesh`.
- [ ] `humain.race.json` parse en JSON valide.
- [ ] Tous les `joint indices` de `humain.skinmesh` < `humanoid_v1`
      jointCount.
- [ ] Le test runtime de chargement (test générique race livré en
      CHAR-MODEL.23) charge sans erreur l'Humain et le rend en bind
      pose.
- [ ] Tailles : `humain.skinmesh` < 1 Mio.

---

## Anti-objectifs

- **Ne pas** introduire de matériau réel (juste une `MaterialOverride`
  placeholder via le `colorCode`).
- **Ne pas** modifier `humanoid_v1.skel` ni les clips.
- **Ne pas** livrer les autres races (un ticket par race).
- **Ne pas** câbler le viewer (CHAR-MODEL.24).
