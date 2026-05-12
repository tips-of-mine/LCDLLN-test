# Design — M100.16 Hazard Volume System

**Date** : 2026-05-12
**Ticket** : [tickets/M100/M100.16-HazardVolumeSystem.md](../../../tickets/M100/M100.16-HazardVolumeSystem.md)
**Statut** : Design validé, en attente d'exécution
**Approche retenue** : Approche B — complète (4 types × 4 escape modes, callbacks pour découplage inventaire/animation)
**Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur

---

## 1. Contexte et choix de l'Approche B

Le ticket M100.16 introduit des volumes 3D (boîtes ou cylindres) qui appliquent un effet d'enfoncement progressif (Quicksand, Bog, Tar) ou tuent par contact (LavaSurface). La spec ticket énumère 4 modes d'évasion : `None`, `MashButton`, `LateralMove`, `MashButton+RequireItem`. La simulation est **strictement côté client** ; le serveur n'arbitre rien.

**Audit code-direct** confirme qu'aucune infrastructure n'existe :
- `src/client/world/hazard/` inexistant
- `src/world_editor/hazard/` inexistant (mais pattern clair via `terrain/`, `water/`, `splat/`)
- `CharacterController::Die`, `m_sinking`, hooks `OnHazardEnter/Update/Exit` absents
- `ThirdPersonCamera::SetGroundOffset` absent
- `StreamCache::LoadHazards` absent (pattern `LoadTerrainChunk` / `LoadSplatMap` à imiter)
- Système d'inventaire runtime absent (UI existe : `src/client/inventory/InventoryUi.h`)
- Système d'animation runtime absent

**Décision Approche B** : tous les couplages externes (inventaire, animation, audio, mort) passent par **callbacks injectés** au `HazardSimulator`. Même pattern éprouvé sur M100.15 (`onEnterWater/onExitWater`). Cela permet :
- Tests headless complets (callbacks mockés)
- Stubs côté Engine pour M100.16 (animation, inventaire), remplaçables quand les systèmes runtime arriveront
- Découplage strict : `HazardSimulator` et `CharacterController` n'incluent aucun header inventaire / animation / audio

## 2. Architecture

```
┌──────────────────────────────────┐
│ HazardVolumes (data + I/O)       │  format hazards.bin
│ src/client/world/hazard/         │  ─ HazardType, HazardShape, HazardInstance
└─────────────┬────────────────────┘  ─ Save/LoadHazardsBin (round-trip)
              │ ref const
              ▼
┌──────────────────────────────────┐
│ HazardSimulator                  │  state machine joueur ↔ hazard
│ src/client/world/hazard/         │  ─ Update(dt, playerPos, input)
│                                  │  ─ détection entrée (point-in-volume)
│                                  │  ─ progression sinking, slowdown
│                                  │  ─ escape modes (Mash, Lateral, Item)
│                                  │  ─ death timer LavaSurface
│                                  │  callbacks :
│                                  │    onSink(dy, slowdown) → CC applique
│                                  │    onEnter()/onExit()  → camera + anim
│                                  │    hasItem(itemId)     → inventaire
│                                  │    die(reason)         → mort scriptée
└─────────────┬────────────────────┘
              │ via Engine wiring
              ▼
┌─────────────────────┐  ┌─────────────────────┐
│ CharacterController │  │ ThirdPersonCamera   │
│ (modifié)           │  │ (modifié)           │
│ - m_sinking         │  │ - m_groundOffsetY   │
│ - ApplyHazardEffect │  │ - SetGroundOffset() │
└─────────────────────┘  └─────────────────────┘

──────────── éditeur ────────────

┌──────────────────────────────────┐
│ HazardDocument                   │  état monde côté éditeur
│ src/world_editor/hazard/         │  ─ AddHazard, RemoveHazard, snapshots
└─────────────┬────────────────────┘
              │
              ▼
┌──────────────────────────────────┐
│ HazardTool                       │  outil ImGui : type/shape/sliders
│ src/world_editor/hazard/         │  ─ raycast click → spawn
└──────────────────────────────────┘
```

## 3. Fichiers livrés

### 3.1 Créés

