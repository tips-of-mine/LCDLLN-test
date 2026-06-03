# CHAR-MODEL.14 — Rig humanoïde standard `humanoid_v1.skel` + clips placeholder

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : game/data/configuration/races/humains.json
> - Manque : .skel/.anim binaires absents
> - Resume : Rig humanoid manifest ok, assets absents

## Dépendances
- CHAR-MODEL.9 (locomotion : nécessite `humanoid_v1` pour les clips)
- CHAR-MODEL.10 (actions)
- CHAR-MODEL.11 (combat)
- CHAR-MODEL.12 (sockets : on livre le `.sockets` canonique du rig)

## Cadrage

Livrer le **rig humanoïde canonique** `humanoid_v1` partagé par les
8 races jouables (CHAR-MODEL.15–22) :
- fichier squelette `humanoid_v1.skel` (~ 60 os, conventions strictes),
- catalogue de sockets `humanoid_v1.sockets`,
- **clips d'animation placeholder** pour toutes les SM des phases 1
  (locomotion, actions, combat) — utilisables tant que les anims finales
  ne sont pas livrées.

Les modèles `.skinmesh` par race seront skinnés sur ce rig (un rig pour
toutes les races, géométries différentes).

---

## Pré-requis vérifiables

```bash
git status
ls tools/skeleton_builder/                 # CHAR-MODEL.2
ls tools/anim_builder/                     # CHAR-MODEL.6
ls engine/render/AttachmentSocket.h        # CHAR-MODEL.12
ls game/data/skeletons/ 2>/dev/null || mkdir -p game/data/skeletons
ls game/data/animations/humanoid 2>/dev/null || mkdir -p game/data/animations/humanoid
```

---

## Spécification technique

### Conventions os (anglais snake_case)

Découpage canonique imposé par le cahier des charges :

```
root
└─ pelvis
   ├─ spine_1
   │  └─ spine_2
   │     ├─ spine_3
   │     │  ├─ neck
   │     │  │  └─ head
   │     │  ├─ clavicle_l
   │     │  │  └─ shoulder_l        (épaule)
   │     │  │     └─ upperarm_l     (bras, du épaule au coude)
   │     │  │        └─ forearm_l   (avant-bras, du coude au poignet)
   │     │  │           └─ hand_l   (main, du poignet)
   │     │  │              ├─ finger_l_thumb
   │     │  │              ├─ finger_l_index
   │     │  │              ├─ finger_l_middle
   │     │  │              ├─ finger_l_ring
   │     │  │              └─ finger_l_pinky
   │     │  └─ clavicle_r → shoulder_r → upperarm_r → forearm_r → hand_r → fingers_r
   │     └─ (rien d'autre)
   ├─ hip_l
   │  └─ thigh_l           (cuisse, de la hanche au genou)
   │     └─ shin_l         (jambe, du genou à la cheville)
   │        └─ foot_l      (pied, de la cheville)
   │           └─ toes_l
   └─ hip_r → thigh_r → shin_r → foot_r → toes_r
```

Total ≈ 60 os (avec doigts × 5 par main).

**Tronc séparé des hanches** : `pelvis` est parent commun de `spine_1`
**et** `hip_l/hip_r` (les hanches ne sont pas dans la chaîne spine).

**Cou + tête** : amplitudes humaines naturelles (pas de poupée),
documentées dans le manifest (informatif, pas de constraint runtime).

### Bind pose

T-pose : bras horizontaux paumes vers le bas, jambes droites, pieds
écartés à largeur de hanches. Y-up, regard +Z.

### Sockets canoniques `humanoid_v1.sockets`

Livrer **a minima** :

| Socket name | Joint     | Usage                              |
|-------------|-----------|-------------------------------------|
| `mainR`     | `hand_r`  | Arme droite (épée 1H, dague, …)    |
| `mainL`     | `hand_l`  | Arme gauche / bouclier             |
| `dos`       | `spine_3` | Arme rangée (épée 2H, arc)         |
| `ceinture`  | `pelvis`  | Potions, sacoches                  |
| `casque`    | `head`    | Casque, capuche                    |

Offsets : translations en mètres mesurées en pose bind. Quaternions
identité par défaut (à ajuster par maquetteur — placeholder OK).

### Clips placeholder (livraison binaire `.anim`)

À produire avec `anim_builder` à partir de clips glTF placeholder
(animation simple, peu de keyframes — on assure juste que les SM
peuvent jouer quelque chose visible) :

