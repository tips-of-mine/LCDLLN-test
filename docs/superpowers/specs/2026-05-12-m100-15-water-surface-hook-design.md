# Design — M100.15 Water Surface Hook (Wading & Swimming)

**Date** : 2026-05-12
**Ticket** : [tickets/M100/M100.15-WaterSurfaceHook.md](../../../tickets/M100/M100.15-WaterSurfaceHook.md)
**Statut** : Design validé, en attente d'exécution
**Approche retenue** : Approche 2 — complète (sampler + collider concret + hystérésis CC + callbacks audio)
**Déploiement** : ✅ client uniquement, pas de redéploiement serveur

---

## 1. Contexte et delta avec la spec brute

La spec ticket M100.15 a été rédigée avant que la logique swimming soit câblée dans `CharacterController`. À ce jour, le code expose :

- `MovementMode::Water` complet ([CharacterController.h:152-158](../../../src/client/gameplay/CharacterController.h:152)) avec gravité réduite, contrôle vertical, surface breaching.
- `IWorldCollider::QueryWater(pos, out)` ([CharacterController.h:47-53](../../../src/client/gameplay/CharacterController.h:47)) — interface dont l'implémentation par défaut retourne `inWater = false`, et **aucune classe concrète n'implémente cette interface** dans le code actuel.
- `SurfaceType::ShallowWater` et `SurfaceType::DeepWater` déjà dans l'enum ([SurfaceType.h:19-20](../../../src/client/world/surface/SurfaceType.h:19)).
- `WaterScene { lakes, rivers }` livré par M100.13 ([WaterSurfaces.h:42-46](../../../src/client/world/water/WaterSurfaces.h:42)), sans méthode `Sample()`.

L'audit `docs/superpowers/audits/2026-05-06-m100-gap-analysis.md` (ligne 411) avait fixé la règle : *« `WaterQuery` autoritaire pour flottabilité ; `SurfaceQueryService` autoritaire pour vitesse / audio / thermal »*. Ce design respecte cette séparation.

M100.15 livre donc trois pièces manquantes :

1. **`WaterSampler`** — géométrie « où est l'eau » à partir d'un `WaterScene`.
2. **`WorldColliderImpl`** — première implémentation concrète d'`IWorldCollider`. `QueryWater()` consomme le sampler. `SweepCapsule()` reste un stub MVP (raycast vertical sur la heightmap) pour les besoins du test, hors périmètre M100.15 pour le sol complet.
3. **Branchements** — `SurfaceQueryService` override `ShallowWater`/`DeepWater`, `CharacterController` applique l'hystérésis 1.0 m / 0.7 m, expose les callbacks `onEnterWater`/`onExitWater` pour le splash audio.

Le `CharacterController` lui-même n'est pas câblé au runtime client actuel (la Phase 3 utilise `OrbitalCameraController`). M100.15 livre donc le système **testable de façon headless via CTest** ; l'intégration runtime in-game appartient à la chaîne CHAR-MODEL (notamment CHAR-MODEL.26).

## 2. Architecture

```
                ┌──────────────────────────┐
                │     WaterScene (M100.13) │  lacs[] + rivières[]
                └────────────┬─────────────┘
                             │ ref const
                             ▼
                ┌──────────────────────────┐
                │      WaterSampler        │  Sample(pos) → optional<WaterSample>
                │  (nouveau, water/)       │  PIP lac + projection spline rivière
                └────────┬────────────┬────┘
                  ref    │            │ ref
                         ▼            ▼
        ┌─────────────────────┐   ┌──────────────────────────┐
        │ SurfaceQueryService │   │   WorldColliderImpl      │  nouveau
        │ (modifié)           │   │   : public IWorldCollider│  - SweepCapsule (stub MVP)
        │ override Query()    │   │                          │  - QueryWater via Sampler
        │ → Shallow/DeepWater │   └─────────┬────────────────┘
        └─────────────────────┘             │ par référence (Update)
                                            ▼
                                  ┌──────────────────────────┐
                                  │   CharacterController    │
                                  │   - hystérésis 1.0/0.7   │ (modif logique mode)
                                  │   - OnEnterWater/Exit cb │ (nouveau hook)
                                  └─────────┬────────────────┘
                                            │ callback
                                            ▼
                                Engine (consommateur) → audio.PlayOneShot("splash_*")
```

**Principes** :

