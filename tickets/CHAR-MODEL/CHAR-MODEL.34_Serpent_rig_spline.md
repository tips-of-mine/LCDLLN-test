# CHAR-MODEL.34 — Serpent (rig spline / chaîne d'os, anim sinusoïdale)

## Dépendances
- CHAR-MODEL.2 (`skeleton_builder`)
- CHAR-MODEL.6 (`anim_builder`)

## Cadrage

Livrer le **rig serpent** `snake_v1.skel` : chaîne linéaire d'os
articulés qui propage une **onde sinusoïdale** (clip `slither_loop`).
Un seul modèle `serpent`.

---

## Pré-requis vérifiables

```bash
git status
ls tools/skeleton_builder/
mkdir -p game/data/skeletons game/data/animations/snake game/data/models/serpent
```

---

## Spécification technique

### Rig `snake_v1`

Chaîne linéaire de 16 os :

```
root → head → spine_01 → spine_02 → … → spine_14 → tail_tip
```

Total = 16 os. `head` est le 2ᵉ os pour permettre l'animation de la
tête indépendamment.

### Clips

`game/data/animations/snake/` :
- `idle.anim` (loop, ondulation lente, 3 s),
- `slither_loop.anim` (loop, déplacement par onde sinusoïdale, 1 s),
- `attack_strike.anim` (one-shot, 0.5 s, tête qui frappe vers l'avant),
- `die.anim` (one-shot, 1.5 s).

### Manifest serpent

```json
{
  "id": "serpent",
  "skeleton": "game/data/skeletons/snake_v1.skel",
  "skinmesh":  "game/data/models/serpent/serpent.skinmesh",
  "ai": {
    "archetype": "Predator",
    "wanderSpeed": 0.4,
    "attackSpeed": 1.5,
    "attackTriggerDistance": 1.5,
    "sightRange": 5.0
  },
  "mountable": false,
  "colorCode":  "#3F5731"
}
```

### Animation sinusoïdale

Génération offline du clip `slither_loop` : pour chaque os `spine_k`,
une rotation autour de Y de `A * sin(2π * k / 8 + 2π * t / period)`,
amplitude `A = 12°`, période 1 s. Builder calcule analytiquement et
écrit le `.anim`.

---

## Liste des fichiers

**Créés :**
- `game/data/skeletons/snake_v1.skel`
- `game/data/skeletons/snake_v1.sockets` (peut être vide ou sans socket)
- `game/data/skeletons/snake_v1.json`
- `game/data/animations/snake/{idle,slither_loop,attack_strike,die}.anim`
- `game/data/animations/snake/SOURCES.md`
- `game/data/models/serpent/serpent.{skinmesh,species.json}`
- `tools/anim_placeholders/snake/snake_rig.gltf` + clips ou script de
  génération sinusoïdale.

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skeleton_builder --validate` exit 0 sur `snake_v1.skel` (16 os,
      chaîne linéaire).
- [ ] Les 4 clips compilent.
- [ ] `serpent.skinmesh` validé, `jointIndices` < 16.
- [ ] À l'inspection, le clip `slither_loop` produit une onde lisible
      (ne pas figer à 0°).

---

## Anti-objectifs

- **Ne pas** introduire d'IK spline (le sampler interpole par os).
- **Ne pas** introduire de cobra / vipère / boa différenciés (un seul
  modèle).
- **Ne pas** modifier les SM existantes — l'AnimalAI quadrupède est
  réutilisée avec mapping clips serpent.
