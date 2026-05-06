# CHAR-MODEL.20 — Modèle **Chevalier-Dragon**

## Dépendances
- CHAR-MODEL.14

## Cadrage

Livrer le **modèle Chevalier-Dragon** : humanoïde reptilien (taille
humaine, écailleux, museau, queue placeholder statique, parfois cornes).
Skin sur `humanoid_v1`. Proportions proches d'un Orc costaud mais plus
élancé.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
mkdir -p game/data/models/chevalier_dragon
grep -n '"id": "divins"\|"id": "chevalier_dragon"' game/data/races/races.json
# Si l'ID 'chevalier_dragon' n'est pas encore dans races.json, l'ajouter
# dans le ticket CHAR-MODEL.23 (extension races.json).
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/chevalier_dragon/chevalier_dragon.gltf` :
- skin sur `humanoid_v1`,
- proportions : `globalScale=1.10`, `shouldersScale=1.15`,
  `limbsLength=1.05`, `torsoScale=1.10`,
- submeshes : `body`, `head` (museau), `hands` (griffes), `scales_overlay`
  (placeholder pour la peau écailleuse), `tail_static` (queue rigide
  attachée à `pelvis`), `clothes_placeholder`.

### Manifest race `chevalier_dragon.race.json`

```json
{
  "id": "chevalier_dragon",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/chevalier_dragon/chevalier_dragon.skinmesh",
  "scale": {
    "global":   1.10,
    "shoulders":1.15,
    "limbs":    1.05,
    "torso":    1.10
  },
  "morphologyBounds": {
    "globalScale":   [1.00, 1.20],
    "corpulence":    [1.00, 1.30],
    "limbsLength":   [1.00, 1.10]
  },
  "colorCode": "#3D6B8A",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0,
    "scales_overlay": 6, "tail_static": 0,
    "clothes_placeholder": 1
  }
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/chevalier_dragon/chevalier_dragon.skinmesh`
- `game/data/models/chevalier_dragon/chevalier_dragon.race.json`
- `tools/model_placeholders/chevalier_dragon/chevalier_dragon.gltf`
- `tools/model_placeholders/chevalier_dragon/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `chevalier_dragon.race.json` parse.
- [ ] `jointIndices` valides.
- [ ] La queue rigide ne provoque pas d'oscillation visuelle (skinnée
      à 100% sur `pelvis`).

---

## Anti-objectifs

- **Ne pas** introduire de queue articulée (réservé à un rig étendu
  futur, hors release).
- **Ne pas** introduire de variante ailé (réservé au Démon).
- Mêmes que CHAR-MODEL.15 pour le reste.
