# Sous-projet A — Fondations skinning + runtime animation

**Statut** : design validé par l'utilisateur le 2026-05-18, prêt pour rédaction d'un plan d'implémentation.

**Périmètre** : `lcdlln_client.exe` (client de jeu) et accessoirement `lcdlln_world_editor.exe`.

**Déploiement** : ✅ **Client uniquement, pas de redéploiement serveur.** Aucun changement wire, aucun nouveau handler serveur, aucune migration DB.

---

## 1. Contexte et motivation

Ce spec est le **sous-projet A** d'une décomposition à 11 sous-projets (A→K) couvrant à terme :
animation humanoïde complète, races, création de personnage 3D, locomotion, combat, montures, animaux, IA d'ambiance, etc. Cette décomposition a été validée avec l'utilisateur avant de rentrer dans A.

A est le **chemin critique** : tous les autres sous-projets dépendent d'un format de mesh skinné, d'un runtime de squelette, et d'un lecteur de clip d'animation. Tant que A n'est pas livré, le client est incapable d'afficher un personnage animé.

### État actuel constaté

- L'avatar du joueur est un cube monochrome `game/data/meshes/avatar_placeholder.mesh` (stride 32 = pos+normal+UV). Aucun bone, aucun skin weight.
- Aucun fichier `*Skeleton*` ni `*Anim*` dans `src/`. Aucun runtime d'animation.
- `OrbitalCameraController` a déjà un enum `LocomotionState {Idle, Walk, Run}` et un `walkBobPhase`, mais c'est un bob Y synthétique sur la matrice de l'avatar — pas une vraie animation squelettique.
- 6 races sont déclarées en DB (`humains, elfes, orcs, nains, demons, chevaliers_dragons`), mais le rendu ne change pas selon la race.

### Jalon de validation de A

Le sous-projet A est **terminé** quand, dans `lcdlln_client.exe` post-EnterWorld, le cube `avatar_placeholder.mesh` est remplacé par un personnage humanoïde Mixamo qui **joue un cycle de marche en boucle, en permanence**, peu importe que le joueur bouge ou non. La connexion "le perso ne marche que quand il avance" est explicitement le périmètre du sous-projet B (state machine de locomotion).

Ce jalon prouve **tout** le runtime end-to-end : import glTF, squelette, données de clip, sampling temporel, hiérarchie de bones, calcul de matrices finales, upload SSBO, skinning GPU.

---

## 2. Source des assets : Mixamo + conversion FBX → glTF

### Workflow d'authoring (manuel, occasionnel)

1. L'utilisateur télécharge un personnage et une animation sur https://www.mixamo.com (compte Adobe gratuit).
2. Export Mixamo : **FBX Binary** (pas FBX ASCII, pas FBX for Unity, pas FBX 7.4/6.1, pas Collada — choix arrêté lors du brainstorm).
3. L'utilisateur dépose le `.fbx` dans `tools/asset_pipeline/inbox/<entity>.fbx`.
4. L'utilisateur lance `tools/asset_pipeline/fbx_to_gltf.ps1 <entity>` qui appelle `tools/asset_pipeline/bin/FBX2glTF.exe` et produit `game/data/models/<category>/<entity>/<entity>.glb`.

### Conversion : FBX2glTF

