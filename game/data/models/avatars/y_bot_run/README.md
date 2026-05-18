# Y Bot — Run (running, Mixamo)

**Source** : Mixamo (Adobe), animation **running**, **animation-only** (option "without skin" cochée).
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_run -Category avatars -SourceFbx "running"` (FBX2glTF v0.13.0 Godot fork).
**Taille** : ~43 KB (très petit — pas de mesh, juste les keyframes du squelette).

## Particularité : animation-only

Ce `.glb` n'a **pas** de `cgltf_skin` (Mixamo n'en exporte pas quand "without skin" est coché). Le loader standard `SkinnedMeshLoader::LoadClipsRetargeted` ne marche PAS dessus — il bail au check `data->skins_count == 0`.

## Animations dans le fichier

- **`mixamo.com`** : running cycle, 21 frames. Renommé `Run` au load par Engine.cpp.

## Usage runtime

Chargé via le helper dédié **`SkinnedMeshLoader::LoadClipsAnimOnly`** (Task 4 de B.1) qui parse les channels d'animation directement et retargete sur le skeleton de Y Bot par nom de bone.

État `AvatarLocomotionState::Run` dans la state machine de `Engine.cpp` (déclenché par input.run quand le perso se déplace).

## Choix « running » plutôt que « Fast Run »

Initialement importé depuis `Fast Run.fbx` (with-skin, 1.78 MB). Bascule sur `running.fbx`
(animation-only, 43 KB) le 2026-05-18 pour : (1) cycle de course plus générique / réaliste,
(2) réduction du poids repo (1.94 MB → 43 KB).

## Licence

Mixamo Terms of Use — usage commercial autorisé.
