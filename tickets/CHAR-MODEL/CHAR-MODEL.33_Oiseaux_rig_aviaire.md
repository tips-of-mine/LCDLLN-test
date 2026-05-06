# CHAR-MODEL.33 — Poule, coq, oiseau (rig bipède aviaire `bird_v1.skel`)

## Dépendances
- CHAR-MODEL.9 (locomotion SM réutilisée)
- CHAR-MODEL.6 (`anim_builder`)
- CHAR-MODEL.2 (`skeleton_builder`)

## Cadrage

Livrer le **rig bipède aviaire** `bird_v1.skel` (commun aux oiseaux de
basse-cour et oiseaux sauvages) + 3 modèles : poule, coq, oiseau
générique. Animations : Idle, Peck (becque), Walk, FlapShort (battement
court, oiseau seulement), Fly (loop, oiseau seulement).

---

## Pré-requis vérifiables

```bash
git status
ls tools/skeleton_builder/
ls tools/anim_builder/
mkdir -p game/data/skeletons game/data/animations/bird game/data/models/{poule,coq,oiseau}
```

---

## Spécification technique

### Rig `bird_v1`

```
root → pelvis
├─ spine → chest → neck → head → beak
├─ tail
├─ wing_l_root → wing_l_mid → wing_l_tip
├─ wing_r_root → wing_r_mid → wing_r_tip
├─ thigh_l → shin_l → foot_l
└─ thigh_r → shin_r → foot_r
```

Total ≈ 18 os.

### Sockets

| Socket | Joint | Usage |
|--------|-------|-------|
| `bec`  | `beak`| Objet picoré |

### Clips placeholder

`game/data/animations/bird/` :
- `idle.anim`, `walk.anim`, `peck.anim`, `flap_short.anim`,
  `fly_loop.anim`, `die.anim`.

### Modèles

| Espèce  | `globalScale` | Volant ? | Archétype IA |
|---------|---------------|----------|--------------|
| poule   | 0.40          | Non      | Herbivore    |
| coq     | 0.45          | Non      | Neutral      |
| oiseau  | 0.20          | Oui      | Herbivore (fuit toute approche) |

Manifest exemple `oiseau.species.json` :

```json
{
  "id": "oiseau",
  "skeleton": "game/data/skeletons/bird_v1.skel",
  "skinmesh":  "game/data/models/oiseau/oiseau.skinmesh",
  "ai": {
    "archetype": "Herbivore",
    "wanderSpeed": 0.5,
    "fleeSpeed":   6.0,
    "fleeTriggerDistance": 4.0,
    "sightRange": 10.0,
    "canFly": true
  },
  "mountable": false,
  "colorCode":  "#7A6A52"
}
```

`canFly: true` est un flag pour CHAR-MODEL.37 / Animator pour autoriser
le clip `fly_loop` quand l'IA `Flee` est actif.

---

## Liste des fichiers

**Créés :**
- `game/data/skeletons/bird_v1.skel`
- `game/data/skeletons/bird_v1.sockets`
- `game/data/skeletons/bird_v1.json`
- `game/data/animations/bird/{idle,walk,peck,flap_short,fly_loop,die}.anim`
- `game/data/animations/bird/SOURCES.md`
- `game/data/models/poule/poule.{skinmesh,species.json}`
- `game/data/models/coq/coq.{skinmesh,species.json}`
- `game/data/models/oiseau/oiseau.{skinmesh,species.json}`
- `tools/anim_placeholders/bird/bird_rig.gltf` + clips
- `tools/model_placeholders/{poule,coq,oiseau}/*.gltf`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skeleton_builder --validate` exit 0 sur `bird_v1.skel` (≈ 18 os,
      ordre topologique).
- [ ] Les 6 clips compilent.
- [ ] Les 3 `.skinmesh` validés.
- [ ] Tailles relatives respectées (poule ≈ 30 cm hauteur).
- [ ] `canFly: true` présent uniquement pour `oiseau`.

---

## Anti-objectifs

- **Ne pas** introduire de pathfinding aérien (vol = ligne droite,
  hauteur fixe pendant la fuite).
- **Ne pas** introduire de variations (coq de couleur, oiseau exotique).
- **Ne pas** mélanger avec le rig quadrupède.