- `WaterSampler` est purement stateless (lookup géométrique).
- `SurfaceQueryService` consulte le sampler pour override la sortie `Query()` vers `Shallow/DeepWater`.
- `WorldColliderImpl` consulte le sampler dans `QueryWater()` — c'est ça qui pilote la flottabilité du CC (`MovementMode::Water`).
- `CharacterController` ajoute l'hystérésis (lue depuis `m_mode` actuel — pas de membre dédié) et émet des callbacks d'entrée/sortie d'eau. Aucune dépendance audio dans le CC.
- L'Engine câble les callbacks vers son système audio.

## 3. Fichiers livrés

### 3.1 Créés

| Chemin | Rôle |
|---|---|
| `src/client/world/water/WaterSampler.h` | Déclaration de la classe `WaterSampler` |
| `src/client/world/water/WaterSampler.cpp` | Implémentation : PIP lac, projection segment rivière |
| `src/client/world/water/tests/WaterSamplerTests.cpp` | Tests géométrie (PIP, projection, overlap) |
| `src/client/world/surface/tests/WaterHookTests.cpp` | Tests intégration `SurfaceQueryService` ↔ `WaterSampler` |
| `src/client/gameplay/WorldColliderImpl.h` | Déclaration de la première implémentation concrète d'`IWorldCollider` |
| `src/client/gameplay/WorldColliderImpl.cpp` | Implémentation. `SweepCapsule` = stub raycast vertical heightmap (MVP). `QueryWater` câblé au sampler |
| `src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp` | Tests hystérésis + callbacks |

### 3.2 Modifiés

| Chemin | Modification |
|---|---|
| `src/client/world/water/WaterSurfaces.h` | Ajout `struct WaterSample { float surfaceY; float depthMeters; }` |
| `src/client/world/surface/SurfaceQueryService.h` | Ajout setter `SetWaterSampler(const WaterSampler*)` + membre `m_waterSampler` (nullable) |
| `src/client/world/surface/SurfaceQueryService.cpp` | `Query()` consulte le sampler après le résultat splat-map ; si `depth > 0` → override `base = (depth ≥ 1.0) ? DeepWater : ShallowWater` |
| `src/client/gameplay/CharacterController.h` | Ajout `using WaterTransitionCallback = std::function<void()>;` + `SetWaterTransitionCallbacks(...)` + membres `m_onEnterWater` / `m_onExitWater` |
| `src/client/gameplay/CharacterController.cpp` | Calcul `desiredMode` enrichi avec hystérésis ; émission des callbacks aux transitions vers/depuis `Water` |
| `CMakeLists.txt` racine | Ajout `WaterSampler.cpp` + `WorldColliderImpl.cpp` à `engine_core` (et **non** à `engine_core_server`) ; déclaration des 3 nouveaux exécutables CTest |

## 4. Signatures

### 4.1 `WaterSample` (étend `WaterSurfaces.h`)

```cpp
namespace engine::world::water {
    struct WaterSample {
        float surfaceY = 0.0f;       // monde, mètres
        float depthMeters = 0.0f;    // surfaceY - feetY, toujours >= 0 quand retourné
    };
}
```

### 4.2 `WaterSampler` (nouveau)

```cpp
namespace engine::world::water {
    class WaterSampler {
    public:
        bool Init(const WaterScene& scene) noexcept;

        /// worldPos.y = position monde du joueur (pieds).
        /// Retourne nullopt si hors de tout volume d'eau ou si la surface est
        /// sous les pieds (depth <= 0). Sinon retourne `{ surfaceY, depth }`.
        /// Multi-overlap (ex. lac + rivière) : retourne le hit le plus profond.
        /// Thread : safe pour lectures concurrentes (lecture seule sur m_scene).
        std::optional<WaterSample> Sample(engine::math::Vec3 worldPos) const noexcept;

    private:
        const WaterScene* m_scene = nullptr;
    };
}
```

### 4.3 `WorldColliderImpl` (nouveau)

