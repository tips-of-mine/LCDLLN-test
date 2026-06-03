# CHAR-MODEL.17 — Modèle **Nain**

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : game/data/configuration/races/nains.json
> - Manque : skinmesh binaire absent
> - Resume : Manifest Nain ok, mesh absent

## Dépendances
- CHAR-MODEL.14 (`humanoid_v1.skel`, sockets, clips placeholder)

## Cadrage

Livrer le **modèle Nain** : skinné sur `humanoid_v1`, proportions
**trapues** (–25% taille globale, +20% épaules, jambes courtes).

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
mkdir -p game/data/models/nain
grep -n '"id": "nains"' game/data/races/races.json
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/nain/nain.gltf` :
- skin sur `humanoid_v1`,
- proportions : `globalScale=0.75`, `shouldersScale=1.20`,
  `limbsLength=0.85` (membres ramassés), torse normal.
- Bind pose : T-pose adaptée — les bras horizontaux paraîtront courts,
  c'est attendu.
- Submeshes : `body`, `head`, `hands`, `beard` (barbe — submesh séparé),
  `clothes_placeholder`.

### Manifest race `nain.race.json`

```json
{
  "id": "nains",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/nain/nain.skinmesh",
  "scale": {
    "global":   0.75,
    "shoulders":1.20,
    "limbs":    0.85,
    "torso":    1.00
  },
  "morphologyBounds": {
    "globalScale":   [0.70, 0.82],
    "corpulence":    [0.95, 1.30],
    "limbsLength":   [0.80, 0.90]
  },
  "colorCode": "#A0826D",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0, "beard": 2,
    "clothes_placeholder": 1
  }
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/nain/nain.skinmesh`
- `game/data/models/nain/nain.race.json`
- `tools/model_placeholders/nain/nain.gltf`
- `tools/model_placeholders/nain/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `nain.race.json` parse, `globalScale=0.75`, `shouldersScale=1.20`.
- [ ] `jointIndices` valides.
- [ ] La hauteur monde du modèle au repos est ≈ 1.30–1.45 m (pour un
      humain canonique à 1.80 m), à vérifier en bind pose.

---

## Anti-objectifs

- Mêmes que CHAR-MODEL.15.
- **Ne pas** raccourcir le squelette : on conserve `humanoid_v1` à
  60 os, c'est la **bind pose et la skin** qui encodent la réduction.
