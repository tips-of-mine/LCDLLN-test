# Y Bot — Land (hard landing, Mixamo)

**Source** : Mixamo (Adobe), animation **hard landing**, **animation-only** (option "without skin" cochée).
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_land -Category avatars -SourceFbx "hard landing"` (FBX2glTF v0.13.0).
**Taille** : ~70 KB (très petit — animation-only).

## Particularité : animation-only

Idem `y_bot_fall.glb` : pas de `cgltf_skin`. Chargé via `SkinnedMeshLoader::LoadClipsAnimOnly` (Task 4).

## Animations dans le fichier

- **`mixamo.com`** : hard landing one-shot (~60 frames = impact + recovery posture). Renommé `Land` au load. Joue une seule fois (pas de loop) puis transition vers Idle / Walk / Run selon input courant.

## Usage runtime

État `AvatarLocomotionState::Land` (one-shot après transition depuis Fall quand `cc.IsGrounded()` devient vrai).

## Licence

Mixamo Terms of Use — usage commercial autorisé.
