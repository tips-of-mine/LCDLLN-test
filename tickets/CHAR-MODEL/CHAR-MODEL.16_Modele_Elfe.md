# CHAR-MODEL.16 — Modèle **Elfe**

> **Etat : PARTIEL** (verifie 2026-06-03)
> - Fait / preuves : game/data/configuration/races/elfes.json
> - Manque : skinmesh binaire absent
> - Resume : Manifest Elfe ok, mesh absent

## Dépendances
- CHAR-MODEL.14 (`humanoid_v1.skel`, sockets, clips placeholder)

## Cadrage

Livrer le **modèle Elfe** : skinné sur `humanoid_v1`, proportions
**élancées** (+5% taille globale, –10% épaules) par rapport à l'étalon
humain. Mêmes 60 os, géométrie distincte.

---

## Pré-requis vérifiables

```bash
git status
ls game/data/skeletons/humanoid_v1.skel
ls tools/skinmesh_builder/
ls game/data/models/ 2>/dev/null || mkdir -p game/data/models/elfe
grep -n '"id": "elfes"' game/data/races/races.json
```

---

## Spécification technique

### Mesh source

`tools/model_placeholders/elfe/elfe.gltf` :
- skin sur `humanoid_v1` (mêmes noms d'os),
- proportions : `globalScale=1.05`, `shouldersScale=0.90`,
  `limbsLength=1.05` (jambes/bras un peu plus longs),
- submeshes : `body`, `head`, `hands`, `ears` (oreilles pointues — submesh
  séparé pour autoriser variation), `clothes_placeholder`.

### Manifest race `elfe.race.json`

```json
{
  "id": "elfes",
  "skeleton": "game/data/skeletons/humanoid_v1.skel",
  "skinmesh":  "game/data/models/elfe/elfe.skinmesh",
  "scale": {
    "global":   1.05,
    "shoulders":0.90,
    "limbs":    1.05,
    "torso":    1.00
  },
  "morphologyBounds": {
    "globalScale":   [1.00, 1.10],
    "corpulence":    [0.80, 1.05],
    "limbsLength":   [1.00, 1.10]
  },
  "colorCode": "#F5DEB3",
  "submeshMaterialSlots": {
    "body": 0, "head": 0, "hands": 0, "ears": 0,
    "clothes_placeholder": 1
  }
}
```

---

## Liste des fichiers

**Créés :**
- `game/data/models/elfe/elfe.skinmesh`
- `game/data/models/elfe/elfe.race.json`
- `tools/model_placeholders/elfe/elfe.gltf`
- `tools/model_placeholders/elfe/SOURCES.md`

---

## CMakeLists.txt

Aucune modification.

---

## Critères d'acceptation

- [ ] `skinmesh_builder --validate` exit 0.
- [ ] `elfe.race.json` parse, `globalScale=1.05`, `shouldersScale=0.90`.
- [ ] Tous les `jointIndices` < humanoid_v1.jointCount.
- [ ] Rendu en bind pose visible et conforme aux proportions (validation
      manuelle si pas de test automatique).

---

## Anti-objectifs

- Mêmes que CHAR-MODEL.15 (pas de matériau, pas de modification rig,
  pas de viewer).
- **Ne pas** introduire de variant `elfe_noir` / `haut_elfe` — un seul
  modèle Elfe à ce stade.