```cpp
namespace engine::gameplay {
    class WorldColliderImpl : public IWorldCollider {
    public:
        void SetWaterSampler(const engine::world::water::WaterSampler* sampler) noexcept;

        // MVP : raycast vertical sur la heightmap (suffit pour les tests CC
        // de M100.15). Le sweep complet 3D viendra avec CHAR-MODEL.
        bool SweepCapsule(const Capsule& capsule,
                          const engine::math::Vec3& startCenter,
                          const engine::math::Vec3& endCenter,
                          SweepHit& outHit) const override;

        bool QueryWater(const engine::math::Vec3& worldCenter,
                        WaterQuery& out) const override;

    private:
        const engine::world::water::WaterSampler* m_waterSampler = nullptr;
        // (Hook heightmap injecté plus tard — pour M100.15, SweepCapsule
        //  retourne `hit = false` si pas de heightmap, ce qui suffit aux tests
        //  unitaires water focused.)
    };
}
```

### 4.4 `CharacterController` (additions)

```cpp
class CharacterController final {
public:
    using WaterTransitionCallback = std::function<void()>;
    void SetWaterTransitionCallbacks(WaterTransitionCallback onEnter,
                                     WaterTransitionCallback onExit);

private:
    // Deux nouveaux membres pour les callbacks audio (M100.15).
    WaterTransitionCallback m_onEnterWater;
    WaterTransitionCallback m_onExitWater;
    // Aucun nouveau membre dédié à l'hystérésis : m_mode (déjà existant)
    // mémorise l'état précédent, ce qui suffit à la transition asymétrique
    // 1.0 m → entrée, 0.7 m → sortie (cf. section 7).
};
```

## 5. Algorithme du sampler

### 5.1 Lac

Pour chaque `LakeInstance` :

1. Test point-in-polygon 2D dans XZ via raycast horizontal (algorithme du nombre d'intersections impaires).
2. Si dedans : `surfaceY = lake.waterLevelY`, `depth = surfaceY - worldPos.y`.
3. Retenir le hit avec la plus grande profondeur si plusieurs lacs.

Le polygone est CCW garanti par M100.13 ([WaterSurfaces.h:20](../../../src/client/world/water/WaterSurfaces.h:20)). Pas besoin de test de winding.

### 5.2 Rivière

Pour chaque `RiverInstance`, pour chaque segment `[nodes[i], nodes[i+1]]` :

1. Projeter `worldPos.xz` sur la droite portant le segment. Calculer `t ∈ [0, 1]` clampé.
2. Position projetée monde : `proj = lerp(nodes[i].position, nodes[i+1].position, t)`.
3. Distance horizontale `d = length((worldPos - proj).xz)`.
4. Largeur locale : `widthLocal = lerp(nodes[i].widthMeters, nodes[i+1].widthMeters, t)`.
5. Si `d <= widthLocal / 2` : hit. `surfaceY = proj.y`, `depth = surfaceY - worldPos.y`.

### 5.3 Priorité multi-overlap

Si plusieurs hits (ex. embouchure rivière → lac), retourner celui de **profondeur maximale**. Évite les sauts artificiels entre rivière peu profonde et lac profond à la frontière géométrique.

### 5.4 Filtrage `depth > 0`

Si `depth <= 0` (joueur au-dessus de la surface, par exemple un saut), retourner `nullopt`. Le CC ne doit pas entrer en mode Water depuis les airs.

## 6. Branchement `SurfaceQueryService`

Le body actuel de `Query()` ([SurfaceQueryService.cpp:27-…](../../../src/client/world/surface/SurfaceQueryService.cpp:27)) calcule le résultat depuis la splat-map en ligne. M100.15 patche la fin de la fonction, **juste avant `return`**, pour appliquer l'override eau :

```cpp
SurfaceQueryResult SurfaceQueryService::Query(engine::math::Vec3 worldPos) const {
    SurfaceQueryResult result = /* logique splat-map existante, inchangée */;

    // M100.15 — override water (consultation du sampler après le résultat splat)
    if (m_waterSampler) {
        if (auto sample = m_waterSampler->Sample(worldPos)) {
            result.base = (sample->depthMeters >= 1.0f)
                ? SurfaceType::DeepWater
                : SurfaceType::ShallowWater;
        }
    }
    return result;
}
```

Si `m_waterSampler == nullptr` (test isolé, ou éditeur sans WaterScene chargé), le comportement reste l'ancien — pas de régression. Pas de refactoring de la logique splat-map existante (pas d'extraction en helper `QueryFromSplat` pour minimiser le diff).

## 7. Hystérésis dans `CharacterController::Update`

Constantes locales dans `CharacterController.cpp` :

