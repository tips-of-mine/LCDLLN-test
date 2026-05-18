# Sous-projet B.1 — Locomotion state machine — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Brancher le `CharacterController` existant à l'avatar in-game, étendre la state machine A (3 états → 7 états : Idle/StartWalking/Walk/Run/Jump/Fall/Land), ajouter le crossfade entre clips, supprimer le bobY synthétique, et fournir un `TerrainCollider` minimal pour que la physique fonctionne sur le terrain heightmap.

**Architecture:** `CharacterController` (déjà écrit, jamais branché) prend en charge tout le mouvement du joueur. `OrbitalCameraController` rétrogradé en caméra pure (suit une cible, gère yaw/pitch/zoom). `TerrainCollider` implémente `IWorldCollider` via heightmap query (bilinear interpolation). Animations gérées par nouveau `AnimationCrossfade` qui mixe deux poses sur 0.15s.

**Tech Stack:** C++20, Vulkan 1.x, CMake 3.20, cgltf (déjà vendored), framework de tests minimal (REQUIRE macro + `lcdlln_add_simple_test`).

**Spec source :** [2026-05-18-locomotion-state-machine-design.md](../specs/2026-05-18-locomotion-state-machine-design.md) (validé 2026-05-18).

**Déploiement :** ✅ Client uniquement, pas de redéploiement serveur.

---

## File Structure

### Nouveaux fichiers

| Chemin | Rôle | Taille estimée |
|---|---|---|
| `src/client/gameplay/TerrainCollider.h` | Impl `IWorldCollider` via heightmap query bilinéaire | ~50 lignes |
| `src/client/gameplay/TerrainCollider.cpp` | SweepCapsule (raycast vertical contre heightmap) + GroundHeightAt | ~150 lignes |
| `src/client/render/skinned/AnimationCrossfade.h` | API Play(clip, loops, now) + Sample(skel, now) | ~50 lignes |
| `src/client/render/skinned/AnimationCrossfade.cpp` | lerp/slerp TRS par bone entre 2 ActiveAnimation | ~140 lignes |
| `src/client/render/skinned/tests/AnimationCrossfadeTests.cpp` | 3 tests : alpha=0, alpha=1, alpha=0.5 | ~100 lignes |
| `src/client/gameplay/tests/TerrainColliderTests.cpp` | Tests bilinear interp + hors-map fallback | ~90 lignes |
| `src/shared/math/tests/Mat4HelpersTests.cpp` | Tests Translate/RotateY/Identity | ~70 lignes |
| `game/data/models/avatars/y_bot_run/y_bot_run.glb` | Mixamo Fast Run with-skin (~2 MB) | binaire |
| `game/data/models/avatars/y_bot_jump/y_bot_jump.glb` | Mixamo Jump with-skin (~2 MB) | binaire |
| `game/data/models/avatars/y_bot_fall/y_bot_fall.glb` | Mixamo falling idle animation-only (~500 KB) | binaire |
| `game/data/models/avatars/y_bot_land/y_bot_land.glb` | Mixamo hard landing animation-only (~500 KB) | binaire |
| `game/data/models/avatars/y_bot_run/README.md` | Source + clip name | texte |
| `game/data/models/avatars/y_bot_jump/README.md` | idem | texte |
| `game/data/models/avatars/y_bot_fall/README.md` | idem + note animation-only | texte |
| `game/data/models/avatars/y_bot_land/README.md` | idem + note animation-only | texte |

### Fichiers modifiés

| Chemin | Modification |
|---|---|
| `src/shared/math/Math.h` | Ajouter méthodes statiques `Mat4::Identity()`, `Mat4::Translate(const Vec3&)`, `Mat4::RotateY(float radians)` |
| `src/client/render/skinned/AnimationSampler.h` | Exposer `ComposeTRS` en public static (anonymous namespace → static membre) |
| `src/client/render/skinned/AnimationSampler.cpp` | Idem |
| `src/client/render/skinned/SkinnedMeshLoader.h` | Ajouter `LoadClipsAnimOnly(path, targetSkeleton)` pour fichiers animation-only |
| `src/client/render/skinned/SkinnedMeshLoader.cpp` | Implémenter `LoadClipsAnimOnly` (parse cgltf nodes sans skin, retarget par nom) |
| `src/client/render/Camera.h` | Retirer `m_locomotion`, `m_walkBobPhase`, `m_verticalVelocityY`, `m_verticalOffsetY`, `m_isCrouching`, enum `LocomotionState`. Garder `m_target`, `m_distance`. Simplifier signature de `Update`. |
| `src/client/render/Camera.cpp` | Refacto `OrbitalCameraController::Update` : retirer lecture WASD/Shift/Space + walkBob + saut + collision sol vertical. Garder uniquement : lecture souris (yaw/pitch clic droit) + molette (zoom) + calcul `camera.position = target + orbital_offset(yaw, pitch, distance)`. |
| `src/client/app/Engine.h` | Étendre enum `AvatarLocomotionState { Idle, StartWalking, Walk, Run, Jump, Fall, Land }` (renommer `Walking` → `Walk`). Nouveaux membres `m_characterController`, `m_terrainCollider`, `m_avatarCrossfade`, `m_avatarYaw`. |
| `src/client/app/Engine.cpp` | Init CharacterController + TerrainCollider au boot. Charger 4 nouveaux clips. Refacto per-frame : BuildPlayerMoveInput → cc.Update → cameraController.SetTarget → state machine étendue → crossfade.Sample → Record. Supprimer le bobY synthétique. |
| `CMakeLists.txt` (root) | Lister `TerrainCollider.cpp` + `AnimationCrossfade.cpp` dans `engine_core` sources |
| `src/CMakeLists.txt` | Enregistrer 3 nouveaux tests : `terrain_collider_tests`, `animation_crossfade_tests`, `mat4_helpers_tests` via `lcdlln_add_simple_test` |
| `config.json` | Ajouter section `player.movement.*` |
| `CODEBASE_MAP.md` §14.5 | Mettre à jour : state machine étendue à 7 états + crossfade, suppression bobY, intégration CharacterController + TerrainCollider, mapping clips étendu |

---

## Task 1 — Math helpers `Mat4::Identity`, `Translate`, `RotateY`

**Files:**
- Modify: `src/shared/math/Math.h` (ajouter 3 méthodes statiques inline)
- Create: `src/shared/math/tests/Mat4HelpersTests.cpp`
- Modify: `src/CMakeLists.txt` (enregistrer `mat4_helpers_tests`)

**Why TDD first:** ces helpers seront utilisés partout dans Engine.cpp (build model matrix), AnimationCrossfade (compose pose), TerrainCollider (worldFromLocal). Petite zone facile à TDD avec des résultats arithmétiques exacts.

- [ ] **Step 1.1 — Écrire les tests (failing)**

Créer `src/shared/math/tests/Mat4HelpersTests.cpp` :
```cpp
#include "src/shared/math/Math.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    void Test_Identity_IsIdentity()
    {
        Mat4 m = Mat4::Identity();
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                REQUIRE(Approx(m.m[col * 4 + row], (col == row) ? 1.0f : 0.0f));
    }

    void Test_Translate_PutsTranslationInLastColumn()
    {
        Mat4 m = Mat4::Translate(Vec3{5.0f, 6.0f, 7.0f});
        REQUIRE(Approx(m.m[12], 5.0f));  // col 3, row 0
        REQUIRE(Approx(m.m[13], 6.0f));  // col 3, row 1
        REQUIRE(Approx(m.m[14], 7.0f));  // col 3, row 2
        REQUIRE(Approx(m.m[15], 1.0f));
        REQUIRE(Approx(m.m[0], 1.0f));   // diag stays identity
        REQUIRE(Approx(m.m[5], 1.0f));
        REQUIRE(Approx(m.m[10], 1.0f));
    }

    void Test_RotateY_Pi_FlipsXAndZ()
    {
        Mat4 m = Mat4::RotateY(3.14159265f);
        // Y axis stays.
        REQUIRE(Approx(m.m[5], 1.0f));
        // X column inverted, Z column inverted.
        REQUIRE(Approx(m.m[0], -1.0f, 1e-3f));   // R(π) → cos(π) = -1
        REQUIRE(Approx(m.m[10], -1.0f, 1e-3f));  // cos(π) = -1
        // Off-diagonals sin/-sin (≈0 at π).
        REQUIRE(Approx(m.m[2], 0.0f, 1e-3f));    // sin(π) ≈ 0
        REQUIRE(Approx(m.m[8], 0.0f, 1e-3f));    // -sin(π) ≈ 0
    }

    void Test_RotateY_HalfPi_RotatesXTowardMinusZ()
    {
        Mat4 m = Mat4::RotateY(1.5707963f);  // 90°
        // Standard column-major right-hand: x basis becomes (0, 0, -1)
        REQUIRE(Approx(m.m[0], 0.0f, 1e-3f));
        REQUIRE(Approx(m.m[2], -1.0f, 1e-3f));
        // z basis becomes (1, 0, 0)
        REQUIRE(Approx(m.m[8], 1.0f, 1e-3f));
        REQUIRE(Approx(m.m[10], 0.0f, 1e-3f));
    }

    void Test_TranslateThenRotateY_Composes()
    {
        Mat4 t = Mat4::Translate(Vec3{10.0f, 0.0f, 0.0f});
        Mat4 r = Mat4::RotateY(1.5707963f);
        // T * R : first rotate then translate (column-vector convention).
        Mat4 tr = t * r;
        // Translation column unchanged by post-multiplied rotation : still (10, 0, 0).
        REQUIRE(Approx(tr.m[12], 10.0f));
        REQUIRE(Approx(tr.m[13], 0.0f));
        REQUIRE(Approx(tr.m[14], 0.0f));
        // Rotation part preserved.
        REQUIRE(Approx(tr.m[0], 0.0f, 1e-3f));
        REQUIRE(Approx(tr.m[2], -1.0f, 1e-3f));
    }
}

int main()
{
    Test_Identity_IsIdentity();
    Test_Translate_PutsTranslationInLastColumn();
    Test_RotateY_Pi_FlipsXAndZ();
    Test_RotateY_HalfPi_RotatesXTowardMinusZ();
    Test_TranslateThenRotateY_Composes();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 1.2 — Implémenter dans `Math.h`**

Ajouter dans la struct `Mat4` (après `operator*`, avant `PerspectiveVulkan`) :
```cpp
    static Mat4 Identity()
    {
        return Mat4{};  // default ctor already builds identity
    }

    static Mat4 Translate(const Vec3& t)
    {
        Mat4 m;
        m.m[12] = t.x;
        m.m[13] = t.y;
        m.m[14] = t.z;
        return m;
    }

    static Mat4 RotateY(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        Mat4 m;
        m.m[0]  = c;
        m.m[2]  = -s;
        m.m[8]  = s;
        m.m[10] = c;
        return m;
    }