| Chemin | Rôle |
|---|---|
| `src/client/world/hazard/HazardVolumes.h` | Enums + struct `HazardInstance` + struct `HazardScene` + API serialize |
| `src/client/world/hazard/HazardVolumes.cpp` | Implémentation `SaveHazardsBin` / `LoadHazardsBin` (round-trip) |
| `src/client/world/hazard/HazardSimulator.h` | Classe `HazardSimulator` (state machine + callbacks) |
| `src/client/world/hazard/HazardSimulator.cpp` | Implémentation : détection, sinking, escape, lava timer |
| `src/client/world/hazard/tests/HazardVolumesTests.cpp` | 1 test round-trip binaire |
| `src/client/world/hazard/tests/HazardSimulatorTests.cpp` | 5 tests state machine (sink rate, mash, lateral, lava 3s, death on max depth) |
| `src/world_editor/hazard/HazardDocument.h` | État monde éditeur (liste hazards, undo/redo) |
| `src/world_editor/hazard/HazardDocument.cpp` | Implémentation, Save/Load via HazardVolumes |
| `src/world_editor/hazard/HazardTool.h` | Outil ImGui (panneau Tool Properties) |
| `src/world_editor/hazard/HazardTool.cpp` | Implémentation : raycast + spawn + sliders |

### 3.2 Modifiés

| Chemin | Modification |
|---|---|
| `src/client/gameplay/CharacterController.h` | Ajout setter `SetHazardEffect(sinkRateMps, slowdownMul)` + getter `IsSinking()` + membre `m_hazardSink{0,1}` (sink rate + slowdown actifs) |
| `src/client/gameplay/CharacterController.cpp` | Application de l'effet : remplace `vel.y` par `-sinkRateMps` quand sinking ; multiplie horizontalVel par `slowdownMul` |
| `src/client/render/Camera.h` | Ajout setter `SetGroundOffset(float)` sur `OrbitalCameraController` (= ThirdPersonCamera dans M100.15 wording) |
| `src/client/render/Camera.cpp` | Application offset au calcul de position |
| `src/client/world/StreamCache.h` | Déclaration `LoadHazards(zone) → std::shared_ptr<HazardScene>` |
| `src/client/world/StreamCache.cpp` | Implémentation lecture `instances/hazards.bin` |
| `tools/zone_builder/lib/Public/zone_builder/ChunkPackageWriter.h` | Déclaration `WriteHazards(scene)` |
| `tools/zone_builder/lib/ChunkPackageWriter.cpp` | Implémentation writer (delegate à `SaveHazardsBin`) |
| `CMakeLists.txt` | Ajout 4 sources hazard à `engine_core` + 2 sources `world_editor_lib` + 2 exécutables CTest |

**Note importante** : la spec ticket nomme `ThirdPersonCamera`, mais le code actuel ([Camera.h:233-234](../../../src/client/render/Camera.h:233)) expose `OrbitalCameraController` (post-EnterWorld). C'est le même contrat fonctionnel. Le `SetGroundOffset` sera ajouté à `OrbitalCameraController`. La spec mentionne `ThirdPersonCamera.cpp` qui n'existe pas en l'état du code (juste un fichier `ThirdPersonCamera.h/.cpp` ayant un rôle différent — à vérifier au moment de l'implémentation, et adapter).

## 4. Signatures

### 4.1 `HazardVolumes.h` (nouveau)

