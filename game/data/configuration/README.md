# Configuration — Système de personnalisation de personnages

Ce dossier contient la configuration *data-driven* du système de customisation
de personnages (CHAR-MODEL.25). Ajouter du contenu ici **ne nécessite pas de
recompiler** : `CharacterCustomizationSystem` (C++) charge ces fichiers au
démarrage.

## Arborescence

```
configuration/
├── races/                  # 1 fichier JSON par race (id aligné sur races.json)
│   ├── humains.json
│   ├── elfes.json
│   ├── orcs.json
│   ├── nains.json
│   ├── morts_vivants.json
│   ├── corrompus.json
│   ├── divins.json
│   └── demons.json
├── customization/
│   └── body_proportions.json   # presets + plages globales de proportions
├── equipment/
│   ├── armor_sets.json         # sets d'armure + bonus de set
│   └── sockets_attachments.json# sockets d'attachement sur humanoid_base
└── animations/
    └── animation_sets.json     # ensembles d'animations par race
```

## Alignement sur l'existant

Les `raceId` de `races/*.json` sont **exactement** ceux de
`game/data/races/races.json` (utilisé par le MVP `CharacterCreationPresenter`).
Il n'y a **pas** de taxonomie parallèle : ce dossier ajoute une *couche de
customisation* (limites physiques, modules, traits raciaux, morph targets) à des
races déjà définies ailleurs.

Les fichiers `races/*.json` sont **générés** depuis `races.json` par :

```
python3 tools/asset_pipeline/gen_race_configs.py
```

La source de vérité des palettes de couleurs (peau/cheveux/yeux) reste
`races.json` (`defaultSkinColors`, `defaultHairColors`, `defaultEyeColors`). Le
générateur les convertit en entrées `id`/`hex`/`texture`. Pour ajuster les
limites physiques, types de corps ou traits raciaux, éditer la table
`RACE_SPECS` du générateur puis le ré-exécuter.

## Schéma d'un fichier de race

Champs lus par le C++ (`RaceConfiguration::LoadFromFile`) :

| Clé | Type | Rôle |
|-----|------|------|
| `raceId`, `displayName`, `description` | string | Identité (id aligné sur races.json) |
| `baseSkeleton`, `animationSet` | string | Squelette + set d'animations |
| `physicalLimits.height.{baseMeters,scaleRange}` | number | Taille + plage de scaling |
| `physicalLimits.bodyMass.range` | number | Plage de corpulence |
| `physicalLimits.proportions.{legLength,shoulderWidth,torsoWidth}` | number | Plages de proportions |
| `collisionDefaults.{radius,height}` | number | Capsule de collision de base |
| `genders[]` | string | `male`, `female` |
| `bodyTypes.<gender>[]` | objet | `{id, model, displayName, description}` |
| `heads.<gender>[]`, `hairStyles.<gender>[]`, `facialHair.<gender>[]` | objet | `{id, model, displayName}` |
| `racialFeatures.<feature>[]` | objet | features reconnues : `tusks`, `horns`, `tails`, `ears`, `scales`, `wings`, `halos`, `mutations` |
| `skinTones[]` | objet | `{id, displayName, hex, diffuse, normal, orm}` |
| `hairColors[]`, `eyeColors[]` | objet | `{id, displayName, hex, texture[, emissive]}` |
| `morphTargets.{face,body}[]` | objet | `{name, min, max, default, displayName}` |
| `additionalAnimations[]` | string | sets d'anim supplémentaires |

Le parseur JSON utilisé est `engine::core::Config` (aplatissement en clés
`a.b[i].c`). Tout JSON valide est accepté.

## Ajouter une nouvelle race

Voir `docs/CHARACTER_CUSTOMIZATION.md` (section « Ajouter une race »). En résumé :
1. ajouter la race dans `races.json` (id + palettes) ;
2. ajouter une entrée dans `RACE_SPECS` du générateur ;
3. lancer `gen_race_configs.py` ;
4. créer l'arborescence de modèles `models/characters/<id>/` ;
5. déposer les assets dans `tools/asset_pipeline/inbox/characters/<id>/`.

Aucune recompilation du moteur n'est nécessaire : `Initialize()` découvre
automatiquement tous les `races/*.json`.
