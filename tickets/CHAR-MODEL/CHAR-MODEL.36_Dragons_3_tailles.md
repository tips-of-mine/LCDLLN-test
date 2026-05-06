# CHAR-MODEL.36 — Dragons (rig dédié, 3 tailles via scale skeletal)

## Dépendances
- CHAR-MODEL.2 (`skeleton_builder`)
- CHAR-MODEL.6 (`anim_builder`)
- CHAR-MODEL.12 (sockets dorsal pour cavalier potentiel — futur)

## Cadrage

Livrer le **rig dragon** `dragon_v1.skel` (4 pattes + ailes + queue
articulée) et **trois modèles** (juvénile, adulte, ancien) qui partagent
le **même** `.skinmesh` et différent uniquement par un **scale skeletal
appliqué runtime** + un manifest dédié par taille.

Méthode "3 tailles via scale skeletal" :
- un seul `dragon.skinmesh` skinné sur `dragon_v1.skel`,
- trois manifests `dragon_juvenile.species.json`,
  `dragon_adulte.species.json`, `dragon_ancien.species.json`,
- chacun a un `globalScale` distinct (0.5, 1.0, 1.6) qui multiplie le
  root scale runtime,
- les trois variantes coexistent dans un même monde.

---

## Pré-requis vérifiables

```bash
git status
ls tools/skeleton_builder/
ls engine/render/AttachmentSocket.h
mkdir -p game/data/skeletons game/data/animations/dragon game/data/models/dragon
```

---

## Spécification technique

### Rig `dragon_v1`

```
root → pelvis
├─ spine_1 → spine_2 → spine_3 → neck_1 → neck_2 → head → jaw
├─ tail_1 → tail_2 → tail_3 → tail_4 → tail_tip
├─ wing_l_root → wing_l_mid → wing_l_tip
├─ wing_r_root → wing_r_mid → wing_r_tip
├─ shoulder_fl → upperarm_fl → forearm_fl → claw_fl
├─ shoulder_fr → upperarm_fr → forearm_fr → claw_fr
├─ thigh_bl → shin_bl → claw_bl
└─ thigh_br → shin_br → claw_br
```

Total ≈ 35 os.

### Sockets

| Socket   | Joint     | Usage                                |
|----------|-----------|--------------------------------------|
| `dosDragon` | `spine_2` | Cavalier (futur, hors release)     |
| `gueule` | `jaw`     | Souffle (effet visuel attaché)       |

### Clips

`game/data/animations/dragon/` :
- `idle.anim` (loop, 3 s),
- `walk.anim` (loop, 1.2 s),
- `run.anim` (loop, 0.8 s),
- `fly_loop.anim` (loop, battement d'ailes, 1 s),
- `glide.anim` (loop, 2 s, ailes déployées presque immobiles),
- `roar.anim` (one-shot, 1.5 s, action),
- `breath.anim` (one-shot, 1.0 s, souffle),
- `attack_bite.anim` (one-shot, 0.7 s),
- `die.anim` (one-shot, 3.0 s).

### Modèles & manifests (3 tailles)

`dragon.skinmesh` partagé. Trois manifests :

```json
// dragon_juvenile.species.json
{
  "id": "dragon_juvenile",
  "skeleton": "game/data/skeletons/dragon_v1.skel",
  "skinmesh":  "game/data/models/dragon/dragon.skinmesh",
  "scale": { "global": 0.5 },
  "ai": {
    "archetype": "Predator",
    "wanderSpeed": 1.2,
    "attackSpeed": 4.0,
    "attackTriggerDistance": 7.0,
    "sightRange": 18.0,
    "canFly": true
  },
  "mountable": false,
  "colorCode":  "#3B5C3F"
}

// dragon_adulte.species.json — identique sauf "scale.global": 1.0,
// colorCode: "#7A2A2A"
// dragon_ancien.species.json — "scale.global": 1.6, colorCode:
// "#1F1F1F", "attackSpeed": 6.0, "sightRange": 25.0
```

### Application runtime du scale skeletal

Le `globalScale` est appliqué via `MorphologyApplicator` (CHAR-MODEL.25)
**ou** plus simplement par un scale appliqué sur `pose[root].scale` au
load — les deux approches sont valides ; choix : `MorphologyApplicator`
au tick d'instance pour cohérence avec les humanoïdes.

---

## Liste des fichiers

**Créés :**
- `game/data/skeletons/dragon_v1.skel`
- `game/data/skeletons/dragon_v1.sockets`
- `game/data/skeletons/dragon_v1.json`
- `game/data/animations/dragon/{idle,walk,run,fly_loop,glide,roar,
  breath,attack_bite,die}.anim`
- `game/data/animations/dragon/SOURCES.md`
- `game/data/models/dragon/dragon.skinmesh`
- `game/data/models/dragon/dragon_juvenile.species.json`
- `game/data/models/dragon/dragon_adulte.species.json`
- `game/data/models/dragon/dragon_ancien.species.json`
- `tools/anim_placeholders/dragon/dragon_rig.gltf` + clips
- `tools/model_placeholders/dragon/dragon.gltf`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skeleton_builder --validate` exit 0 sur `dragon_v1.skel`
      (≈ 35 os).
- [ ] Les 9 clips compilent.
- [ ] `dragon.skinmesh` validé, `jointIndices` < 35.
- [ ] Les 3 manifests JSON parsent, `globalScale` distinct.
- [ ] Au runtime (test manuel), les 3 dragons coexistent dans la même
      scène et ont des tailles visiblement différentes (0.5×, 1.0×, 1.6×).

---

## Anti-objectifs

- **Ne pas** créer 3 `.skinmesh` différents — un seul, taille runtime.
- **Ne pas** câbler la monture dragon (réservé à une future release).
- **Ne pas** introduire le souffle (effet visuel particules) — clip
  d'animation seulement.
- **Ne pas** introduire d'IA spécifique au-delà de `Predator`.
- **Ne pas** modifier `quadruped_v1` ni `bird_v1`.
