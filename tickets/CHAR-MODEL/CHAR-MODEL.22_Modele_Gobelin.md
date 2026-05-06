# CHAR-MODEL.22 — Modèle **Gobelin**

## Dépendances
- CHAR-MODEL.14

## Cadrage

Livrer le **modèle Gobelin** : humanoïde **petit** (–35% taille),
proportions ramassées, oreilles longues pointues. Skin sur `humanoid_v1`.
Distinct du Nain : Gobelin est élancé, pas trapu.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
mkdir -p game/data/models/gobelin
grep -n '"id": "gobelin"\|"id": "morts_vivants"' game/data/races/races.json
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/gobelin/gobelin.gltf` :
- skin sur `humanoid_v1`,
- proportions : `globalScale=0.65`, `shouldersScale=0.85`,
  `limbsLength=0.95`, `torsoScale=0.90`, `corpulence=0.85`,
- submeshes : `body`, `head`, `hands`, `ears` (oreilles longues),
  `clothes_placeholder`.

### Manifest race `gobelin.race.json`

```json
{
  "id": "gobelin",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/gobelin/gobelin.skinmesh",
  "scale": {
    "global":   0.65,
    "shoulders":0.85,
    "limbs":    0.95,
    "torso":    0.90
  },
  "morphologyBounds": {
    "globalScale":   [0.60, 0.72],
    "corpulence":    [0.75, 0.95],
    "limbsLength":   [0.90, 1.00]
  },
  "colorCode": "#6E8E3A",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0, "ears": 0,
    "clothes_placeholder": 1
  }
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/gobelin/gobelin.skinmesh`
- `game/data/models/gobelin/gobelin.race.json`
- `tools/model_placeholders/gobelin/gobelin.gltf`
- `tools/model_placeholders/gobelin/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `gobelin.race.json` parse, `globalScale=0.65`.
- [ ] `jointIndices` valides.
- [ ] Hauteur monde au repos ≈ 1.10–1.20 m (pour humain à 1.80 m).

---

## Anti-objectifs

- **Ne pas** confondre avec Nain (silhouette élancée, pas trapue).
- Mêmes que CHAR-MODEL.15.
