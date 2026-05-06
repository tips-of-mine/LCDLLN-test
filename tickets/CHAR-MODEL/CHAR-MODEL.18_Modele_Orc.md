# CHAR-MODEL.18 — Modèle **Orc**

## Dépendances
- CHAR-MODEL.14

## Cadrage

Livrer le **modèle Orc** : skinné sur `humanoid_v1`, taille **humaine**
mais **+25% masse / corpulence** (épaules larges, torse massif).

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
mkdir -p game/data/models/orc
grep -n '"id": "orcs"' game/data/races/races.json
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/orc/orc.gltf` :
- skin sur `humanoid_v1`,
- proportions : `globalScale=1.00`, `shouldersScale=1.25`,
  `limbsLength=1.00`, `torsoScale=1.20`, `corpulence=1.25`.
- Submeshes : `body`, `head`, `hands`, `tusks` (défenses — submesh
  séparé), `clothes_placeholder`.

### Manifest race `orc.race.json`

```json
{
  "id": "orcs",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/orc/orc.skinmesh",
  "scale": {
    "global":   1.00,
    "shoulders":1.25,
    "limbs":    1.00,
    "torso":    1.20
  },
  "morphologyBounds": {
    "globalScale":   [0.95, 1.10],
    "corpulence":    [1.10, 1.40],
    "limbsLength":   [0.95, 1.05]
  },
  "colorCode": "#5E6E3F",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0, "tusks": 3,
    "clothes_placeholder": 1
  }
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/orc/orc.skinmesh`
- `game/data/models/orc/orc.race.json`
- `tools/model_placeholders/orc/orc.gltf`
- `tools/model_placeholders/orc/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `orc.race.json` parse, `shouldersScale=1.25`, `torsoScale=1.20`.
- [ ] `jointIndices` valides.

---

## Anti-objectifs

- Mêmes que CHAR-MODEL.15.