**Locomotion :**
- `idle.anim` (loop, 2 s, légère respiration : spine_3 oscille ±2°)
- `walk.anim` (loop, 1 s)
- `walk_slow.anim` (loop, 1.5 s)
- `run.anim` (loop, 0.7 s)
- `jump.anim` (one-shot, 0.5 s)
- `fall.anim` (loop, 1 s)
- `land.anim` (one-shot, 0.3 s)
- `swim_idle.anim` (loop, 2 s)
- `swim_move.anim` (loop, 1 s)

**Actions :**
- `greet.anim` (one-shot, 1.5 s)
- `sit_ground.anim`, `sit_object.anim` (loop entrée + maintien)
- `lie_down_ground.anim`, `lie_down_bed.anim` (loop)
- `eat_standing.anim`, `eat_sitting.anim` (loop)
- `drink_standing.anim`, `drink_sitting.anim` (loop)
- `open_container.anim` (one-shot, 1 s)
- `use_object.anim` (one-shot, 0.8 s)
- `die.anim` (one-shot, 2 s)

**Combat (placeholder partagé pour toutes les classes d'arme à ce
stade — différenciation par classe arrive plus tard) :**
- `combat_idle.anim` (loop)
- `attack.anim` (one-shot, 0.6 s)
- `attack_chain.anim` (one-shot, 0.5 s)
- `block.anim` (loop)
- `parry.anim` (one-shot, 0.2 s)
- `cast_start.anim` (one-shot, 0.4 s)
- `cast_loop.anim` (loop, 1 s)
- `cast_release.anim` (one-shot, 0.3 s)
- `hit.anim` (one-shot, 0.4 s)

### Manifest rig

`game/data/skeletons/humanoid_v1.json` (informatif) :

```json
{
  "id": "humanoid_v1",
  "skeleton": "humanoid_v1.skel",
  "sockets": "humanoid_v1.sockets",
  "footIK": {
    "hipL": "thigh_l", "kneeL": "shin_l", "footL": "foot_l",
    "hipR": "thigh_r", "kneeR": "shin_r", "footR": "foot_r",
    "pelvisRoot": "pelvis"
  },
  "boneMaskUpperBody": ["spine_2", "clavicle_l", "clavicle_r"],
  "clipsDir": "game/data/animations/humanoid"
}
```

---

## Liste des fichiers (livrés sous `game/data/`)

**Créés :**
- `game/data/skeletons/humanoid_v1.skel` (binaire)
- `game/data/skeletons/humanoid_v1.sockets` (JSON)
- `game/data/skeletons/humanoid_v1.json` (manifest)
- `game/data/animations/humanoid/{idle,walk,walk_slow,run,jump,fall,
  land,swim_idle,swim_move,greet,sit_ground,sit_object,lie_down_ground,
  lie_down_bed,eat_standing,eat_sitting,drink_standing,drink_sitting,
  open_container,use_object,die,combat_idle,attack,attack_chain,block,
  parry,cast_start,cast_loop,cast_release,hit}.anim` (binaires)
- `game/data/animations/humanoid/SOURCES.md` (procédure de regen depuis
  les .gltf placeholders)

**Source glTF placeholder :**
- `tools/anim_placeholders/humanoid/humanoid_rig.gltf` (rig minimal,
  fait main ou via Blender script)
- `tools/anim_placeholders/humanoid/clips/*.gltf`

**Modifiés :**
- *(rien dans `engine/`)*

---

## CMakeLists.txt

Aucune modification : ce ticket ne livre que des **données**.

---

## Critères d'acceptation

- [ ] `skeleton_builder --input humanoid_rig.gltf --output
      humanoid_v1.skel --name humanoid_v1 --validate` exit 0.
- [ ] Le `.skel` produit a entre 50 et 70 os, ordre topologique respecté.
- [ ] `anim_builder` regénère les 30 clips sans erreur.
- [ ] Le `.sockets` charge correctement contre `humanoid_v1.skel` (test
      runtime simple, peut être manuel).
- [ ] `humanoid_v1.json` parse en JSON valide ; tous les noms d'os
      référencés existent dans le squelette.
- [ ] La somme des tailles des `.anim` placeholder reste < 2 Mio (clips
      très simples).

---

## Anti-objectifs

- **Ne pas** modifier `engine/`.
- **Ne pas** livrer les modèles `.skinmesh` (Phase 2 à partir de
  CHAR-MODEL.15).
- **Ne pas** différencier les clips par classe d'arme (les fichiers
  `attack.anim` sont génériques et seront raffinés ultérieurement).
- **Ne pas** introduire de blendshape ni de morph target.
- **Ne pas** poser des contraintes runtime sur les amplitudes cou/tête
  (le manifest est informatif).
