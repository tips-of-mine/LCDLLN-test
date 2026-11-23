# Asset pipeline — Mixamo → glTF

Procédure pour intégrer un personnage Mixamo dans le client de jeu.

## Pré-requis

- Compte Adobe gratuit pour télécharger depuis https://www.mixamo.com.
- PowerShell 5+.

## Première fois : récupérer le convertisseur FBX→glTF

```powershell
.\tools\asset_pipeline\download_fbx2gltf.ps1
```

Télécharge `FBX2glTF.exe` (~8 MB, Godot fork, MIT) dans `tools/asset_pipeline/bin/` avec
vérification SHA256. Le binaire est gitignored (pas dans le repo).

## Workflow par asset

1. Sur Mixamo, choisir un personnage et **télécharger en format "FBX Binary"** (PAS
   FBX ASCII, PAS FBX for Unity, PAS FBX 7.4 / 6.1, PAS Collada).
2. Pour ajouter une animation au personnage : sur Mixamo, choisir l'animation puis cocher
   "with skin" → l'export FBX contient mesh + skeleton + clip baked.
3. Déposer le `.fbx` dans `tools/asset_pipeline/inbox/<nom>.fbx`.
4. Convertir (script ajouté en Task 9) :
   ```powershell
   .\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot -Category avatars
   ```
   → produit `game/data/models/<Category>/<EntityName>/<EntityName>.glb`.
5. Commit le `.glb` (le `.fbx` source reste gitignored).

## Conversion des PROPS (Blender headless)

Les **props statiques** (`tools/asset_pipeline/inbox/meshes/props/*.fbx`) ne passent
**pas** par FBX2glTF mais par Blender (export glTF natif), pour reproduire le pipeline
d'origine : matériaux `MI_Trim_*`, ORM packé en `metallicRoughnessTexture`, `COLOR_0`
(couleur de sommet, utilisée par les props « nature » sans texture : arbres, herbe…),
`doubleSided`, et textures *trim* partagées (`T_Trim_Furniture/Metal/Cloth_*.png`)
référencées par URI relatif dans `game/data/meshes/props/`.

Prérequis : **Blender 5.1** installé (`C:/Program Files/Blender Foundation/Blender 5.1/`).

```powershell
# Convertit tous les FBX de inbox/meshes/props sans .gltf correspondant :
& "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background `
    --python tools/asset_pipeline/convert_props_blender.py

# Un seul prop : --only <Nom>   |   Validation (sortie temp, n'ecrase pas) : --validate
```

Le script `convert_props_blender.py` reconstruit les matériaux `MI_Trim_*` (BaseColor,
Normal, ORM→roughness/metallic) et produit `game/data/meshes/props/<Nom>.gltf` + `.bin`.
**Validation** : re-convertir un prop déjà commité (ex. `Barrel`) en `--validate` puis
comparer la structure (`materials`, `images`, `metallicRoughnessTexture`, attributs) au
`.gltf` de référence avant un batch.

Les props « nature » (arbres…) sont colorés par `COLOR_0` (pas de texture). Le moteur
les rend via le flag matériau `VertexColorAlbedo` (cf. `gbuffer_geometry.frag`).

## Versions pinned

- FBX2glTF : v0.13.0 (Godot fork). Pour mettre à jour : modifier `$Version` dans `download_fbx2gltf.ps1` + recalculer SHA256.

## Arbres « dark fantasy » procéduraux (Blender headless)

`generate_trees_blender.py` génère 8 arbres dans le style sombre des BD, sans add-on
ni image externe (textures d'écorce et cartes de feuillage calculées par le script) :
`DarkOak_1/2` (chênes), `DarkPine_1/2` (pins), `GnarledDeadTree_1/2` (arbres morts),
`MarshWillow_1/2` (saules de marais). Graine fixe par arbre : la sortie est identique
d'une exécution à l'autre.

```powershell
# Régénère tous les arbres dans game/data/meshes/props :
& "C:/Program Files/Blender Foundation/Blender 5.1/blender.exe" --background `
    --python tools/asset_pipeline/generate_trees_blender.py
# Un seul arbre : -- --only DarkOak_1     |   Autre dossier : -- --out <dossier>
```

Aperçu dans un Blender ouvert (les 3 LOD côte à côte, sans export), console Python :
`exec(open(r"<dépôt>/tools/asset_pipeline/generate_trees_blender.py").read(), {"__name__": "__main__", "LCDLLN_TREES_PREVIEW": True})`

Sorties :

| Fichier | Rôle | Triangles (budget) |
|---|---|---|
| `meshes/props/<Nom>.gltf` | LOD0, détail complet (utilisé par le jeu et le catalogue) | ≤ 26 000 (≈ 4 000 à 14 000) |
| `meshes/props/<Nom>_LOD1.gltf` | LOD1 : moins de côtés, brindilles retirées, cartes plus grandes | ≤ 9 000 |
| `meshes/props/<Nom>_LOD2.gltf` | LOD2 : tronc, charpentières, quelques grandes cartes | ≤ 2 500 |
| `meshes/props/Bark_*.png`, `Leaves_*.png` | textures partagées (écorce + normal map, feuillage RGBA) | — |

Le feuillage est en `alphaMode: BLEND` (le moteur active la découpe alpha), normales des
cartes orientées vers l'extérieur de la couronne. Les arbres sont à taille réelle
(chêne ≈ 10 m, pin ≈ 17 m) et ajoutés aux tables de `tools/world_gen/lib/Vegetation.cpp`
(feuillus, conifères, marais, terres mortes). Le client bascule LOD0 → LOD1 → LOD2 →
billboard selon la distance (`world.scatter.lod1_m`, `lod2_m`, `billboard_m`) ; le
billboard `<Nom>_BB.gltf` (deux plans croisés, texture `T_Decor_BB_<Nom>.png`) se refait
avec `decor/build_tree_billboards.py` après toute régénération des arbres :

```powershell
blender -b -P tools/asset_pipeline/decor/build_tree_billboards.py
```

L'impostor `.mipo` (non versionné, ≈ 3 Mo par arbre, affiché seulement si
`world.impostor.enabled`) reste possible :

```powershell
impostor_builder.exe --input game/data/meshes/props/DarkOak_1.gltf --output game/data/meshes/props/DarkOak_1.mipo
```