```cpp
namespace engine::world::hazard
{
    constexpr uint32_t kHazardsMagic   = 0x5A415748u;  // "HAZA"
    constexpr uint32_t kHazardsVersion = 1u;

    enum class HazardType : uint32_t {
        Quicksand = 0,
        Bog = 1,
        Tar = 2,
        LavaSurface = 3
    };

    enum class HazardShape : uint32_t {
        Box = 0,
        Cylinder = 1
    };

    enum class EscapeMode : uint32_t {
        None = 0,             // mort scriptée si maxDepth atteinte
        MashButton = 1,       // 10 appuis "Action" en 5 s
        LateralMove = 2,      // 2 m de déplacement horizontal cumulé
        MashButtonItem = 3    // MashButton + item requis dans l'inventaire
    };

    struct HazardInstance
    {
        HazardType type = HazardType::Quicksand;
        HazardShape shape = HazardShape::Cylinder;
        engine::math::Vec3 position{0, 0, 0};
        engine::math::Vec3 boxHalfExtents{2, 1, 2};  // utilisé si shape==Box
        float cylRadius = 4.0f;                       // utilisé si shape==Cylinder
        float cylHeight = 2.0f;                       // utilisé si shape==Cylinder
        float sinkRateMps = 0.15f;
        float maxDepthMeters = 1.8f;
        float slowdownMul = 0.10f;
        EscapeMode escapeMode = EscapeMode::MashButton;
        uint32_t requiredItemId = 0;                  // 0 si EscapeMode ≠ MashButtonItem
    };

    struct HazardScene
    {
        std::vector<HazardInstance> hazards;
    };

    /// Sérialise au format `instances/hazards.bin`. Header OutputVersionHeader
    /// (magic=kHazardsMagic, version=1, contentHash=xxhash64).
    bool SaveHazardsBin(const HazardScene& scene,
        std::vector<uint8_t>& outBytes, std::string& outError);

    /// Désérialise. Valide magic, version, contentHash. Reset outScene.
    bool LoadHazardsBin(std::span<const uint8_t> bytes,
        HazardScene& outScene, std::string& outError);

    /// Test point-in-volume 3D (Box ou Cylinder).
    bool PointInHazard(const HazardInstance& hz, engine::math::Vec3 worldPos) noexcept;
}
```

### 4.2 `HazardSimulator.h` (nouveau)

```cpp
namespace engine::world::hazard
{
    /// Callbacks injectés par l'Engine pour découpler le simulator des systèmes
    /// inventaire, animation, audio et mort scriptée.
    struct HazardCallbacks
    {
        std::function<bool(uint32_t itemId)> hasItem;       // inventaire
        std::function<void()> onEnter;                       // anim + audio enter
        std::function<void()> onExit;                        // anim + audio exit
        std::function<void(std::string_view reason)> die;    // mort scriptée
    };

    /// État courant du simulator (read-only, pour debug HUD ou tests).
    struct HazardState
    {
        bool inHazard = false;
        const HazardInstance* activeHazard = nullptr;
        float currentDepth = 0.0f;     // mètres enfoncés
        float lateralTraveled = 0.0f;  // mètres horizontal cumulés (LateralMove)
        int mashCount = 0;             // appuis "Action" récents
        float mashWindowSec = 0.0f;    // fenêtre temps écoulée pour mash
        float lavaTimer = 0.0f;        // secondes dans LavaSurface
    };

    /// Effet à appliquer au CharacterController chaque frame quand sinking.
    /// `applySinkRate=true` → CC force `vel.y = -sinkRateMps`. `slowdownMul`
    /// multiplie la vitesse horizontale.
    struct HazardEffect
    {
        bool applySinkRate = false;
        float sinkRateMps = 0.0f;
        float slowdownMul = 1.0f;
    };

    class HazardSimulator
    {
    public:
        void Init(const HazardScene& scene, const HazardCallbacks& cb) noexcept;

        /// Avance la simulation d'une frame. `playerPos` = pieds joueur monde.
        /// `actionPressed` = front montant du bouton Action (true exactement
        /// le frame où le joueur appuie). Retourne l'effet à appliquer au CC.
        HazardEffect Update(float dt, engine::math::Vec3 playerPos,
                            bool actionPressed) noexcept;

        /// Lecture seule de l'état (debug, tests).
        const HazardState& State() const noexcept { return m_state; }

    private:
        const HazardScene* m_scene = nullptr;
        HazardCallbacks m_cb;
        HazardState m_state;
        engine::math::Vec3 m_lastPlayerPos{0, 0, 0};
        bool m_hasLastPos = false;
    };
}
```

### 4.3 `CharacterController` (modifications)

```cpp
class CharacterController final {
public:
    /// M100.16 — applique un effet hazard au tick courant. `slowdownMul`
    /// multiplie la vitesse horizontale. Si `applySinkRate=true`, le vel.y
    /// est forcé à `-sinkRateMps` (override de la gravité).
    /// Appelé par l'Engine après HazardSimulator::Update().
    /// Sans appel : pas d'effet (comportement par défaut M100.15).
    void SetHazardEffect(bool applySinkRate, float sinkRateMps, float slowdownMul) noexcept;

    bool IsSinking() const noexcept { return m_hazardEffect.applySinkRate; }

private:
    engine::world::hazard::HazardEffect m_hazardEffect;  // re-set chaque frame
};
```

