# Format de l'atlas d'impostors octaédriques (M45.4)

Fichier binaire produit par `impostor_builder`, consommé au runtime par M45.5.
Toutes les valeurs multi-octets sont en **little-endian**, sérialisées
**champ par champ** (indépendant du layout mémoire / padding des structs).

## Disposition

| Offset | Taille | Champ            | Type    | Description                              |
|-------:|-------:|------------------|---------|------------------------------------------|
| 0      | 4      | `magic`          | uint32  | `0x4F50494D` = ASCII `MIPO` (LE)         |
| 4      | 4      | `formatVersion`  | uint32  | Version du format (=1)                   |
| 8      | 4      | `builderVersion` | uint32  | Version de l'outil                       |
| 12     | 4      | `engineVersion`  | uint32  | Version moteur cible (0 = indéfini v1)   |
| 16     | 8      | `contentHash`    | uint64  | Hash du mesh source (0 si non calculé)   |
| 24     | 4      | `viewsPerAxis`   | uint32  | N : grille N×N de vues                   |
| 28     | 4      | `tileSize`       | uint32  | Côté d'une tile en pixels                |
| 32     | 4      | `channels`       | uint32  | Canaux par texel (=4, RGBA8)             |
| 36     | 12     | `boundsMin[3]`   | float×3 | Coin min AABB monde du mesh              |
| 48     | 12     | `boundsMax[3]`   | float×3 | Coin max AABB monde du mesh              |
| 60     | 8      | `albedoSize`     | uint64  | Taille en octets de l'atlas albedo       |
| 68     | albedoSize | `albedo`     | uint8[] | Atlas albedo RGBA8                       |
| …      | 8      | `normalSize`     | uint64  | Taille en octets de l'atlas normal       |
| …      | normalSize | `normal`     | uint8[] | Atlas normal RGBA8 (encodé + masque)     |

`albedoSize == normalSize == (viewsPerAxis*tileSize)^2 * 4`.

Le header logique fait 24 octets (`ImpostorHeader`), suivi des métadonnées
`ImpostorAtlasInfo`, puis des deux blocs taille+données.

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

## Encodage des canaux

- **Albedo** : RGBA8. En v1, l'albedo provient de la **couleur de sommet**
  (`COLOR_0`) interpolée — voir limites ci-dessous. Alpha = alpha couleur.
- **Normale** : RGBA8. La normale monde interpolée est encodée
  `n * 0.5 + 0.5` par canal. L'**alpha vaut 255 si le texel est couvert**
  (masque de silhouette), 0 sinon.

## Limites v1 (suivi)

- **Albedo = couleur de sommet uniquement.** Les textures matériau
  (baseColorTexture, UV `TEXCOORD_0`) **ne sont pas échantillonnées** en v1.
  Un mesh sans `COLOR_0` rend en blanc opaque. Échantillonnage de textures =
  évolution future (v2).
- Projection **orthographique** uniquement (pas de parallaxe perspective).
- Pas d'anti-aliasing (1 échantillon par pixel, test de couverture binaire).
- Pas de hash de contenu calculé (`contentHash = 0`).
