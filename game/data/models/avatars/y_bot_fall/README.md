# Y Bot — Fall (falling idle, Mixamo)

**Source** : Mixamo (Adobe), animation **falling idle**, **animation-only** (option "without skin" cochée).
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_fall -Category avatars -SourceFbx "falling idle"` (FBX2glTF v0.13.0).
**Taille** : ~43 KB (très petit — pas de mesh, juste les keyframes du squelette).

## Particularité : animation-only

Ce `.glb` n'a **pas** de `cgltf_skin` (Mixamo n'en exporte pas quand "without skin" est coché). Le loader standard `SkinnedMeshLoader::LoadClipsRetargeted` ne marche PAS dessus — il bail au check `data->skins_count == 0`.

## Animations dans le fichier

- **`mixamo.com`** : cycle Fall loop (~21 frames). Renommé `Fall` au load par Engine.cpp.
- Pas de `Take 001` (Mixamo n'exporte pas le placeholder vide pour les fichiers animation-only).

## Usage runtime

Chargé via le helper dédié **`SkinnedMeshLoader::LoadClipsAnimOnly`** (ajouté par Task 4 de B.1) qui parse les channels d'animation directement et retargete sur le skeleton de Y Bot par nom de bone (les noms `mixamorig:*` matchent ceux de y_bot.glb).

État `AvatarLocomotionState::Fall` (loop in-air après transition depuis Jump).

## Licence

Mixamo Terms of Use — usage commercial autorisé.