```

Vérifier que `<cmath>` est déjà inclus en haut de `Math.h` (sinon l'ajouter).

- [ ] **Step 1.3 — Enregistrer le test dans CMake**

Dans `src/CMakeLists.txt`, après les tests skinned existants (autour de `skinned_mesh_loader_tests`), ajouter :
```cmake
lcdlln_add_simple_test(mat4_helpers_tests src/shared/math/tests/Mat4HelpersTests.cpp)
```

- [ ] **Step 1.4 — Commit**

```bash
git add src/shared/math/Math.h src/shared/math/tests/Mat4HelpersTests.cpp src/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(math): add Mat4::Identity / Translate / RotateY static helpers (1/14)

Mat4 n'avait que le constructeur (identity par defaut) et operator*.
B.1 va construire des model matrix T(pos) * R_y(yaw) a chaque frame :
sans helpers, chaque call site reecrit la composition manuellement.

5 tests unitaires : identity, translate, rotateY(pi), rotateY(pi/2),
T * R compose correctement.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2 — Exposer `ComposeTRS` en public static dans `AnimationSampler`

**Files:**
- Modify: `src/client/render/skinned/AnimationSampler.h`
- Modify: `src/client/render/skinned/AnimationSampler.cpp`

**Why:** `AnimationCrossfade` (Task 3) doit composer une matrice 4×4 depuis un triplet TRS interpolé. `ComposeTRS` est actuellement dans un anonymous namespace dans `AnimationSampler.cpp` — pas accessible. Le passer en public static évite la duplication. Pas de test dédié (couvert par les tests existants de `SamplePose` qui l'utilise déjà).

- [ ] **Step 2.1 — Ajouter la déclaration dans `AnimationSampler.h`**

Dans la classe `AnimationSampler`, à la fin de la section `public:` (après `ComputeFinalMatrices`), ajouter :
```cpp
    // Composes a translation/rotation/scale triplet into a 4x4 matrix (column-major).
    // Used by SamplePose internally and by AnimationCrossfade for pose interpolation.
    static engine::math::Mat4 ComposeTRS(const engine::math::Vec3& translation,
                                          const engine::math::Quat& rotation,
                                          const engine::math::Vec3& scale);
```

- [ ] **Step 2.2 — Déplacer l'implémentation hors de l'anonymous namespace**

Dans `AnimationSampler.cpp`, l'actuelle fonction `ComposeTRS` est dans le namespace anonyme (lignes ~10-25 selon implémentation A). La déplacer hors du namespace anonyme, en method statique de `AnimationSampler` :

Avant (dans anonymous namespace) :
```cpp
namespace
{
    engine::math::Mat4 ComposeTRS(const engine::math::Vec3& t,
                                  const engine::math::Quat& r,
                                  const engine::math::Vec3& s)
    {
        // ... (existing body)
    }
}
```

Après (sortir du namespace anonyme, mettre en static membre) :
```cpp
// (anonymous namespace can now be empty or removed if it only held ComposeTRS)

engine::math::Mat4 AnimationSampler::ComposeTRS(const engine::math::Vec3& t,
                                                 const engine::math::Quat& r,
                                                 const engine::math::Vec3& s)
{
    // ... (same body as before)
}
```

Update les call sites internes (dans `SamplePose`) : `ComposeTRS(...)` devient `AnimationSampler::ComposeTRS(...)` ou simplement `ComposeTRS(...)` si on appelle depuis un membre statique de la même classe (le résolveur de nom de classe trouve le static).

- [ ] **Step 2.3 — Vérifier que les tests existants passent toujours**

Les tests `animation_sampler_tests` (livrés par A) doivent rester verts puisqu'on n'a que déplacé du code. Pas de nouveau test à ajouter ici.

- [ ] **Step 2.4 — Commit**

```bash
git add src/client/render/skinned/AnimationSampler.h src/client/render/skinned/AnimationSampler.cpp
git commit -m "$(cat <<'EOF'
refactor(skinned): expose ComposeTRS as public AnimationSampler static (2/14)

AnimationCrossfade (Task 3) doit composer une matrice 4x4 depuis un
triplet TRS interpole entre 2 poses. ComposeTRS etait prive dans un
anonymous namespace de AnimationSampler.cpp — la passer en public
static evite la duplication.

Aucun changement de comportement, juste un deplacement de scope.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3 — `AnimationCrossfade` (lerp/slerp TRS entre 2 poses)

**Files:**
- Create: `src/client/render/skinned/AnimationCrossfade.h`
- Create: `src/client/render/skinned/AnimationCrossfade.cpp`
- Create: `src/client/render/skinned/tests/AnimationCrossfadeTests.cpp`
- Modify: root `CMakeLists.txt` (engine_core sources)
- Modify: `src/CMakeLists.txt` (test)

- [ ] **Step 3.1 — Écrire les tests (failing)**

Créer `src/client/render/skinned/tests/AnimationCrossfadeTests.cpp` :
```cpp
#include "src/client/render/skinned/AnimationCrossfade.h"
#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Quat;
using engine::math::Vec3;
using engine::render::skinned::AnimationClip;
using engine::render::skinned::AnimationCrossfade;
using engine::render::skinned::Bone;
using engine::render::skinned::BoneTracks;
using engine::render::skinned::Skeleton;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    // Build a 1-bone skeleton + 1-bone clip translated to a known position.
    Skeleton MakeOneBoneSkel()
    {
        Skeleton s;
        s.bones.push_back(Bone{"root", -1, Mat4::Identity(), Mat4::Identity()});
        return s;
    }

    AnimationClip MakeStaticTranslateClip(const std::string& name, float x)
    {
        AnimationClip c;
        c.name = name;
        c.duration = 1.0f;
        c.tracks.resize(1);
        c.tracks[0].translation = {{0.0f, Vec3{x, 0.0f, 0.0f}}, {1.0f, Vec3{x, 0.0f, 0.0f}}};
        return c;
    }

    void Test_NoCrossfade_PlaysCurrentClipOnly()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationCrossfade cf;
        cf.Play(a, /*loops=*/ true, /*now=*/ 0.0f);

        // Sample after some time, no crossfade in flight.
        auto poses = cf.Sample(skel, /*now=*/ 0.5f);
        REQUIRE(poses.size() == 1);
        REQUIRE(Approx(poses[0].m[12], 10.0f));  // translation X
    }

    void Test_Crossfade_AtAlphaZero_ReturnsOldPose()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationClip b = MakeStaticTranslateClip("B", 20.0f);
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);  // crossfade starts at now=1.0

        // Sample exactly at start of crossfade → alpha = 0 → pure A pose.
        auto poses = cf.Sample(skel, 1.0f);
        REQUIRE(Approx(poses[0].m[12], 10.0f));
    }

    void Test_Crossfade_AtAlphaOne_ReturnsNewPose()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationClip b = MakeStaticTranslateClip("B", 20.0f);
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);

        // Sample after the crossfade duration (0.15s default) → pure B pose.
        auto poses = cf.Sample(skel, 1.0f + 0.15f + 0.01f);
        REQUIRE(Approx(poses[0].m[12], 20.0f));
    }

    void Test_Crossfade_AtAlphaHalf_Midway()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip a = MakeStaticTranslateClip("A", 10.0f);
        AnimationClip b = MakeStaticTranslateClip("B", 20.0f);
        AnimationCrossfade cf;
        cf.Play(a, true, 0.0f);
        cf.Play(b, true, 1.0f);

        // Sample at half crossfade : 1.0 + 0.075s → alpha = 0.5
        auto poses = cf.Sample(skel, 1.0f + 0.075f);
        REQUIRE(Approx(poses[0].m[12], 15.0f, 1e-3f));  // lerp(10, 20, 0.5)
    }

    void Test_OneShot_Clamped_AtClipEnd()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip jump = MakeStaticTranslateClip("Jump", 5.0f);  // duration = 1.0
        AnimationCrossfade cf;
        cf.Play(jump, /*loops=*/ false, /*now=*/ 0.0f);

        // Sample after clip duration (1.5s, clip is 1.0s, non-looping → clamp at end).
        auto poses = cf.Sample(skel, 1.5f);
        REQUIRE(Approx(poses[0].m[12], 5.0f));  // last keyframe value, no wrap
    }
}

