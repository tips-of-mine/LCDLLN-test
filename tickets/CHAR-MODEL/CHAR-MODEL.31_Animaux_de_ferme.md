# CHAR-MODEL.31 — Vache, cochon, chèvre, lapin (animaux de ferme)

## Dépendances
- CHAR-MODEL.27 (`quadruped_v1.skel` + clips)

## Cadrage

Livrer **quatre modèles** d'animaux de ferme, chacun dans son propre
dossier (un fichier par espèce, règle du repo) :
- vache, cochon, chèvre, lapin.

Tous skinnés sur `quadruped_v1`. Différences uniquement géométriques
(taille, proportions). Archétypes IA :
- Vache, chèvre, lapin → `Herbivore`.
- Cochon → `Neutral`.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/quadruped_v1.skel
ls game/data/animations/quadruped/
mkdir -p game/data/models/{vache,cochon,chevre,lapin}
```

---

## Spécification technique

### Tailles cibles (au garrot)

| Espèce  | Taille cible | `globalScale` (vs `quadruped_v1` étalon = 1.0) |
|---------|--------------|------------------------------------------------|
| Vache   | 1.40 m       | 1.00                                           |
| Cochon  | 0.80 m       | 0.55                                           |
| Chèvre  | 0.80 m       | 0.55                                           |
| Lapin   | 0.30 m       | 0.18                                           |

(Le quadrupède étalon = vache adulte ≈ 1.40 m au garrot.)

### Manifests

Chaque manifest suit le même schéma que `cheval.species.json`
(CHAR-MODEL.29) **sans** `mountable` (par défaut absent ou `false`).

Exemple `vache.species.json` :

```json
{
  "id": "vache",
  "skeleton": "game/data/skeletons/quadruped_v1.skel",
  "skinmesh":  "game/data/models/vache/vache.skinmesh",
  "ai": {
    "archetype": "Herbivore",
    "wanderSpeed": 0.8,
    "fleeSpeed":   3.0,
    "fleeTriggerDistance": 6.0,
    "sightRange": 12.0
  },
  "mountable": false,
  "colorCode":  "#A47148"
}
```

Variantes par espèce :

| Espèce | `archetype` | `wanderSpeed` | `fleeSpeed` | `colorCode` |
|--------|-------------|---------------|-------------|-------------|
| vache  | Herbivore   | 0.8           | 3.0         | `#A47148`   |
| cochon | Neutral     | 0.6           | 2.0         | `#D8AFA0`   |
| chèvre | Herbivore   | 1.2           | 4.0         | `#E0DACE`   |
| lapin  | Herbivore   | 1.5           | 5.0         | `#9C8C7E`   |

### Submeshes

Tous : `body`, `head`. Cochon ajoute `ears`, chèvre ajoute `horns`,
lapin ajoute `ears` (longues). Pas de `tail_static` séparée — la queue
est skinnée via les os `tail_*` du rig.

---

## Liste des fichiers

**Créés (un par espèce, en parallèle) :**
- `game/data/models/vache/vache.skinmesh`
- `game/data/models/vache/vache.species.json`
- `tools/model_placeholders/vache/vache.gltf`
- `tools/model_placeholders/vache/SOURCES.md`
- *(idem pour cochon, chèvre, lapin)*

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] Pour chacun des 4 modèles : `skinmesh_builder --validate` exit 0,
      manifest JSON parse, `jointIndices` valides.
- [ ] Tailles relatives respectées (vérification par lecture des
      bounds locales du `.skinmesh` + `globalScale` du manifest).
- [ ] Aucun fichier ne fusionne deux espèces.

---

## Anti-objectifs

- **Ne pas** ajouter de `mountable` (aucun de ces 4 n'est montable).
- **Ne pas** introduire de comportement de troupeau / grégaire (réservé
  à un système de groupes futur).
- **Ne pas** introduire de variantes de robe par espèce dans cette
  release.
- **Ne pas** modifier `quadruped_v1.skel` ni les clips.
