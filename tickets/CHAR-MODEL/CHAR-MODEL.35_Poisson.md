# CHAR-MODEL.35 — Poisson (rig simple, anim swim)

## Dépendances
- CHAR-MODEL.2 (`skeleton_builder`)
- CHAR-MODEL.6 (`anim_builder`)

## Cadrage

Livrer un **rig poisson** simple `fish_v1.skel` (chaîne courte 6 os :
tête + corps + nageoire caudale + 2 nageoires latérales + queue) et un
modèle `poisson` placeholder, archétype IA `Herbivore` aquatique, qui
fuit l'approche.

---

## Pré-requis vérifiables

```bash
git status
ls tools/skeleton_builder/
mkdir -p game/data/skeletons game/data/animations/fish game/data/models/poisson
```

---

## Spécification technique

### Rig `fish_v1`

```
root → body → head
            → tail_root → tail_tip
            → fin_l
            → fin_r
```

Total = 7 os.

### Clips

`game/data/animations/fish/` :
- `swim_idle.anim` (loop, oscillation queue ±5°, 1.5 s),
- `swim_move.anim` (loop, oscillation queue ±15°, 0.6 s).

### Manifest

```json
{
  "id": "poisson",
  "skeleton": "game/data/skeletons/fish_v1.skel",
  "skinmesh":  "game/data/models/poisson/poisson.skinmesh",
  "ai": {
    "archetype": "Herbivore",
    "wanderSpeed": 0.6,
    "fleeSpeed":   2.5,
    "fleeTriggerDistance": 3.0,
    "sightRange": 5.0,
    "aquaticOnly": true
  },
  "mountable": false,
  "colorCode":  "#5C8A8F"
}
```

`aquaticOnly: true` indique au tick AI / locomotion d'utiliser
`mode=Swim` en permanence.

### Animation

Mêmes outils, oscillation analytique du `tail_root` autour de Y.

---

## Liste des fichiers

**Créés :**
- `game/data/skeletons/fish_v1.skel`
- `game/data/skeletons/fish_v1.sockets` (vide)
- `game/data/skeletons/fish_v1.json`
- `game/data/animations/fish/{swim_idle,swim_move}.anim`
- `game/data/animations/fish/SOURCES.md`
- `game/data/models/poisson/poisson.{skinmesh,species.json}`
- `tools/anim_placeholders/fish/fish_rig.gltf` + clips

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skeleton_builder --validate` exit 0 (7 os).
- [ ] Les 2 clips compilent.
- [ ] `poisson.skinmesh` validé.
- [ ] `aquaticOnly: true` lu correctement par le système qui utilisera
      cette donnée (CHAR-MODEL.37).

---

## Anti-objectifs

- **Ne pas** introduire de bancs / shoaling (groupes coordonnés).
- **Ne pas** introduire de variantes (poissons d'eau douce / salée).
- **Ne pas** introduire de comportement de pêche / interaction joueur.
