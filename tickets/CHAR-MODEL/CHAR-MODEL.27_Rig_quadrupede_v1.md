# CHAR-MODEL.27 — Rig quadrupède générique `quadruped_v1.skel` + clips

## Dépendances
- CHAR-MODEL.9 (locomotion SM, réutilisée pour les animaux)

## Cadrage

Livrer le **rig quadrupède canonique** `quadruped_v1` partagé par les
mammifères terrestres (cheval, vache, cochon, chèvre, lapin, loup, ours)
+ catalogue de sockets + clips placeholder (Idle, Walk, Run, Eat,
Observe).

Le rig quadrupède réutilise les **mêmes machines de locomotion**
(CHAR-MODEL.9) avec un mapping clips spécifique. Pas d'IK pieds dans
cette release pour les quadrupèdes (réservé à plus tard).

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
ls tools/skeleton_builder/
ls tools/anim_builder/
mkdir -p game/data/skeletons game/data/animations/quadruped
```

---

## Spécification technique

### Conventions os (anglais snake_case)

```
root
└─ pelvis
   ├─ spine_1 → spine_2 → spine_3 → neck → head
   │                                       └─ jaw
   │                                       └─ ear_l, ear_r (option)
   ├─ tail_1 → tail_2 → tail_3
   ├─ shoulder_fl → upperarm_fl → forearm_fl → paw_fl  (front-left)
   ├─ shoulder_fr → upperarm_fr → forearm_fr → paw_fr  (front-right)
   ├─ thigh_bl   → shin_bl     → paw_bl                (back-left)
   └─ thigh_br   → shin_br     → paw_br                (back-right)
```

Total ≈ 25 os. Ordre topologique respecté.

### Sockets canoniques

| Socket   | Joint      | Usage                                       |
|----------|------------|---------------------------------------------|
| `dosCheval` | `spine_2` | Selle / cavalier (sera utilisé pour le cheval, ignoré sinon) |
| `cou`    | `neck`     | Collier, longe                              |
| `gueule` | `jaw`      | Objet tenu en gueule (loup, chien)          |

### Clips placeholder

`game/data/animations/quadruped/` :
- `idle.anim` (loop, 2 s),
- `walk.anim` (loop, 1 s),
- `run.anim` (loop, 0.6 s),
- `eat.anim` (loop, 1.5 s, tête baissée),
- `observe.anim` (loop, 2 s, tête levée),
- `attack.anim` (one-shot, 0.5 s, utilisée par loup/ours en CHAR-MODEL.32),
- `flee.anim` (loop, 0.5 s, identique à run plus rapide),
- `die.anim` (one-shot, 1.5 s).

### Manifest

```json
// game/data/skeletons/quadruped_v1.json
{
  "id": "quadruped_v1",
  "skeleton": "quadruped_v1.skel",
  "sockets": "quadruped_v1.sockets",
  "clipsDir": "game/data/animations/quadruped"
}
```

### Locomotion mapping (informatif pour CHAR-MODEL.28)

Mapping de `LocomotionStateMachine.ClipMap` pour quadrupède :

| État SM       | Clip quadrupède         |
|---------------|-------------------------|
| Idle          | `idle.anim`             |
| Walk / WalkSlow | `walk.anim`           |
| Run           | `run.anim`              |
| Jump / Fall / Land | non utilisé (les animaux ne sautent pas dans cette release ; brancher au même clip que `idle` à défaut) |
| SwimIdle / SwimMove | `idle.anim` / `walk.anim` (fallback) |

---

## Liste des fichiers

**Créés :**
- `game/data/skeletons/quadruped_v1.skel`
- `game/data/skeletons/quadruped_v1.sockets`
- `game/data/skeletons/quadruped_v1.json`
- `game/data/animations/quadruped/{idle,walk,run,eat,observe,attack,flee,die}.anim`
- `game/data/animations/quadruped/SOURCES.md`
- `tools/anim_placeholders/quadruped/quadruped_rig.gltf`
- `tools/anim_placeholders/quadruped/clips/*.gltf`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skeleton_builder --validate` exit 0 sur `quadruped_v1.skel`
      (≈ 25 os, ordre topologique).
- [ ] Les 8 clips compilent sans erreur.
- [ ] `quadruped_v1.sockets` charge contre `quadruped_v1.skel`.
- [ ] `quadruped_v1.json` parse, tous les noms d'os référencés présents.

---

## Anti-objectifs

- **Ne pas** introduire d'IK pieds quadrupèdes.
- **Ne pas** différencier les rigs par espèce (un rig = tous les
  mammifères terrestres ; les variantes biomécaniques se feront via
  bind pose et skin).
- **Ne pas** livrer les `.skinmesh` d'animaux (CHAR-MODEL.29+).
- **Ne pas** modifier `humanoid_v1`.