```cpp
constexpr float kSwimEnterDepth = 1.0f;
constexpr float kSwimExitDepth  = 0.7f;
```

Remplacement de la logique actuelle (ligne ~84) :

```cpp
const IWorldCollider::WaterQuery wq = /* QueryWater */;
const bool inWater = /* QueryWater retourne true ET wq.inWater */;

const bool wasInWaterMode = (m_mode == MovementMode::Water);
const bool depthSaysSwim =
    inWater && (wq.depth >= kSwimEnterDepth ||
                (wasInWaterMode && wq.depth >= kSwimExitDepth));

const MovementMode desiredMode =
    isFlying ? MovementMode::Fly
             : (depthSaysSwim ? MovementMode::Water
             : (m_isGrounded ? MovementMode::Ground : MovementMode::Air));

if (desiredMode != m_mode) {
    if (desiredMode == MovementMode::Water && m_onEnterWater) {
        m_onEnterWater();
    } else if (m_mode == MovementMode::Water
               && desiredMode != MovementMode::Water
               && m_onExitWater) {
        m_onExitWater();
    }
    m_mode = desiredMode;
    // (logs LOG_INFO existants conservés)
}
```

**Pourquoi l'hystérésis tient dans un seul `if` sans membre dédié** : `m_mode` est déjà la mémoire de l'état précédent. La condition `wasInWaterMode && depth >= 0.7` permet de rester en Water entre 0.7 et 1.0 m, alors qu'un joueur qui entre depuis l'extérieur ne bascule qu'à >= 1.0. C'est exactement la définition de l'hystérésis bidirectionnelle.

## 8. Contrat callbacks audio

Le `CharacterController` n'a aucune dépendance audio (le module audio est `src/client/audio/`, le CC vit dans `src/client/gameplay/`). Pour préserver cette séparation :

- Le CC expose `SetWaterTransitionCallbacks(onEnter, onExit)`. Les callbacks sont des `std::function<void()>` stockées par valeur.
- L'**Engine** est le câblage. À l'init, après création du CC et du collider :

  ```cpp
  m_characterController.SetWaterTransitionCallbacks(
      [this]() { m_audio.PlayOneShot("splash_water_enter"); },
      [this]() { m_audio.PlayOneShot("splash_water_exit"); });
  ```

- Si les callbacks ne sont pas définies (test headless, ou Engine qui ne câble pas), les transitions s'exécutent silencieusement. Aucune erreur, aucun warning.

**Pourquoi pas un `IAudioPlayer*` dans le CC** : ça créerait une dépendance directe gameplay → audio que la spec M100.15 ne mandate pas. Les callbacks restent la solution la plus découplée et la plus testable.

## 9. Tests CTest livrés

> **Note** : le stub `SweepCapsule` de `WorldColliderImpl` n'est pas exercé par les tests M100.15. Les tests `cc_water_hysteresis_tests` construisent un `WorldColliderImpl` minimal (sampler câblé, `SweepCapsule` retourne `hit=false`) et appellent `CharacterController::Update` avec des inputs neutres pour ne tester que la transition de mode et les callbacks. La validation complète de `SweepCapsule` viendra avec CHAR-MODEL.

