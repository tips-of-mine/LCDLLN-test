# Sous-projet B.1 — Locomotion state machine (locomotion fluide locale)

**Statut** : design validé par l'utilisateur le 2026-05-18, prêt pour rédaction du plan d'implémentation.

**Périmètre** : `lcdlln_client.exe` (client de jeu) uniquement. `lcdlln_world_editor.exe` peut bénéficier de la state machine mais l'éditeur n'a pas d'input gameplay (la caméra éditeur est libre), donc impact éditeur = nul fonctionnellement.

**Déploiement** : ✅ **Client uniquement, pas de redéploiement serveur.** Aucun changement wire, aucun nouveau handler serveur, aucune migration DB. Tout est local au client. La synchronisation réseau des animations de remote players est explicitement le sous-projet B.3 (séparé, avec redéploiement serveur).

---

## 1. Contexte

Ce spec est le **sous-projet B.1** d'une décomposition supplémentaire du sous-projet B (locomotion state machine complète) en 3 morceaux indépendants :

- **B.1** (ce spec) : locomotion fluide locale — vrai input, blend, Run, Jump, sans surface modulation ni remote players.
- **B.2** : surface modulation (water/sand/snow → walk speed × modifier). Dépend du système `SurfaceQuery` planifié en M100.11 (non encore livré).
- **B.3** : remote players animés — protocol UDP gameplay + GameplayUdpServer + sync animation state. **Redéploiement serveur requis.**

### Ce que B.1 hérite de A

Le sous-projet A (mergé 2026-05-18, PR #645) a livré :
- Runtime skinning + animation glTF/cgltf (`src/client/render/skinned/`).
- 3 clips Y Bot chargés au boot : `Idle` / `StartWalking` / `Walking` (cf. `game/data/models/avatars/y_bot{,_idle,_start_walking}/*.glb`).
- State machine **minimale** 3 états avec **hard cuts** et détection mouvement par delta XZ position frame-to-frame (threshold 1e-4 m). Marqué dans A comme « à durcir en B ».
- Helper `SkinnedMeshLoader::LoadClipsRetargeted(path, targetSkeleton)` pour merger des clips depuis plusieurs fichiers `.glb` par nom de bone (robuste si Mixamo réordonne les joints).
- Orientation 180° appliquée à la model matrix de l'avatar skinné (on voit son dos en 3ᵉ personne).

### Ce que B.1 corrige et apporte

- ❌ Détection mouvement par delta XZ → ✅ Vraie détection input/gameplay via `CharacterController.MoveInput`.
- ❌ Hard cuts entre clips → ✅ Crossfade linéaire de 0.15s entre poses.
- ❌ Pas d'état Run → ✅ Shift en input → état Run distinct avec clip `Fast Run`.
- ❌ Pas de saut → ✅ Espace → Jump → Fall → Land, avec vraie physique (gravité, coyote time, jump buffer) via `CharacterController` existant.
- ❌ `OrbitalCameraController` fait double-emploi caméra + mouvement → ✅ rétrogradé en caméra pure, mouvement délégué à `CharacterController`.
- ❌ bobY synthétique appliqué EN PLUS de l'animation → ✅ supprimé (l'animation Walking porte sa propre oscillation Y).

### Découverte importante issue de l'exploration

`CharacterController` (`src/client/gameplay/CharacterController.h/.cpp`) **existe déjà** dans le repo mais n'est **branché nulle part**. C'est une classe complète et production-ready : Config (walkSpeed, runSpeed, gravity, jumpSpeed, coyoteTimeSec, jumpBufferSec), modes Ground/Air/Water/Fly, capsule sweep collision via interface `IWorldCollider`, walkable slope check + step-up logic. B.1 le branche enfin.

**Manque** : aucune implémentation de `IWorldCollider`. B.1 fournit `TerrainCollider` (heightmap query) comme première implémentation minimale.

---

## 2. State machine — 7 états

```
       (input.moveDirXZ != 0)
Idle ───────────────────────► StartWalking
  ▲                              │
  │ (moveDirXZ=0)                │ (elapsed >= StartWalking.duration)
  │                              ▼
  ├──────────────────────────► Walk ◄────────► Run
  │                              ▲    (Shift)    │
  │                              │ (Shift off)   │
  │                                              │
  │ (input.jumpPressed && IsGrounded)            │
  ├────────────────────────────────────────────► Jump
  │                                                │ (elapsed >= takeoffDuration)
  │                                                ▼
  │                                              Fall
  │                                                │ (IsGrounded)
  │                                                ▼
  │ (Land.clip done)                             Land
  └◄────────────────────────────────────────────  │
```

