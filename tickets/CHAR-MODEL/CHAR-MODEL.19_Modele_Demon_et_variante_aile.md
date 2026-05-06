# CHAR-MODEL.19 — Modèle **Démon** (+ variante ailé)

## Dépendances
- CHAR-MODEL.14
- CHAR-MODEL.12 (sockets : ajout d'un socket `ailes` au rig)

## Cadrage

Livrer **deux .skinmesh** dans le même dossier (un fichier par modèle,
règle du repo) :
- `demon.skinmesh` : démon de base, sans ailes ;
- `demon_winged.skinmesh` : variante avec ailes attachées au dos via
  socket dédié.

Proportions : **+30% taille globale, +40% masse**. Cornes (tête) et
queue intégrées au mesh principal (pas de submesh séparé pour la queue
puisqu'elle n'a pas d'os dans `humanoid_v1` — la queue sera un cosmétique
statique pendant cette release ; un rig étendu viendra plus tard).

Les ailes sont rendues via un **submesh à part** dans
`demon_winged.skinmesh`, skinnées sur 2 os virtuels (`wing_root_l`,
`wing_root_r`). Comme `humanoid_v1` n'a pas ces os, la variante ailé
n'utilise **pas** `humanoid_v1` directement : on extension le rig en
fournissant `humanoid_v1_winged.skel` (variante du rig livrée dans ce
ticket).

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
ls engine/render/AttachmentSocket.h
mkdir -p game/data/models/demon
grep -n '"id": "demons"' game/data/races/races.json
```

---

## Spécification technique

### Variante de rig `humanoid_v1_winged.skel`

Identique à `humanoid_v1` + 4 os additionnels enfants de `spine_3` :

```
spine_3
├─ wing_root_l
│  └─ wing_tip_l
└─ wing_root_r
   └─ wing_tip_r
```

Total ≈ 64 os. Les bind poses des ailes sont en arrière, pliées le long
du dos.

Le sampler/blender existants supportent ce rig sans modification (taille
≤ 256 os). Les clips d'animation pour le démon **non-ailé** continuent
d'utiliser `humanoid_v1` (les os ailes sont absents → ignorés).

### Modèles

`tools/model_placeholders/demon/demon.gltf` (sans ailes, skin sur
`humanoid_v1`) et `tools/model_placeholders/demon/demon_winged.gltf`
(avec ailes, skin sur `humanoid_v1_winged`).

### Manifest race `demon.race.json`

```json
{
  "id": "demons",
  "variants": [
    {
      "name": "default",
      "skeleton": "game/data/skeletons/humanoid_v1.skel",
      "skinmesh":  "game/data/models/demon/demon.skinmesh"
    },
    {
      "name": "winged",
      "skeleton": "game/data/skeletons/humanoid_v1_winged.skel",
      "skinmesh":  "game/data/models/demon/demon_winged.skinmesh"
    }
  ],
  "defaultVariant": "default",
  "scale": {
    "global":   1.30,
    "shoulders":1.30,
    "limbs":    1.10,
    "torso":    1.30
  },
  "morphologyBounds": {
    "globalScale":   [1.20, 1.40],
    "corpulence":    [1.20, 1.50],
    "limbsLength":   [1.05, 1.15]
  },
  "colorCode": "#7A1F1F",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0, "horns": 4,
    "wings": 5, "clothes_placeholder": 1
  }
}
```

### Sockets ailes (catalogue)

`game/data/skeletons/humanoid_v1_winged.sockets` étend
`humanoid_v1.sockets` avec :

| Socket | Joint           | Usage                       |
|--------|-----------------|-----------------------------|
| `aileR`| `wing_tip_r`    | Effet visuel attaché droite |
| `aileL`| `wing_tip_l`    | Effet visuel attaché gauche |

---

## Liste des fichiers

**Créés :**
- `game/data/models/demon/demon.skinmesh`
- `game/data/models/demon/demon_winged.skinmesh`
- `game/data/models/demon/demon.race.json`
- `game/data/skeletons/humanoid_v1_winged.skel`
- `game/data/skeletons/humanoid_v1_winged.sockets`
- `game/data/skeletons/humanoid_v1_winged.json` (manifest rig)
- `tools/model_placeholders/demon/demon.gltf`
- `tools/model_placeholders/demon/demon_winged.gltf`
- `tools/model_placeholders/demon/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skeleton_builder` produit `humanoid_v1_winged.skel` valide,
      ordre topologique préservé, taille ≤ 256 os.
- [ ] `skinmesh_builder --validate` exit 0 sur les deux `.skinmesh`.
- [ ] `demon.race.json` parse, le tableau `variants` contient 2 entrées,
      le `defaultVariant` existe.
- [ ] `humanoid_v1_winged.sockets` charge contre le rig étendu.

---

## Anti-objectifs

- **Ne pas** modifier `humanoid_v1.skel` — le rig étendu est un
  **fichier distinct**.
- **Ne pas** introduire de queue rigide (placeholder statique pour
  cette release).
- **Ne pas** introduire d'animation de battement d'ailes (clip placeholder
  livré plus tard si nécessaire).
- **Ne pas** câbler les sockets ailes en tant que socket de monture
  (CHAR-MODEL.30).