| Exécutable | Test | Vérification |
|---|---|---|
| `water_sampler_tests` | `Test_Lake_PointInside_ReturnsSurfaceY` | `Sample()` dans polygone lac → `{surfaceY == lake.waterLevelY, depth > 0}` |
| `water_sampler_tests` | `Test_Lake_PointOutside_ReturnsNullopt` | `Sample()` hors polygone → `nullopt` |
| `water_sampler_tests` | `Test_River_ProjectionOnSegment` | Point à `d < width/2` d'un segment → hit avec `surfaceY` interpolé |
| `water_sampler_tests` | `Test_River_PointBeyondWidth_Misses` | Point à `d > width/2` → `nullopt` |
| `water_sampler_tests` | `Test_MultiOverlap_ReturnsDeepest` | Lac (depth=2) + rivière (depth=1) au même `worldPos` → retour à `depth=2` |
| `water_sampler_tests` | `Test_FeetAboveSurface_ReturnsNullopt` | `worldPos.y > waterLevelY` → `nullopt` |
| `water_hook_tests` | `Test_DepthBelow1m_IsShallowWater` | `SurfaceQueryService::Query(depth=0.3)` → `base == ShallowWater` |
| `water_hook_tests` | `Test_DepthAtOrAbove1m_IsDeepWater` | `Query(depth=1.0)` et `Query(depth=1.2)` → `base == DeepWater` |
| `water_hook_tests` | `Test_NoSampler_FallsBackToSplat` | `Query()` sans `SetWaterSampler` → comportement M100.11 inchangé |
| `cc_water_hysteresis_tests` | `Test_EntersWaterAt1m` | Depth passe de 0.5 → 1.1 → `m_mode == Water` |
| `cc_water_hysteresis_tests` | `Test_ExitsWaterAt0p7m` | CC en Water à depth=1.2, descend à 0.8 → reste Water ; à 0.6 → sort |
| `cc_water_hysteresis_tests` | `Test_HysteresisDoesNotFlicker` | Oscillation 0.95 ↔ 1.05 sur 10 frames → 1 seule transition Air→Water |
| `cc_water_hysteresis_tests` | `Test_OnEnterCallbackFiresOnce` | onEnter appelé 1× exactement à l'entrée |
| `cc_water_hysteresis_tests` | `Test_OnExitCallbackFiresOnce` | onExit appelé 1× exactement à la sortie |
| `cc_water_hysteresis_tests` | `Test_NoCallbacksSetIsNoCrash` | CC sans callbacks → transitions silencieuses, pas de crash |
| (build linux) | (compile-only) | `src/masterd/` ne compile pas `WaterSampler.cpp` ni `WorldColliderImpl.cpp` (vérifié via split CMake `engine_core` vs `engine_core_server`) |

## 10. Diff CMake

Sources ajoutées à `engine_core` (cible client uniquement) :

```cmake
add_library(engine_core
    ...
    src/client/world/water/WaterSampler.cpp           # nouveau
    src/client/gameplay/WorldColliderImpl.cpp         # nouveau
    ...
)
```

Trois nouveaux exécutables CTest :

```cmake
add_executable(water_sampler_tests src/client/world/water/tests/WaterSamplerTests.cpp)
target_link_libraries(water_sampler_tests PRIVATE engine_core)
add_test(NAME water_sampler_tests COMMAND water_sampler_tests)

add_executable(water_hook_tests src/client/world/surface/tests/WaterHookTests.cpp)
target_link_libraries(water_hook_tests PRIVATE engine_core)
add_test(NAME water_hook_tests COMMAND water_hook_tests)

add_executable(cc_water_hysteresis_tests src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp)
target_link_libraries(cc_water_hysteresis_tests PRIVATE engine_core)
add_test(NAME cc_water_hysteresis_tests COMMAND cc_water_hysteresis_tests)
```

**À noter (mémoire CI Linux)** : `build-linux.yml` est compile-only, pas de ctest. Les tests tournent sur Windows CI seulement.

## 11. Critères d'acceptation (ticket M100.15)

- [ ] Depth 0.3 m → `SurfaceQueryService::Query` retourne `base == ShallowWater`
- [ ] Depth 1.2 m → `Query` retourne `base == DeepWater` ET CC passe `m_mode == Water`
- [ ] CC sort de `Water` à depth < 0.7 m (hystérésis)
- [ ] Callbacks `onEnterWater` / `onExitWater` déclenchés aux transitions (consommés par l'Engine pour le splash audio)
- [ ] Sampling fonctionne lac (PIP) + rivière (projection ≤ widthMeters/2)
- [ ] `src/masterd/` ne compile pas `WaterSampler.cpp` ni `WorldColliderImpl.cpp` (vérifié dans le CMake serveur)

## 12. Hors scope explicite

- Pas d'animation de nage propre — réutilise la machine d'état `MovementMode::Water` existante.
- Pas de noyade (hazards M100.16).
- Pas de natation contre courant — `RiverInstance` n'a pas encore de champ `flowSpeed` ; viendra dans un futur ticket gameplay.
- Pas d'intégration runtime in-game du `CharacterController` — appartient à la chaîne CHAR-MODEL.
- Le `SweepCapsule` de `WorldColliderImpl` reste un stub. Son implémentation complète (heightmap + collision proxies M100.12) viendra avec CHAR-MODEL ou un ticket dédié physique.

## 13. Déploiement

> **Déploiement** : ✅ client uniquement, pas de redéploiement serveur. Aucun opcode, aucune migration DB, aucun changement de format binaire.
