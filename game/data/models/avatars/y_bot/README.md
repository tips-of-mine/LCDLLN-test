# Y Bot — placeholder humanoid (Mixamo)

**Source** : https://www.mixamo.com (compte Adobe gratuit)
**Personnage** : Y Bot (mannequin gris Mixamo standard)
**Animation incluse** : Standard Walk (cycle de marche en place, 36 frames, in-place)
**Téléchargé le** : 2026-05-18

## Origine du fichier

Téléchargé depuis Mixamo en FBX Binary avec option « With Skin » cochée
(le FBX contient à la fois le mesh + le squelette + le clip d'animation).
Le fichier source `Standard Walk.fbx` (~1.8 MB) a été déposé dans
`tools/asset_pipeline/inbox/` puis converti en glTF 2.0 binaire via :

```powershell
.\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot -Category avatars -SourceFbx "Standard Walk"
```

Convertisseur : FBX2glTF v0.13.0 (fork Godot, MIT).
Flags appliqués : `--binary --khr-materials-unlit --skinning-weights 4`
(4 joints/vertex pour matcher la stride 56 octets de `SkinnedVertex`).

## Licence

Mixamo Terms of Use — assets téléchargeables libres de droits pour usage
dans un produit commercial dérivé. Cf. https://www.mixamo.com/faq.

## Usage runtime

Chargé par `SkinnedMeshLoader::Load` (`src/client/render/skinned/SkinnedMeshLoader.cpp`,
Task 10/17 du sous-projet A) au moment de l'EnterWorld, remplace le cube
`game/data/meshes/avatar_placeholder.mesh`.

## Animations dans le fichier

Le .glb contient 2 animations exposées par FBX2glTF :
- **`mixamo.com`** : cycle Walking, 36 frames — c'est le clip à jouer.
- **`Take 001`** : track vide héritée de l'export FBX (range [UINT32_MAX, UINT32_MAX]).
  À ignorer côté runtime — `SkinnedMeshLoader` lit la duration depuis l'accessor input
  et filtrera ce clip vide naturellement (duration = 0).