### Tableau des transitions

| Depuis | Vers | Condition |
|---|---|---|
| Idle | StartWalking | `input.moveDirXZ.LengthSq() > 0` et `IsGrounded` |
| StartWalking | Walk | `stateElapsed >= clip.duration` (StartWalking est une clip one-shot, ~0.5s) |
| StartWalking | Idle | input devient nul avant fin de StartWalking |
| Walk | Run | `input.run == true` (Shift maintenu) |
| Run | Walk | `input.run == false` (Shift relâché) |
| Walk | Idle | `input.moveDirXZ == 0` |
| Run | Idle | `input.moveDirXZ == 0` |
| Walk / Run / Idle / StartWalking | Jump | `input.jumpPressed && cc.IsGrounded` |
| Jump | Fall | `stateElapsed >= takeoffDuration` (~0.4s du début de la clip Jump) OU `!cc.IsGrounded` après takeoff |
| Fall | Land | `cc.IsGrounded` (le perso vient de toucher le sol) |
| Land | Idle / Walk / Run | `stateElapsed >= Land.clip.duration` (~0.3s), choix selon input courant |

### Notes sur les transitions

- **Jump est interruptible par rien** (engagement physique). Une fois Jump déclenché, on attend Fall puis Land.
- **Fall n'a pas de timeout** — dure tant que `!cc.IsGrounded` (le perso peut tomber d'une falaise très longtemps).
- **Land joue toujours sa clip entière** (~0.3s), même si le joueur relâche les inputs. Pas de cancel.
- Pas de transition directe **Run → Jump pour saut en longueur** dans B.1 (gardé pour la version "complète" si demandée plus tard — `Running Jump.fbx` est déjà dans l'inbox).

---

## 3. Mapping clips Mixamo

| État | Source FBX | Type fichier | Mode load | Note |
|---|---|---|---|---|
| Idle | `Standing Idle.fbx` | with-skin (déjà converti par A → `y_bot_idle.glb`) | déjà chargé via `LoadClipsRetargeted` (A polish) | ✅ Réutilisé inchangé |
| StartWalking | `Start Walking.fbx` | with-skin (déjà converti par A → `y_bot_start_walking.glb`) | déjà chargé | ✅ Réutilisé inchangé |
| Walk | `Standard Walk.fbx` | with-skin (déjà converti par A → `y_bot.glb`) | déjà chargé (clip principal de y_bot.glb) | ✅ Réutilisé inchangé |
| **Run** | `Fast Run.fbx` (1.78 MB with-skin) | with-skin | nouveau `.glb` via `fbx_to_gltf.ps1` + load via `LoadClipsRetargeted` | **À ajouter** |
| **Jump** | `Jump.fbx` (2.12 MB with-skin) | with-skin | nouveau `.glb` via `fbx_to_gltf.ps1` + load via `LoadClipsRetargeted` | **À ajouter**. La clip contient takeoff + airtime + landing — on play juste les ~0.4s de takeoff (puis transition Fall). |
| **Fall** | `falling idle.fbx` (349 KB animation-only) | animation-only | nouveau `.glb` via `fbx_to_gltf.ps1` + load via `LoadClipsRetargeted` | **À ajouter**. Pas de mesh dans la source — `LoadClipsRetargeted` retargete les tracks sur le skeleton de y_bot par nom de bone. |
| **Land** | `hard landing.fbx` (456 KB animation-only) | animation-only | nouveau `.glb` via `fbx_to_gltf.ps1` + load via `LoadClipsRetargeted` | **À ajouter**. Idem Fall. |

### Risque animation-only files

`LoadClipsRetargeted` actuel (livré par A) appelle `LoadCpuOnlyForTests` qui bail si `data->skins_count == 0`. Pour les fichiers animation-only, on suppose que Mixamo inclut quand même le skeleton (même sans mesh). Si ce n'est pas le cas après conversion FBX→glTF, deux fallbacks :

1. Re-télécharger `falling idle` et `hard landing` depuis Mixamo avec « With Skin » coché (devient avec mesh, ~2 MB chacun au lieu de ~400 KB).
2. Étendre `LoadClipsRetargeted` pour parser un `.glb` skeleton-only (sans mesh ni skin VkBuffers) — possible mais nécessite refacto de `SkinnedMeshLoader::LoadCpuOnlyForTests` pour gérer le cas `skins_count > 0 && meshes_count == 0`.

À déterminer à l'implémentation (Task 1 du plan).

### Conventions de naming des clips

Mixamo nomme TOUS ses clips `"mixamo.com"`. Au merge dans `m_playerSkinnedMesh->clips`, on renomme :
- Walking → déjà renommé `"Walking"` (A polish)
- Standing Idle → déjà renommé `"Idle"` (A polish)
- Start Walking → déjà renommé `"StartWalking"` (A polish)
- Fast Run → renommer `"Run"`
- Jump → renommer `"Jump"`
- falling idle → renommer `"Fall"`
- hard landing → renommer `"Land"`

---

## 4. Architecture — séparation des responsabilités

### Avant B.1 (état actuel post-A)

```
OrbitalCameraController
├── Lit input WASD/Shift/Space
├── Calcule m_target (déplacement XZ)
├── Calcule m_walkBobPhase (oscillation Y synthétique)
├── Calcule m_distance (zoom molette)
├── Calcule yaw / pitch (rotation clic droit)
└── Expose camera.position + camera.yaw/pitch

Engine
├── Appelle OrbitalCameraController::Update
├── Lit rs.objectModelMatrix (= T(target) * R_y(yaw))
├── State machine A simpliste (delta XZ → Idle/StartWalking/Walking)
└── Draw skinned avatar avec model matrix
```

### Après B.1

```
CharacterController                  OrbitalCameraController (rétrogradé)
├── Reçoit MoveInput par frame      ├── Reçoit target par frame
├── Applique gravity / jump         ├── Gère uniquement yaw/pitch (souris)
├── Sweep capsule contre collider   ├── Gère uniquement zoom (molette)
├── Expose position, velocity,      ├── Calcule camera.position = target + offset_orbital
│   IsGrounded, mode (Ground/       └── Expose camera.position + camera.yaw/pitch
│   Air/Water)                            (PAS de m_walkBobPhase, PAS de m_target movement)
└── Tout est driven par MoveInput

Engine
├── Build MoveInput depuis input clavier (WASD/Shift/Space)
├── cc.Update(dt, input, &m_terrainCollider)
├── cameraController.SetTarget(cc.GetPosition())
├── cameraController.Update(dt) — gère yaw/pitch/zoom uniquement
├── State machine B.1 driven par cc.IsGrounded + cc.GetVelocity + input
└── Draw skinned avatar avec model matrix dérivée de cc.GetPosition + yaw_pivot
```

### Changements concrets dans le code existant

| Fichier | Modification |
|---|---|
| `src/client/render/Camera.h` | Supprimer membres `m_locomotion`, `m_walkBobPhase`, `m_verticalVelocityY`, `m_verticalOffsetY`, `m_isCrouching`. Supprimer les enums liées au mouvement. |
| `src/client/render/Camera.cpp::OrbitalCameraController::Update` | Plus de lecture input WASD/Shift/Space. Plus de calcul de m_target. Plus de walkBob. Garde uniquement : lecture souris (yaw/pitch clic droit), lecture molette (zoom), update camera.position depuis m_target (passé en param). |
| `src/client/render/Camera.cpp::OrbitalCameraController` | Nouveau setter `SetTarget(const Vec3& worldPos)` appelé par `Engine` chaque frame. |
| `src/client/gameplay/CharacterController.h/.cpp` | **Aucune modification du code existant.** B.1 l'utilise tel quel. |
| `src/client/gameplay/TerrainCollider.h/.cpp` | **Nouveau** — implémente `IWorldCollider`. |
| `src/client/app/Engine.h` | Nouveau membre `m_characterController`. Nouveau membre `m_terrainCollider`. State machine étendue à 7 états (extend enum `AvatarLocomotionState`). |
| `src/client/app/Engine.cpp` | Refacto : build MoveInput → cc.Update → camera.SetTarget. Refacto state machine pour utiliser cc.IsGrounded + cc.GetVelocity + input. |

---

## 5. `IWorldCollider` — implémentation `TerrainCollider`

`CharacterController` demande un `IWorldCollider` pour ses sweep tests. L'interface (déjà définie dans `CharacterController.h`) expose typiquement :

```cpp
class IWorldCollider {
public:
    virtual ~IWorldCollider() = default;
    // Renvoie l'altitude au sol (Y) au point (worldX, worldZ).
    virtual float GroundHeightAt(float worldX, float worldZ) const = 0;
    // (autres méthodes selon CharacterController.h existant — à vérifier à l'impl)
};
```

(L'interface exacte est à confirmer en lisant `CharacterController.h` lors du writing-plans — peut inclure sweep tests plus complexes.)

### `TerrainCollider` — implémentation B.1

```cpp
// src/client/gameplay/TerrainCollider.h
class TerrainCollider : public engine::gameplay::IWorldCollider
{
public:
    // Bind sur le heightmap déjà chargé par TerrainRenderer.
    void BindTerrain(const engine::render::TerrainRenderer* terrainRenderer);

    // Renvoie Y au sol via interpolation bilinéaire du heightmap.
    // Fallback Y=0 si terrainRenderer nul ou (X, Z) hors map.
    float GroundHeightAt(float worldX, float worldZ) const override;
};
```

Le heightmap est déjà chargé en CPU au boot par `HeightmapLoader::Loaded OK (64x64, 4096 pixels)` (cf. logs run). On a juste à exposer un accesseur read-only sur `TerrainRenderer` qui renvoie le buffer + worldSize + heightScale, puis l'interpolation bilinéaire est triviale.

### Hors scope B.1 (collision)

- Collision contre props / bâtiments / arbres (pas encore présents dans le monde de jeu)
- Murs invisibles / volumes de zone
- Capsule sweep contre meshes arbitraires

Ces fonctionnalités attendent que l'engine ait un système d'objets dans le monde. Pour B.1 le terrain seul suffit (le perso ne peut tomber que via Fall, atterrir sur le terrain, et ne traverse rien parce qu'il n'y a rien à traverser).

---

## 6. Crossfade entre clips

### Principe

Au moment d'une transition d'état (ex. Idle → Walk), on ne jette pas l'ancienne pose. On garde 2 clips actives en parallèle pendant `kCrossfadeDuration = 0.15s` :
- Old clip : continue à avancer dans son temps mais figée visuellement (poids décroissant)
- New clip : démarre à t=0, poids croissant

Pendant la transition : `final_pose = lerp(old_pose, new_pose, alpha)` où `alpha = clamp(crossfadeElapsed / kCrossfadeDuration, 0, 1)`.

Après `kCrossfadeDuration` : on jette l'ancienne clip, seule la nouvelle reste.

### Implémentation

Nouveau type dans `src/client/render/skinned/` :

```cpp
struct ActiveAnimation {
    const AnimationClip* clip;
    float timeInClip;    // wraps via fmod (loop) ou clamp (one-shot)
    bool loops;          // true pour Idle/Walk/Run/Fall, false pour StartWalking/Jump/Land
};

class AnimationCrossfade {
public:
    void Play(const AnimationClip& newClip, bool loops, float now);
    // Sample : pour CHAQUE bone, renvoie le local transform interpolé entre
    // les 2 clips actives (selon alpha de crossfade). Si une seule active,
    // renvoie sa pose directement.
    std::vector<engine::math::Mat4> Sample(const Skeleton& skel, float now) const;

private:
    static constexpr float kCrossfadeDuration = 0.15f;
    ActiveAnimation m_current;
    std::optional<ActiveAnimation> m_previous;  // valide pendant le crossfade
    float m_crossfadeStartTime = 0.0f;
};
```

Le lerp se fait au niveau des **poses locales** (translation/rotation/scale par bone), PAS au niveau des matrices finales (qui ne se lerpent pas correctement car non commutatives avec rotation). Translation et scale = lerp linéaire, rotation = slerp quaternion (qu'on a déjà depuis A — `engine::math::Quat::Slerp`).

Conséquence : il faut décomposer le résultat de `AnimationSampler::SamplePose` en TRS par bone, lerp, puis recomposer. Le helper `ComposeTRS` privé dans `AnimationSampler.cpp` **doit être exposé** (passe d'anonyme → static membre de `AnimationSampler`) pour que `AnimationCrossfade` le réutilise sans dupliquer la math.

### Engine.cpp utilisation

```cpp
// Au changement d'état :
m_avatarCrossfade.Play(*newClip, /*loops=*/ stateLoops, now);

// À chaque frame :
auto locals = m_avatarCrossfade.Sample(m_playerSkinnedMesh->skeleton, now);
auto globals = AnimationSampler::ComputeGlobalMatrices(skel, locals);
auto finals = AnimationSampler::ComputeFinalMatrices(skel, globals);
m_skinnedRenderer.Record(..., finals, ...);
```

L'API `AnimationSampler::SamplePose` reste publique pour les tests unitaires (pas besoin de l'enlever).

---

## 7. Input → MoveInput

### Build par frame

Nouveau helper privé dans `Engine.cpp` (ou dans une nouvelle classe `PlayerInputController` selon la taille — à arbitrer au plan) :

```cpp
engine::gameplay::CharacterController::MoveInput BuildPlayerMoveInput(
    const engine::platform::Input& input,
    const engine::render::OrbitalCameraController& camera,
    float dt)
{
    engine::gameplay::CharacterController::MoveInput moveInput{};

    // Direction relative à la caméra (Forward = vers la direction de regard, Right = perpendiculaire).
    const Vec3 forward = camera.GetForwardXZ();  // normalisé, Y=0
    const Vec3 right   = camera.GetRightXZ();    // normalisé, Y=0

    Vec3 dir{0, 0, 0};
    if (input.IsDown(Key::W) || input.IsDown(Key::Z)) dir += forward;
    if (input.IsDown(Key::S))                          dir -= forward;
    if (input.IsDown(Key::D))                          dir += right;
    if (input.IsDown(Key::A) || input.IsDown(Key::Q)) dir -= right;

    const float lenSq = dir.LengthSq();
    if (lenSq > 0.0f) {
        const float invLen = 1.0f / std::sqrt(lenSq);
        moveInput.moveDirXZ = Vec3{dir.x * invLen, 0, dir.z * invLen};
    }

    moveInput.run          = input.IsDown(Key::LeftShift);
    moveInput.jumpPressed  = input.WasPressedThisFrame(Key::Space);
    moveInput.swimUp       = false;  // hors B.1
    moveInput.swimDown     = false;
    moveInput.flyPressed   = false;  // cheat mode futur

    return moveInput;
}
```

### Orientation du modèle

Le yaw du modèle suit `moveDirXZ` (pas la caméra) :

```cpp
if (moveInput.moveDirXZ.LengthSq() > 0.0f) {
    // Snap immédiat pour B.1 (rotation lissée = polish ultérieur).
    m_avatarYaw = std::atan2(moveInput.moveDirXZ.x, moveInput.moveDirXZ.z);
}
// Sinon : garde le yaw précédent (perso reste orienté dans la dernière direction de mouvement).
```

Conséquence visible :
- Tu appuies W → le perso pivote pour faire face à la direction caméra (avance « tout droit »).
- Tu appuies D → le perso pivote pour faire face à la droite caméra et marche dans cette direction.
- Pas de strafe distinct (le perso pivote toujours pour aller vers la direction).
- Tu appuies S → le perso pivote 180° pour faire face à la direction opposée à la caméra, et marche dans cette direction (donc on voit son visage de face, vu de la caméra qui est derrière sa position de départ).

C'est la convention « free-look 3ᵉ personne classique » (ARPGs Diablo-like). Une convention plus stricte « caméra-relative » avec strafe distinct est possible mais demande l'option Strafe/Backward = sous-projet B futur (cf. design « riche » 9 états dans le brainstorm).

---

## 8. Layout fichiers / dossiers

### Nouveaux fichiers

| Chemin | Rôle | Taille estimée |
|---|---|---|
| `src/client/gameplay/TerrainCollider.h` | Interface `IWorldCollider` impl pour terrain heightmap | ~30 lignes |
| `src/client/gameplay/TerrainCollider.cpp` | Bilinear interpolation du heightmap | ~80 lignes |
| `src/client/render/skinned/AnimationCrossfade.h` | API Play + Sample crossfade | ~50 lignes |
| `src/client/render/skinned/AnimationCrossfade.cpp` | Lerp/slerp TRS entre 2 poses | ~120 lignes |
| `src/client/render/skinned/tests/AnimationCrossfadeTests.cpp` | Tests : alpha=0 → old, alpha=1 → new, alpha=0.5 → midway | ~80 lignes |
| `src/client/gameplay/tests/TerrainColliderTests.cpp` | Tests : sample center, edge, hors-map (fallback Y=0) | ~80 lignes |
| `game/data/models/avatars/y_bot_run/y_bot_run.glb` | Mixamo Y Bot + clip Fast Run (~2 MB) | binaire |
| `game/data/models/avatars/y_bot_jump/y_bot_jump.glb` | Mixamo Y Bot + clip Jump (~2 MB) | binaire |
| `game/data/models/avatars/y_bot_fall/y_bot_fall.glb` | Mixamo skeleton + clip falling idle (~500 KB si animation-only fonctionne, sinon ~2 MB avec re-download with-skin) | binaire |
| `game/data/models/avatars/y_bot_land/y_bot_land.glb` | Mixamo skeleton + clip hard landing (~500 KB si OK) | binaire |

### Fichiers modifiés

| Chemin | Modification |
|---|---|
| `src/client/render/Camera.h` | Retirer membres locomotion (m_locomotion, m_walkBobPhase, m_verticalVelocityY, m_verticalOffsetY, m_isCrouching). Ajouter setter `SetTarget(Vec3)`. Ajouter getters `GetForwardXZ()`, `GetRightXZ()`. |
| `src/client/render/Camera.cpp` | Refacto `OrbitalCameraController::Update` : plus de lecture input movement, plus de calcul m_target, plus de walkBob. Garde uniquement souris (yaw/pitch) + molette (zoom). |
| `src/client/app/Engine.h` | Étendre enum `AvatarLocomotionState { Idle, StartWalking, Walk, Run, Jump, Fall, Land }`. Nouveaux membres `m_characterController`, `m_terrainCollider`, `m_avatarCrossfade`, `m_avatarYaw`. |
| `src/client/app/Engine.cpp` | Init au boot : instancier TerrainCollider + CharacterController. Per frame : BuildPlayerMoveInput → cc.Update → cameraController.SetTarget → state machine étendue → m_avatarCrossfade.Sample → Record. Supprimer le bobY synthétique. |
| `src/CMakeLists.txt` | Lister TerrainCollider.cpp + AnimationCrossfade.cpp + les 2 tests via `lcdlln_add_simple_test`. |
| `CMakeLists.txt` (root) | Lister TerrainCollider.cpp + AnimationCrossfade.cpp dans engine_core sources. |
| `CODEBASE_MAP.md` §14.5 | Mettre à jour : state machine étendue à 7 états avec crossfade, suppression bobY synthétique, intégration CharacterController + TerrainCollider, mapping clips étendu. |

---

## 9. Intégration `Engine.cpp` — points clés

### Au boot (extension du bloc Task 15 actuel)

Après le chargement réussi de y_bot.glb + idle + start_walking (A) :

```cpp
// B.1 : charger les 4 nouveaux clips Run/Jump/Fall/Land via LoadClipsRetargeted.
auto loadClipInto = [&](const char* glbPath, const char* renamedTo) {
    auto clips = SkinnedMeshLoader::LoadClipsRetargeted(glbPath, m_playerSkinnedMesh->skeleton);
    for (auto& c : clips) {
        if (c.duration > 0.0f && c.name == "mixamo.com") {
            c.name = renamedTo;
            m_playerSkinnedMesh->clips.push_back(std::move(c));
            return true;
        }
    }
    return false;
};
const std::string runPath  = contentRoot + "/models/avatars/y_bot_run/y_bot_run.glb";
const std::string jumpPath = contentRoot + "/models/avatars/y_bot_jump/y_bot_jump.glb";
const std::string fallPath = contentRoot + "/models/avatars/y_bot_fall/y_bot_fall.glb";
const std::string landPath = contentRoot + "/models/avatars/y_bot_land/y_bot_land.glb";
if (!loadClipInto(runPath.c_str(),  "Run"))  LOG_WARN(Render, "[Engine] Run clip not loaded");
if (!loadClipInto(jumpPath.c_str(), "Jump")) LOG_WARN(Render, "[Engine] Jump clip not loaded");
if (!loadClipInto(fallPath.c_str(), "Fall")) LOG_WARN(Render, "[Engine] Fall clip not loaded");
if (!loadClipInto(landPath.c_str(), "Land")) LOG_WARN(Render, "[Engine] Land clip not loaded");

// B.1 : init TerrainCollider + CharacterController.
m_terrainCollider.BindTerrain(&m_terrainRenderer);
m_characterController.SetConfig({
    .walkSpeed    = m_cfg.GetFloat("player.movement.walk_speed",     5.0f),
    .runSpeed     = m_cfg.GetFloat("player.movement.run_speed",      9.0f),
    .gravity      = m_cfg.GetFloat("player.movement.gravity",      -20.0f),
    .jumpSpeed    = m_cfg.GetFloat("player.movement.jump_speed",     9.0f),
    .coyoteTimeSec= m_cfg.GetFloat("player.movement.coyote_time_s",  0.1f),
    .jumpBufferSec= m_cfg.GetFloat("player.movement.jump_buffer_s",  0.1f),
});
m_characterController.SetPosition(Vec3{0, 0, 0});  // ou spawn point depuis save
```

### Per frame (refacto du bloc skinned avatar)

```cpp
// 1. Build input
auto moveInput = BuildPlayerMoveInput(m_input, m_orbitalCamera, dt);

// 2. Update physics
m_characterController.Update(dt, moveInput, &m_terrainCollider);
const Vec3 position = m_characterController.GetPosition();
const Vec3 velocity = m_characterController.GetVelocity();
const bool grounded = m_characterController.IsGrounded();

// 3. Update camera (suit le perso)
m_orbitalCamera.SetTarget(position);
m_orbitalCamera.Update(dt);  // gère yaw/pitch/zoom seulement

// 4. Update yaw model
if (moveInput.moveDirXZ.LengthSq() > 0.0f) {
    m_avatarYaw = std::atan2(moveInput.moveDirXZ.x, moveInput.moveDirXZ.z);
}

// 5. State machine (voir §2 tableau de transitions)
UpdateAvatarLocomotionState(moveInput, grounded, now);

// 6. Animation crossfade : si on vient de changer d'état, déclenche Play.
if (stateChangedThisFrame) {
    const AnimationClip* clip = m_playerSkinnedMesh->FindClip(StateToClipName(m_avatarLocoState));
    if (clip) {
        const bool loops = (m_avatarLocoState == Idle || m_avatarLocoState == Walk
                         || m_avatarLocoState == Run  || m_avatarLocoState == Fall);
        m_avatarCrossfade.Play(*clip, loops, now);
    }
}

// 7. Sample + skinning + draw
auto locals  = m_avatarCrossfade.Sample(m_playerSkinnedMesh->skeleton, now);
auto globals = AnimationSampler::ComputeGlobalMatrices(skel, locals);
auto finals  = AnimationSampler::ComputeFinalMatrices(skel, globals);

// 8. Build model matrix : T(position) * R_y(m_avatarYaw + π pour orientation 180°)
Mat4 modelMat = Mat4::Translate(position) * Mat4::RotateY(m_avatarYaw + 3.14159f);
m_skinnedRenderer.Record(..., finals, ..., modelMat.m, ...);
```

---

## 10. Configuration `config.json`

Nouvelle section `player.movement` à ajouter (avec defaults sensibles pour fallback) :

```json
"player": {
    "movement": {
        "walk_speed": 5.0,
        "run_speed": 9.0,
        "gravity": -20.0,
        "jump_speed": 9.0,
        "coyote_time_s": 0.1,
        "jump_buffer_s": 0.1
    }
}
```

Pas requis pour le bon fonctionnement (defaults dans `CharacterController::Config` font le boulot), mais documenté pour permettre tuning sans recompiler.

---

## 11. Stratégie de tests

### Tests unitaires (CI Linux ctest)

- **`AnimationCrossfadeTests`** :
  - À alpha=0 : la pose sortie == pose old.
  - À alpha=1 : la pose sortie == pose new.
  - À alpha=0.5 : translation = lerp linéaire, rotation = slerp midway.
  - Property : crossfade jamais > kCrossfadeDuration en temps réel.

- **`TerrainColliderTests`** :
  - Heightmap synthétique 4×4 (1m par cell), valeurs connues aux 4 coins.
  - `GroundHeightAt(0,0)` = valeur centre interpolée.
  - `GroundHeightAt(-100, -100)` (hors map) = fallback Y=0.
  - Property : interpolation bilinéaire continue (échantillonner 100 points le long d'une ligne → pas de saut).

### Smoke test visuel (manuel)

- Lancer `lcdlln.exe`, EnterWorld.
- **Critère 1 (locomotion)** : presser W/A/S/D → perso se déplace dans la bonne direction, joue Walk en boucle.
- **Critère 2 (Run)** : maintenir Shift → perso accélère, joue Run en boucle. Relâcher Shift → retour Walk avec crossfade visible.
- **Critère 3 (Jump)** : presser Espace → perso saute (Jump → airtime → Land). Pas de figure freeze. Atterrit au bon Y.
- **Critère 4 (Idle)** : relâcher tous les inputs → perso passe Idle, joue Idle en boucle, oscille subtilement.
- **Critère 5 (orientation)** : presser D → perso pivote pour faire face à droite caméra, marche.
- **Critère 6 (terrain following)** : se déplacer sur le terrain → le perso suit le relief (monte les pentes, ne flotte pas).
- **Critère 7 (no regression)** : terrain toujours visible, pas de validation Vulkan warning, FPS stable.

---

## 12. Périmètre explicitement HORS B.1

| Exclu | Renvoyé vers |
|---|---|
| Modulation vitesse selon surface (water/sand/snow) | **B.2** (dépend SurfaceQuery M100.11) |
| Animation des remote players + sync réseau | **B.3** (UDP gameplay, server redeploy) |
| Strafe Left/Right anims distincts | B futur (option "riche" 9 états) |
| Walk Backward anim distinct | B futur |
| Crouch / Sneak | B futur ou C+ |
| Turn-in-place anims (left turn / right turn) | B futur (option "complet" 12+ états) |
| Saut en longueur distinct du saut en hauteur (Running Jump) | B futur ou option "complet" |
| Collision contre props / bâtiments / arbres | futur (dépend du système d'objets dans le monde) |
| Lissé de la rotation du yaw modèle (rotation snap actuellement) | polish futur |
| Animation des actions sociales (saluer, s'asseoir, mourir, etc.) | **E** |
| Combat / casting / armes en main | **F** |
| Nage en eau profonde (le perso passe en mode Water dans CharacterController mais on n'a pas d'anim swim) | B.2 ou ultérieur |
| Variantes raciales (Y Bot pour tous) | **C** |
| Multi-avatar sur l'écran (mob, PNJ, autres joueurs) | B.3 (joueurs) ou I (animaux) ou ultérieur (mob/PNJ) |

---

## 13. Risques identifiés et mitigations

| Risque | Mitigation |
|---|---|
| `LoadClipsRetargeted` rejette les fichiers animation-only (falling idle, hard landing) car `skins_count == 0` | Test au début de l'impl. Si rejet : re-télécharger en with-skin (ajoute ~3 MB au repo total). Si accepté : impact zéro. Décision finale au moment de Task 1. |
| `CharacterController::Update` API signature differs from what I assume (MoveInput fields, return values) | Lecture détaillée de `CharacterController.h/.cpp` lors de writing-plans Task 0. Adapter le glue code. |
| `IWorldCollider` interface plus complexe que juste `GroundHeightAt` | Idem : lecture détaillée. Si l'interface demande sweep tests, implémenter une version dégénérée (sweep = juste GroundHeightAt à la fin du sweep). |
| Le bobY synthétique supprimé révèle que l'animation Walk n'a pas assez d'oscillation Y → perso glisse au lieu de marcher | Vérifier visuellement au smoke test. Si peu d'oscillation Y dans la clip, garder un MINI bobY (1-2 cm) ou trouver une autre clip Walk avec plus de bounce. |
| FIF race sur bone SSBO (concern A non résolu) | Out of scope B.1 — toujours 1 seul avatar. Documenter à nouveau dans CODEBASE_MAP. Fix devra venir avec B.3 (multi-avatar). |
| Crossfade de 0.15s trop long ou trop court visuellement | Paramétrable via const dans AnimationCrossfade.cpp. Si pénible visuellement, ajuster (0.1s à 0.25s typique). |
| Performance : 2 clips actives + 7 états + 65 bones × 60fps = peut-être négligeable, peut-être pas selon CPU | Profiler après smoke test. Si bottleneck, cache les results de SamplePose dans le crossfade (1 sample par clip par frame, pas 2). |
| Re-conversion FBX → glTF des 4 nouveaux clips peut produire des animations mal alignées (Mixamo bug occasionnel sur Take 001 vide) | Le clip de duration > 0 est filtré dans LoadClipsRetargeted déjà. Si problème, fbx_to_gltf.ps1 a déjà un flag pour ignorer animations vides. |

---

## 14. Critères d'acceptation (Definition of Done)

Le sous-projet B.1 est livré quand **tous** les critères suivants sont vrais :

1. ✅ 4 nouveaux fichiers `.glb` committed : y_bot_run, y_bot_jump, y_bot_fall, y_bot_land.
2. ✅ `src/client/gameplay/TerrainCollider.{h,cpp}` créés + lié à `engine_core`.
3. ✅ `src/client/render/skinned/AnimationCrossfade.{h,cpp}` créés + lié à `engine_core`.
4. ✅ Tests CI Linux verts : `terrain_collider_tests` + `animation_crossfade_tests` enregistrés via `lcdlln_add_simple_test`.
5. ✅ `OrbitalCameraController` refactoré : plus de `m_locomotion`/`m_walkBobPhase`/`m_verticalVelocityY` etc. Build OK.
6. ✅ `Engine.cpp` : `CharacterController` instancié + branché. State machine étendue à 7 états. Crossfade utilisé. bobY synthétique supprimé.
7. ✅ `config.json` : section `player.movement` ajoutée (defaults documentés).
8. ✅ Smoke test visuel : tous les 7 critères §11 validés. Screenshot/vidéo dans la PR.
9. ✅ Aucun warning Vulkan validation layer nouveau.
10. ✅ CODEBASE_MAP §14.5 mis à jour (state machine étendue, suppression bobY, intégration CharacterController/TerrainCollider).
11. ✅ Mention "Déploiement : ✅ client uniquement" dans la description de la PR.

---

## 15. Suite (post-B.1)

- **B.2** (surface modulation) — débloque dès que M100.11 (SurfaceQuery) est livré, OU dès qu'on décide de faire un MVP local de SurfaceQuery dans B.2.
- **B.3** (remote players animés) — indépendant, peut démarrer dès que B.1 est mergé. Plus gros chantier (server redeploy required).
- **C** (variantes raciales) — débloqué par A et indépendant de B. Peut être traité en parallèle de B.2/B.3.
- **D** (preview 3D dans CharacterCreate) — dépend de C.
- **E à K** — selon priorités produit.