int main()
{
    Test_NoCrossfade_PlaysCurrentClipOnly();
    Test_Crossfade_AtAlphaZero_ReturnsOldPose();
    Test_Crossfade_AtAlphaOne_ReturnsNewPose();
    Test_Crossfade_AtAlphaHalf_Midway();
    Test_OneShot_Clamped_AtClipEnd();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 3.2 — Implémenter `AnimationCrossfade.h`**

Créer `src/client/render/skinned/AnimationCrossfade.h` :
```cpp
#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/math/Math.h"

#include <optional>
#include <vector>

namespace engine::render::skinned
{

// Une clip en cours de lecture (current ou previous pendant un crossfade).
struct ActiveAnimation
{
    const AnimationClip* clip = nullptr;  // non-owning ; durée de vie >= cette struct
    float startTime = 0.0f;               // temps absolu (steady_clock::now en secondes) du Play
    bool  loops = true;
};

// Crossfade entre deux animations. Quand Play est appelé alors qu'une autre est en
// cours, l'ancienne est conservée comme "previous" et un blend linéaire commence
// sur kCrossfadeDuration secondes. Après cette durée, previous est libérée et seul
// current reste.
class AnimationCrossfade
{
public:
    static constexpr float kCrossfadeDuration = 0.15f;  // seconds

    // Démarre une nouvelle clip. Si une est déjà en cours, déclenche un blend.
    void Play(const AnimationClip& newClip, bool loops, float now);

    // Échantillonne la pose (locales par bone) au temps `now`. Si un crossfade est
    // en cours (now - crossfadeStart < kCrossfadeDuration), interpole entre les
    // poses old et new. Sinon renvoie la pose de current.
    std::vector<engine::math::Mat4> Sample(const Skeleton& skeleton, float now) const;

private:
    ActiveAnimation                m_current;
    std::optional<ActiveAnimation> m_previous;
    float                          m_crossfadeStartTime = 0.0f;

    // Échantillonne une seule clip à `now` en respectant loops vs clamp.
    static std::vector<engine::math::Mat4> SampleSingle(const Skeleton& skel,
                                                         const ActiveAnimation& anim,
                                                         float now);
};

}  // namespace engine::render::skinned
```

- [ ] **Step 3.3 — Implémenter `AnimationCrossfade.cpp`**

Créer `src/client/render/skinned/AnimationCrossfade.cpp` :
```cpp
#include "src/client/render/skinned/AnimationCrossfade.h"
#include "src/client/render/skinned/AnimationSampler.h"

#include <algorithm>
#include <cmath>

namespace engine::render::skinned
{

void AnimationCrossfade::Play(const AnimationClip& newClip, bool loops, float now)
{
    if (m_current.clip == &newClip) {
        // Same clip already playing : no-op (avoid resetting time and visual hitch).
        return;
    }
    if (m_current.clip != nullptr) {
        // Move current to previous and start crossfade.
        m_previous = m_current;
        m_crossfadeStartTime = now;
    }
    m_current.clip = &newClip;
    m_current.startTime = now;
    m_current.loops = loops;
}

std::vector<engine::math::Mat4>
AnimationCrossfade::SampleSingle(const Skeleton& skel, const ActiveAnimation& anim, float now)
{
    std::vector<engine::math::Mat4> empty;
    if (!anim.clip || anim.clip->duration <= 0.0f) return empty;

    const float elapsed = now - anim.startTime;
    const float t = anim.loops
                  ? std::fmod(std::max(0.0f, elapsed), anim.clip->duration)
                  : std::min(std::max(0.0f, elapsed), anim.clip->duration);
    return AnimationSampler::SamplePose(skel, *anim.clip, t);
}

std::vector<engine::math::Mat4>
AnimationCrossfade::Sample(const Skeleton& skeleton, float now) const
{
    // Pas de current → renvoie pose bind (chaque bone = bindLocal).
    if (!m_current.clip) {
        std::vector<engine::math::Mat4> bind(skeleton.bones.size());
        for (size_t i = 0; i < skeleton.bones.size(); ++i)
            bind[i] = skeleton.bones[i].bindLocal;
        return bind;
    }

    auto curPose = SampleSingle(skeleton, m_current, now);

    // Pas de previous OU crossfade terminé → renvoie current.
    const float crossfadeElapsed = now - m_crossfadeStartTime;
    if (!m_previous.has_value() || crossfadeElapsed >= kCrossfadeDuration) {
        return curPose;
    }

    // Crossfade actif : interpole entre prev et cur par TRS.
    auto prevPose = SampleSingle(skeleton, *m_previous, now);
    const float alpha = std::clamp(crossfadeElapsed / kCrossfadeDuration, 0.0f, 1.0f);

    // Décomposer chaque matrice locale (de SamplePose, qui retourne T*R*S) en TRS,
    // lerp/slerp, recomposer via AnimationSampler::ComposeTRS.
    // Note : SamplePose stocke la matrice composée par bone — pour le lerp on a
    // besoin des TRS séparés. Solution : ré-échantillonner les tracks brutes ici.
    std::vector<engine::math::Mat4> blended(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        // Re-sample des keyframes brutes du current et du previous pour ce bone.
        // (Plus coûteux que de juste lerp les matrices mais correct.)
        const float curElapsed = now - m_current.startTime;
        const float curT = m_current.loops
                         ? std::fmod(std::max(0.0f, curElapsed), m_current.clip->duration)
                         : std::min(std::max(0.0f, curElapsed), m_current.clip->duration);
        const float prevElapsed = now - m_previous->startTime;
        const float prevT = m_previous->loops
                         ? std::fmod(std::max(0.0f, prevElapsed), m_previous->clip->duration)
                         : std::min(std::max(0.0f, prevElapsed), m_previous->clip->duration);

        // Fallback bindLocal pour translation/rotation/scale si le track est absent.
        const Bone& bone = skeleton.bones[i];
        const engine::math::Vec3 bindT{bone.bindLocal.m[12], bone.bindLocal.m[13], bone.bindLocal.m[14]};

        engine::math::Vec3 curTr = bindT, prevTr = bindT;
        engine::math::Quat curRo = engine::math::Quat::Identity(), prevRo = engine::math::Quat::Identity();
        engine::math::Vec3 curSc{1, 1, 1}, prevSc{1, 1, 1};

        if (i < m_current.clip->tracks.size()) {
            const auto& trk = m_current.clip->tracks[i];
            curTr = InterpolateKeyframes(trk.translation, curT, bindT);
            curRo = InterpolateKeyframes(trk.rotation, curT, engine::math::Quat::Identity());
            curSc = InterpolateKeyframes(trk.scale, curT, engine::math::Vec3{1,1,1});
        }
        if (i < m_previous->clip->tracks.size()) {
            const auto& trk = m_previous->clip->tracks[i];
            prevTr = InterpolateKeyframes(trk.translation, prevT, bindT);
            prevRo = InterpolateKeyframes(trk.rotation, prevT, engine::math::Quat::Identity());
            prevSc = InterpolateKeyframes(trk.scale, prevT, engine::math::Vec3{1,1,1});
        }

        const engine::math::Vec3 blendedT{
            prevTr.x + alpha * (curTr.x - prevTr.x),
            prevTr.y + alpha * (curTr.y - prevTr.y),
            prevTr.z + alpha * (curTr.z - prevTr.z)
        };
        const engine::math::Quat blendedR = engine::math::Quat::Slerp(prevRo, curRo, alpha);
        const engine::math::Vec3 blendedS{
            prevSc.x + alpha * (curSc.x - prevSc.x),
            prevSc.y + alpha * (curSc.y - prevSc.y),
            prevSc.z + alpha * (curSc.z - prevSc.z)
        };
        blended[i] = AnimationSampler::ComposeTRS(blendedT, blendedR, blendedS);
    }
    return blended;
}

}  // namespace engine::render::skinned
```

- [ ] **Step 3.4 — Wire CMake**

Dans `CMakeLists.txt` (root), ajouter `src/client/render/skinned/AnimationCrossfade.cpp` à `engine_core` sources juste après `AnimationSampler.cpp`.

Dans `src/CMakeLists.txt`, après `mat4_helpers_tests`, ajouter :
```cmake
lcdlln_add_simple_test(animation_crossfade_tests src/client/render/skinned/tests/AnimationCrossfadeTests.cpp)
```

- [ ] **Step 3.5 — Commit**

```bash
git add src/client/render/skinned/AnimationCrossfade.h src/client/render/skinned/AnimationCrossfade.cpp src/client/render/skinned/tests/AnimationCrossfadeTests.cpp CMakeLists.txt src/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(skinned): AnimationCrossfade lerp/slerp TRS entre 2 poses (3/14)

Resoud les hard cuts entre clips de A. Quand Play est appele alors qu'une
clip joue deja, l'ancienne devient "previous" et un blend lineaire
commence sur kCrossfadeDuration=0.15s. Apres : seule la nouvelle reste.

Le blend est fait au niveau TRS (translation lerp, rotation slerp, scale
lerp) puis recompose via AnimationSampler::ComposeTRS — pas un lerp
naif sur les matrices (qui serait faux pour la rotation).

5 tests : pas de blend, blend alpha=0, alpha=1, alpha=0.5, one-shot
clampe a la fin (pour Jump/StartWalking/Land).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4 — `SkinnedMeshLoader::LoadClipsAnimOnly` pour fichiers animation-only

**Files:**
- Modify: `src/client/render/skinned/SkinnedMeshLoader.h` (ajouter la déclaration)
- Modify: `src/client/render/skinned/SkinnedMeshLoader.cpp` (implémenter)

**Why:** `falling idle.fbx` et `hard landing.fbx` sont des FBX animation-only (~400 KB chacun). Après conversion glTF, ils n'ont pas de `skin` (skin = lien mesh ↔ joints, sans mesh pas de skin). `LoadClipsRetargeted` actuel bail à la ligne 104 (`skins_count == 0`). Cette nouvelle helper traverse les nodes glTF directement et retargete les channels d'animation par nom de bone.

Pas de test unitaire dédié — couvert indirectement par le smoke test in-game (si les clips Fall/Land s'affichent c'est que le helper marche). Tester un .glb animation-only sans en construire un est compliqué.

- [ ] **Step 4.1 — Ajouter la déclaration dans `SkinnedMeshLoader.h`**

Après la déclaration de `LoadClipsRetargeted`, ajouter :
```cpp
    // Loads animation clips from a .glb that has no skin (animation-only export
    // from Mixamo "without skin" option). Retargets channels by node name onto
    // the target skeleton's bones. Bones absent from the source are silently
    // skipped (animation simply doesn't drive those bones).
    // Returns empty vector on parse failure or if no animations are present.
    static std::vector<AnimationClip> LoadClipsAnimOnly(const std::string& path,
                                                        const Skeleton& targetSkeleton);
```

- [ ] **Step 4.2 — Implémenter dans `SkinnedMeshLoader.cpp`**

Après l'implémentation de `LoadClipsRetargeted`, ajouter :
```cpp
std::vector<AnimationClip> SkinnedMeshLoader::LoadClipsAnimOnly(const std::string& path,
                                                                  const Skeleton& targetSkeleton)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        LOG_WARN(Render, "[SkinnedMeshLoader] LoadClipsAnimOnly parse failed for '{}'", path);
        if (data) cgltf_free(data);
        return {};
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        LOG_WARN(Render, "[SkinnedMeshLoader] LoadClipsAnimOnly buffer load failed for '{}'", path);
        cgltf_free(data);
        return {};
    }

    // Build a map node-pointer -> target bone index via name match.
    // We use ALL nodes here (not skin->joints), because animation-only files have no skin.
    auto FindBoneByName = [&](const char* name) -> int {
        if (!name) return -1;
        const std::string n = name;
        for (size_t i = 0; i < targetSkeleton.bones.size(); ++i) {
            if (targetSkeleton.bones[i].name == n) return static_cast<int>(i);
        }
        return -1;
    };

    std::vector<AnimationClip> out;
    out.reserve(data->animations_count);
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation* anim = &data->animations[ai];
        AnimationClip clip;
        clip.name = anim->name ? anim->name : ("anim_" + std::to_string(ai));
        clip.tracks.resize(targetSkeleton.bones.size());
        float maxTime = 0.0f;

        for (cgltf_size ci = 0; ci < anim->channels_count; ++ci) {
            const cgltf_animation_channel* ch = &anim->channels[ci];
            if (!ch->target_node) continue;

            const int boneIdx = FindBoneByName(ch->target_node->name);
            if (boneIdx < 0) continue;  // node absent du target skeleton -> skip

            BoneTracks& trk = clip.tracks[boneIdx];
            const cgltf_animation_sampler* s = ch->sampler;
            if (!s) continue;
            if (s->input && s->input->has_max && s->input->max[0] > maxTime) {
                maxTime = s->input->max[0];
            }
            if (ch->target_path == cgltf_animation_path_type_translation) {
                trk.translation = LoadKeyframes<engine::math::Vec3, 3>(s,
                    [](const float* v){ return engine::math::Vec3{v[0], v[1], v[2]}; });
            } else if (ch->target_path == cgltf_animation_path_type_rotation) {
                trk.rotation = LoadKeyframes<engine::math::Quat, 4>(s,
                    [](const float* v){ return engine::math::Quat{v[0], v[1], v[2], v[3]}; });
            } else if (ch->target_path == cgltf_animation_path_type_scale) {
                trk.scale = LoadKeyframes<engine::math::Vec3, 3>(s,
                    [](const float* v){ return engine::math::Vec3{v[0], v[1], v[2]}; });
            }
        }
        clip.duration = maxTime;
        if (clip.duration > 0.0f) {
            out.push_back(std::move(clip));
        }
    }

    cgltf_free(data);
    return out;
}
```

**Note** : la fonction template helper `LoadKeyframes` est déjà définie dans le namespace anonyme de `SkinnedMeshLoader.cpp` par A. On la réutilise.

- [ ] **Step 4.3 — Commit**

```bash
git add src/client/render/skinned/SkinnedMeshLoader.h src/client/render/skinned/SkinnedMeshLoader.cpp
git commit -m "$(cat <<'EOF'
feat(skinned): add LoadClipsAnimOnly for skin-less Mixamo files (4/14)

