# CHAR-MODEL.32 — Loup, ours (prédateurs)

## Dépendances
- CHAR-MODEL.27 (`quadruped_v1.skel` + clips, dont `attack.anim`)
- CHAR-MODEL.28 (FSM IA, archétype `Predator`)

## Cadrage

Livrer **deux modèles** prédateurs : loup et ours. Skin sur
`quadruped_v1`, archétype IA `Predator` : peuvent attaquer le joueur en
deçà de `attackTriggerDistance`.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/quadruped_v1.skel
ls game/data/animations/quadruped/attack.anim
mkdir -p game/data/models/{loup,ours}
```

---

## Spécification technique

### Manifests

`game/data/models/loup/loup.species.json` :

```json
{
  "id": "loup",
  "skeleton": "game/data/skeletons/quadruped_v1.skel",
  "skinmesh":  "game/data/models/loup/loup.skinmesh",
  "ai": {
    "archetype": "Predator",
    "wanderSpeed": 1.5,
    "attackSpeed": 5.0,
    "attackTriggerDistance": 6.0,
    "sightRange": 14.0
  },
  "mountable": false,
  "colorCode":  "#5C5448"
}
```

`game/data/models/ours/ours.species.json` :

```json
{
  "id": "ours",
  "skeleton": "game/data/skeletons/quadruped_v1.skel",
  "skinmesh":  "game/data/models/ours/ours.skinmesh",
  "ai": {
    "archetype": "Predator",
    "wanderSpeed": 1.0,
    "attackSpeed": 3.5,
    "attackTriggerDistance": 5.0,
    "sightRange": 12.0
  },
  "mountable": false,
  "colorCode":  "#3B2A1E"
}
```

### Tailles

| Espèce | Taille au garrot | `globalScale` |
|--------|------------------|---------------|
| Loup   | 0.85 m           | 0.60          |
| Ours   | 1.30 m           | 0.95          |

### Submeshes

Loup : `body`, `head`, `tail_static` *(option : tail skinnée via
`tail_*` du rig si possible)*, `teeth`.
Ours : `body`, `head`, `claws`.

---

## Liste des fichiers

**Créés :**
- `game/data/models/loup/loup.skinmesh`
- `game/data/models/loup/loup.species.json`
- `tools/model_placeholders/loup/loup.gltf`
- `tools/model_placeholders/loup/SOURCES.md`
- `game/data/models/ours/ours.skinmesh`
- `game/data/models/ours/ours.species.json`
- `tools/model_placeholders/ours/ours.gltf`
- `tools/model_placeholders/ours/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] Pour chacun des 2 modèles : `skinmesh_builder --validate` exit 0,
      manifest JSON parse, `archetype="Predator"`, `jointIndices` valides.
- [ ] Tailles relatives respectées.
- [ ] Test runtime AnimalAI (déjà existant en CHAR-MODEL.28) :
      injection d'une instance `loup` à 5 m du joueur visible →
      transition vers `Attack`.

---

## Anti-objectifs

- **Ne pas** introduire de meute (groupes coordonnés) — système de
  groupes hors release.
- **Ne pas** modifier `quadruped_v1` ni les clips.
- **Ne pas** ajouter de variantes (loup blanc, ours brun, polaire) —
  une entrée placeholder par espèce.