Dans `Update()` :
- Si `m_hazardEffect.applySinkRate` : remplace le calcul `vel.y +=` par `vel.y = -m_hazardEffect.sinkRateMps`.
- Multiplie `vel.x` et `vel.z` par `m_hazardEffect.slowdownMul` (post-calcul vitesse, avant sweep).

### 4.4 `OrbitalCameraController` (modifications)

```cpp
class OrbitalCameraController {
public:
    /// M100.16 — décale le calcul de hauteur cible pour que la caméra reste
    /// à `currentTarget + offset`. Utile pour hazards : pendant l'enfoncement,
    /// les pieds descendent mais la tête (caméra cible) reste à sa hauteur
    /// d'origine. `offset > 0` → caméra plus haute que la cible.
    void SetGroundOffset(float offsetY) noexcept;

private:
    float m_groundOffsetY = 0.0f;  // ajouté à target.y dans le calcul de pos
};
```

## 5. Algorithmes

### 5.1 Détection point-in-volume

`Box` : test `|pos - center|.{x,y,z} <= halfExtents.{x,y,z}` composant par composant.
`Cylinder` : test `pos.y ∈ [center.y, center.y + cylHeight]` ET `(pos.xz - center.xz).length <= cylRadius`.

### 5.2 State machine `HazardSimulator::Update`

```
Si !m_state.inHazard :
  ├─ Pour chaque hazard h dans scene.hazards :
  │   └─ Si PointInHazard(h, playerPos) :
  │       ├─ m_state = { inHazard=true, activeHazard=&h, currentDepth=0, ... }
  │       ├─ cb.onEnter()
  │       └─ Retourner HazardEffect{applySinkRate=true, sinkRate=h.sinkRateMps, slowdown=h.slowdownMul}
  └─ (Aucun hazard touché) : Retourner HazardEffect{}  (no-op)

Si m_state.inHazard :
  ├─ Si type == LavaSurface :
  │   ├─ m_state.lavaTimer += dt
  │   ├─ Si lavaTimer >= 3.0s : cb.die("lava_burning"); m_state = {} ; return {}
  │   └─ Sinon retourner HazardEffect{slowdown=h.slowdownMul, applySinkRate=false}
  ├─ Sinon (Quicksand/Bog/Tar) :
  │   ├─ m_state.currentDepth += h.sinkRateMps * dt
  │   ├─ Vérifier escape selon escapeMode (cf. 5.3)
  │   ├─ Si m_state.currentDepth >= h.maxDepthMeters :
  │   │   └─ cb.die("hazard_drowning") ; m_state = {} ; return {}
  │   ├─ Si escape réussi :
  │   │   ├─ cb.onExit() ; m_state = {} ; return {}
  │   └─ Sinon : return HazardEffect{applySinkRate=true, sinkRate=h.sinkRateMps, slowdown=h.slowdownMul}
```

### 5.3 Détection escape

**MashButton** : compter `actionPressed==true` sur une fenêtre glissante de 5 s. Si `mashCount >= 10` → escape.

**LateralMove** : accumuler `|playerPos.xz - m_lastPlayerPos.xz|` chaque frame. Si `lateralTraveled >= 2.0 m` → escape.

**MashButtonItem** : identique à MashButton, mais l'escape requiert aussi `m_cb.hasItem(activeHazard->requiredItemId) == true`. Si l'item manque, mash compte quand même mais ne libère pas — le joueur meurt à maxDepth.

**None** (LavaSurface) : pas d'escape possible (le timer 3s tue).

### 5.4 Camera ground offset

L'Engine, à chaque frame, calcule l'offset à appliquer :
```cpp
if (cc.IsSinking()) {
    float feetY = cc.GetPosition().y - cc.GetCapsule().height * 0.5f;
    float originalHeadY = m_hazardEntryHeadY;  // mémorisé à onEnter
    float currentHeadY = feetY + cc.GetCapsule().height;
    camera.SetGroundOffset(originalHeadY - currentHeadY);
} else {
    camera.SetGroundOffset(0.0f);
}
```

## 6. Tests