Mixamo permet d'exporter une animation sans le mesh + skin (option
"without skin"). Le .glb resultant n'a pas de cgltf_skin — donc
LoadClipsRetargeted bail au check skins_count == 0.

LoadClipsAnimOnly parse les channels d'animation directement et
matchee les target_node->name contre les noms de bones du target
skeleton. Bones absents = skip silencieux.

Reutilisable pour tous les futurs imports animation-only (sous-projets
E/F/G auront beaucoup de clips Mixamo).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5 — `TerrainCollider` (impl `IWorldCollider`)

**Files:**
- Create: `src/client/gameplay/TerrainCollider.h`
- Create: `src/client/gameplay/TerrainCollider.cpp`
- Modify: `CMakeLists.txt` (root) — ajouter à engine_core

**Pré-requis** : exposer un accesseur read-only sur `TerrainRenderer` pour récupérer le heightmap CPU + worldSize + heightScale. Si pas déjà exposé, à ajouter dans cette task.

- [ ] **Step 5.1 — Vérifier l'API existante de `TerrainRenderer`**

Ouvrir `src/client/render/terrain/TerrainRenderer.h`. Chercher s'il y a déjà :
- Un getter du heightmap CPU (buffer 64×64 uint16) — typiquement `const std::vector<uint16_t>& GetHeightmapCpu() const`
- Un getter du worldSize (typiquement 1024 m) et heightScale (typiquement 200 m)

S'ils n'existent pas, les ajouter (méthodes inline) en exposant les membres privés correspondants. Le code post-A devrait avoir au moins `m_heightmapData` (vecteur uint16) et `m_worldSize` / `m_heightScale` (constants ou config-driven).

Si la structure ne s'y prête pas (membres trop private, layout différent), refacto minimal pour exposer un POD `HeightmapDesc { const uint16_t* data; int width; int height; float worldSize; float heightScale; }`.

- [ ] **Step 5.2 — Écrire `TerrainCollider.h`**

Créer `src/client/gameplay/TerrainCollider.h` :
```cpp
#pragma once

#include "src/client/gameplay/CharacterController.h"  // for IWorldCollider

namespace engine::render
{
    class TerrainRenderer;  // forward
}

namespace engine::gameplay
{

// Implémentation de IWorldCollider pour le terrain heightmap.
// Bind sur un TerrainRenderer déjà initialisé ; query le Y au sol via
// interpolation bilinéaire. SweepCapsule = raycast vertical + clamp.
//
// Limitations B.1 :
// - Pas de collision contre props/bâtiments (pas encore présents dans le monde).
// - Pas de mur invisible (le terrain est solide partout sauf altitude).
// - QueryWater renvoie toujours inWater=false (B.2 / B.3 ajoutera).
class TerrainCollider final : public IWorldCollider
{
public:
    TerrainCollider();
    ~TerrainCollider() override;

    // Bind sur le terrain (non-owning). Doit être appelé avant Update.
    void BindTerrain(const engine::render::TerrainRenderer* terrainRenderer);

    // IWorldCollider implementation.
    bool SweepCapsule(const Capsule& capsule,
                      const engine::math::Vec3& startCenter,
                      const engine::math::Vec3& endCenter,
                      SweepHit& outHit) const override;

    // Helper public (utile pour tests + spawn point lookup).
    float GroundHeightAt(float worldX, float worldZ) const;

private:
    const engine::render::TerrainRenderer* m_terrain = nullptr;
};

}  // namespace engine::gameplay
```

- [ ] **Step 5.3 — Écrire `TerrainCollider.cpp`**

Créer `src/client/gameplay/TerrainCollider.cpp` :
```cpp
#include "src/client/gameplay/TerrainCollider.h"
#include "src/client/render/terrain/TerrainRenderer.h"

#include <algorithm>
#include <cmath>

namespace engine::gameplay
{

TerrainCollider::TerrainCollider() = default;
TerrainCollider::~TerrainCollider() = default;

void TerrainCollider::BindTerrain(const engine::render::TerrainRenderer* terrainRenderer)
{
    m_terrain = terrainRenderer;
}

float TerrainCollider::GroundHeightAt(float worldX, float worldZ) const
{
    if (!m_terrain) return 0.0f;

    // Convention : terrain center at (0, 0) on XZ plane, worldSize = total side length in m.
    // heightmap is N×N uint16 values, value/65535 * heightScale = altitude in m.
    // Adapt to the actual TerrainRenderer API found in Step 5.1.
    const float worldSize  = m_terrain->GetWorldSize();
    const float heightScale = m_terrain->GetHeightScale();
    const int   N           = m_terrain->GetHeightmapWidth();
    const uint16_t* data    = m_terrain->GetHeightmapData();
    if (!data || N <= 1) return 0.0f;

    // World [-worldSize/2 ; +worldSize/2] -> heightmap [0 ; N-1]
    const float u = (worldX + worldSize * 0.5f) / worldSize;
    const float v = (worldZ + worldSize * 0.5f) / worldSize;
    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) return 0.0f;  // hors map

    const float xf = u * static_cast<float>(N - 1);
    const float zf = v * static_cast<float>(N - 1);
    const int x0 = static_cast<int>(std::floor(xf));
    const int z0 = static_cast<int>(std::floor(zf));
    const int x1 = std::min(x0 + 1, N - 1);
    const int z1 = std::min(z0 + 1, N - 1);
    const float fx = xf - static_cast<float>(x0);
    const float fz = zf - static_cast<float>(z0);

    auto sampleAt = [&](int x, int z) -> float {
        const uint16_t raw = data[z * N + x];
        return (static_cast<float>(raw) / 65535.0f) * heightScale;
    };

    const float h00 = sampleAt(x0, z0);
    const float h10 = sampleAt(x1, z0);
    const float h01 = sampleAt(x0, z1);
    const float h11 = sampleAt(x1, z1);

    // Bilinear interpolation.
    const float h0 = h00 * (1.0f - fx) + h10 * fx;
    const float h1 = h01 * (1.0f - fx) + h11 * fx;
    return h0 * (1.0f - fz) + h1 * fz;
}

bool TerrainCollider::SweepCapsule(const Capsule& /*capsule*/,
                                    const engine::math::Vec3& startCenter,
                                    const engine::math::Vec3& endCenter,
                                    SweepHit& outHit) const
{
    // MVP B.1 : sweep vertical seulement (descente sous gravité → clamp à Y ground).
    // - Si startCenter.y >= ground && endCenter.y < ground -> hit avec fraction calculée.
    // - Sinon pas de hit (mouvement libre, capsule passe au-dessus du sol).
    //
    // Note : on ignore le radius/height de la capsule pour B.1 (le centre de la capsule
    // est posé directement au-dessus du sol, jambe = height/2 sous le centre).
    // Améliorer en B futur quand on aura des obstacles autres que le terrain.

    const float startY  = startCenter.y;
    const float endY    = endCenter.y;
    const float endGround = GroundHeightAt(endCenter.x, endCenter.z);

    outHit.hit = false;
    outHit.fraction = 1.0f;
    outHit.normal = engine::math::Vec3{0.0f, 1.0f, 0.0f};

    if (endY < endGround && startY >= endGround) {
        // On traverse le sol pendant ce sweep -> calcule la fraction.
        const float startGround = GroundHeightAt(startCenter.x, startCenter.z);
        const float deltaY = endY - startY;       // <=0 (descente)
        const float targetY = endGround;          // altitude à atteindre pour rester au sol
        // Linear interp pour trouver à quelle fraction du sweep on touche le sol.
        // (Approximation : on suppose le sol plat entre start et end. Vrai si la pente
        // est faible et le sweep court — cas B.1.)
        if (std::fabs(deltaY) > 1e-6f) {
            outHit.fraction = std::clamp((targetY - startY) / deltaY, 0.0f, 1.0f);
        } else {
            outHit.fraction = 0.0f;
        }
        outHit.hit = true;
        (void)startGround;  // pour future amélioration (slope detection)
    }
    return outHit.hit;
}

}  // namespace engine::gameplay
```

- [ ] **Step 5.4 — Wire CMake**

