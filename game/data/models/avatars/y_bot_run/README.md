# Y Bot — Run (Fast Run, Mixamo)

**Source** : Mixamo (Adobe), personnage Y Bot, animation **Fast Run**, with-skin.
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_run -Category avatars -SourceFbx "Fast Run"` (FBX2glTF v0.13.0 Godot fork).
**Taille** : ~1.94 MB (with-skin = mesh + skeleton + clip baked).

## Animations dans le fichier

Le .glb contient 2 animations exposées par FBX2glTF :
- **`mixamo.com`** : Fast Run cycle, 16 frames — c'est le clip à jouer, renommé `Run` au load par Engine.cpp.
- **`Take 001`** : track vide héritée de l'export FBX (range UINT32_MAX). Filtrée par `duration > 0.0f` dans le loader.

## Usage runtime

Chargé via `SkinnedMeshLoader::LoadClipsRetargeted` (loader standard for with-skin files).
État `AvatarLocomotionState::Run` dans la state machine de `Engine.cpp` (sous-projet B.1).

## Licence

Mixamo Terms of Use — usage commercial autorisé pour les assets téléchargés.
