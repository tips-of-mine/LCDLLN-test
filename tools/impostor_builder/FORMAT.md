# Format de l'atlas d'impostors octaédriques (M45.4 — FORMAT v2)

Fichier binaire produit par `impostor_builder`, consommé au runtime par M45.5.
Toutes les valeurs multi-octets sont en **little-endian**, sérialisées
**champ par champ** (indépendant du layout mémoire / padding des structs).

La **version 2** du format ajoute un **troisième atlas** (ORM) après albedo et
normal, échantillonne l'albedo depuis les **textures baseColor** du matériau,
applique un **anti-aliasing par supersampling** et renseigne un `contentHash`.

## Disposition

| Offset | Taille | Champ            | Type    | Description                              |
|-------:|-------:|------------------|---------|------------------------------------------|
| 0      | 4      | `magic`          | uint32  | `0x4F50494D` = ASCII `MIPO` (LE)         |
| 4      | 4      | `formatVersion`  | uint32  | Version du format (=2)                   |
| 8      | 4      | `builderVersion` | uint32  | Version de l'outil (=2)                  |
| 12     | 4      | `engineVersion`  | uint32  | Version moteur cible (0 = indéfini)      |
| 16     | 8      | `contentHash`    | uint64  | FNV-1a 64 des octets du .gltf source     |
| 24     | 4      | `viewsPerAxis`   | uint32  | N : grille N×N de vues                   |
| 28     | 4      | `tileSize`       | uint32  | Côté d'une tile en pixels                |
| 32     | 4      | `channels`       | uint32  | Canaux par texel (=4, RGBA8)             |
| 36     | 12     | `boundsMin[3]`   | float×3 | Coin min AABB monde du mesh              |
| 48     | 12     | `boundsMax[3]`   | float×3 | Coin max AABB monde du mesh              |
| 60     | 8      | `albedoSize`     | uint64  | Taille en octets de l'atlas albedo       |
| 68     | albedoSize | `albedo`     | uint8[] | Atlas albedo RGBA8                       |
| …      | 8      | `normalSize`     | uint64  | Taille en octets de l'atlas normal       |
| …      | normalSize | `normal`     | uint8[] | Atlas normal RGBA8 (encodé + profondeur) |
| …      | 8      | `ormSize`        | uint64  | Taille en octets de l'atlas ORM          |
| …      | ormSize | `orm`           | uint8[] | Atlas ORM RGBA8 (AO/rough/metal)         |

`albedoSize == normalSize == ormSize == (viewsPerAxis*tileSize)^2 * 4`.

Le header logique fait 24 octets (`ImpostorHeader`), suivi des métadonnées
`ImpostorAtlasInfo`, puis des **trois** blocs taille+données dans l'ordre disque
**albedo, normal, orm**.

## Hash de contenu (`contentHash`)

`contentHash` est le **FNV-1a 64 bits** des **octets bruts** du fichier `.gltf`
source (offset basis `1469598103934665603`, prime `1099511628211`). Il s'agit
bien de FNV-1a, **pas** de xxHash. Il permet de détecter qu'un mesh source a
changé sans rejouer le rendu.

## Anti-aliasing par supersampling

Chaque tile est rendue à une résolution **SS×** (`--ss`, défaut 2) :
`ssTile = tileSize * SS`. Après rendu de toutes les vues, chaque atlas est
**box-downsamplé** vers la résolution finale en moyennant, par canal RGBA, le
bloc `SS×SS` de texels source pour chaque texel final (moyenne simple). Le
fichier ne contient que les atlas à la résolution finale (`tileSize`).

## Disposition de l'atlas (tiles)

L'atlas est une grille `viewsPerAxis × viewsPerAxis` de tiles carrées de
`tileSize` pixels. La tile à la colonne `i` (axe X) et la ligne `j` (axe Y)
occupe la sous-région :

```
x ∈ [ i*tileSize , i*tileSize + tileSize )
y ∈ [ j*tileSize , j*tileSize + tileSize )
```

La ligne `y=0` est en haut (projection ortho de type Vulkan, Y inversé).

## Mapping octaédrique (DOIT être identique au runtime M45.5)

Pour la tile `(i, j)`, la coordonnée octaédrique échantillonne le **centre** de
la cellule :

```
u = (i + 0.5) / viewsPerAxis
v = (j + 0.5) / viewsPerAxis
```

Décodage `(u,v) → direction` (mapping octaédrique standard, cf. Cigolle 2014) :

```
p   = (2u - 1, 2v - 1)
z   = 1 - |p.x| - |p.y|
si z < 0 :
    x' = (1 - |p.y|) * sign(p.x)
    y' = (1 - |p.x|) * sign(p.y)
    (x, y) = (x', y')
sinon :
    (x, y) = (p.x, p.y)
dir = normalize(x, y, z)
```

`dir` est la direction **mesh → caméra**. La caméra est placée à
`centre + dir * 2*radius` et regarde le centre de l'AABB, projection
orthographique cadrant la sphère englobante (côté `2*radius`).

Référence d'implémentation : `OctahedralMap.h` (`OctaToDir`, `ViewDir`).

## Encodage des canaux (v2)

- **Albedo** : RGBA8. `rgb` = albedo = texture **baseColor** échantillonnée via
  les UV (`TEXCOORD_0`) × `baseColorFactor.rgb` (ou simplement `baseColorFactor`
  si le matériau n'a pas de texture). `a` = **couverture** (255 sur un fragment
  écrit). Le **cutout** feuillage : pour un matériau `alphaMode` BLEND/MASK, un
  fragment dont la couverture interpolée est < 0.5 **n'est pas écrit** (préserve
  la silhouette + le z-buffer). Après downsample, l'alpha exprime la couverture
  partielle (bords antialiasés).
- **Normale** : RGBA8. `rgb` = normale monde interpolée encodée `n * 0.5 + 0.5`
  par canal. `a` = **profondeur relative** normalisée sur `[depthNear, depthFar]`
  (0 = près du plan de vue, 255 = loin).
- **ORM** : RGBA8. `r` = AO (255 par défaut), `g` = `roughnessFactor`×255,
  `b` = `metallicFactor`×255, `a` = 255.

## Limites (suivi)

- Projection **orthographique** uniquement (pas de parallaxe perspective).
- Anti-aliasing par **supersampling box** uniquement (pas de filtrage avancé).
- AO toujours 255 (pas de bake d'occlusion ambiante).