| Exécutable | Test | Vérification |
|---|---|---|
| `hazard_volumes_tests` | `Test_Hazards_RoundtripBin` | Save → Load → field-by-field equality sur tous les champs des 4 types |
| `hazard_simulator_tests` | `Test_HazardSimulator_SinkRate` | Player dans Quicksand 1 s → depth ≈ 0.15 m (sinkRate=0.15 m/s × 1 s) |
| `hazard_simulator_tests` | `Test_HazardSimulator_MashEscape` | 10 actionPressed sur 5 s → onExit fired, state cleared |
| `hazard_simulator_tests` | `Test_HazardSimulator_LateralEscape` | Bog + déplacement cumulé 2.1 m → onExit fired |
| `hazard_simulator_tests` | `Test_HazardSimulator_LavaKills3s` | LavaSurface, 3.05 s → cb.die("lava_burning") fired exactement 1× |
| `hazard_simulator_tests` | `Test_HazardSimulator_DeathOnMaxDepth` | Quicksand sans escape, dt assez long pour atteindre maxDepth → cb.die("hazard_drowning") fired |

Total : 6 tests sur 2 exécutables.

## 7. Diff CMake

Sources ajoutées à `engine_core` (cible client) :
```cmake
  src/client/world/hazard/HazardVolumes.cpp
  src/client/world/hazard/HazardSimulator.cpp
```

Sources ajoutées à la cible éditeur (lib statique `world_editor_lib` ou similaire — à vérifier au moment de l'implémentation) :
```cmake
  src/world_editor/hazard/HazardDocument.cpp
  src/world_editor/hazard/HazardTool.cpp
```

Deux nouveaux exécutables CTest (gardés `if(WIN32)`) :
```cmake
add_executable(hazard_volumes_tests src/client/world/hazard/tests/HazardVolumesTests.cpp)
target_include_directories(hazard_volumes_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(hazard_volumes_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(hazard_volumes_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME hazard_volumes_tests COMMAND hazard_volumes_tests)

add_executable(hazard_simulator_tests src/client/world/hazard/tests/HazardSimulatorTests.cpp)
target_include_directories(hazard_simulator_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(hazard_simulator_tests PRIVATE engine_core)
if(MSVC)
  target_compile_options(hazard_simulator_tests PRIVATE /W4 /permissive- /Zc:preprocessor)
endif()
add_test(NAME hazard_simulator_tests COMMAND hazard_simulator_tests)
```

## 8. Critères d'acceptation (ticket M100.16)

- [ ] Les 4 types de hazards sont sélectionnables et sauvegardés correctement (HazardTool).
- [ ] Round-trip `hazards.bin` parfait (`Test_Hazards_RoundtripBin`).
- [ ] Le joueur s'enfonce à `sinkRate` m/s mesurés (`Test_HazardSimulator_SinkRate`).
- [ ] La caméra tierce reste à hauteur de tête originale pendant l'enfoncement (compensation via `SetGroundOffset`, vérifié manuellement et par test camera).
- [ ] Mash button compté correctement, 10 appuis en 5 s → escape (`Test_HazardSimulator_MashEscape`).
- [ ] LavaSurface tue après exactement 3 s (`Test_HazardSimulator_LavaKills3s`).
- [ ] Aucun fichier hazard compilé dans une cible serveur (vérifié par grep sur CMakeLists.txt).
- [ ] Aucun message réseau spécifique aux hazards (vérifié — pas d'opcode ajouté, mort utilise le protocole existant).

## 9. Hors scope explicite

- Pas de hazards type "spike trap" / "saw blade" / "fall damage" (futur ticket).
- Pas de rescue par PNJ.
- Pas de validation serveur de la mort hazard (mort scriptée côté client → utilise le protocole position/death existant).
- Pas d'audio loops complets (placeholder par callback ; mix complet en M100.33).
- Animation `ActivateStruggleAnim` / `PullUpAnim` : callback stub (vrais hooks animation arrivent avec CHAR-MODEL.x).
- Inventory check : callback `hasItem(itemId)` ; l'implémentation Engine retourne `false` par défaut tant que l'inventaire runtime n'existe pas, ce qui rend le mode `MashButtonItem` non-praticable mais ne casse rien (le joueur meurt à maxDepth comme prévu pour le mode `None`).

## 10. Déploiement

> **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement serveur. Aucun opcode, aucune migration DB, aucun changement de format binaire serveur. Sources hazard linkées dans `engine_core` (cible client) et `world_editor_lib` (cible éditeur) uniquement.