Dans `CMakeLists.txt` (root), ajouter `src/client/gameplay/TerrainCollider.cpp` à `engine_core` sources (à côté d'autres `src/client/gameplay/*.cpp` existants — `CharacterController.cpp` est probablement déjà listé).

- [ ] **Step 5.5 — Commit**

```bash
git add src/client/gameplay/TerrainCollider.h src/client/gameplay/TerrainCollider.cpp src/client/render/terrain/TerrainRenderer.h CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(gameplay): TerrainCollider impl IWorldCollider via heightmap (5/14)

CharacterController demande un IWorldCollider pour ses sweep tests.
TerrainCollider est la premiere impl : raycast vertical contre le
heightmap 64x64 du TerrainRenderer, avec interpolation bilineaire pour
GroundHeightAt. SweepCapsule renvoie hit + fraction calculee a la
traversee du sol (approximation lineaire, vraie B.1).

Limitations documentees : pas de collision props/buildings (rien dans
le monde encore), pas de QueryWater (B.2/B.3).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6 — Tests `TerrainCollider`

**Files:**
- Create: `src/client/gameplay/tests/TerrainColliderTests.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 6.1 — Écrire les tests**

Créer `src/client/gameplay/tests/TerrainColliderTests.cpp` :
```cpp
#include "src/client/gameplay/TerrainCollider.h"

#include <cmath>
#include <cstdio>

using engine::gameplay::TerrainCollider;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

    void Test_NoTerrainBound_GroundIsZero()
    {
        TerrainCollider c;
        REQUIRE(Approx(c.GroundHeightAt(0.0f, 0.0f), 0.0f));
        REQUIRE(Approx(c.GroundHeightAt(100.0f, -50.0f), 0.0f));
    }

    void Test_NoTerrainBound_SweepReportsNoHit()
    {
        TerrainCollider c;
        engine::gameplay::IWorldCollider::Capsule cap{};
        engine::gameplay::IWorldCollider::SweepHit hit{};
        bool hitOccurred = c.SweepCapsule(cap, Vec3{0, 10, 0}, Vec3{0, -10, 0}, hit);
        // Sans terrain : GroundHeightAt = 0 ; départ y=10 ; arrivée y=-10.
        // 10 >= 0 (départ au-dessus) && -10 < 0 (arrivée en-dessous) -> HIT.
        REQUIRE(hitOccurred);
        REQUIRE(hit.hit);
        // fraction = (0 - 10) / (-10 - 10) = 0.5
        REQUIRE(Approx(hit.fraction, 0.5f));
    }

    void Test_NoTerrainBound_AscendingSweep_NoHit()
    {
        TerrainCollider c;
        engine::gameplay::IWorldCollider::Capsule cap{};
        engine::gameplay::IWorldCollider::SweepHit hit{};
        bool hitOccurred = c.SweepCapsule(cap, Vec3{0, 1, 0}, Vec3{0, 5, 0}, hit);
        // Ascending : ne devrait pas hit le sol.
        REQUIRE(!hitOccurred);
        REQUIRE(!hit.hit);
        REQUIRE(Approx(hit.fraction, 1.0f));
    }

    void Test_NoTerrainBound_BothAboveGround_NoHit()
    {
        TerrainCollider c;
        engine::gameplay::IWorldCollider::Capsule cap{};
        engine::gameplay::IWorldCollider::SweepHit hit{};
        bool hitOccurred = c.SweepCapsule(cap, Vec3{0, 5, 0}, Vec3{2, 5, 2}, hit);
        REQUIRE(!hitOccurred);
        REQUIRE(Approx(hit.fraction, 1.0f));
    }
}

int main()
{
    Test_NoTerrainBound_GroundIsZero();
    Test_NoTerrainBound_SweepReportsNoHit();
    Test_NoTerrainBound_AscendingSweep_NoHit();
    Test_NoTerrainBound_BothAboveGround_NoHit();
    return g_failed == 0 ? 0 : 1;
}
```

(Tests « avec terrain bound » nécessitent d'instancier un `TerrainRenderer` réel, ce qui demande Vulkan device + boot. Out of scope pour le test unitaire. Validation visuelle au smoke test final.)

- [ ] **Step 6.2 — Enregistrer dans CMake**

Dans `src/CMakeLists.txt`, ajouter :
```cmake
lcdlln_add_simple_test(terrain_collider_tests src/client/gameplay/tests/TerrainColliderTests.cpp)
```

- [ ] **Step 6.3 — Commit**

```bash
git add src/client/gameplay/tests/TerrainColliderTests.cpp src/CMakeLists.txt
git commit -m "test(gameplay): TerrainCollider sweep fallback Y=0 + edge cases (6/14)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7 — Convertir 4 FBX → glb + commit

**Files:**
- Create: `game/data/models/avatars/y_bot_run/y_bot_run.glb` + README.md
- Create: `game/data/models/avatars/y_bot_jump/y_bot_jump.glb` + README.md
- Create: `game/data/models/avatars/y_bot_fall/y_bot_fall.glb` + README.md
- Create: `game/data/models/avatars/y_bot_land/y_bot_land.glb` + README.md

- [ ] **Step 7.1 — Convertir les 4 FBX**

```powershell
.\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot_run -Category avatars -SourceFbx "Fast Run"
.\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot_jump -Category avatars -SourceFbx "Jump"
.\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot_fall -Category avatars -SourceFbx "falling idle"
.\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot_land -Category avatars -SourceFbx "hard landing"
```

Expected : 4 `.glb` créés sous `game/data/models/avatars/<entity>/<entity>.glb`.

Vérifier les tailles :
- `y_bot_run.glb` : ~1.9 MB (with-skin Fast Run)
- `y_bot_jump.glb` : ~2.2 MB (with-skin Jump)
- `y_bot_fall.glb` : ~400 KB (animation-only — pas de mesh)
- `y_bot_land.glb` : ~500 KB (animation-only — pas de mesh)

Smoke-check chaque .glb :
```powershell
foreach ($name in @("y_bot_run", "y_bot_jump", "y_bot_fall", "y_bot_land")) {
    $bytes = [System.IO.File]::ReadAllBytes("game/data/models/avatars/$name/$name.glb")
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes[0..3])
    if ($magic -ne "glTF") { throw "$name not a valid .glb (magic=$magic)" }
    Write-Host "$name OK ($($bytes.Length) bytes)"
}
```

- [ ] **Step 7.2 — README pour chaque asset**

Créer `game/data/models/avatars/y_bot_run/README.md` :
```markdown
# Y Bot — Run (Fast Run, Mixamo)

**Source** : Mixamo (Adobe), personnage Y Bot, animation Fast Run, with-skin.
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_run -Category avatars -SourceFbx "Fast Run"`.

## Animations

- `mixamo.com` → renommé `Run` au load par `Engine.cpp`.

## Usage runtime

Chargé via `SkinnedMeshLoader::LoadClipsRetargeted` (file with-skin → loader standard).
```

Créer `game/data/models/avatars/y_bot_jump/README.md` (idem mais Jump).

Créer `game/data/models/avatars/y_bot_fall/README.md` :
```markdown
# Y Bot — Fall (falling idle, Mixamo)

**Source** : Mixamo (Adobe), animation falling idle, **animation-only** (no skin).
**Téléchargé le** : 2026-05-18.
**Conversion** : `fbx_to_gltf.ps1 -EntityName y_bot_fall -Category avatars -SourceFbx "falling idle"`.

## Animations

- `mixamo.com` → renommé `Fall` au load par `Engine.cpp`.

## Usage runtime

Le fichier est animation-only (pas de mesh ni skin). Chargé via le helper dédié
`SkinnedMeshLoader::LoadClipsAnimOnly` (Task 4 de B.1) qui parse les channels
d'animation directement et retargete sur le skeleton de Y Bot par nom de bone.
```

Créer `game/data/models/avatars/y_bot_land/README.md` (idem mais Land).

- [ ] **Step 7.3 — Commit**

```bash
git add game/data/models/avatars/y_bot_run/ game/data/models/avatars/y_bot_jump/ game/data/models/avatars/y_bot_fall/ game/data/models/avatars/y_bot_land/
git commit -m "$(cat <<'EOF'
feat(asset): import Mixamo Run / Jump / Fall / Land clips (7/14)

4 nouveaux .glb pour la state machine B.1 :
- y_bot_run.glb (~1.9 MB, Fast Run with-skin)
- y_bot_jump.glb (~2.2 MB, Jump with-skin)
- y_bot_fall.glb (~400 KB, falling idle animation-only)
- y_bot_land.glb (~500 KB, hard landing animation-only)

Les fichiers animation-only seront charges via LoadClipsAnimOnly
(Task 4) qui ne necessite pas de cgltf_skin.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8 — Refacto `OrbitalCameraController` (caméra pure)

**Files:**
- Modify: `src/client/render/Camera.h`
- Modify: `src/client/render/Camera.cpp`

Objectif : retirer tout ce qui concerne le mouvement (lecture WASD, walkBob, gravité verticale, collision sol), garder uniquement souris + molette + suivi de cible.

- [ ] **Step 8.1 — Refacto `Camera.h`**

Dans la classe `OrbitalCameraController` :

**Supprimer** :
- `static constexpr float kWalkSpeed`, `kRunSpeed`, `kTargetEyeHeight`
- L'enum `LocomotionState`
- `LocomotionState GetLocomotionState() const { return m_locomotion; }`
- `float GetWalkBobPhaseRad() const { return m_walkBobPhase; }`
- Le param `applyKeyboardMove`, `groundYAtTarget`, `speedMultiplier` de `Update`
- Les membres privés : `m_locomotion`, `m_walkBobPhase`, `m_verticalVelocityY`, `m_verticalOffsetY`, `m_isCrouching`

**Garder / modifier** :
- `kPitchMin`, `kPitchMax`, `kDistanceMin/Max/Default`, `kZoomStep` — gardés.
- `SetTargetPosition(const Vec3&)` — gardé.
- `GetTargetPosition() const` — gardé.
- `GetDistance() const` — gardé.
- `m_target` — gardé (mais maintenant fixé par l'extérieur via SetTargetPosition).
- `m_distance` — gardé.
- `m_initialized` — gardé.

**Modifier signature `Update`** :
```cpp
// Avant :
// void Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel, bool invertY,
//     MovementLayout layout, bool applyMouseLook, bool applyKeyboardMove, Camera& camera,
//     float groundYAtTarget = 0.0f, float speedMultiplier = 1.0f);

// Après :
void Update(engine::platform::Input& input, double dt, float mouseSensitivityRadPerPixel,
            bool invertY, bool applyMouseLook, Camera& camera);
```

**Ajouter getters pour Engine.cpp** :
```cpp
// Used by Engine to project keyboard input onto the XZ plane in camera-relative space.
engine::math::Vec3 GetForwardXZ() const;  // returns (sin(yaw), 0, cos(yaw)) normalized
engine::math::Vec3 GetRightXZ() const;    // returns (cos(yaw), 0, -sin(yaw)) normalized
float GetYawRad() const;
```

- [ ] **Step 8.2 — Refacto `Camera.cpp::OrbitalCameraController::Update`**

Réécrire la méthode en supprimant tout ce qui concerne le mouvement. Conserver uniquement (pseudo-code) :

1. Initialisation : si `!m_initialized`, set camera state initial.
2. **Souris** (uniquement si `applyMouseLook && input.IsMouseButtonDown(MouseButton::Right)`) :
   - Lire `MouseDeltaX()` / `MouseDeltaY()`.
   - Update `camera.yaw += dx * mouseSensitivityRadPerPixel`.
   - Update `camera.pitch = clamp(camera.pitch - dy * mouseSensitivityRadPerPixel * (invertY ? -1 : +1), kPitchMin, kPitchMax)`.
3. **Molette** : zoom in/out.
   - `m_distance = clamp(m_distance - input.MouseScrollDelta() * kZoomStep, kDistanceMin, kDistanceMax)`.
4. **Calcul position caméra** (suit `m_target`) :
   - `camera.position = m_target + offset_orbital(camera.yaw, camera.pitch, m_distance)`.
   - Formule offset : `Vec3{ -sin(yaw)*cos(pitch), sin(pitch), -cos(yaw)*cos(pitch) } * m_distance` (à ajuster selon convention engine).

**Retirer complètement** :
- Toute lecture WASD / ZQSD / Shift / Space.
- `m_walkBobPhase += ...`.
- `m_verticalVelocityY += gravity * dt`, `m_verticalOffsetY += ...`.
- Détection accroupi (m_isCrouching).
- Collision sol vertical (sera fait par CharacterController).
- Calcul de m_target (sera passé par SetTargetPosition depuis Engine).

- [ ] **Step 8.3 — Implémenter les nouveaux getters**

Ajouter à `Camera.cpp` :
```cpp
engine::math::Vec3 OrbitalCameraController::GetForwardXZ() const
{
    // Camera yaw is stored in Camera, not here. Forward in XZ plane.
    // Adapter à l'API réelle (peut nécessiter de passer la caméra en param ou de stocker un yaw local).
    // Si Camera::yaw est exposé via getter, c'est trivial. Sinon stocker un m_lastYaw lors de Update.
    const float yaw = m_lastYaw;  // see Step 8.2
    return engine::math::Vec3{std::sin(yaw), 0.0f, std::cos(yaw)};
}

engine::math::Vec3 OrbitalCameraController::GetRightXZ() const
{
    const float yaw = m_lastYaw;
    return engine::math::Vec3{std::cos(yaw), 0.0f, -std::sin(yaw)};
}

float OrbitalCameraController::GetYawRad() const { return m_lastYaw; }
```

Ajouter membre `m_lastYaw = 0.0f` dans `Camera.h` (private) et le mettre à jour dans `Update` après calcul du yaw.

- [ ] **Step 8.4 — Mettre à jour les call sites de `Update`**

Dans `Engine.cpp`, chercher `OrbitalCameraController::Update` et adapter l'appel à la nouvelle signature (retirer `applyKeyboardMove`, `groundYAtTarget`, `speedMultiplier`). C'est nécessaire pour que le code compile entre Step 8.4 et Task 9.

Pour Task 8, garder le comportement actuel : le call site appelle juste la version simplifiée. Le mouvement n'est PAS encore branché sur CharacterController (Task 9 le fait).

Build sanity check : à ce stade, `lcdlln.exe` ne devrait plus pouvoir bouger (puisque OrbitalCameraController ne lit plus l'input WASD), mais compile.

- [ ] **Step 8.5 — Commit**

```bash
git add src/client/render/Camera.h src/client/render/Camera.cpp src/client/app/Engine.cpp
git commit -m "$(cat <<'EOF'
refactor(camera): OrbitalCameraController retrograde en camera pure (8/14)

Supprime de OrbitalCameraController :
- Lecture inputs WASD/ZQSD/Shift/Space
- Walk-bob synthetique (m_walkBobPhase, kTargetEyeHeight)
- Velocite verticale (m_verticalVelocityY, m_verticalOffsetY)
- Detection accroupi (m_isCrouching)
- Enum LocomotionState (deplace dans Engine::AvatarLocomotionState)
- Calcul de m_target (sera fait par CharacterController via SetTargetPosition)

Garde uniquement :
- Souris : yaw/pitch via clic droit + sensibilite
- Molette : zoom
- Calcul camera.position = m_target + offset_orbital(yaw,pitch,distance)

Nouveaux getters : GetForwardXZ / GetRightXZ / GetYawRad (utilises par
Engine pour projeter l'input clavier dans le repere camera).

A ce stade le joueur ne peut plus bouger (Task 9 branche CharacterController).
Le build compile mais le perso reste fige a (0,0,0).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9 — Wire `CharacterController` + `TerrainCollider` dans `Engine.cpp`

**Files:**
- Modify: `src/client/app/Engine.h`
- Modify: `src/client/app/Engine.cpp`

À ce stade, on instancie les deux + on construit `MoveInput` par frame + on call `cc.Update`, mais la state machine reste celle de A (3 états). L'étendre en 7 états vient en Task 11.

- [ ] **Step 9.1 — Ajouter les membres dans `Engine.h`**

Dans la section private d'Engine, après les membres skinned avatar de A, ajouter :
```cpp
    // Sous-projet B.1 : physics + collision.
    engine::gameplay::CharacterController m_characterController;
    engine::gameplay::TerrainCollider     m_terrainCollider;
    float                                  m_avatarYaw = 0.0f;
```

Includes à ajouter en haut :
```cpp
#include "src/client/gameplay/CharacterController.h"
#include "src/client/gameplay/TerrainCollider.h"
```

- [ ] **Step 9.2 — Init au boot**

Dans le bloc Task 15 d'A (autour de la ligne 3750+, à la suite du chargement des clips Idle/StartWalking), ajouter :
```cpp
// B.1 : init TerrainCollider + CharacterController (apres terrain ready).
m_terrainCollider.BindTerrain(&m_terrainRenderer);
engine::gameplay::CharacterController::Config ccCfg{};
ccCfg.walkSpeed      = m_cfg.GetFloat("player.movement.walk_speed",      5.0f);
ccCfg.runSpeed       = m_cfg.GetFloat("player.movement.run_speed",       9.0f);
ccCfg.gravity        = m_cfg.GetFloat("player.movement.gravity",       -20.0f);
ccCfg.jumpSpeed      = m_cfg.GetFloat("player.movement.jump_speed",      9.0f);
ccCfg.coyoteTimeSec  = m_cfg.GetFloat("player.movement.coyote_time_s",   0.1f);
ccCfg.jumpBufferSec  = m_cfg.GetFloat("player.movement.jump_buffer_s",   0.1f);
ccCfg.capsule.radius = 0.3f;
ccCfg.capsule.height = 1.8f;
m_characterController = engine::gameplay::CharacterController(ccCfg);

// Position de spawn : centre de la map, posé sur le sol.
const float spawnX = 0.0f, spawnZ = 0.0f;
const float spawnY = m_terrainCollider.GroundHeightAt(spawnX, spawnZ) + 0.9f;  // half-capsule height
m_characterController.Init(engine::math::Vec3{spawnX, spawnY, spawnZ});
LOG_INFO(Render, "[Engine] CharacterController spawned at ({}, {}, {})", spawnX, spawnY, spawnZ);
```

(Le nom exact `m_terrainRenderer` à valider selon ce qui existe dans Engine.h. Si c'est `m_terrain` ou autre, adapter.)

- [ ] **Step 9.3 — Build `MoveInput` par frame**

Ajouter une fonction libre statique dans Engine.cpp (avant `Engine::Update` ou en haut du fichier) :
```cpp
namespace
{
    engine::gameplay::CharacterController::MoveInput BuildMoveInput(
        const engine::platform::Input& input,
        const engine::render::OrbitalCameraController& camera)
    {
        engine::gameplay::CharacterController::MoveInput out{};

        const engine::math::Vec3 forward = camera.GetForwardXZ();
        const engine::math::Vec3 right   = camera.GetRightXZ();

        engine::math::Vec3 dir{0, 0, 0};
        if (input.IsDown(engine::platform::Key::W) || input.IsDown(engine::platform::Key::Z))
            dir.x += forward.x, dir.z += forward.z;
        if (input.IsDown(engine::platform::Key::S))
            dir.x -= forward.x, dir.z -= forward.z;
        if (input.IsDown(engine::platform::Key::D))
            dir.x += right.x, dir.z += right.z;
        if (input.IsDown(engine::platform::Key::A) || input.IsDown(engine::platform::Key::Q))
            dir.x -= right.x, dir.z -= right.z;

        const float lenSq = dir.x * dir.x + dir.z * dir.z;
        if (lenSq > 0.0f) {
            const float invLen = 1.0f / std::sqrt(lenSq);
            out.moveDirXZ = engine::math::Vec3{dir.x * invLen, 0.0f, dir.z * invLen};
        }

        out.run         = input.IsDown(engine::platform::Key::Shift);
        out.jumpPressed = input.WasPressed(engine::platform::Key::Space);
        // swim/fly hors B.1.
        return out;
    }
}
```

- [ ] **Step 9.4 — Call cc.Update + push position to camera**

Dans `Engine::Update` (autour de l'ancienne section où OrbitalCameraController::Update lisait l'input), juste AVANT cameraController.Update :
```cpp
// B.1 : physics first, camera follows.
const auto moveInput = BuildMoveInput(m_input, m_orbitalCamera);
m_characterController.Update(static_cast<float>(dt), moveInput, m_terrainCollider);
const engine::math::Vec3 pos = m_characterController.GetPosition();
m_orbitalCamera.SetTargetPosition(pos);

// Mettre à jour le yaw de l'avatar (modèle) selon la direction de mouvement.
if (moveInput.moveDirXZ.x != 0.0f || moveInput.moveDirXZ.z != 0.0f) {
    m_avatarYaw = std::atan2(moveInput.moveDirXZ.x, moveInput.moveDirXZ.z);
}
```

Conserver l'appel `m_orbitalCamera.Update(...)` mais avec la nouvelle signature simplifiée de Task 8.

- [ ] **Step 9.5 — Build the model matrix dans le bloc per-frame**

Dans le bloc per-frame skinned avatar (avant l'appel `m_skinnedRenderer.Record`), remplacer le calcul de la model matrix par :
```cpp
// B.1 : model matrix = T(cc.position) * R_y(avatarYaw + π) (180° pour orientation 3ᵉ personne).
const engine::math::Vec3 pos = m_characterController.GetPosition();
// Position du modèle : pieds au sol, le centre de la capsule est à pieds + halfHeight.
const engine::math::Vec3 feetPos{pos.x, pos.y - 0.9f, pos.z};  // halfHeight = 0.9
const engine::math::Mat4 modelMat =
    engine::math::Mat4::Translate(feetPos) *
    engine::math::Mat4::RotateY(m_avatarYaw + 3.14159265f);
const float* modelMatrixData = modelMat.m;
```

Passer `modelMatrixData` au `Record(...)` à la place de l'ancien `rs.objectModelMatrix`.

**Important** : supprimer aussi le bobY synthétique (le delta XZ check + la composition `+ bobY` qui existait en A). L'animation porte sa propre oscillation Y, plus besoin du synthétique.

- [ ] **Step 9.6 — Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "$(cat <<'EOF'
feat(client): wire CharacterController + TerrainCollider in Engine (9/14)

Branche le CharacterController existant (non utilise jusqu'ici) :
- Init au boot avec config depuis config.json (defaults sensibles fallback)
- BuildMoveInput par frame depuis input clavier projete dans repere camera
- cc.Update(dt, moveInput, &m_terrainCollider) chaque frame
- m_orbitalCamera.SetTargetPosition(cc.GetPosition()) -> camera suit le perso
- Avatar yaw suit la direction de mouvement (atan2 moveDirXZ)
- Model matrix = T(feetPos) * R_y(yaw + pi) (orientation 180 conservee)

Supprime le bobY synthetique (l'anim porte sa propre oscillation).

State machine reste celle de A (3 etats) pour cette task — Task 11
l'etendra en 7 etats.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10 — Config `config.json` section `player.movement`

**Files:**
- Modify: `config.json`

- [ ] **Step 10.1 — Ajouter la section**

Dans `config.json`, ajouter au niveau racine (ou dans une section logique existante) :
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

Vérifier que la syntaxe JSON globale reste valide (virgule au bon endroit selon la position dans le fichier).

- [ ] **Step 10.2 — Commit**

```bash
git add config.json
git commit -m "config: add player.movement section for CharacterController tuning (10/14)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11 — Étendre l'enum `AvatarLocomotionState` à 7 états + state machine + crossfade

**Files:**
- Modify: `src/client/app/Engine.h`
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 11.1 — Étendre l'enum dans Engine.h**

```cpp
// Avant :
// enum class AvatarLocomotionState { Idle, StartWalking, Walking };

// Après :
enum class AvatarLocomotionState
{
    Idle,
    StartWalking,
    Walk,         // renomme Walking -> Walk (plus court, cohérent avec Mixamo)
    Run,
    Jump,
    Fall,
    Land
};
```

Ajouter le membre AnimationCrossfade :
```cpp
    engine::render::skinned::AnimationCrossfade m_avatarCrossfade;
```

- [ ] **Step 11.2 — Ajouter helper `StateToClipName`**

Dans Engine.cpp, fonction libre statique :
```cpp
namespace
{
    const char* StateToClipName(engine::Engine::AvatarLocomotionState s)
    {
        switch (s) {
            case engine::Engine::AvatarLocomotionState::Idle:         return "Idle";
            case engine::Engine::AvatarLocomotionState::StartWalking: return "StartWalking";
            case engine::Engine::AvatarLocomotionState::Walk:         return "Walk";
            case engine::Engine::AvatarLocomotionState::Run:          return "Run";
            case engine::Engine::AvatarLocomotionState::Jump:         return "Jump";
            case engine::Engine::AvatarLocomotionState::Fall:         return "Fall";
            case engine::Engine::AvatarLocomotionState::Land:         return "Land";
        }
        return "Idle";
    }

    bool ClipLoops(engine::Engine::AvatarLocomotionState s)
    {
        // Idle / Walk / Run / Fall loopent. StartWalking / Jump / Land sont one-shot.
        return s == engine::Engine::AvatarLocomotionState::Idle
            || s == engine::Engine::AvatarLocomotionState::Walk
            || s == engine::Engine::AvatarLocomotionState::Run
            || s == engine::Engine::AvatarLocomotionState::Fall;
    }
}
```

- [ ] **Step 11.3 — Implémenter la state machine étendue dans `Engine::Update`**

**Important architectural note** : la state machine de A vivait dans le lambda FrameGraph "Geometry" (qui ne tournait que pendant Render). Pour B.1 on la déplace dans `Engine::Update` (juste après `cc.Update` de Task 9.4). Ça permet à la state machine d'accéder facilement à `moveInput` (déjà construit) et `cc.IsGrounded()` sans plumbing exotique. Le lambda Geometry, lui, devient juste : `Sample crossfade → render`.

Stocker la state-machine output dans des membres pour que le lambda Geometry le lise :
```cpp
// In Engine.h, ajouter aux membres B.1 :
engine::gameplay::CharacterController::MoveInput m_lastMoveInput{};
```

Dans `Engine::Update`, juste après le block Task 9 (cc.Update + camera follow), ajouter :
```cpp
// B.1 : state machine driven par CharacterController + input.
m_lastMoveInput = moveInput;  // store for the Geometry lambda
const bool grounded = m_characterController.IsGrounded();
const bool moving = (moveInput.moveDirXZ.x != 0.0f || moveInput.moveDirXZ.z != 0.0f);

const auto now = std::chrono::steady_clock::now();
const float nowSec = std::chrono::duration<float>(now.time_since_epoch()).count();
const float stateElapsed = std::chrono::duration<float>(now - m_avatarLocoStateEnterTime).count();

const engine::render::skinned::AnimationClip* startWalkClip =
    m_playerSkinnedMesh ? m_playerSkinnedMesh->FindClip("StartWalking") : nullptr;
const engine::render::skinned::AnimationClip* jumpClip =
    m_playerSkinnedMesh ? m_playerSkinnedMesh->FindClip("Jump") : nullptr;
const engine::render::skinned::AnimationClip* landClip =
    m_playerSkinnedMesh ? m_playerSkinnedMesh->FindClip("Land") : nullptr;

AvatarLocomotionState newState = m_avatarLocoState;
if (grounded) {
    switch (m_avatarLocoState) {
        case AvatarLocomotionState::Idle:
            if (moveInput.jumpPressed)        newState = AvatarLocomotionState::Jump;
            else if (moving)                  newState = AvatarLocomotionState::StartWalking;
            break;
        case AvatarLocomotionState::StartWalking:
            if (!moving)                      newState = AvatarLocomotionState::Idle;
            else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
            else if (startWalkClip && stateElapsed >= startWalkClip->duration)
                newState = (moveInput.run ? AvatarLocomotionState::Run : AvatarLocomotionState::Walk);
            break;
        case AvatarLocomotionState::Walk:
            if (!moving)                      newState = AvatarLocomotionState::Idle;
            else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
            else if (moveInput.run)           newState = AvatarLocomotionState::Run;
            break;
        case AvatarLocomotionState::Run:
            if (!moving)                      newState = AvatarLocomotionState::Idle;
            else if (moveInput.jumpPressed)   newState = AvatarLocomotionState::Jump;
            else if (!moveInput.run)          newState = AvatarLocomotionState::Walk;
            break;
        case AvatarLocomotionState::Jump:
            // Jump can transition to Fall once takeoff is done (clip elapsed) OR ungrounded.
            if (jumpClip && stateElapsed >= jumpClip->duration * 0.4f)  // takeoff = first 40%
                newState = AvatarLocomotionState::Fall;
            break;
        case AvatarLocomotionState::Fall:
            // Touch ground -> Land.
            newState = AvatarLocomotionState::Land;
            break;
        case AvatarLocomotionState::Land:
            if (landClip && stateElapsed >= landClip->duration) {
                if (!moving)                  newState = AvatarLocomotionState::Idle;
                else if (moveInput.run)       newState = AvatarLocomotionState::Run;
                else                          newState = AvatarLocomotionState::Walk;
            }
            break;
    }
} else {
    // Not grounded : Jump (takeoff) -> Fall after takeoff duration, sinon Fall direct.
    if (m_avatarLocoState == AvatarLocomotionState::Jump) {
        if (jumpClip && stateElapsed >= jumpClip->duration * 0.4f)
            newState = AvatarLocomotionState::Fall;
    } else {
        newState = AvatarLocomotionState::Fall;
    }
}

if (newState != m_avatarLocoState) {
    m_avatarLocoState = newState;
    m_avatarLocoStateEnterTime = now;

    // Trigger crossfade.
    const char* clipName = StateToClipName(newState);
    const engine::render::skinned::AnimationClip* newClip = m_playerSkinnedMesh->FindClip(clipName);
    if (newClip) {
        m_avatarCrossfade.Play(*newClip, ClipLoops(newState), nowSec);
    }
}
```

- [ ] **Step 11.4 — Sample crossfade dans le lambda FrameGraph "Geometry"**

Le lambda Geometry (qui dessine l'avatar skinné, défini dans le bloc per-frame d'A) devient minimal : il échantillonne le crossfade au temps courant et passe à Record. Plus de state machine ici (elle est en Update — Step 11.3).

Remplacer dans le lambda :
```cpp
// Avant (A) — state machine + SamplePose direct dans le lambda :
// const float t = std::fmod(elapsed, walkClip->duration);
// auto locals = engine::render::skinned::AnimationSampler::SamplePose(skel, *walkClip, t);

// Après (B.1) — juste sample du crossfade qui contient déjà la pose blendée :
const float nowSec = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
auto locals = m_avatarCrossfade.Sample(m_playerSkinnedMesh->skeleton, nowSec);
auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(m_playerSkinnedMesh->skeleton, locals);
auto finals = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(m_playerSkinnedMesh->skeleton, globals);
```

Le code obsolète de la state machine A (delta XZ, FindClip("mixamo.com"), etc.) doit être SUPPRIMÉ du lambda — il vit maintenant dans `Engine::Update` via Step 11.3.

- [ ] **Step 11.5 — Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "$(cat <<'EOF'
feat(client): state machine 7 etats + crossfade entre clips (11/14)

Etend AvatarLocomotionState a 7 etats :
- Idle, StartWalking, Walk, Run (Walking renomme Walk pour coherence)
- Jump, Fall, Land

Transitions driven par CharacterController.IsGrounded + input.jumpPressed
+ input.run + moveDirXZ.LengthSq.

Jump -> Fall apres 40% de la clip Jump (= takeoff). Fall -> Land au touch
ground. Land -> Idle/Walk/Run selon input apres fin de clip.

Crossfade entre etats via AnimationCrossfade (0.15s par default). Plus
de hard cuts.

Sample via m_avatarCrossfade.Sample (au lieu de SamplePose direct) qui
gere automatiquement le blend si crossfade en cours.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12 — Charger les 4 nouveaux clips au boot

**Files:**
- Modify: `src/client/app/Engine.cpp`

- [ ] **Step 12.1 — Étendre le bloc de chargement de clips**

Dans le bloc qui charge Idle + StartWalking via `LoadClipsRetargeted` (livré par A polish), AJOUTER après :
```cpp
// B.1 : load 4 new clips (Run with-skin, Jump with-skin, Fall + Land animation-only).
auto loadWithSkin = [&](const char* path, const char* renameTo) {
    auto clips = engine::render::skinned::SkinnedMeshLoader::LoadClipsRetargeted(path, m_playerSkinnedMesh->skeleton);
    for (auto& c : clips) {
        if (c.duration > 0.0f && c.name == "mixamo.com") {
            c.name = renameTo;
            m_playerSkinnedMesh->clips.push_back(std::move(c));
            return true;
        }
    }
    return false;
};
auto loadAnimOnly = [&](const char* path, const char* renameTo) {
    auto clips = engine::render::skinned::SkinnedMeshLoader::LoadClipsAnimOnly(path, m_playerSkinnedMesh->skeleton);
    for (auto& c : clips) {
        if (c.duration > 0.0f && c.name == "mixamo.com") {
            c.name = renameTo;
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

if (loadWithSkin(runPath.c_str(),  "Run"))  LOG_INFO(Render, "[Engine] Run clip loaded from '{}'", runPath);
else                                         LOG_WARN(Render, "[Engine] Run clip not loaded from '{}'", runPath);
if (loadWithSkin(jumpPath.c_str(), "Jump")) LOG_INFO(Render, "[Engine] Jump clip loaded from '{}'", jumpPath);
else                                         LOG_WARN(Render, "[Engine] Jump clip not loaded from '{}'", jumpPath);
if (loadAnimOnly(fallPath.c_str(), "Fall")) LOG_INFO(Render, "[Engine] Fall clip loaded from '{}'", fallPath);
else                                         LOG_WARN(Render, "[Engine] Fall clip not loaded from '{}'", fallPath);
if (loadAnimOnly(landPath.c_str(), "Land")) LOG_INFO(Render, "[Engine] Land clip loaded from '{}'", landPath);
else                                         LOG_WARN(Render, "[Engine] Land clip not loaded from '{}'", landPath);

// Initial state : Idle. Lance la première animation.
const engine::render::skinned::AnimationClip* idleClip = m_playerSkinnedMesh->FindClip("Idle");
if (idleClip) {
    const float nowSec = std::chrono::duration<float>(std::chrono::steady_clock::now().time_since_epoch()).count();
    m_avatarCrossfade.Play(*idleClip, /*loops=*/ true, nowSec);
    m_avatarLocoStateEnterTime = std::chrono::steady_clock::now();
}
```

- [ ] **Step 12.2 — Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "$(cat <<'EOF'
feat(client): load Run/Jump/Fall/Land clips at boot (12/14)

Charge les 4 nouveaux clips B.1 :
- Run via LoadClipsRetargeted (with-skin, Fast Run)
- Jump via LoadClipsRetargeted (with-skin)
- Fall via LoadClipsAnimOnly (animation-only, falling idle)
- Land via LoadClipsAnimOnly (animation-only, hard landing)

Demarre la premiere animation (Idle loop) au boot.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13 — Smoke test visuel + screenshot

**Files:** aucun changement code. Validation manuelle.

- [ ] **Step 13.1 — Pull la branche localement + build CI artifact**

Quand CI termine sur le commit final, télécharger l'artifact Windows. OU build local si Vulkan SDK installé.

- [ ] **Step 13.2 — Lancer le smoke test**

Lancer `lcdlln.exe`. Login → CharacterSelect → Jouer → EnterWorld.

Vérifier les 7 critères du spec §11 :
1. **Locomotion** : W/A/S/D → perso se déplace, joue Walk en boucle.
2. **Run** : Shift + W → perso accélère, joue Run. Relâcher Shift → retour Walk avec crossfade visible (pas de snap).
3. **Jump** : Espace → perso saute (anim Jump → airtime Fall → atterrissage Land). Retombe au bon Y.
4. **Idle** : relâcher tout → Idle en boucle, oscillation subtile.
5. **Orientation** : D → perso pivote pour faire face à droite caméra.
6. **Terrain following** : se déplacer sur le terrain → perso suit le relief.
7. **No regression** : terrain visible, pas de validation Vulkan warning, FPS stable.

- [ ] **Step 13.3 — Screenshot + log**

Capturer un screenshot du perso en mid-Jump (Win+Shift+S). Capturer le log file complet. Joindre à la description PR.

- [ ] **Step 13.4 — Si bugs : itérer**

Symptômes possibles + causes :
- Perso glisse sans bouger les jambes → bobY supprimé mais anim Walk n'a pas assez d'oscillation Y. Solution : revérifier que crossfade.Sample renvoie bien la pose Walk (debug log).
- Perso tombe à travers le terrain → bug TerrainCollider.SweepCapsule. Debug : log la valeur de hit.fraction.
- Crossfade non visible (snap entre clips) → bug AnimationCrossfade. Vérifier que Play est bien appelé au state change.
- Perso glisse vers le haut quand on saute → gravity wrong sign ou jumpSpeed inversé.
- Run vitesse identique à Walk → input.run pas correctement lu OU CharacterController ne respecte pas run.

Pour chaque symptôme : commit fix isolé.

---

## Task 14 — Update `CODEBASE_MAP.md` §14.5

**Files:**
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 14.1 — Mettre à jour la section §14.5**

Trouver §14.5 (livrée par A). Mettre à jour :

1. **Fichiers clés** : ajouter :
   - `src/client/gameplay/TerrainCollider.h/.cpp` — Impl IWorldCollider via heightmap query bilinéaire.
   - `src/client/render/skinned/AnimationCrossfade.h/.cpp` — Blend TRS lerp/slerp entre 2 poses sur 0.15s.
   - `src/shared/math/Math.h` — Mat4::Identity/Translate/RotateY ajoutés.
   - `game/data/models/avatars/y_bot_{run,jump,fall,land}/*.glb` — 4 nouveaux clips.

2. **State machine** : remplacer le tableau A (3 états Idle/StartWalking/Walking) par le tableau B.1 (7 états + transitions).

3. **Limites assumées** : retirer celles que B.1 résout (delta XZ → CharacterController, hard cuts → crossfade, no Run → Run), garder celles qui restent (FIF race single-avatar, pas de surface modulation → B.2, pas de remote players → B.3).

4. **Ajouter** une sous-section « 14.5.1 Locomotion physics » qui pointe sur `CharacterController.h/.cpp` (existait déjà mais non utilisé, branché par B.1) + `TerrainCollider.h/.cpp` (nouveau).

5. **Mettre à jour §14** (Vue 3ème personne) :
   - Remplacer le bullet « bobY synthétique toujours appliqué » par « avatar driven par CharacterController, vraie physique de saut + grounding ».
   - Mentionner que OrbitalCameraController est désormais caméra pure (plus de mouvement).

- [ ] **Step 14.2 — Commit**

```bash
git add CODEBASE_MAP.md
git commit -m "$(cat <<'EOF'
docs(map): update §14.5 with B.1 state machine + CharacterController (14/14)

Cloture le sous-projet B.1. CODEBASE_MAP.md documente :
- State machine etendue a 7 etats avec crossfade 0.15s
- TerrainCollider impl IWorldCollider via heightmap query bilineaire
- CharacterController enfin branche (n'etait jamais utilise)
- OrbitalCameraController retrograde en camera pure
- 4 nouveaux clips Mixamo (Run/Jump/Fall/Land)
- Mat4 helpers Identity/Translate/RotateY

Resolves limites assumees A : delta XZ remplace, hard cuts -> crossfade,
Run distinct, jump fonctionnel. Reste : FIF race single-avatar (B.3),
surface modulation (B.2), remote players (B.3).

Deploiement : ✅ client uniquement, pas de redéploiement serveur.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Vérification finale & PR

- [ ] **Tous les tests verts (CI Linux)**

```
ctest --test-dir build --output-on-failure
```

5 nouveaux tests : `mat4_helpers_tests`, `animation_crossfade_tests`, `terrain_collider_tests`, plus les existants de A (quat_tests, skeleton_tests, animation_clip_tests, animation_sampler_tests, skinned_mesh_loader_tests).

- [ ] **CI Windows : build OK**

- [ ] **Smoke test visuel : 7 critères §11 validés** (screenshot + log)

- [ ] **Description PR mise à jour**

Inclure :
- Lien spec/plan
- Screenshot mid-Jump
- Mention « Déploiement : ✅ client uniquement »
- Liste des 14 tasks complétées

---

## Périmètre HORS B.1 (rappel)

- **B.2** : surface modulation (water/sand/snow → walk speed). Dépend SurfaceQuery M100.11.
- **B.3** : remote players sync animation (UDP gameplay, server redeploy).
- **Strafe/Backward anims distincts** — B futur.
- **Crouch / Sneak** — B futur ou C+.
- **Saut en longueur distinct** (Running Jump) — B futur.
- **Lissage rotation yaw modèle** — polish futur.
- **Variantes raciales** — sous-projet C.
- **Animation actions sociales / combat / magie** — E / F.
- **Animaux** — I.
