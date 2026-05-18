# Y Bot — Jump (Mixamo)

**Source** : Mixamo (Adobe), personnage Y Bot, animation **Jump**, with-skin.
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_jump -Category avatars -SourceFbx "Jump"` (FBX2glTF v0.13.0 Godot fork).
**Taille** : ~1.98 MB (with-skin = mesh + skeleton + clip baked).

## Animations dans le fichier

- **`mixamo.com`** : cycle Jump complet (~65 frames = takeoff + airtime + landing). État machine B.1 utilise les ~40% du début pour la phase de takeoff, puis transition vers `Fall` après duration × 0.4.
- **`Take 001`** : track vide, filtrée par `duration > 0.0f`.

## Usage runtime

Chargé via `SkinnedMeshLoader::LoadClipsRetargeted`. État `AvatarLocomotionState::Jump`.

## Licence

Mixamo Terms of Use — usage commercial autorisé.
