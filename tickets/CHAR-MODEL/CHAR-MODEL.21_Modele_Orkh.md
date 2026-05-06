# CHAR-MODEL.21 — Modèle **Orkh**

## Dépendances
- CHAR-MODEL.14

## Cadrage

Livrer le **modèle Orkh** : humanoïde robuste, distinct de l'Orc — proche
d'un humain musculeux, sans défenses proéminentes, peau gris-vert pâle.
Taille légèrement supérieure à l'Humain (+10%), épaules normales, jambes
proportionnelles. Skin sur `humanoid_v1`.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
mkdir -p game/data/models/orkh
grep -n '"id": "orkh"\|"id": "corrompus"' game/data/races/races.json
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/orkh/orkh.gltf` :
- skin sur `humanoid_v1`,
- proportions : `globalScale=1.10`, `shouldersScale=1.10`,
  `limbsLength=1.00`, `torsoScale=1.10`, `corpulence=1.15`,
- submeshes : `body`, `head`, `hands`, `clothes_placeholder`.

### Manifest race `orkh.race.json`

```json
{
  "id": "orkh",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/orkh/orkh.skinmesh",
  "scale": {
    "global":   1.10,
    "shoulders":1.10,
    "limbs":    1.00,
    "torso":    1.10
  },
  "morphologyBounds": {
    "globalScale":   [1.00, 1.20],
    "corpulence":    [1.00, 1.30],
    "limbsLength":   [0.95, 1.05]
  },
  "colorCode": "#8FA890",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0, "clothes_placeholder": 1
  }
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/orkh/orkh.skinmesh`
- `game/data/models/orkh/orkh.race.json`
- `tools/model_placeholders/orkh/orkh.gltf`
- `tools/model_placeholders/orkh/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `orkh.race.json` parse, valeurs conformes au cadrage.
- [ ] `jointIndices` valides.

---

## Anti-objectifs

- **Ne pas** confondre avec Orc : Orkh n'a pas de défenses (`tusks`),
  silhouette plus humaine.
- Mêmes que CHAR-MODEL.15 pour le reste.
