# Conventions de nommage — assets de personnages

S'applique aux assets du système de customisation (`game/data/models/characters/`,
`game/data/textures/characters/`, `tools/asset_pipeline/inbox/`).

## Règle générale

- **snake_case**, ASCII minuscule, chiffres, underscore. **Pas d'espaces**, pas
  de majuscules, pas d'accents dans les noms de fichiers/dossiers.
- Indices sur 2 chiffres avec zéro initial : `head_01`, `long_02`.
- `validate_fbx.py` rejette tout nom non conforme (`^[a-z0-9_]+\.fbx$`).

## Ids de race

Toujours ceux de `game/data/races/races.json` :
`humains`, `elfes`, `orcs`, `nains`, `morts_vivants`, `corrompus`, `divins`,
`demons`.

## Fichiers FBX

Format : `{categorie}_{variante}_{detail}.fbx`

| Type | Pattern | Exemple |
|------|---------|---------|
| Corps | `{gender}_body_{type}.fbx` | `male_body_base.fbx`, `female_body_athletic.fbx` |
| Tête | `head_{NN}.fbx` | `head_01.fbx` |
| Cheveux | `{style}_{NN}.fbx` | `long_01.fbx`, `braided_01.fbx`, `bald.fbx` |
| Pilosité | `beard_{variante}.fbx` / `mustache_{NN}.fbx` | `beard_full_01.fbx`, `goatee_01.fbx` |
| Trait racial | `{nom}.fbx` | `pointed_long.fbx`, `curved_01.fbx`, `spaded_long.fbx` |
| Absence | `none.fbx` | utilisé pour « aucun » (cornes, queue, pilosité…) |

Les chemins déclarés dans `configuration/races/<id>.json` sont relatifs à
`paths.content` (`game/data`), p.ex.
`models/characters/orcs/tusks/large.fbx`.

## Arborescence des modèles

```
game/data/models/characters/<race>/
├── base/            {gender}_body_{type}.fbx
├── heads/{male,female}/   head_NN.fbx
├── hair/{male,female}/    {style}_NN.fbx
├── facial_hair/     beard_*.fbx, mustache_*.fbx, none.fbx
└── <feature>/       tusks|horns|tails|ears|scales|wings|halos|mutations
```

## Bones du squelette (`humanoid_base`)

| Zone | Bones |
|------|-------|
| Racine / bassin | `root`, `pelvis` |
| Colonne | `spine_01`, `spine_02`, `spine_03`, `neck`, `head` |
| Clavicules | `clavicle_left`, `clavicle_right` |
| Bras | `upperarm_{left,right}`, `forearm_{left,right}`, `hand_{left,right}` |
| Jambes | `thigh_{left,right}`, `calf_{left,right}`, `foot_{left,right}` |

Ces noms sont utilisés par `ResolvedCharacterAssets.boneScales` et par les
sockets d'`sockets_attachments.json` — toute divergence casse l'attachement et
le scaling.

## Textures

Format : `{nom}_{type}.png` — types : `diffuse`, `normal`, `orm`
(Occlusion / Roughness / Metallic).

| Catégorie | Exemple |
|-----------|---------|
| Peau | `skin_01_diffuse.png`, `skin_01_normal.png`, `skin_01_orm.png` |
| Cheveux | `hair_01.png` |
| Yeux | `eye_01.png` |

Dossier : `game/data/textures/characters/<race>/{skin,hair,eyes}/`.

## Sets d'animations

`humanoid_base` (partagé) ; sets raciaux suffixés `_specific` et alignés sur
l'id de race : `orcs_specific`, `demons_specific`, `nains_specific`.