- Binaire utilisé : **fork Godot** [github.com/godotengine/FBX2glTF](https://github.com/godotengine/FBX2glTF) (le repo Meta `facebookincubator/FBX2glTF` est archivé depuis 2022).
- Stratégie repo : binaire **gitignored** (~5 MB). Le repo commit à la place :
  - `tools/asset_pipeline/download_fbx2gltf.ps1` : télécharge le release officiel, vérifie le SHA256 pinné, place le binaire dans `tools/asset_pipeline/bin/`.
  - `tools/asset_pipeline/README.md` : procédure utilisateur (Mixamo → drop → conversion → résultat), version pinned du binaire.
- La toute première utilisation du sous-projet A exécutera ce script une fois pour récupérer le binaire localement.

### Asset de référence livré avec A

`game/data/models/avatars/y_bot/y_bot.glb` : personnage Mixamo "Y Bot" (gratuit, libre d'usage), avec **un seul clip "Walk"** baked dans le fichier. C'est l'avatar qui remplace le cube. Taille attendue : ~5 MB.

---

## 3. Runtime : cgltf au load, structures plain data

### Bibliothèque tierce : cgltf

- Source : [github.com/jkuhlmann/cgltf](https://github.com/jkuhlmann/cgltf), MIT, **single-header** (~150 KB de source, ~5 KB compilé).
- Vendored dans `vendor/cgltf/cgltf.h` + `vendor/cgltf/LICENSE`. Version pinned dans un commentaire en tête de header.
- Utilisée **uniquement** par `SkinnedMeshLoader.cpp` (un seul `.cpp` définit `CGLTF_IMPLEMENTATION`).

### Structures de données runtime (plain data, indépendantes de glTF)

```
Skeleton
  std::vector<Bone> bones;          // ordre stable (parent toujours avant enfant)
  struct Bone {
    std::string name;               // ex. "mixamorig:LeftArm"
    int parentIndex;                // -1 pour la racine
    Mat4 bindLocal;                 // transform local en bind pose
    Mat4 inverseBindGlobal;         // inverse de la matrice globale en bind pose
  };

SkinnedMesh
  VkBuffer vertexBuffer;            // stride 56 : pos(12) + normal(12) + uv(8) + boneIdx[4]×u16(8) + weights[4]×f32(16)
  VkBuffer indexBuffer;             // u32 indices
  uint32_t indexCount;
  std::shared_ptr<Skeleton> skeleton;

AnimationClip
  std::string name;                 // ex. "Walk"
  float duration;                   // en secondes
  std::vector<BoneTracks> tracks;   // un par bone (tableau aligné sur Skeleton::bones)
  struct BoneTracks {
    std::vector<Keyframe<Vec3>> translation;
    std::vector<Keyframe<Quat>> rotation;
    std::vector<Keyframe<Vec3>> scale;
  };
  // Un bone sans keyframes utilise sa Skeleton::Bone::bindLocal en permanence.
```

Ces 3 types n'ont **aucune dépendance** à cgltf ou glTF. Si demain on change de format source (Blender → glTF custom, ou autre), seul `SkinnedMeshLoader` est touché.

### Sampling et calcul des matrices finales

À chaque frame, pour chaque avatar visible :

1. `AnimationSampler::Sample(clip, t) → std::vector<Mat4> localPoses` :
   - Pour chaque bone, interpole linéairement les keyframes T/R/S autour de `t` (rotation slerp).
   - Compose en matrice locale 4×4. Si le bone n'a pas de keyframes pour une track, fallback sur `Skeleton::Bone::bindLocal`.
2. `AnimationSampler::ComputeGlobalMatrices(skeleton, localPoses) → std::vector<Mat4> globals` :
   - Walk de hiérarchie en ordre d'index (garantie : parent avant enfant). `globals[i] = (parent==-1 ? localPoses[i] : globals[parent] * localPoses[i])`.
3. `AnimationSampler::ComputeFinalMatrices(skeleton, globals) → std::vector<Mat4> finals` :
   - `finals[i] = globals[i] * skeleton.bones[i].inverseBindGlobal`.
   - Ce sont ces matrices qu'on upload au shader.

Coût : O(bones) par frame par avatar. Pour ~65 bones Mixamo, négligeable.

---

## 4. Pipeline Vulkan skinné

Nouveau pipeline `SkinnedRenderer` **séparé** du `GeometryPass` actuel. Le `GeometryPass` reste utilisé pour le cube placeholder pendant la transition et pour d'éventuels props statiques après A.

### Shaders

**`game/data/shaders/skinned.vert`** :
```glsl
#version 450
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in uvec4 inBoneIdx;
layout(location = 4) in vec4  inWeights;

layout(set = 0, binding = 0) uniform CameraUBO { mat4 view; mat4 proj; } cam;
layout(push_constant) uniform PC { mat4 model; } pc;
layout(set = 1, binding = 0) readonly buffer BonesSSBO { mat4 bones[]; };

layout(location = 0) out vec3 vNormalWS;
layout(location = 1) out vec2 vUV;

void main() {
    mat4 skin =
        inWeights.x * bones[inBoneIdx.x] +
        inWeights.y * bones[inBoneIdx.y] +
        inWeights.z * bones[inBoneIdx.z] +
        inWeights.w * bones[inBoneIdx.w];
    vec4 posLocal = skin * vec4(inPos, 1.0);
    vec4 posWorld = pc.model * posLocal;
    gl_Position = cam.proj * cam.view * posWorld;
    vNormalWS = mat3(pc.model) * mat3(skin) * inNormal;
    vUV = inUV;
}
```

**`game/data/shaders/skinned.frag`** : lambert basique + couleur unie placeholder. Pas de texture (différé au sous-projet C). Format identique aux autres frags pour cohérence (sortie albedo + normal + ORM si on est en gbuffer, ou sortie color directe si on est en forward — à aligner sur l'architecture de `GeometryPass` au moment de l'implémentation).

### Configuration de l'état Vulkan — winding/face culling

⚠️ **Section critique** (cf. CLAUDE.md section "Convention winding / face culling").

- La spec glTF 2.0 mandate **CCW** winding pour les faces front.
- `Mat4::PerspectiveVulkan` (src/shared/math/Math.h) inverse Y → un mesh CCW en world-space reste CCW en framebuffer Vulkan.
- → `SkinnedRenderer` pipeline : `frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE`, `cullMode = VK_CULL_MODE_BACK_BIT`.

**À ne PAS faire** : copier la convention `CW` du `GeometryPass` actuel. Ce `CW` est correct **pour le cube** `avatar_placeholder.mesh` (qui est CW dans son fichier), mais incorrect pour un mesh glTF (qui est CCW par spec). Chaque pipeline a sa propre convention selon la source de son mesh. Cette règle est documentée dans CLAUDE.md ; ne pas la re-casser.

### SSBO de matrices d'os

- Set 1, binding 0 : `readonly buffer BonesSSBO { mat4 bones[]; };` — taille variable, dimensionnée au load selon le nombre de bones du skeleton (~65 pour Mixamo).
- Un SSBO **par avatar** (single descriptor set par avatar, recyclé par frame in-flight via le mécanisme habituel).
- Pour A : un seul avatar (le joueur local). La gestion multi-avatar est explicitement sub-projet B.
- Upload CPU→GPU à chaque frame via `vkCmdUpdateBuffer` ou un staging persistent-mapped. Choix exact arrêté lors du plan d'implémentation.

### Pipeline layout

- Set 0 : CameraUBO (partagé avec autres pipelines)
- Set 1 : BonesSSBO (spécifique au skinned)
- Push constant : `mat4 model` (16 octets sur 128 dispo — confortable)

---

## 5. Layout fichiers / dossiers

```
vendor/cgltf/
  cgltf.h                              single-header MIT (vendored)
  LICENSE

src/client/render/skinned/
  Skeleton.h / .cpp                    données Skeleton + walk hiérarchie
  SkinnedMesh.h / .cpp                 VkBuffers + lien Skeleton
  AnimationClip.h / .cpp               keyframes + duration
  AnimationSampler.h / .cpp            Sample + ComputeGlobal + ComputeFinal
  SkinnedMeshLoader.h / .cpp           cgltf → Skeleton + SkinnedMesh + AnimationClip[]
  SkinnedRenderer.h / .cpp             VkPipeline + descriptor sets + draw
  tests/
    SkinnedMeshLoaderTests.cpp         loader fixture cube_one_bone.glb
    AnimationSamplerTests.cpp          sample à t=0, t=0.25, t=0.5
    fixtures/
      cube_one_bone.glb                fixture 24 verts, 1 bone, 1 clip "wave" 0.5s

game/data/models/avatars/y_bot/
  y_bot.glb                            personnage Mixamo de référence

game/data/shaders/
  skinned.vert / .frag                 + .spv compilés par le build existant

tools/asset_pipeline/
  README.md                            procédure Mixamo → drop → conversion
  download_fbx2gltf.ps1                récupère FBX2glTF.exe (gitignored)
  fbx_to_gltf.ps1                      wrapper user-facing
  bin/
    .gitignore                         ignore FBX2glTF.exe
  inbox/
    .gitignore                         ignore *.fbx (drop zone)
```

Convention 1-fichier-par-modèle : chaque entity vit dans son propre dossier `game/data/models/<category>/<entity>/`. Conforme à la demande utilisateur "chaque modèle doit avoir ses propres fichiers". À A on en pose une seule (`y_bot`), mais l'arbre est prêt pour C (variantes raciales).

---

## 6. Intégration dans `Engine.cpp`

Deux points d'usage actuels du cube doivent basculer sur `SkinnedRenderer` :

1. **Post-EnterWorld (client de jeu)** : la branche `!m_editorMode` après le clic "Jouer" de `CharacterSelect`. Le `OrbitalCameraController` continue de pourvoir `target` ; on calcule `objectModelMatrix = T(target) × R_y(yaw_avatar)` (le `bobY` synthétique est **retiré** — c'est l'animation Walk qui produit le mouvement vertical). On passe cette matrice en push-constant au `SkinnedRenderer::Draw(playerAvatar, modelMatrix, now)`.
2. **Mode éditeur monde** (`Engine.cpp:3525-3548`, debug avatar visible dans l'éditeur) : même remplacement. On garde le même avatar pour cohérence visuelle entre client et éditeur.

Le cube `avatar_placeholder.mesh` n'est **pas supprimé** : il reste comme fallback. Le mécanisme : si `SkinnedMeshLoader::Load(path)` retourne `nullopt` (fichier absent / parse échoué), le call site dans `Engine.cpp` log l'erreur et conserve le draw du cube via le `GeometryPass` existant. Vérifié par le smoke test "supprimer y_bot.glb → client n'affiche pas de crash, affiche le cube + log d'erreur".

À l'enter-world, le `.glb` du joueur est chargé une fois (synchronously pour A, async possible dans une PR ultérieure si le load est perceptible).

### Choix du `.glb` selon la race

**Hors périmètre de A.** A charge inconditionnellement `game/data/models/avatars/y_bot/y_bot.glb`. La sélection conditionnelle race → modèle est le sous-projet C.

---

## 7. Stratégie de tests

### Tests unitaires (CI bloquante)

- **`SkinnedMeshLoaderTests`** :
  - Fixture `cube_one_bone.glb` (cube 24 verts, 1 bone "root", 1 clip "wave" de 0.5s).
  - Asserts : 1 mesh, vertex count = 24, index count = 36, skeleton.bones.size() = 1, clips.size() = 1, clip.duration ≈ 0.5f, clip.tracks[0].rotation.size() ≥ 2.
  - Test négatif : fichier inexistant → erreur explicite, pas de crash.
- **`AnimationSamplerTests`** :
  - À partir d'un `Skeleton` + `AnimationClip` construits in-memory (1 bone, 3 keyframes rotation Y).
  - Vérifie `Sample(t=0.0)` ≈ premier keyframe, `Sample(t=0.5)` ≈ slerp 50%, `Sample(t=1.0)` ≈ dernier keyframe.
  - Vérifie `ComputeGlobalMatrices` produit la composition attendue sur une hiérarchie à 3 bones.
  - Vérifie `ComputeFinalMatrices` ramène à la bind pose si `localPoses = bindLocal` pour tous les bones (les matrices finales valent l'identité).

### Smoke test visuel (manuel, documenté dans la PR)

- Lancer `lcdlln_client.exe`, créer/sélectionner un personnage, EnterWorld.
- **Critère de succès** : on voit un humanoïde Y Bot (au lieu du cube orange) qui exécute son cycle de marche en boucle, en permanence, debout sur le terrain.
- **Critères de non-régression** : terrain toujours visible (cf. CLAUDE.md section winding), caméra orbitale toujours fonctionnelle, FPS in-game stable (l'ajout du skinning ne doit pas faire chuter le framerate de manière perceptible).

### Test couvrant le fallback

- Si on supprime `y_bot.glb`, le client ne crash pas : il loggue une erreur et affiche le cube placeholder à la place. Vérifié manuellement dans la PR.

---

## 8. Périmètre explicitement HORS sous-projet A

| Exclu | Renvoyé vers |
|---|---|
| State machine Idle/Walk/Run | sous-projet B |
| Blending entre clips, transitions | sous-projet B |
| Surface-aware speed (eau/sable/neige) | sous-projet B |
| Saut hauteur/longueur | sous-projet B |
| Variantes raciales (6 squelettes différents) | sous-projet C |
| Code couleur placeholder par race | sous-projet C |
| Sélection conditionnelle race → modèle | sous-projet C |
| Preview 3D dans `CharacterCreate` + sliders morpho | sous-projet D |
| Saluer, s'asseoir, s'allonger, mourir, ouvrir conteneur, manger/boire | sous-projet E |
| Système d'attach points (armes en main, sac à dos) | sous-projet F |
| Cycles d'attaque, casting de sorts | sous-projet F |
| Cycle de nage + détection in-water | sous-projet G |
| Système de monture (perso assis sur cheval) | sous-projet H |
| Animaux (squelettes quadrupède, oiseau, etc.) | sous-projet I |
| IA d'ambiance "ils sont en vie" (look-at, wander, idle) | sous-projet J |
| Remote players visibles animés sur l'écran | sous-projet B + réseau gameplay |
| Texture diffuse sur le mesh skinné (couleur unie suffit) | sous-projet C ou ultérieur |
| IK (foot placement, look-at avec rotation cou) | sous-projet J ou ultérieur |
| Hot-reload du `.glb` à chaud | future amélioration QoL |

---

## 9. Risques identifiés et mitigations

| Risque | Mitigation |
|---|---|
| **Erreur de winding** → humanoïde invisible (régression terrain #613) | Section 4 explicite : `frontFace=CCW` car glTF mandate CCW. Smoke test visuel obligatoire dans la PR. Si invisible : d'abord vérifier `frontFace`, puis tester `cullMode=NONE` pour confirmer (cf. CLAUDE.md). |
| **Axes Mixamo ≠ axes engine** | Mixamo exporte Y up + meters. Notre engine est Y up + meters. FBX2glTF préserve la convention glTF (Y up, -Z forward). À vérifier sur Y Bot que le perso est debout sur le sol et orienté face caméra à yaw=0. Si nécessaire, appliquer une rotation correctrice au load (consignée dans le loader, pas dans la matrice modèle). |
| **`cgltf` parse `.glb` Mixamo mais pas tous les variants** | cgltf supporte glTF 2.0 complet, y compris skins et animations. La conversion FBX2glTF produit un glTF valide. Test : si load échoue sur Y Bot, log la cause exacte (cgltf retourne un enum d'erreur), reporter à FBX2glTF avant de patcher cgltf. |
| **Performance : sample + matrices finales coûteux par frame** | 65 bones × 1 avatar = négligeable. Si on passe à N avatars (B), profile avant d'optimiser. Pas de prematurate optimization dans A. |
| **Le `.glb` Y Bot pèse trop lourd pour git** | Mixamo Y Bot fait ~3-5 MB. Acceptable. Si on ajoute un personnage par race au sous-projet C, on saura à ce moment-là s'il faut LFS. |
| **`FBX2glTF.exe` indisponible / changement d'URL** | Le script `download_fbx2gltf.ps1` pinne une version + SHA256. Si la release disparaît, on fork le binaire (poids 5 MB, on peut le push sur un release du repo LCDLLN si nécessaire). |

---

## 10. Critères d'acceptation (Definition of Done)

Le sous-projet A est livré quand **tous** les critères suivants sont vrais :

1. ✅ `vendor/cgltf/cgltf.h` présent, version pinned dans header.
2. ✅ `tools/asset_pipeline/download_fbx2gltf.ps1` télécharge le binaire avec vérification SHA256, et `fbx_to_gltf.ps1` produit un `.glb` à partir d'un `.fbx` Mixamo de test.
3. ✅ `src/client/render/skinned/` contient les 6 fichiers `.h/.cpp` (Skeleton, SkinnedMesh, AnimationClip, AnimationSampler, SkinnedMeshLoader, SkinnedRenderer) + 2 tests.
4. ✅ `game/data/shaders/skinned.{vert,frag}` compilent en `.spv` via la chaîne de build existante.
5. ✅ `game/data/models/avatars/y_bot/y_bot.glb` commit dans le repo (ou pulled via LFS si décidé).
6. ✅ `Engine.cpp` post-EnterWorld charge Y Bot et appelle `SkinnedRenderer::Draw` à chaque frame. Le cube `avatar_placeholder.mesh` reste comme fallback.
7. ✅ Mode éditeur (`Engine.cpp:3525-3548`) affiche aussi Y Bot animé.
8. ✅ Tests unitaires verts : `SkinnedMeshLoaderTests` + `AnimationSamplerTests`.
9. ✅ Smoke test visuel : screenshot du client in-game montrant Y Bot en pleine animation de marche, joint à la PR.
10. ✅ Aucun warning Vulkan validation layer ajouté par le nouveau pipeline.
11. ✅ CODEBASE_MAP.md mis à jour avec une section "Skinning + animation runtime" listant les nouveaux fichiers + leur rôle.
12. ✅ Mention "Déploiement : ✅ client uniquement" dans la description de la PR.

---

## 11. Suite (post-A)

Une fois A mergé, les sous-projets B et C peuvent démarrer **en parallèle** (ils dépendent tous deux de A mais sont indépendants entre eux) :

- **B** : state machine Idle/Walk/Run, transitions, surface-modulation, jump. Utilise les clips supplémentaires Mixamo (idle, run, jump) chargés dans le même `.glb` ou en `.glb` séparés selon ce qu'on décide à ce moment-là.
- **C** : 6 squelettes raciaux. Décision majeure à brainstormer : retarget des clips Mixamo sur tous les squelettes (un seul jeu d'anims partagé) vs anims dédiées par race ?

Le sous-projet D (preview 3D dans CharacterCreate) dépend de C (variantes raciales visualisables).

Les sous-projets E à K viennent ensuite, dans un ordre à arbitrer selon les priorités produit.
