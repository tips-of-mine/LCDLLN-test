# CHAR-MODEL.29 — Modèle **Cheval** (montable)

## Dépendances
- CHAR-MODEL.27 (`quadruped_v1.skel` + clips + sockets `dosCheval`)
- CHAR-MODEL.28 (FSM IA animal — le cheval sauvage en hérite)

## Cadrage

Livrer le **modèle Cheval** :
- skin sur `quadruped_v1`,
- prêt à recevoir un cavalier via le socket `dosCheval` (consommé par
  CHAR-MODEL.30),
- archétype IA `Neutral` (pas de fuite immédiate, pas d'attaque).

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/quadruped_v1.skel
mkdir -p game/data/models/cheval
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/cheval/cheval.gltf` :
- skin sur `quadruped_v1`,
- proportions : grand quadrupède au garrot ≈ 1.55 m,
- submeshes : `body`, `head`, `mane` (crinière, statique placeholder),
  `tail_static` (queue, attachée à `tail_3`).
- Le socket `dosCheval` (ancré sur `spine_2`) doit avoir un offset
  vertical correspondant à la hauteur de la selle (≈ +0.15 m Y dans
  le repère local de `spine_2`).

### Manifest

```json
// game/data/models/cheval/cheval.species.json
{
  "id": "cheval",
  "skeleton": "game/data/skeletons/quadruped_v1.skel",
  "skinmesh":  "game/data/models/cheval/cheval.skinmesh",
  "ai": {
    "archetype": "Neutral",
    "wanderSpeed": 1.5,
    "fleeSpeed":   8.0,
    "sightRange": 15.0
  },
  "mountable": true,
  "mountSocket": "dosCheval",
  "colorCode":   "#6F4F2A"
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/cheval/cheval.skinmesh`
- `game/data/models/cheval/cheval.species.json`
- `tools/model_placeholders/cheval/cheval.gltf`
- `tools/model_placeholders/cheval/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `cheval.species.json` parse, `mountable=true`,
      `mountSocket="dosCheval"`.
- [ ] Le socket `dosCheval` du rig `quadruped_v1.sockets` se résout
      bien sur `spine_2`.
- [ ] `jointIndices` valides (< 25, taille rig).

---

## Anti-objectifs

- **Ne pas** câbler `MountSystem` (CHAR-MODEL.30).
- **Ne pas** ajouter de variantes de robe (cheval noir / blanc / pie) —
  une seule entrée placeholder dans cette release.
- **Ne pas** introduire d'IA spécifique (l'archétype Neutral suffit).
- **Ne pas** modifier `quadruped_v1.skel`.
