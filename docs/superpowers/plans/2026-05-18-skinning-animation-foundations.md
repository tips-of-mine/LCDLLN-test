# Sous-projet A — Fondations skinning + runtime animation — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remplacer le cube `avatar_placeholder.mesh` par un humanoïde Mixamo Y Bot qui exécute un cycle de marche en boucle permanente, dans `lcdlln_client.exe` et `lcdlln_world_editor.exe`. Livrer un runtime de squelette + clip player + skinning Vulkan GPU end-to-end, prêt à recevoir state machine (B) et variantes raciales (C).

**Architecture:** glTF 2.0 (binaire `.glb`) comme format runtime, produit par conversion FBX → glTF de l'asset Mixamo. Parsing au load via `cgltf` (single-header MIT). Pipeline Vulkan skinné séparé de `GeometryPass`, réutilisant `gbuffer_geometry.frag` (mêmes outputs G-buffer A/B/C/Velocity) avec un vertex shader dédié qui applique le skinning par matrices d'os uploadées dans un SSBO. Winding **CCW** (mandate glTF spec) avec `cullMode=BACK` — explicitement documenté pour éviter la régression d'invisibilité.

**Tech Stack:** C++20, Vulkan 1.x, CMake 3.20, cgltf (vendored), FBX2glTF (Godot fork, gitignored binary), glslangValidator (pipeline shader existant), framework de tests minimal custom (`lcdlln_add_simple_test` + `REQUIRE` macro).

**Spec source :** [2026-05-18-skinning-animation-foundations-design.md](../specs/2026-05-18-skinning-animation-foundations-design.md) (validé 2026-05-18).

**Déploiement :** ✅ Client uniquement, pas de redéploiement serveur.

---

## File Structure

### Nouveaux fichiers

| Chemin | Rôle | Taille estimée |
|---|---|---|
| `external/cgltf/cgltf.h` | Single-header MIT (vendored) | ~150 KB |
| `external/cgltf/LICENSE` | MIT license | ~1 KB |
| `src/shared/math/Quat.h` | Type Quaternion + slerp (nouveau dans Math) | ~60 lignes |
| `src/shared/math/Quat.cpp` | Implémentation slerp + Quat ops | ~80 lignes |
| `src/client/render/skinned/Skeleton.h` | Struct `Skeleton` + `Bone` (plain data) | ~50 lignes |
| `src/client/render/skinned/Skeleton.cpp` | Helpers de walk hiérarchie | ~40 lignes |
| `src/client/render/skinned/AnimationClip.h` | Struct `AnimationClip` + `Keyframe<T>` + `BoneTracks` | ~80 lignes |
| `src/client/render/skinned/AnimationClip.cpp` | Helpers d'interpolation linéaire / slerp | ~100 lignes |
| `src/client/render/skinned/AnimationSampler.h` | API `SamplePose` / `ComputeGlobal` / `ComputeFinal` | ~30 lignes |
| `src/client/render/skinned/AnimationSampler.cpp` | Logique de sampling + walk hiérarchie | ~120 lignes |
| `src/client/render/skinned/SkinnedMesh.h` | Struct `SkinnedMesh` (VkBuffers + Skeleton ref) | ~50 lignes |
| `src/client/render/skinned/SkinnedMesh.cpp` | Upload GPU + destroy | ~80 lignes |
| `src/client/render/skinned/SkinnedMeshLoader.h` | API `Load` + `LoadCpuOnlyForTests` | ~40 lignes |
| `src/client/render/skinned/SkinnedMeshLoader.cpp` | Wrapper cgltf → Skeleton/SkinnedMesh/Clips | ~300 lignes |
| `src/client/render/skinned/SkinnedRenderer.h` | API `Init` / `Record` / `Destroy` | ~80 lignes |
| `src/client/render/skinned/SkinnedRenderer.cpp` | Pipeline Vulkan + bone SSBO + draw | ~400 lignes |
| `src/client/render/skinned/tests/QuatTests.cpp` | Tests Quat + slerp | ~80 lignes |
| `src/client/render/skinned/tests/SkeletonTests.cpp` | Tests Skeleton | ~50 lignes |
| `src/client/render/skinned/tests/AnimationClipTests.cpp` | Tests keyframe interp | ~80 lignes |
| `src/client/render/skinned/tests/AnimationSamplerTests.cpp` | Tests sampler + hiérarchie | ~150 lignes |
| `src/client/render/skinned/tests/SkinnedMeshLoaderTests.cpp` | Tests loader vs y_bot.glb | ~100 lignes |
| `game/data/shaders/skinned_gbuffer.vert` | Vertex shader skinné (réutilise gbuffer_geometry.frag) | ~50 lignes |
| `game/data/models/avatars/y_bot/y_bot.glb` | Asset Mixamo Y Bot avec clip Walk baked | ~3-5 MB |
| `game/data/models/avatars/y_bot/README.md` | Source Mixamo + licence | ~20 lignes |
| `tools/asset_pipeline/README.md` | Procédure Mixamo → drop → conversion | ~60 lignes |
| `tools/asset_pipeline/download_fbx2gltf.ps1` | Download FBX2glTF.exe avec vérif SHA256 | ~50 lignes |
| `tools/asset_pipeline/fbx_to_gltf.ps1` | Wrapper user-facing | ~40 lignes |
| `tools/asset_pipeline/bin/.gitignore` | Ignore FBX2glTF.exe | 1 ligne |
| `tools/asset_pipeline/inbox/.gitignore` | Ignore *.fbx | 1 ligne |

### Fichiers modifiés

| Chemin | Modification |
|---|---|
| `CMakeLists.txt` (root) | Ajouter `external/cgltf` à l'include path + nouveaux .cpp dans le target client |
| `src/CMakeLists.txt` | Lister les nouveaux .cpp `src/client/render/skinned/*.cpp` + `src/shared/math/Quat.cpp` |
| `cmake/LCDLLNHelpers.cmake` ou équivalent | Enregistrer 5 nouveaux tests via `lcdlln_add_simple_test` |
| `src/client/app/Engine.cpp` | Post-EnterWorld (~ligne 3720) : load Y Bot + appel `SkinnedRenderer::Record` à la place du cube |
| `src/client/app/Engine.cpp:3525-3548` | Editor debug avatar : même swap |
| `src/client/app/Engine.h` | Membres `m_skinnedRenderer`, `m_playerSkinnedMesh`, `m_playerAnimationClip`, `m_playerStartTime` |
| `.gitignore` (root) | Ajouter `tools/asset_pipeline/bin/*` et `tools/asset_pipeline/inbox/*.fbx` |
| `CODEBASE_MAP.md` §6 et §14 | Nouvelles sous-sections runtime skinné + mise à jour cube → humanoïde |

---

## Task 1 — Vendor cgltf

**Files:**
- Create: `external/cgltf/cgltf.h`
- Create: `external/cgltf/LICENSE`
- Modify: `CMakeLists.txt` (root, ajout include path)

- [ ] **Step 1.1 — Télécharger cgltf v1.14**

Run (PowerShell) :
```powershell
$cgltfDir = "external/cgltf"
New-Item -ItemType Directory -Force -Path $cgltfDir | Out-Null
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.14/cgltf.h" -OutFile "$cgltfDir/cgltf.h"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/jkuhlmann/cgltf/v1.14/LICENSE" -OutFile "$cgltfDir/LICENSE"
```

Expected: 2 fichiers créés, `cgltf.h` ~150 KB, `LICENSE` ~1 KB.

- [ ] **Step 1.2 — Ajouter une bannière de version en tête de `external/cgltf/cgltf.h`**

Préfixer le fichier avec un bloc de 5 lignes pour pinner la version :
```
/*
 * cgltf v1.14 — vendored 2026-05-18 from https://github.com/jkuhlmann/cgltf
 * License: MIT (see LICENSE in this directory)
 * Update procedure: re-download from the same tag, re-run all skinning tests.
 */
```

(Insérer **après** le premier `/* */` d'en-tête existant du fichier, ou en tout début si pas de bloc.)

- [ ] **Step 1.3 — Ajouter `external/cgltf` à l'include path CMake**

Trouver dans `CMakeLists.txt` la ligne qui ajoute `external/stb` à l'include path (probablement `target_include_directories(engine_core PRIVATE ${CMAKE_SOURCE_DIR}/external/stb)`). Juste à côté, ajouter :
```cmake
target_include_directories(engine_core PRIVATE ${CMAKE_SOURCE_DIR}/external/cgltf)
```

(Si le target est différent — `lcdlln_client`, `engine`, `lcdlln_shared` — copier le pattern utilisé pour stb.)

- [ ] **Step 1.4 — Build sanity check**

Run :
```powershell
cmake --build build --target engine_core
```
Expected: pas de nouvelle erreur (cgltf.h n'est pas encore inclus nulle part, donc build inchangé).

- [ ] **Step 1.5 — Commit**

```bash
git add external/cgltf/ CMakeLists.txt
git commit -m "$(cat <<'EOF'
chore(deps): vendor cgltf v1.14 (single-header MIT)

Pour le sous-projet A (fondations skinning + animation).
cgltf parse glTF/glb au runtime sans dépendance externe.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2 — Type Quat + slerp dans shared math

**Files:**
- Create: `src/shared/math/Quat.h`
- Create: `src/shared/math/Quat.cpp`
- Create: `src/client/render/skinned/tests/QuatTests.cpp`
- Modify: `src/CMakeLists.txt` (lister `Quat.cpp`)
- Modify: `cmake/LCDLLNHelpers.cmake` ou `tests/CMakeLists.txt` (enregistrer `quat_tests`)

Pourquoi shared : les quaternions seront aussi utiles côté serveur si on synchronise des rotations d'animation, et côté éditeur. Pas spécifique au client.

- [ ] **Step 2.1 — Écrire le test (failing)**

Créer `src/client/render/skinned/tests/QuatTests.cpp` :
```cpp
// Tests for engine::math::Quat — construction, identity, multiply, slerp.

#include "src/shared/math/Quat.h"

#include <cmath>
#include <cstdio>

using engine::math::Quat;
using engine::math::Vec3;

namespace
{
    int g_failed = 0;

    #define REQUIRE(cond) do { \
        if (!(cond)) { \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failed; \
        } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    void Test_IdentityQuat_HasZeroXYZ_OneW()
    {
        Quat q = Quat::Identity();
        REQUIRE(Approx(q.x, 0.0f));
        REQUIRE(Approx(q.y, 0.0f));
        REQUIRE(Approx(q.z, 0.0f));
        REQUIRE(Approx(q.w, 1.0f));
    }

    void Test_FromAxisAngle_HalfPiAroundY_IsExpected()
    {
        Quat q = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f); // 90 deg
        REQUIRE(Approx(q.x, 0.0f));
        REQUIRE(Approx(q.y, 0.70710677f));
        REQUIRE(Approx(q.z, 0.0f));
        REQUIRE(Approx(q.w, 0.70710677f));
    }

    void Test_Multiply_IdentityIsNeutral()
    {
        Quat a = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 0.5f);
        Quat r = a * Quat::Identity();
        REQUIRE(Approx(r.x, a.x));
        REQUIRE(Approx(r.y, a.y));
        REQUIRE(Approx(r.z, a.z));
        REQUIRE(Approx(r.w, a.w));
    }

    void Test_Slerp_AtZero_ReturnsA()
    {
        Quat a = Quat::Identity();
        Quat b = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f);
        Quat r = Quat::Slerp(a, b, 0.0f);
        REQUIRE(Approx(r.w, 1.0f));
    }

    void Test_Slerp_AtOne_ReturnsB()
    {
        Quat a = Quat::Identity();
        Quat b = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f);
        Quat r = Quat::Slerp(a, b, 1.0f);
        REQUIRE(Approx(r.y, 0.70710677f));
        REQUIRE(Approx(r.w, 0.70710677f));
    }

    void Test_Slerp_AtHalf_IsMidwayRotation()
    {
        Quat a = Quat::Identity();
        Quat b = Quat::FromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, 1.5707963f); // 90 deg
        Quat r = Quat::Slerp(a, b, 0.5f);
        // Half of 90deg = 45deg → y = sin(22.5deg), w = cos(22.5deg)
        REQUIRE(Approx(r.y, std::sin(0.39269908f)));
        REQUIRE(Approx(r.w, std::cos(0.39269908f)));
    }
}

int main()
{
    Test_IdentityQuat_HasZeroXYZ_OneW();
    Test_FromAxisAngle_HalfPiAroundY_IsExpected();
    Test_Multiply_IdentityIsNeutral();
    Test_Slerp_AtZero_ReturnsA();
    Test_Slerp_AtOne_ReturnsB();
    Test_Slerp_AtHalf_IsMidwayRotation();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2.2 — Run test : doit échouer car `Quat` n'existe pas**

Run :
```powershell
cmake --build build --target quat_tests
```
Expected: erreur de compilation `cannot find Quat.h` ou cible inexistante.

- [ ] **Step 2.3 — Implémenter `Quat.h`**

Créer `src/shared/math/Quat.h` :
```cpp
#pragma once

#include "src/shared/math/Math.h"

namespace engine::math
{

struct Quat
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

    static Quat Identity() { return Quat{0.0f, 0.0f, 0.0f, 1.0f}; }
    static Quat FromAxisAngle(const Vec3& axis, float radians);
    static Quat Slerp(const Quat& a, const Quat& b, float t);

    Quat operator*(const Quat& rhs) const;

    // Convert to 4x4 rotation matrix (column-major, identity translation).
    Mat4 ToMat4() const;
};

}  // namespace engine::math
```

- [ ] **Step 2.4 — Implémenter `Quat.cpp`**

Créer `src/shared/math/Quat.cpp` :
```cpp
#include "src/shared/math/Quat.h"

#include <cmath>

namespace engine::math
{

Quat Quat::FromAxisAngle(const Vec3& axis, float radians)
{
    const float h = 0.5f * radians;
    const float s = std::sin(h);
    return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(h)};
}

Quat Quat::operator*(const Quat& r) const
{
    return Quat{
        w * r.x + x * r.w + y * r.z - z * r.y,
        w * r.y - x * r.z + y * r.w + z * r.x,
        w * r.z + x * r.y - y * r.x + z * r.w,
        w * r.w - x * r.x - y * r.y - z * r.z
    };
}

Quat Quat::Slerp(const Quat& a, const Quat& b, float t)
{
    // Compute the cosine of the angle between the two vectors.
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

    Quat bAdj = b;
    if (dot < 0.0f)
    {
        // Take the shorter arc.
        bAdj.x = -b.x; bAdj.y = -b.y; bAdj.z = -b.z; bAdj.w = -b.w;
        dot = -dot;
    }

    // If very close, fall back to linear interpolation + renormalize.
    if (dot > 0.9995f)
    {
        Quat r{
            a.x + t * (bAdj.x - a.x),
            a.y + t * (bAdj.y - a.y),
            a.z + t * (bAdj.z - a.z),
            a.w + t * (bAdj.w - a.w)
        };
        const float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);
        if (len > 0.0f) { r.x /= len; r.y /= len; r.z /= len; r.w /= len; }
        return r;
    }

    const float theta0 = std::acos(dot);
    const float theta = theta0 * t;
    const float sinTheta = std::sin(theta);
    const float sinTheta0 = std::sin(theta0);

    const float s0 = std::cos(theta) - dot * sinTheta / sinTheta0;
    const float s1 = sinTheta / sinTheta0;

    return Quat{
        s0 * a.x + s1 * bAdj.x,
        s0 * a.y + s1 * bAdj.y,
        s0 * a.z + s1 * bAdj.z,
        s0 * a.w + s1 * bAdj.w
    };
}

Mat4 Quat::ToMat4() const
{
    // Column-major, matches Mat4 layout in Math.h.
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;

    Mat4 m;
    m.m[0] = 1.0f - 2.0f * (yy + zz);
    m.m[1] = 2.0f * (xy + wz);
    m.m[2] = 2.0f * (xz - wy);
    m.m[3] = 0.0f;

    m.m[4] = 2.0f * (xy - wz);
    m.m[5] = 1.0f - 2.0f * (xx + zz);
    m.m[6] = 2.0f * (yz + wx);
    m.m[7] = 0.0f;

    m.m[8]  = 2.0f * (xz + wy);
    m.m[9]  = 2.0f * (yz - wx);
    m.m[10] = 1.0f - 2.0f * (xx + yy);
    m.m[11] = 0.0f;

    m.m[12] = 0.0f; m.m[13] = 0.0f; m.m[14] = 0.0f; m.m[15] = 1.0f;
    return m;
}

}  // namespace engine::math
```

- [ ] **Step 2.5 — Ajouter `Quat.cpp` au build**

Modifier `src/CMakeLists.txt` : trouver la liste de sources qui inclut `src/shared/math/Math.cpp` (ou équivalent), ajouter `src/shared/math/Quat.cpp` à côté.

- [ ] **Step 2.6 — Enregistrer `quat_tests` comme test CMake**

Trouver la déclaration des tests existants (ex. `lcdlln_add_simple_test(water_pass_tests ...)`). Ajouter :
```cmake
lcdlln_add_simple_test(quat_tests src/client/render/skinned/tests/QuatTests.cpp)
```

- [ ] **Step 2.7 — Run le test : doit passer**

Run :
```powershell
cmake --build build --target quat_tests
ctest --test-dir build -R quat_tests --output-on-failure
```
Expected: `1/1 Test #X: quat_tests ............ Passed`.

- [ ] **Step 2.8 — Commit**

```bash
git add src/shared/math/Quat.h src/shared/math/Quat.cpp src/client/render/skinned/tests/QuatTests.cpp src/CMakeLists.txt cmake/
git commit -m "$(cat <<'EOF'
feat(math): add Quat type + slerp for animation runtime

Sous-projet A étape 2/17. Pas de Quat dans Math.h aujourd'hui ;
les clips d'animation Mixamo stockent les rotations en quaternions.
Slerp implémente l'arc court (dot<0 → négation b) + fallback nlerp
quand dot > 0.9995 pour éviter division par zéro.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3 — Struct `Skeleton`

**Files:**
- Create: `src/client/render/skinned/Skeleton.h`
- Create: `src/client/render/skinned/Skeleton.cpp`
- Create: `src/client/render/skinned/tests/SkeletonTests.cpp`
- Modify: `src/CMakeLists.txt`, `cmake/LCDLLNHelpers.cmake`

- [ ] **Step 3.1 — Écrire les tests (failing)**

Créer `src/client/render/skinned/tests/SkeletonTests.cpp` :
```cpp
#include "src/client/render/skinned/Skeleton.h"

#include <cstdio>

using engine::render::skinned::Bone;
using engine::render::skinned::Skeleton;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    Skeleton MakeThreeBoneChain()
    {
        Skeleton s;
        s.bones.push_back(Bone{"root",       -1, engine::math::Mat4{}, engine::math::Mat4{}});
        s.bones.push_back(Bone{"upperArm",    0, engine::math::Mat4{}, engine::math::Mat4{}});
        s.bones.push_back(Bone{"foreArm",     1, engine::math::Mat4{}, engine::math::Mat4{}});
        return s;
    }

    void Test_EmptySkeleton_HasZeroBones()
    {
        Skeleton s;
        REQUIRE(s.bones.empty());
        REQUIRE(s.FindBoneIndex("anything") == -1);
    }

    void Test_FindBoneIndex_ReturnsCorrectIndex()
    {
        Skeleton s = MakeThreeBoneChain();
        REQUIRE(s.FindBoneIndex("root") == 0);
        REQUIRE(s.FindBoneIndex("upperArm") == 1);
        REQUIRE(s.FindBoneIndex("foreArm") == 2);
        REQUIRE(s.FindBoneIndex("unknown") == -1);
    }

    void Test_BonesAreOrderedParentBeforeChild()
    {
        Skeleton s = MakeThreeBoneChain();
        for (size_t i = 0; i < s.bones.size(); ++i) {
            REQUIRE(s.bones[i].parentIndex < static_cast<int>(i));
        }
    }
}

int main()
{
    Test_EmptySkeleton_HasZeroBones();
    Test_FindBoneIndex_ReturnsCorrectIndex();
    Test_BonesAreOrderedParentBeforeChild();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 3.2 — Run : doit échouer**

Run : `cmake --build build --target skeleton_tests` → erreur (Skeleton.h n'existe pas).

- [ ] **Step 3.3 — Implémenter `Skeleton.h`**

Créer `src/client/render/skinned/Skeleton.h` :
```cpp
#pragma once

#include "src/shared/math/Math.h"

#include <string>
#include <vector>

namespace engine::render::skinned
{

struct Bone
{
    std::string name;             // ex. "mixamorig:LeftArm"
    int parentIndex = -1;         // -1 pour la racine ; toujours < propre index
    engine::math::Mat4 bindLocal; // transform local en bind pose
    engine::math::Mat4 inverseBindGlobal; // inverse de la matrice globale en bind pose
};

struct Skeleton
{
    std::vector<Bone> bones;

    // Returns the bone index by name, or -1 if not found.
    int FindBoneIndex(const std::string& name) const;
};

}  // namespace engine::render::skinned
```

- [ ] **Step 3.4 — Implémenter `Skeleton.cpp`**

Créer `src/client/render/skinned/Skeleton.cpp` :
```cpp
#include "src/client/render/skinned/Skeleton.h"

namespace engine::render::skinned
{

int Skeleton::FindBoneIndex(const std::string& name) const
{
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace engine::render::skinned
```

- [ ] **Step 3.5 — Ajouter aux CMake**

Dans `src/CMakeLists.txt`, ajouter `src/client/render/skinned/Skeleton.cpp` à la liste de sources client.

Enregistrer le test :
```cmake
lcdlln_add_simple_test(skeleton_tests src/client/render/skinned/tests/SkeletonTests.cpp)
```

- [ ] **Step 3.6 — Run : doit passer**

Run : `ctest --test-dir build -R skeleton_tests --output-on-failure` → Passed.

- [ ] **Step 3.7 — Commit**

```bash
git add src/client/render/skinned/Skeleton.h src/client/render/skinned/Skeleton.cpp src/client/render/skinned/tests/SkeletonTests.cpp src/CMakeLists.txt cmake/
git commit -m "feat(skinned): add Skeleton + Bone plain-data types (3/17)"
```

---

## Task 4 — Struct `AnimationClip` + helpers d'interpolation

**Files:**
- Create: `src/client/render/skinned/AnimationClip.h`
- Create: `src/client/render/skinned/AnimationClip.cpp`
- Create: `src/client/render/skinned/tests/AnimationClipTests.cpp`
- Modify: `src/CMakeLists.txt`, `cmake/LCDLLNHelpers.cmake`

- [ ] **Step 4.1 — Écrire les tests (failing)**

Créer `src/client/render/skinned/tests/AnimationClipTests.cpp` :
```cpp
#include "src/client/render/skinned/AnimationClip.h"

#include <cmath>
#include <cstdio>

using engine::math::Quat;
using engine::math::Vec3;
using engine::render::skinned::AnimationClip;
using engine::render::skinned::BoneTracks;
using engine::render::skinned::InterpolateKeyframes;
using engine::render::skinned::Keyframe;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    bool Approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

    void Test_InterpolateVec3_BetweenTwoKeys()
    {
        std::vector<Keyframe<Vec3>> kf = {
            {0.0f, Vec3{0.0f, 0.0f, 0.0f}},
            {1.0f, Vec3{10.0f, 20.0f, 30.0f}}
        };
        Vec3 r = InterpolateKeyframes(kf, 0.5f, Vec3{0.0f, 0.0f, 0.0f});
        REQUIRE(Approx(r.x, 5.0f));
        REQUIRE(Approx(r.y, 10.0f));
        REQUIRE(Approx(r.z, 15.0f));
    }

    void Test_InterpolateVec3_BeforeFirstKey_ClampsToFirst()
    {
        std::vector<Keyframe<Vec3>> kf = {
            {1.0f, Vec3{10.0f, 0.0f, 0.0f}},
            {2.0f, Vec3{20.0f, 0.0f, 0.0f}}
        };
        Vec3 r = InterpolateKeyframes(kf, 0.5f, Vec3{99.0f, 99.0f, 99.0f});
        REQUIRE(Approx(r.x, 10.0f));
    }

    void Test_InterpolateVec3_AfterLastKey_ClampsToLast()
    {
        std::vector<Keyframe<Vec3>> kf = {
            {0.0f, Vec3{0.0f, 0.0f, 0.0f}},
            {1.0f, Vec3{10.0f, 0.0f, 0.0f}}
        };
        Vec3 r = InterpolateKeyframes(kf, 5.0f, Vec3{99.0f, 99.0f, 99.0f});
        REQUIRE(Approx(r.x, 10.0f));
    }

    void Test_InterpolateVec3_Empty_ReturnsFallback()
    {
        std::vector<Keyframe<Vec3>> kf;
        Vec3 r = InterpolateKeyframes(kf, 0.5f, Vec3{42.0f, 43.0f, 44.0f});
        REQUIRE(Approx(r.x, 42.0f));
        REQUIRE(Approx(r.y, 43.0f));
        REQUIRE(Approx(r.z, 44.0f));
    }

    void Test_InterpolateQuat_UsesSlerp()
    {
        std::vector<Keyframe<Quat>> kf = {
            {0.0f, Quat::Identity()},
            {1.0f, Quat::FromAxisAngle(Vec3{0,1,0}, 1.5707963f)}
        };
        Quat r = InterpolateKeyframes(kf, 0.5f, Quat::Identity());
        // 45 degrees around Y
        REQUIRE(Approx(r.y, std::sin(0.39269908f)));
        REQUIRE(Approx(r.w, std::cos(0.39269908f)));
    }

    void Test_AnimationClip_Construction()
    {
        AnimationClip clip;
        clip.name = "Walk";
        clip.duration = 1.5f;
        clip.tracks.resize(3);
        REQUIRE(clip.name == "Walk");
        REQUIRE(Approx(clip.duration, 1.5f));
        REQUIRE(clip.tracks.size() == 3);
    }
}

int main()
{
    Test_InterpolateVec3_BetweenTwoKeys();
    Test_InterpolateVec3_BeforeFirstKey_ClampsToFirst();
    Test_InterpolateVec3_AfterLastKey_ClampsToLast();
    Test_InterpolateVec3_Empty_ReturnsFallback();
    Test_InterpolateQuat_UsesSlerp();
    Test_AnimationClip_Construction();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 4.2 — Implémenter `AnimationClip.h`**

Créer `src/client/render/skinned/AnimationClip.h` :
```cpp
#pragma once

#include "src/shared/math/Math.h"
#include "src/shared/math/Quat.h"

#include <string>
#include <vector>

namespace engine::render::skinned
{

template <typename T>
struct Keyframe
{
    float time;  // seconds since clip start
    T value;
};

struct BoneTracks
{
    std::vector<Keyframe<engine::math::Vec3>> translation;
    std::vector<Keyframe<engine::math::Quat>> rotation;
    std::vector<Keyframe<engine::math::Vec3>> scale;
};

struct AnimationClip
{
    std::string name;
    float duration = 0.0f;
    std::vector<BoneTracks> tracks;  // aligned on Skeleton::bones (index = bone index)
};

// Interpolates between consecutive keyframes at time t (linear for Vec3, slerp for Quat).
// Returns fallback if the track is empty. Clamps to first/last keyframe if t is out of range.
engine::math::Vec3 InterpolateKeyframes(const std::vector<Keyframe<engine::math::Vec3>>& kfs,
                                        float t,
                                        const engine::math::Vec3& fallback);
engine::math::Quat InterpolateKeyframes(const std::vector<Keyframe<engine::math::Quat>>& kfs,
                                        float t,
                                        const engine::math::Quat& fallback);

}  // namespace engine::render::skinned
```

- [ ] **Step 4.3 — Implémenter `AnimationClip.cpp`**

Créer `src/client/render/skinned/AnimationClip.cpp` :
```cpp
#include "src/client/render/skinned/AnimationClip.h"

#include <algorithm>

namespace engine::render::skinned
{

namespace
{
    // Find consecutive keyframes (i, i+1) such that kfs[i].time <= t < kfs[i+1].time.
    // Returns {-1, -1} if kfs.empty(). Returns {0, 0} if t < kfs[0].time. Returns {last, last} if t >= kfs[last].time.
    template <typename T>
    std::pair<int, int> FindBracket(const std::vector<Keyframe<T>>& kfs, float t)
    {
        if (kfs.empty()) return {-1, -1};
        if (t <= kfs.front().time) return {0, 0};
        if (t >= kfs.back().time) return {static_cast<int>(kfs.size()) - 1, static_cast<int>(kfs.size()) - 1};
        // Linear scan (small N for animation clips; binary search would be premature optim).
        for (size_t i = 0; i + 1 < kfs.size(); ++i) {
            if (t < kfs[i + 1].time) return {static_cast<int>(i), static_cast<int>(i) + 1};
        }
        return {static_cast<int>(kfs.size()) - 1, static_cast<int>(kfs.size()) - 1};
    }

    float ComputeAlpha(float t, float t0, float t1)
    {
        const float dt = t1 - t0;
        return (dt > 0.0f) ? (t - t0) / dt : 0.0f;
    }
}

engine::math::Vec3 InterpolateKeyframes(const std::vector<Keyframe<engine::math::Vec3>>& kfs,
                                        float t,
                                        const engine::math::Vec3& fallback)
{
    auto [i0, i1] = FindBracket(kfs, t);
    if (i0 < 0) return fallback;
    if (i0 == i1) return kfs[i0].value;
    const float a = ComputeAlpha(t, kfs[i0].time, kfs[i1].time);
    return engine::math::Vec3{
        kfs[i0].value.x + a * (kfs[i1].value.x - kfs[i0].value.x),
        kfs[i0].value.y + a * (kfs[i1].value.y - kfs[i0].value.y),
        kfs[i0].value.z + a * (kfs[i1].value.z - kfs[i0].value.z)
    };
}

engine::math::Quat InterpolateKeyframes(const std::vector<Keyframe<engine::math::Quat>>& kfs,
                                        float t,
                                        const engine::math::Quat& fallback)
{
    auto [i0, i1] = FindBracket(kfs, t);
    if (i0 < 0) return fallback;
    if (i0 == i1) return kfs[i0].value;
    const float a = ComputeAlpha(t, kfs[i0].time, kfs[i1].time);
    return engine::math::Quat::Slerp(kfs[i0].value, kfs[i1].value, a);
}

}  // namespace engine::render::skinned
```

- [ ] **Step 4.4 — Build + enregistrer test**

`src/CMakeLists.txt` : ajouter `src/client/render/skinned/AnimationClip.cpp`.
`cmake/` : ajouter `lcdlln_add_simple_test(animation_clip_tests src/client/render/skinned/tests/AnimationClipTests.cpp)`.

- [ ] **Step 4.5 — Run : doit passer**

`ctest --test-dir build -R animation_clip_tests --output-on-failure` → Passed.

- [ ] **Step 4.6 — Commit**

```bash
git add src/client/render/skinned/AnimationClip.h src/client/render/skinned/AnimationClip.cpp src/client/render/skinned/tests/AnimationClipTests.cpp src/CMakeLists.txt cmake/
git commit -m "feat(skinned): add AnimationClip + keyframe interpolation (4/17)"
```

---

## Task 5 — `AnimationSampler::SamplePose`

**Files:**
- Create: `src/client/render/skinned/AnimationSampler.h`
- Create: `src/client/render/skinned/AnimationSampler.cpp`
- Create: `src/client/render/skinned/tests/AnimationSamplerTests.cpp`

- [ ] **Step 5.1 — Écrire le test pour `SamplePose` (failing)**

Créer `src/client/render/skinned/tests/AnimationSamplerTests.cpp` (premier test seulement, on en ajoutera plus tard) :
```cpp
#include "src/client/render/skinned/AnimationSampler.h"
#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"

#include <cmath>
#include <cstdio>

using engine::math::Mat4;
using engine::math::Quat;
using engine::math::Vec3;
using engine::render::skinned::AnimationClip;
using engine::render::skinned::AnimationSampler;
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

    Skeleton MakeOneBoneSkel()
    {
        Skeleton s;
        s.bones.push_back(Bone{"root", -1, Mat4{}, Mat4{}});
        return s;
    }

    AnimationClip MakeRotateY90Clip()
    {
        AnimationClip clip;
        clip.name = "wave";
        clip.duration = 1.0f;
        clip.tracks.resize(1);
        clip.tracks[0].rotation = {
            {0.0f, Quat::Identity()},
            {1.0f, Quat::FromAxisAngle(Vec3{0,1,0}, 1.5707963f)}
        };
        return clip;
    }

    void Test_SamplePose_AtMidTime_HalfRotation()
    {
        Skeleton skel = MakeOneBoneSkel();
        AnimationClip clip = MakeRotateY90Clip();
        std::vector<Mat4> locals = AnimationSampler::SamplePose(skel, clip, 0.5f);
        REQUIRE(locals.size() == 1);
        // 45 degrees rotation around Y → mat[0] = cos(45), mat[2] = -sin(45) (approx, depends on convention)
        // Easiest: convert back to euler-ish check via the rotation columns.
        // 45deg: cos(45)=0.7071, sin(45)=0.7071
        REQUIRE(Approx(locals[0].m[0],  0.70710677f, 1e-3f)); // column 0, x component
        REQUIRE(Approx(locals[0].m[10], 0.70710677f, 1e-3f)); // column 2, z component
    }
}

int main()
{
    Test_SamplePose_AtMidTime_HalfRotation();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 5.2 — Run : doit échouer (Sampler.h n'existe pas)**

- [ ] **Step 5.3 — Implémenter `AnimationSampler.h`**

Créer `src/client/render/skinned/AnimationSampler.h` :
```cpp
#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/shared/math/Math.h"

#include <vector>

namespace engine::render::skinned
{

class AnimationSampler
{
public:
    // Samples the clip at time t and returns one local 4x4 matrix per bone.
    // Bones without a track for a given channel fall back to the skeleton's bindLocal.
    static std::vector<engine::math::Mat4> SamplePose(const Skeleton& skeleton,
                                                       const AnimationClip& clip,
                                                       float t);

    // Walks the bone hierarchy (parent always before child) and returns global matrices.
    static std::vector<engine::math::Mat4> ComputeGlobalMatrices(const Skeleton& skeleton,
                                                                  const std::vector<engine::math::Mat4>& locals);

    // Multiplies each global matrix by the bone's inverse-bind matrix.
    // The result is what gets uploaded to the bone matrix SSBO.
    static std::vector<engine::math::Mat4> ComputeFinalMatrices(const Skeleton& skeleton,
                                                                 const std::vector<engine::math::Mat4>& globals);
};

}  // namespace engine::render::skinned
```

- [ ] **Step 5.4 — Implémenter `SamplePose` dans `AnimationSampler.cpp`**

Créer `src/client/render/skinned/AnimationSampler.cpp` :
```cpp
#include "src/client/render/skinned/AnimationSampler.h"

namespace engine::render::skinned
{

namespace
{
    engine::math::Mat4 ComposeTRS(const engine::math::Vec3& t,
                                  const engine::math::Quat& r,
                                  const engine::math::Vec3& s)
    {
        engine::math::Mat4 rot = r.ToMat4();
        // Apply scale to rotation columns.
        rot.m[0] *= s.x; rot.m[1] *= s.x; rot.m[2] *= s.x;
        rot.m[4] *= s.y; rot.m[5] *= s.y; rot.m[6] *= s.y;
        rot.m[8] *= s.z; rot.m[9] *= s.z; rot.m[10] *= s.z;
        // Translation in column 3.
        rot.m[12] = t.x; rot.m[13] = t.y; rot.m[14] = t.z;
        rot.m[15] = 1.0f;
        return rot;
    }
}

std::vector<engine::math::Mat4> AnimationSampler::SamplePose(const Skeleton& skeleton,
                                                              const AnimationClip& clip,
                                                              float t)
{
    std::vector<engine::math::Mat4> locals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Bone& b = skeleton.bones[i];
        engine::math::Vec3 tr{0,0,0}, sc{1,1,1};
        engine::math::Quat ro = engine::math::Quat::Identity();
        // Default fallbacks come from bindLocal — decompose if needed, else use identity TRS.
        // For simplicity: fallback to bindLocal column 3 (translation) and identity rotation
        // (scale 1). Mixamo clips animate every bone every frame so the fallback path is
        // rarely hit on real assets, but kept correct for synthetic clips.
        if (i < clip.tracks.size()) {
            const BoneTracks& trk = clip.tracks[i];
            tr = InterpolateKeyframes(trk.translation, t, engine::math::Vec3{b.bindLocal.m[12], b.bindLocal.m[13], b.bindLocal.m[14]});
            ro = InterpolateKeyframes(trk.rotation, t, engine::math::Quat::Identity());
            sc = InterpolateKeyframes(trk.scale, t, engine::math::Vec3{1,1,1});
        } else {
            tr = engine::math::Vec3{b.bindLocal.m[12], b.bindLocal.m[13], b.bindLocal.m[14]};
        }
        locals[i] = ComposeTRS(tr, ro, sc);
    }
    return locals;
}

// ComputeGlobalMatrices and ComputeFinalMatrices come in the next tasks.

}  // namespace engine::render::skinned
```

- [ ] **Step 5.5 — Build + register test + run**

Ajouter à `src/CMakeLists.txt` : `src/client/render/skinned/AnimationSampler.cpp`.
Ajouter test : `lcdlln_add_simple_test(animation_sampler_tests src/client/render/skinned/tests/AnimationSamplerTests.cpp)`.
Run : `ctest -R animation_sampler_tests --output-on-failure` → Passed.

- [ ] **Step 5.6 — Commit**

```bash
git add src/client/render/skinned/AnimationSampler.h src/client/render/skinned/AnimationSampler.cpp src/client/render/skinned/tests/AnimationSamplerTests.cpp src/CMakeLists.txt cmake/
git commit -m "feat(skinned): AnimationSampler::SamplePose with TRS compose (5/17)"
```

---

## Task 6 — `AnimationSampler::ComputeGlobalMatrices`

**Files:** Modify `src/client/render/skinned/AnimationSampler.cpp`, `tests/AnimationSamplerTests.cpp`.

- [ ] **Step 6.1 — Ajouter le test (failing)**

Dans `AnimationSamplerTests.cpp`, ajouter une fonction :
```cpp
void Test_ComputeGlobalMatrices_TwoBoneChain_AppliesParent()
{
    Skeleton skel;
    skel.bones.push_back(Bone{"root", -1, Mat4{}, Mat4{}});
    skel.bones.push_back(Bone{"child", 0, Mat4{}, Mat4{}});

    // root local = translate (10, 0, 0), child local = identity.
    Mat4 rootLocal;
    rootLocal.m[12] = 10.0f;
    Mat4 childLocal; // identity

    std::vector<Mat4> locals = {rootLocal, childLocal};
    std::vector<Mat4> globals = AnimationSampler::ComputeGlobalMatrices(skel, locals);

    REQUIRE(globals.size() == 2);
    // root global == root local (parent == -1)
    REQUIRE(Approx(globals[0].m[12], 10.0f));
    // child global == root global * child local == translate(10,0,0)
    REQUIRE(Approx(globals[1].m[12], 10.0f));
}
```
Et l'appeler dans `main()`.

- [ ] **Step 6.2 — Implémenter `ComputeGlobalMatrices`**

Dans `AnimationSampler.cpp`, ajouter :
```cpp
std::vector<engine::math::Mat4> AnimationSampler::ComputeGlobalMatrices(const Skeleton& skeleton,
                                                                         const std::vector<engine::math::Mat4>& locals)
{
    std::vector<engine::math::Mat4> globals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const int parent = skeleton.bones[i].parentIndex;
        if (parent < 0) {
            globals[i] = locals[i];
        } else {
            globals[i] = globals[parent] * locals[i];
        }
    }
    return globals;
}
```

- [ ] **Step 6.3 — Run : doit passer**

- [ ] **Step 6.4 — Commit**

```bash
git add src/client/render/skinned/AnimationSampler.cpp src/client/render/skinned/tests/AnimationSamplerTests.cpp
git commit -m "feat(skinned): AnimationSampler::ComputeGlobalMatrices (6/17)"
```

---

## Task 7 — `AnimationSampler::ComputeFinalMatrices`

**Files:** Modify `src/client/render/skinned/AnimationSampler.cpp`, `tests/AnimationSamplerTests.cpp`.

- [ ] **Step 7.1 — Test failing : pose en bind = identité partout**

Dans `AnimationSamplerTests.cpp`, ajouter :
```cpp
void Test_ComputeFinalMatrices_BindPose_GivesIdentity()
{
    // If the bones are sampled as their bindLocal AND inverseBindGlobal is correctly set,
    // the final matrices should all be identity (i.e., the mesh is in bind pose, undeformed).
    Skeleton skel;
    Mat4 rootBindLocal; rootBindLocal.m[12] = 5.0f; // translate(5,0,0)
    Mat4 rootInvBindGlobal; rootInvBindGlobal.m[12] = -5.0f; // inverse
    skel.bones.push_back(Bone{"root", -1, rootBindLocal, rootInvBindGlobal});

    std::vector<Mat4> locals = {rootBindLocal};
    std::vector<Mat4> globals = AnimationSampler::ComputeGlobalMatrices(skel, locals);
    std::vector<Mat4> finals = AnimationSampler::ComputeFinalMatrices(skel, globals);

    REQUIRE(finals.size() == 1);
    REQUIRE(Approx(finals[0].m[0], 1.0f));
    REQUIRE(Approx(finals[0].m[5], 1.0f));
    REQUIRE(Approx(finals[0].m[10], 1.0f));
    REQUIRE(Approx(finals[0].m[12], 0.0f)); // translation cancels out
}
```

- [ ] **Step 7.2 — Implémenter `ComputeFinalMatrices`**

Dans `AnimationSampler.cpp` :
```cpp
std::vector<engine::math::Mat4> AnimationSampler::ComputeFinalMatrices(const Skeleton& skeleton,
                                                                        const std::vector<engine::math::Mat4>& globals)
{
    std::vector<engine::math::Mat4> finals(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        finals[i] = globals[i] * skeleton.bones[i].inverseBindGlobal;
    }
    return finals;
}
```

- [ ] **Step 7.3 — Run + commit**

```bash
git add src/client/render/skinned/AnimationSampler.cpp src/client/render/skinned/tests/AnimationSamplerTests.cpp
git commit -m "feat(skinned): AnimationSampler::ComputeFinalMatrices (7/17)"
```

---

## Task 8 — `download_fbx2gltf.ps1` + README

**Files:**
- Create: `tools/asset_pipeline/README.md`
- Create: `tools/asset_pipeline/download_fbx2gltf.ps1`
- Create: `tools/asset_pipeline/bin/.gitignore`
- Create: `tools/asset_pipeline/inbox/.gitignore`
- Modify: `.gitignore` (root, ajout `tools/asset_pipeline/bin/`)

- [ ] **Step 8.1 — Identifier l'URL et SHA256 de la release Godot fork**

Aller sur https://github.com/godotengine/FBX2glTF/releases — récupérer la dernière release Windows x64.
Noter : URL du `.zip`, SHA256 (souvent dans les release notes, sinon calculer après download).

Au moment de la rédaction (à ré-vérifier à l'exécution) :
- URL exemple : `https://github.com/godotengine/FBX2glTF/releases/download/v0.9.7/FBX2glTF-windows-x64.exe`
- Le binaire est livré directement en `.exe`, pas dans un `.zip`.

- [ ] **Step 8.2 — Écrire `download_fbx2gltf.ps1`**

```powershell
# Downloads FBX2glTF.exe (Godot fork) into tools/asset_pipeline/bin/.
# Verifies SHA256 against a pinned value. Re-running is idempotent.

param([switch]$Force)

$ErrorActionPreference = "Stop"

$Version = "0.9.7"
$Url = "https://github.com/godotengine/FBX2glTF/releases/download/v$Version/FBX2glTF-windows-x64.exe"
$ExpectedSha256 = "REPLACE_WITH_REAL_SHA256_AT_DOWNLOAD_TIME"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$binDir = Join-Path $PSScriptRoot "bin"
$dst = Join-Path $binDir "FBX2glTF.exe"

if ((Test-Path -LiteralPath $dst) -and -not $Force) {
    $existingSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $dst).Hash
    if ($existingSha -ieq $ExpectedSha256) {
        Write-Host "[download_fbx2gltf] Already present at $dst (SHA matches v$Version)."
        exit 0
    }
    Write-Host "[download_fbx2gltf] Existing binary SHA mismatch; re-downloading." -ForegroundColor Yellow
}

New-Item -ItemType Directory -Force -Path $binDir | Out-Null

Write-Host "[download_fbx2gltf] Downloading FBX2glTF v$Version..."
Invoke-WebRequest -Uri $Url -OutFile $dst -UseBasicParsing

$actualSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $dst).Hash
if ($actualSha -ine $ExpectedSha256) {
    Write-Host "[download_fbx2gltf] SHA256 mismatch!" -ForegroundColor Red
    Write-Host "  Expected: $ExpectedSha256"
    Write-Host "  Actual:   $actualSha"
    Remove-Item -LiteralPath $dst -Force
    exit 1
}

Write-Host "[download_fbx2gltf] OK ($dst, SHA matches v$Version)"
```

**Note pour l'engineer** : lors du premier run, calculer le SHA256 actuel du binaire téléchargé et **remplacer** `REPLACE_WITH_REAL_SHA256_AT_DOWNLOAD_TIME` par cette valeur. Commit ensuite avec le SHA pinné.

- [ ] **Step 8.3 — Écrire `tools/asset_pipeline/README.md`**

```markdown
# Asset pipeline — Mixamo → glTF

Procédure pour intégrer un personnage Mixamo dans le client de jeu.

## Pré-requis

- Compte Adobe gratuit pour télécharger depuis https://www.mixamo.com.
- PowerShell 5+.

## Première fois : récupérer le convertisseur FBX→glTF

```powershell
.\tools\asset_pipeline\download_fbx2gltf.ps1
```

Télécharge `FBX2glTF.exe` (~5 MB, Godot fork, MIT) dans `tools/asset_pipeline/bin/` avec
vérification SHA256. Le binaire est gitignored (pas dans le repo).

## Workflow par asset

1. Sur Mixamo, choisir un personnage et **télécharger en format "FBX Binary"** (PAS
   FBX ASCII, PAS FBX for Unity, PAS FBX 7.4 / 6.1, PAS Collada).
2. Pour ajouter une animation au personnage : sur Mixamo, choisir l'animation puis cocher
   "with skin" → l'export FBX contient mesh + skeleton + clip baked.
3. Déposer le `.fbx` dans `tools/asset_pipeline/inbox/<nom>.fbx`.
4. Convertir :
   ```powershell
   .\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot -Category avatars
   ```
   → produit `game/data/models/<Category>/<EntityName>/<EntityName>.glb`.
5. Commit le `.glb` (le `.fbx` source reste gitignored).

## Versions pinned

- FBX2glTF : v0.9.7 (Godot fork). Pour mettre à jour : modifier `$Version` dans `download_fbx2gltf.ps1` + recalculer SHA256.
```

- [ ] **Step 8.4 — `tools/asset_pipeline/bin/.gitignore`**

```
# FBX2glTF.exe is downloaded by download_fbx2gltf.ps1, not committed.
*
!.gitignore
```

- [ ] **Step 8.5 — `tools/asset_pipeline/inbox/.gitignore`**

```
# Source FBX files dropped here are inputs only, not committed.
*.fbx
!.gitignore
```

- [ ] **Step 8.6 — Run le script + remplacer le SHA256**

Run :
```powershell
.\tools\asset_pipeline\download_fbx2gltf.ps1
```
Première exécution : va échouer avec "SHA256 mismatch" parce que le placeholder n'est pas le vrai SHA.
Récupérer le SHA affiché dans "Actual:" → l'inscrire à la place de `REPLACE_WITH_REAL_SHA256_AT_DOWNLOAD_TIME` dans le script.

Relancer : doit afficher "OK".

- [ ] **Step 8.7 — Commit**

```bash
git add tools/asset_pipeline/ .gitignore
git commit -m "$(cat <<'EOF'
feat(asset_pipeline): download script + README for FBX2glTF (8/17)

Tool pinné : FBX2glTF v0.9.7 (Godot fork, fork actif du repo Meta
facebookincubator/FBX2glTF archivé depuis 2022).
Le binaire est gitignored ; download_fbx2gltf.ps1 le récupère
avec vérification SHA256.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9 — `fbx_to_gltf.ps1` + commit Y Bot

**Files:**
- Create: `tools/asset_pipeline/fbx_to_gltf.ps1`
- Create: `game/data/models/avatars/y_bot/y_bot.glb`
- Create: `game/data/models/avatars/y_bot/README.md`

- [ ] **Step 9.1 — Écrire `fbx_to_gltf.ps1`**

```powershell
# Converts a Mixamo FBX (in tools/asset_pipeline/inbox/) to glTF binary (.glb)
# in game/data/models/<Category>/<EntityName>/<EntityName>.glb.

param(
    [Parameter(Mandatory=$true)][string]$EntityName,
    [Parameter(Mandatory=$true)][string]$Category
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$converter = Join-Path $PSScriptRoot "bin\FBX2glTF.exe"
$srcFbx = Join-Path $PSScriptRoot "inbox\$EntityName.fbx"
$outDir = Join-Path $root "game\data\models\$Category\$EntityName"
$outGlb = Join-Path $outDir "$EntityName.glb"

if (-not (Test-Path -LiteralPath $converter)) {
    throw "FBX2glTF.exe not found at $converter. Run download_fbx2gltf.ps1 first."
}
if (-not (Test-Path -LiteralPath $srcFbx)) {
    throw "Source FBX not found at $srcFbx. Drop your Mixamo .fbx there first."
}

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

Write-Host "[fbx_to_gltf] Converting $srcFbx ..."
& $converter --input $srcFbx --output $outGlb --binary --khr-materials-unlit
if ($LASTEXITCODE -ne 0) { throw "FBX2glTF failed with exit code $LASTEXITCODE" }

Write-Host "[fbx_to_gltf] Produced $outGlb"
```

- [ ] **Step 9.2 — Télécharger Y Bot + clip Walk depuis Mixamo (manuel)**

1. Aller sur https://www.mixamo.com (compte Adobe).
2. Sélectionner le personnage "Y Bot" (T-pose, gris uni — c'est le placeholder universel Mixamo).
3. Onglet Animations → chercher "walking" → choisir le cycle nommé "Walking" (in-place de base).
4. Bouton "Download" → format "FBX Binary", Pose "T-pose", "with skin" coché, frames "30", "Keyframe Reduction None".
5. Renommer le fichier téléchargé en `y_bot.fbx`.
6. Déposer dans `tools/asset_pipeline/inbox/y_bot.fbx`.

- [ ] **Step 9.3 — Convertir**

```powershell
.\tools\asset_pipeline\fbx_to_gltf.ps1 -EntityName y_bot -Category avatars
```

Expected : `game/data/models/avatars/y_bot/y_bot.glb` créé, ~3-5 MB. Si erreur, lire la sortie FBX2glTF et corriger les options.

- [ ] **Step 9.4 — Smoke-check le `.glb`**

Run un parser online ou (mieux) :
```powershell
# Quick sanity: file starts with glTF magic "glTF" (0x46546C67 little-endian).
$bytes = [System.IO.File]::ReadAllBytes("game/data/models/avatars/y_bot/y_bot.glb")
$magic = [System.Text.Encoding]::ASCII.GetString($bytes[0..3])
if ($magic -ne "glTF") { throw "Not a valid .glb: magic = $magic" }
Write-Host "OK, glTF magic confirmed."
```

- [ ] **Step 9.5 — README de l'asset**

Créer `game/data/models/avatars/y_bot/README.md` :
```markdown
# Y Bot — placeholder humanoid (Mixamo)

**Source** : https://www.mixamo.com (compte Adobe gratuit, asset "Y Bot")
**Licence** : Mixamo Terms of Use — assets téléchargeables libres de droits pour
usage dans un produit commercial dérivé (cf. https://www.mixamo.com/faq).
**Animation incluse** : "Walking" (cycle de marche en place, ~1.0 s, in-place).
**Téléchargé le** : 2026-05-18.

## Origine du fichier

Téléchargé depuis Mixamo en FBX Binary (Pose T, with skin, 30 fps), converti
en glTF 2.0 binaire via le script `tools/asset_pipeline/fbx_to_gltf.ps1`
(FBX2glTF v0.9.7, Godot fork).

## Usage runtime

Chargé par `SkinnedMeshLoader::Load` (`src/client/render/skinned/SkinnedMeshLoader.cpp`)
au moment de l'EnterWorld, remplace le cube `avatar_placeholder.mesh`.
```

- [ ] **Step 9.6 — Commit**

```bash
git add tools/asset_pipeline/fbx_to_gltf.ps1 game/data/models/avatars/y_bot/
git commit -m "$(cat <<'EOF'
feat(asset): import Mixamo Y Bot + Walk clip (9/17)

Asset placeholder humanoïde converti via fbx_to_gltf.ps1 (FBX2glTF Godot
fork v0.9.7). 1 mesh, ~65 bones (skeleton Mixamo standard), 1 clip "Walking"
~1s baked dans le .glb.

Licence Mixamo : usage commercial autorisé pour les assets téléchargés
(cf. game/data/models/avatars/y_bot/README.md).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10 — `SkinnedMeshLoader::LoadCpuOnlyForTests` + tests

**Files:**
- Create: `src/client/render/skinned/SkinnedMeshLoader.h`
- Create: `src/client/render/skinned/SkinnedMeshLoader.cpp`
- Create: `src/client/render/skinned/tests/SkinnedMeshLoaderTests.cpp`

- [ ] **Step 10.1 — Tests (failing)**

Créer `src/client/render/skinned/tests/SkinnedMeshLoaderTests.cpp` :
```cpp
#include "src/client/render/skinned/SkinnedMeshLoader.h"

#include <cstdio>

using engine::render::skinned::SkinnedMeshCpuData;
using engine::render::skinned::SkinnedMeshLoader;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    void Test_LoadYBot_HasSkeletonAndWalkClip()
    {
        auto result = SkinnedMeshLoader::LoadCpuOnlyForTests("game/data/models/avatars/y_bot/y_bot.glb");
        REQUIRE(result.has_value());
        if (!result.has_value()) return;

        const SkinnedMeshCpuData& cpu = *result;

        // Y Bot has ~65 bones from Mixamo's standard humanoid skeleton.
        REQUIRE(cpu.skeleton.bones.size() >= 30);

        // The mesh has vertices and indices.
        REQUIRE(!cpu.vertices.empty());
        REQUIRE(!cpu.indices.empty());

        // Each vertex stride is 56 bytes (pos12 + normal12 + uv8 + boneIdx8 + weights16).
        // We stored as struct, so vertices count > 0 is sufficient.

        // The .glb contains at least one animation, named like "Walking" or "mixamo.com|Walking".
        REQUIRE(!cpu.clips.empty());
    }

    void Test_LoadMissingFile_ReturnsNullopt()
    {
        auto result = SkinnedMeshLoader::LoadCpuOnlyForTests("game/data/models/avatars/does_not_exist.glb");
        REQUIRE(!result.has_value());
    }
}

int main()
{
    Test_LoadYBot_HasSkeletonAndWalkClip();
    Test_LoadMissingFile_ReturnsNullopt();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 10.2 — Définir `SkinnedMeshLoader.h`**

```cpp
#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace engine::render::skinned
{

struct SkinnedVertex
{
    float pos[3];
    float normal[3];
    float uv[2];
    uint16_t boneIndices[4];
    float weights[4];
};
// Stride = 56 bytes.
static_assert(sizeof(SkinnedVertex) == 56, "SkinnedVertex stride must be 56");

struct SkinnedMeshCpuData
{
    Skeleton skeleton;
    std::vector<SkinnedVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<AnimationClip> clips;
};

class SkinnedMeshLoader
{
public:
    // CPU-only load, used by unit tests and by Load() internally.
    // Returns nullopt if file missing or parse failed.
    static std::optional<SkinnedMeshCpuData> LoadCpuOnlyForTests(const std::string& path);
};

}  // namespace engine::render::skinned
```

- [ ] **Step 10.3 — Implémenter `SkinnedMeshLoader.cpp` (CPU only)**

Créer `src/client/render/skinned/SkinnedMeshLoader.cpp` :
```cpp
#include "src/client/render/skinned/SkinnedMeshLoader.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace engine::render::skinned
{

namespace
{
    bool ReadAccessorFloat(const cgltf_accessor* acc, size_t i, float* out, int components)
    {
        return cgltf_accessor_read_float(acc, i, out, components) != 0;
    }

    // Load TRS keyframes from a cgltf_animation_channel.
    template <typename T, int Components>
    std::vector<Keyframe<T>> LoadKeyframes(const cgltf_animation_sampler* sampler,
                                            std::function<T(const float*)> ctor)
    {
        std::vector<Keyframe<T>> out;
        if (!sampler || !sampler->input || !sampler->output) return out;
        const cgltf_accessor* input = sampler->input;
        const cgltf_accessor* output = sampler->output;
        if (input->count != output->count) return out;
        out.reserve(input->count);
        for (cgltf_size i = 0; i < input->count; ++i) {
            float t = 0.0f;
            if (!ReadAccessorFloat(input, i, &t, 1)) continue;
            float vals[4] = {0,0,0,0};
            if (!ReadAccessorFloat(output, i, vals, Components)) continue;
            out.push_back({t, ctor(vals)});
        }
        return out;
    }
}

std::optional<SkinnedMeshCpuData> SkinnedMeshLoader::LoadCpuOnlyForTests(const std::string& path)
{
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        spdlog::warn("[SkinnedMeshLoader] Parse failed for '{}' (cgltf result={})", path, static_cast<int>(result));
        if (data) cgltf_free(data);
        return std::nullopt;
    }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        spdlog::warn("[SkinnedMeshLoader] Buffer load failed for '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }

    SkinnedMeshCpuData out;

    // Take the first skin found.
    if (data->skins_count == 0 || !data->skins) {
        spdlog::warn("[SkinnedMeshLoader] No skin in '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }
    const cgltf_skin* skin = &data->skins[0];

    // Build skeleton from skin->joints.
    out.skeleton.bones.resize(skin->joints_count);
    for (cgltf_size i = 0; i < skin->joints_count; ++i) {
        const cgltf_node* joint = skin->joints[i];
        out.skeleton.bones[i].name = joint->name ? joint->name : "bone_" + std::to_string(i);
        out.skeleton.bones[i].parentIndex = -1;
        // bindLocal from node TRS or matrix.
        if (joint->has_matrix) {
            std::memcpy(out.skeleton.bones[i].bindLocal.m, joint->matrix, sizeof(float) * 16);
        } else {
            // Compose TRS into matrix.
            engine::math::Vec3 t{joint->translation[0], joint->translation[1], joint->translation[2]};
            engine::math::Quat r{joint->rotation[0], joint->rotation[1], joint->rotation[2], joint->rotation[3]};
            engine::math::Vec3 s{joint->scale[0], joint->scale[1], joint->scale[2]};
            engine::math::Mat4 rot = r.ToMat4();
            rot.m[0] *= s.x; rot.m[1] *= s.x; rot.m[2] *= s.x;
            rot.m[4] *= s.y; rot.m[5] *= s.y; rot.m[6] *= s.y;
            rot.m[8] *= s.z; rot.m[9] *= s.z; rot.m[10] *= s.z;
            rot.m[12] = t.x; rot.m[13] = t.y; rot.m[14] = t.z;
            out.skeleton.bones[i].bindLocal = rot;
        }
        // inverseBindGlobal from skin->inverse_bind_matrices.
        if (skin->inverse_bind_matrices) {
            float ibm[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, i, ibm, 16);
            std::memcpy(out.skeleton.bones[i].inverseBindGlobal.m, ibm, sizeof(float) * 16);
        }
    }
    // Resolve parent indices: walk joint nodes and find each parent's position in the skin->joints array.
    auto FindJointIndex = [&](const cgltf_node* n) -> int {
        for (cgltf_size i = 0; i < skin->joints_count; ++i) {
            if (skin->joints[i] == n) return static_cast<int>(i);
        }
        return -1;
    };
    for (cgltf_size i = 0; i < skin->joints_count; ++i) {
        const cgltf_node* joint = skin->joints[i];
        out.skeleton.bones[i].parentIndex = (joint->parent ? FindJointIndex(joint->parent) : -1);
    }

    // Walk meshes; take the first primitive that has POSITION + JOINTS_0 + WEIGHTS_0.
    bool meshLoaded = false;
    for (cgltf_size mi = 0; mi < data->meshes_count && !meshLoaded; ++mi) {
        const cgltf_mesh* mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count && !meshLoaded; ++pi) {
            const cgltf_primitive* prim = &mesh->primitives[pi];
            const cgltf_accessor *aPos=nullptr, *aNor=nullptr, *aUv=nullptr, *aJoints=nullptr, *aWeights=nullptr;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
                const cgltf_attribute& at = prim->attributes[ai];
                if (at.type == cgltf_attribute_type_position) aPos = at.data;
                else if (at.type == cgltf_attribute_type_normal) aNor = at.data;
                else if (at.type == cgltf_attribute_type_texcoord && at.index == 0) aUv = at.data;
                else if (at.type == cgltf_attribute_type_joints && at.index == 0) aJoints = at.data;
                else if (at.type == cgltf_attribute_type_weights && at.index == 0) aWeights = at.data;
            }
            if (!aPos || !aJoints || !aWeights) continue;

            const size_t nv = aPos->count;
            out.vertices.resize(nv);
            for (size_t v = 0; v < nv; ++v) {
                SkinnedVertex& sv = out.vertices[v];
                cgltf_accessor_read_float(aPos, v, sv.pos, 3);
                if (aNor) cgltf_accessor_read_float(aNor, v, sv.normal, 3);
                else { sv.normal[0]=0; sv.normal[1]=0; sv.normal[2]=1; }
                if (aUv) cgltf_accessor_read_float(aUv, v, sv.uv, 2);
                else { sv.uv[0]=0; sv.uv[1]=0; }
                cgltf_uint joints[4] = {0,0,0,0};
                cgltf_accessor_read_uint(aJoints, v, joints, 4);
                for (int k = 0; k < 4; ++k) sv.boneIndices[k] = static_cast<uint16_t>(joints[k]);
                cgltf_accessor_read_float(aWeights, v, sv.weights, 4);
            }
            // Indices.
            if (prim->indices) {
                const size_t ni = prim->indices->count;
                out.indices.resize(ni);
                for (size_t i = 0; i < ni; ++i) {
                    out.indices[i] = static_cast<uint32_t>(cgltf_accessor_read_index(prim->indices, i));
                }
            } else {
                out.indices.resize(nv);
                for (size_t i = 0; i < nv; ++i) out.indices[i] = static_cast<uint32_t>(i);
            }
            meshLoaded = true;
        }
    }
    if (!meshLoaded) {
        spdlog::warn("[SkinnedMeshLoader] No skinned primitive in '{}'", path);
        cgltf_free(data);
        return std::nullopt;
    }

    // Load animations.
    auto FindBoneIndexFromNode = [&](const cgltf_node* n) -> int {
        for (cgltf_size i = 0; i < skin->joints_count; ++i) {
            if (skin->joints[i] == n) return static_cast<int>(i);
        }
        return -1;
    };
    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation* anim = &data->animations[ai];
        AnimationClip clip;
        clip.name = anim->name ? anim->name : "anim_" + std::to_string(ai);
        clip.tracks.resize(skin->joints_count);
        float maxTime = 0.0f;
        for (cgltf_size ci = 0; ci < anim->channels_count; ++ci) {
            const cgltf_animation_channel* ch = &anim->channels[ci];
            int boneIdx = FindBoneIndexFromNode(ch->target_node);
            if (boneIdx < 0) continue;
            BoneTracks& trk = clip.tracks[boneIdx];
            const cgltf_animation_sampler* s = ch->sampler;
            if (!s) continue;
            // Track max time from input accessor.
            if (s->input && s->input->has_max) {
                if (s->input->max[0] > maxTime) maxTime = s->input->max[0];
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
        out.clips.push_back(std::move(clip));
    }

    cgltf_free(data);
    return out;
}

}  // namespace engine::render::skinned
```

- [ ] **Step 10.4 — Build + register test**

`src/CMakeLists.txt` : ajouter `src/client/render/skinned/SkinnedMeshLoader.cpp`.
`cmake/` : `lcdlln_add_simple_test(skinned_mesh_loader_tests src/client/render/skinned/tests/SkinnedMeshLoaderTests.cpp)`.

- [ ] **Step 10.5 — Run**

`ctest -R skinned_mesh_loader_tests --output-on-failure` → Passed.

Si échec : examiner le log pour comprendre quel champ Y Bot ne contient pas (probablement `JOINTS_0`/`WEIGHTS_0` si la conversion FBX→glTF a échoué — recommencer Task 9 avec les bonnes options Mixamo).

- [ ] **Step 10.6 — Commit**

```bash
git add src/client/render/skinned/SkinnedMeshLoader.h src/client/render/skinned/SkinnedMeshLoader.cpp src/client/render/skinned/tests/SkinnedMeshLoaderTests.cpp src/CMakeLists.txt cmake/
git commit -m "feat(skinned): SkinnedMeshLoader CPU-only via cgltf (10/17)"
```

---

## Task 11 — `SkinnedMesh` GPU upload + `SkinnedMeshLoader::Load`

**Files:**
- Create: `src/client/render/skinned/SkinnedMesh.h`
- Create: `src/client/render/skinned/SkinnedMesh.cpp`
- Modify: `src/client/render/skinned/SkinnedMeshLoader.h` (ajouter `Load`)
- Modify: `src/client/render/skinned/SkinnedMeshLoader.cpp` (impl `Load` qui appelle `LoadCpuOnlyForTests` puis upload GPU)

Pas de test unitaire ici (nécessite un VkDevice). Validé indirectement par le smoke test visuel à la Task 15.

- [ ] **Step 11.1 — Définir `SkinnedMesh.h`**

```cpp
#pragma once

#include "src/client/render/skinned/AnimationClip.h"
#include "src/client/render/skinned/Skeleton.h"
#include "src/client/render/skinned/SkinnedMeshLoader.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::render::skinned
{

struct SkinnedMesh
{
    Skeleton skeleton;
    std::vector<AnimationClip> clips;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    uint32_t indexCount = 0;

    // Uploads vertex + index data to GPU. Returns false on failure.
    bool Upload(VkDevice device, VkPhysicalDevice physicalDevice, const SkinnedMeshCpuData& cpu);
    void Destroy(VkDevice device);

    // Returns the clip by name, or nullptr if absent.
    const AnimationClip* FindClip(const std::string& name) const;
};

}  // namespace engine::render::skinned
```

- [ ] **Step 11.2 — Implémenter `SkinnedMesh.cpp`**

```cpp
#include "src/client/render/skinned/SkinnedMesh.h"

#include <spdlog/spdlog.h>
#include <cstring>

namespace engine::render::skinned
{

namespace
{
    uint32_t FindMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags wanted)
    {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        }
        return UINT32_MAX;
    }

    bool CreateHostVisibleBuffer(VkDevice device, VkPhysicalDevice phys, VkBufferUsageFlags usage,
                                 const void* src, size_t bytes,
                                 VkBuffer* outBuf, VkDeviceMemory* outMem)
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, outBuf) != VK_SUCCESS) return false;

        VkMemoryRequirements mr{};
        vkGetBufferMemoryRequirements(device, *outBuf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = FindMemoryType(phys, mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;

        if (vkAllocateMemory(device, &ai, nullptr, outMem) != VK_SUCCESS) return false;
        if (vkBindBufferMemory(device, *outBuf, *outMem, 0) != VK_SUCCESS) return false;

        void* mapped = nullptr;
        if (vkMapMemory(device, *outMem, 0, bytes, 0, &mapped) != VK_SUCCESS) return false;
        std::memcpy(mapped, src, bytes);
        vkUnmapMemory(device, *outMem);
        return true;
    }
}

bool SkinnedMesh::Upload(VkDevice device, VkPhysicalDevice physicalDevice, const SkinnedMeshCpuData& cpu)
{
    skeleton = cpu.skeleton;
    clips = cpu.clips;
    indexCount = static_cast<uint32_t>(cpu.indices.size());

    const size_t vBytes = cpu.vertices.size() * sizeof(SkinnedVertex);
    const size_t iBytes = cpu.indices.size() * sizeof(uint32_t);

    if (!CreateHostVisibleBuffer(device, physicalDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  cpu.vertices.data(), vBytes, &vertexBuffer, &vertexMemory)) {
        spdlog::error("[SkinnedMesh] vertex buffer creation failed");
        return false;
    }
    if (!CreateHostVisibleBuffer(device, physicalDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  cpu.indices.data(), iBytes, &indexBuffer, &indexMemory)) {
        spdlog::error("[SkinnedMesh] index buffer creation failed");
        return false;
    }
    return true;
}

void SkinnedMesh::Destroy(VkDevice device)
{
    if (vertexBuffer) vkDestroyBuffer(device, vertexBuffer, nullptr); vertexBuffer = VK_NULL_HANDLE;
    if (vertexMemory) vkFreeMemory(device, vertexMemory, nullptr); vertexMemory = VK_NULL_HANDLE;
    if (indexBuffer) vkDestroyBuffer(device, indexBuffer, nullptr); indexBuffer = VK_NULL_HANDLE;
    if (indexMemory) vkFreeMemory(device, indexMemory, nullptr); indexMemory = VK_NULL_HANDLE;
}

const AnimationClip* SkinnedMesh::FindClip(const std::string& name) const
{
    for (const auto& c : clips) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

}  // namespace engine::render::skinned
```

- [ ] **Step 11.3 — Étendre `SkinnedMeshLoader` avec `Load`**

Dans `SkinnedMeshLoader.h`, ajouter :
```cpp
    // Full load: parse glTF + upload to GPU. Returns nullopt on failure.
    static std::optional<SkinnedMesh> Load(VkDevice device, VkPhysicalDevice phys, const std::string& path);
```
(Et inclure `<vulkan/vulkan.h>` + forward-declarer ou inclure `SkinnedMesh.h`.)

Dans `SkinnedMeshLoader.cpp` :
```cpp
std::optional<SkinnedMesh> SkinnedMeshLoader::Load(VkDevice device, VkPhysicalDevice phys, const std::string& path)
{
    auto cpu = LoadCpuOnlyForTests(path);
    if (!cpu) return std::nullopt;
    SkinnedMesh m;
    if (!m.Upload(device, phys, *cpu)) return std::nullopt;
    return m;
}
```

- [ ] **Step 11.4 — Build**

`src/CMakeLists.txt` : ajouter `src/client/render/skinned/SkinnedMesh.cpp`.

Run `cmake --build build --target lcdlln_client` → succès.

- [ ] **Step 11.5 — Commit**

```bash
git add src/client/render/skinned/SkinnedMesh.h src/client/render/skinned/SkinnedMesh.cpp src/client/render/skinned/SkinnedMeshLoader.h src/client/render/skinned/SkinnedMeshLoader.cpp src/CMakeLists.txt
git commit -m "feat(skinned): SkinnedMesh GPU upload + Loader::Load (11/17)"
```

---

## Task 12 — Vertex shader `skinned_gbuffer.vert`

**Files:**
- Create: `game/data/shaders/skinned_gbuffer.vert`
- Build : `game/data/shaders/skinned_gbuffer.vert.spv` (généré par `compile_game_shaders.ps1`)

Stratégie : on **réutilise `gbuffer_geometry.frag` tel quel** (mêmes outputs A/B/C/Velocity). Le seul nouveau shader est le `.vert`.

- [ ] **Step 12.1 — Écrire `skinned_gbuffer.vert`**

```glsl
#version 450
// Skinned variant of gbuffer_geometry.vert.
// - Per-vertex bone indices (uint16x4, widened to uvec4) at location 7
// - Per-vertex weights (float4) at location 8
// - Per-draw bone matrix palette via SSBO set 1 binding 0
// Outputs: same as gbuffer_geometry.vert → paired with gbuffer_geometry.frag unchanged.

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 instanceRow0;
layout(location = 4) in vec4 instanceRow1;
layout(location = 5) in vec4 instanceRow2;
layout(location = 6) in vec4 instanceRow3;
layout(location = 7) in uvec4 inBoneIdx;
layout(location = 8) in vec4  inWeights;

layout(push_constant) uniform PushConstants {
    mat4 prevViewProj;
    mat4 viewProj;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

layout(set = 1, binding = 0) readonly buffer BonesSSBO {
    mat4 bones[];
};

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUv;
layout(location = 2) out vec4 vPrevClip;
layout(location = 3) out vec4 vCurrClip;

void main() {
    // Build the per-vertex skinning matrix as a weighted sum of bone matrices.
    mat4 skin =
        inWeights.x * bones[inBoneIdx.x] +
        inWeights.y * bones[inBoneIdx.y] +
        inWeights.z * bones[inBoneIdx.z] +
        inWeights.w * bones[inBoneIdx.w];

    vec4 posSkinned = skin * vec4(inPosition, 1.0);
    vec3 normalSkinned = mat3(skin) * inNormal;

    mat4 instanceMatrix = mat4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
    vec4 worldPos = instanceMatrix * posSkinned;

    vec4 prevClip = pc.prevViewProj * worldPos;
    vec4 currClip = pc.viewProj * worldPos;
    gl_Position = currClip;

    vNormal = mat3(instanceMatrix) * normalSkinned;
    vUv = inUv;
    vPrevClip = prevClip;
    vCurrClip = currClip;
}
```

- [ ] **Step 12.2 — Compiler les shaders**

Run :
```powershell
.\tools\compile_game_shaders.ps1
```
Expected: `game/data/shaders/skinned_gbuffer.vert.spv` produit, pas d'erreur GLSL.

- [ ] **Step 12.3 — Commit**

```bash
git add game/data/shaders/skinned_gbuffer.vert game/data/shaders/skinned_gbuffer.vert.spv
git commit -m "feat(shaders): skinned_gbuffer.vert (reuses gbuffer_geometry.frag) (12/17)"
```

---

## Task 13 — `SkinnedRenderer::Init` (pipeline Vulkan)

**Files:**
- Create: `src/client/render/skinned/SkinnedRenderer.h`
- Create: `src/client/render/skinned/SkinnedRenderer.cpp`

Le pipeline mirroir de `GeometryPass`, avec ces deux différences :
1. Vertex input layout étendu (stride 56 + locations 7, 8 pour boneIdx/weights).
2. `frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE` (glTF mandate CCW). **Différent** du GeometryPass actuel qui est CW.
3. Set 1 binding 0 : SSBO bone matrices.

- [ ] **Step 13.1 — `SkinnedRenderer.h`**

```cpp
#pragma once

#include "src/client/render/skinned/SkinnedMesh.h"

#include <vulkan/vulkan.h>

namespace engine::render::skinned
{

class SkinnedRenderer
{
public:
    bool Init(VkDevice device,
              VkPhysicalDevice physicalDevice,
              VkFormat formatA, VkFormat formatB, VkFormat formatC,
              VkFormat formatVelocity, VkFormat depthFormat,
              const uint32_t* vertSpirv, size_t vertWordCount,
              const uint32_t* fragSpirv, size_t fragWordCount,
              VkDescriptorSetLayout materialLayout,
              uint32_t maxBonesPerSkeleton);

    // Records the draw command for one skinned avatar at given pose.
    void Record(VkDevice device, VkCommandBuffer cmd,
                VkExtent2D extent,
                VkRenderPass renderPass, VkFramebuffer framebuffer,
                const float* prevViewProj, const float* viewProj,
                const SkinnedMesh& mesh,
                const std::vector<engine::math::Mat4>& finalBoneMatrices,
                VkDescriptorSet materialDescriptorSet,
                const float* modelMatrixRowMajor4x4,
                uint32_t materialIndex);

    void Destroy(VkDevice device);
    bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

private:
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_boneSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_boneDescriptorSet = VK_NULL_HANDLE;
    VkBuffer m_boneSsbo = VK_NULL_HANDLE;
    VkDeviceMemory m_boneSsboMemory = VK_NULL_HANDLE;
    VkBuffer m_modelInstanceBuffer = VK_NULL_HANDLE;   // 1 mat4 per-instance, re-mapped each Record
    VkDeviceMemory m_modelInstanceMemory = VK_NULL_HANDLE;
    uint32_t m_maxBones = 0;
};

}  // namespace engine::render::skinned
```

- [ ] **Step 13.2 — `SkinnedRenderer.cpp` — Init**

Implémenter `Init` en copiant la structure de `GeometryPass::Init` (lignes ~300-500 de `GeometryPass.cpp`), **avec ces modifications** :

1. **Vertex input** : 2 bindings, 9 attributes
   ```cpp
   VkVertexInputBindingDescription bindings[2] = {};
   bindings[0].binding = 0; bindings[0].stride = 56; bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
   bindings[1].binding = 1; bindings[1].stride = 64; bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

   VkVertexInputAttributeDescription attrs[9] = {};
   attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};          // pos
   attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};         // normal
   attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, 24};            // uv
   attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0};       // instance row0
   attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16};      // instance row1
   attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32};      // instance row2
   attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48};      // instance row3
   attrs[7] = {7, 0, VK_FORMAT_R16G16B16A16_UINT, 32};        // boneIdx
   attrs[8] = {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 40};      // weights
   ```
2. **Rasterization** : `frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE`, `cullMode = VK_CULL_MODE_BACK_BIT`. **Différent du GeometryPass actuel — ne pas confondre.**
3. **Descriptor set layouts** : 2 sets. Set 0 = material (passé en paramètre). Set 1 = bone SSBO (créé ici).
4. **Push constants** : identiques à `gbuffer_geometry.vert` (144 bytes).
5. **Bone SSBO** : alloué une fois pour `maxBonesPerSkeleton` matrices (256 par défaut), host-visible + coherent pour upload simple.
6. **Bone descriptor set** : 1 set créé à partir d'un pool dédié, bound à l'SSBO.

**Stratégie d'implémentation** : ouvrir `src/client/render/GeometryPass.cpp::Init` et **copier 1:1** la structure, en appliquant uniquement les deltas suivants :

| Bloc | Action | Détail |
|---|---|---|
| Shader modules (vsModule, fsModule) | **Inchangé** | Même appel `vkCreateShaderModule` pour vert+frag |
| `VkPipelineShaderStageCreateInfo stages[2]` | **Inchangé** | Vert = skinned_gbuffer.vert.spv, frag = gbuffer_geometry.frag.spv |
| `VkPipelineVertexInputStateCreateInfo` | **Remplacer** | Utiliser les 2 bindings + 9 attrs ci-dessus au lieu des bindings actuels du GeometryPass |
| `VkPipelineInputAssemblyStateCreateInfo` | **Inchangé** | TRIANGLE_LIST |
| `VkPipelineRasterizationStateCreateInfo` | **MODIFIER** | `frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE` (au lieu de CW) — **point critique** |
| `VkPipelineMultisampleStateCreateInfo` | **Inchangé** | SAMPLE_COUNT_1_BIT |
| `VkPipelineDepthStencilStateCreateInfo` | **Inchangé** | depthTest+depthWrite, LESS_OR_EQUAL |
| `VkPipelineColorBlendStateCreateInfo` | **Inchangé** | 4 attachments (A, B, C, Velocity), no blend |
| `VkPipelineDynamicStateCreateInfo` | **Inchangé** | VIEWPORT + SCISSOR |
| Descriptor set layouts | **Étendre** | Set 0 = matériel (réutiliser celui passé en param). Set 1 = nouveau bone SSBO layout (1 binding STORAGE_BUFFER, VERTEX_BIT) |
| Pipeline layout | **Étendre** | 2 set layouts au lieu de 1, push constants identiques (144 bytes) |
| Bone SSBO + descriptor set | **Nouveau** | À créer ici : `vkCreateBuffer` (size = maxBones × 64), host-visible+coherent, descriptor set unique alloué d'un pool dédié, écrit avec `vkUpdateDescriptorSets` pour bind l'SSBO |
| `m_modelInstanceBuffer` | **Nouveau** | VkBuffer host-visible de 64 bytes (1 mat4), pour le modelMatrix par draw — alloué ici, mappé/écrit dans Record |
| Identity-instance fallback (legacy) | **Supprimer** | Le SkinnedRenderer écrit toujours `m_modelInstanceBuffer` à chaque Record, pas de fallback identity |

- [ ] **Step 13.3 — Build sanity check**

`cmake --build build --target lcdlln_client` → doit compiler (pas de smoke test fonctionnel encore).

- [ ] **Step 13.4 — Commit**

```bash
git add src/client/render/skinned/SkinnedRenderer.h src/client/render/skinned/SkinnedRenderer.cpp src/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(skinned): SkinnedRenderer pipeline init (CCW winding) (13/17)

Pipeline Vulkan dédié au mesh skinné, mirroir de GeometryPass avec :
- vertex input étendu (boneIdx@7 + weights@8, stride 56)
- frontFace=CCW (glTF spec) — NE PAS confondre avec GeometryPass actuel
  qui est CW (cube mesh CW)
- SSBO bone matrices set 1 binding 0 (max 256 bones)
- réutilise gbuffer_geometry.frag tel quel (mêmes outputs G-buffer)

cf. CLAUDE.md section "Convention winding / face culling".

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14 — `SkinnedRenderer::Record`

**Files:** Modify `src/client/render/skinned/SkinnedRenderer.cpp`.

- [ ] **Step 14.1 — Implémenter `Record`**

Dans `SkinnedRenderer.cpp`, ajouter :
```cpp
void SkinnedRenderer::Record(VkDevice device, VkCommandBuffer cmd,
                             VkExtent2D extent,
                             VkRenderPass renderPass, VkFramebuffer framebuffer,
                             const float* prevViewProj, const float* viewProj,
                             const SkinnedMesh& mesh,
                             const std::vector<engine::math::Mat4>& finalBoneMatrices,
                             VkDescriptorSet materialDescriptorSet,
                             const float* modelMatrixRowMajor4x4,
                             uint32_t materialIndex)
{
    // Upload bone matrices to host-visible SSBO.
    const size_t boneBytes = std::min<size_t>(finalBoneMatrices.size(), m_maxBones) * sizeof(engine::math::Mat4);
    if (boneBytes > 0) {
        void* mapped = nullptr;
        vkMapMemory(device, m_boneSsboMemory, 0, boneBytes, 0, &mapped);
        std::memcpy(mapped, finalBoneMatrices.data(), boneBytes);
        vkUnmapMemory(device, m_boneSsboMemory);
    }

    // Begin render pass (caller's responsibility usually — adapt to the engine's pattern).
    // Bind pipeline.
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    // Dynamic viewport + scissor.
    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Bind descriptor sets: set 0 = material, set 1 = bone SSBO.
    VkDescriptorSet sets[2] = {materialDescriptorSet, m_boneDescriptorSet};
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 2, sets, 0, nullptr);

    // Push constants : prevViewProj (64) + viewProj (64) + materialIndex (16 with padding) = 144 bytes.
    struct PC {
        float prevViewProj[16];
        float viewProj[16];
        uint32_t materialIndex;
        uint32_t pad0, pad1, pad2;
    } pc;
    std::memcpy(pc.prevViewProj, prevViewProj, sizeof(float) * 16);
    std::memcpy(pc.viewProj, viewProj, sizeof(float) * 16);
    pc.materialIndex = materialIndex;
    pc.pad0 = pc.pad1 = pc.pad2 = 0;
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PC), &pc);

    // Update the model instance buffer with the per-draw model matrix.
    {
        void* mapped = nullptr;
        vkMapMemory(device, m_modelInstanceMemory, 0, 64, 0, &mapped);
        std::memcpy(mapped, modelMatrixRowMajor4x4, 64);
        vkUnmapMemory(device, m_modelInstanceMemory);
    }

    // Bind vertex buffer (per-vertex, binding 0) + instance buffer (per-instance, binding 1).
    VkBuffer vbufs[2] = {mesh.vertexBuffer, m_modelInstanceBuffer};
    VkDeviceSize offs[2] = {0, 0};
    vkCmdBindVertexBuffers(cmd, 0, 2, vbufs, offs);
    vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}
```

**Note sur la cohérence frame-in-flight** : `m_boneSsboMemory` et `m_modelInstanceMemory` étant host-coherent et mappés/écrits à chaque Record, si le moteur a déjà des frames in-flight ≥ 2 (FIF), il faut soit (a) dupliquer les deux buffers par frame, soit (b) ajouter une barrière mémoire avant `vkCmdBindVertexBuffers`. L'engineer doit vérifier la convention de `GeometryPass::Record` (qui fait face au même problème pour son materialBuffer) et l'appliquer. Pour A avec 1 seul avatar, l'overhead de FIF×2 buffers est négligeable.

- [ ] **Step 14.2 — Build**

`cmake --build build --target lcdlln_client` → succès.

- [ ] **Step 14.3 — Commit**

```bash
git add src/client/render/skinned/SkinnedRenderer.cpp
git commit -m "feat(skinned): SkinnedRenderer::Record with bone SSBO upload (14/17)"
```

---

## Task 15 — Intégration `Engine.cpp` client post-EnterWorld + smoke test

**Files:**
- Modify: `src/client/app/Engine.cpp` (autour ligne 3720, branche `!m_editorMode` post-EnterWorld)
- Modify: `src/client/app/Engine.h` (nouveaux membres)

- [ ] **Step 15.1 — Ajouter les membres dans `Engine.h`**

Dans `Engine.h`, dans la classe `Engine`, ajouter (section private membres) :
```cpp
    // Sous-projet A : skinned humanoid avatar (replaces cube avatar_placeholder.mesh post-EnterWorld).
    engine::render::skinned::SkinnedRenderer m_skinnedRenderer;
    std::optional<engine::render::skinned::SkinnedMesh> m_playerSkinnedMesh;
    std::chrono::steady_clock::time_point m_playerAnimStartTime;
    bool m_skinnedAvatarReady = false;
```
Inclure :
```cpp
#include "src/client/render/skinned/SkinnedRenderer.h"
#include "src/client/render/skinned/SkinnedMesh.h"
#include <optional>
#include <chrono>
```

- [ ] **Step 15.2 — Initialiser le SkinnedRenderer + load Y Bot au post-EnterWorld**

Dans `Engine.cpp`, à la branche post-EnterWorld (autour ligne 3720, là où `LoadMesh("meshes/avatar_placeholder.mesh")` est appelé), **ajouter** après cette ligne :
```cpp
// Sous-projet A : try to load skinned humanoid. Fall back to cube on failure.
if (!m_skinnedAvatarReady) {
    // Load shaders.
    auto vertSpv = ReadSpirvFile(m_config->paths.gameDataDir + "/shaders/skinned_gbuffer.vert.spv");
    auto fragSpv = ReadSpirvFile(m_config->paths.gameDataDir + "/shaders/gbuffer_geometry.frag.spv");
    if (vertSpv && fragSpv) {
        VkDescriptorSetLayout matLayout = m_pipeline->GetMaterialDescriptorCache().GetSetLayout();
        if (m_skinnedRenderer.Init(m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(),
                                    m_pipeline->GetGBufferFormatA(), m_pipeline->GetGBufferFormatB(),
                                    m_pipeline->GetGBufferFormatC(), m_pipeline->GetVelocityFormat(),
                                    m_pipeline->GetDepthFormat(),
                                    vertSpv->data(), vertSpv->size(),
                                    fragSpv->data(), fragSpv->size(),
                                    matLayout, 256)) {
            const std::string yBotPath = m_config->paths.gameDataDir + "/models/avatars/y_bot/y_bot.glb";
            auto loaded = engine::render::skinned::SkinnedMeshLoader::Load(
                m_vkDeviceContext.GetDevice(), m_vkDeviceContext.GetPhysicalDevice(), yBotPath);
            if (loaded) {
                m_playerSkinnedMesh = std::move(*loaded);
                m_playerAnimStartTime = std::chrono::steady_clock::now();
                m_skinnedAvatarReady = true;
                LOG_INFO(Render, "[Engine] Skinned avatar Y Bot loaded ({} bones, {} clips)",
                         m_playerSkinnedMesh->skeleton.bones.size(),
                         m_playerSkinnedMesh->clips.size());
            } else {
                LOG_WARN(Render, "[Engine] Y Bot load failed; falling back to cube placeholder");
            }
        }
    }
}
```

- [ ] **Step 15.3 — Au moment du draw : remplacer le cube si avatar prêt**

Trouver (dans la même branche post-EnterWorld) le draw du cube via `GeometryPass`. **Avant** cet appel, ajouter une garde :
```cpp
if (m_skinnedAvatarReady && m_playerSkinnedMesh) {
    // Compute current animation time (loop).
    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - m_playerAnimStartTime).count();
    const engine::render::skinned::AnimationClip* walkClip =
        m_playerSkinnedMesh->FindClip("Walking");
    if (!walkClip && !m_playerSkinnedMesh->clips.empty()) {
        walkClip = &m_playerSkinnedMesh->clips[0];  // first clip as fallback
    }
    if (walkClip && walkClip->duration > 0.0f) {
        float t = std::fmod(elapsed, walkClip->duration);
        auto locals = engine::render::skinned::AnimationSampler::SamplePose(
            m_playerSkinnedMesh->skeleton, *walkClip, t);
        auto globals = engine::render::skinned::AnimationSampler::ComputeGlobalMatrices(
            m_playerSkinnedMesh->skeleton, locals);
        auto finals = engine::render::skinned::AnimationSampler::ComputeFinalMatrices(
            m_playerSkinnedMesh->skeleton, globals);

        m_skinnedRenderer.Record(m_vkDeviceContext.GetDevice(), cmd,
                                  m_swapchainExtent,
                                  /*renderPass*/ m_gbufferRenderPass,
                                  /*framebuffer*/ m_currentGbufferFramebuffer,
                                  prevViewProjArray,
                                  viewProjArray,
                                  *m_playerSkinnedMesh,
                                  finals,
                                  m_avatarMaterialDescriptorSet,
                                  modelMatrixArray,
                                  m_avatarMaterialIndex);
    }
} else {
    // Fallback : original cube draw.
    // [keep existing GeometryPass::Record call here unchanged]
}
```

(Adapter les noms `m_gbufferRenderPass`, `m_currentGbufferFramebuffer`, `prevViewProjArray`, `viewProjArray`, `modelMatrixArray`, `m_avatarMaterialDescriptorSet`, `m_avatarMaterialIndex` à ce qui existe vraiment dans Engine.cpp au moment de l'implémentation — ces noms sont indicatifs.)

- [ ] **Step 15.4 — Détruire le SkinnedMesh + Renderer au shutdown**

Dans `Engine::Shutdown` (ou équivalent), ajouter :
```cpp
if (m_playerSkinnedMesh) {
    m_playerSkinnedMesh->Destroy(m_vkDeviceContext.GetDevice());
    m_playerSkinnedMesh.reset();
}
m_skinnedRenderer.Destroy(m_vkDeviceContext.GetDevice());
m_skinnedAvatarReady = false;
```

- [ ] **Step 15.5 — Build + smoke test visuel**

Run :
```powershell
cmake --build build --target lcdlln_client
.\build\Release\lcdlln_client.exe
```

Cliquer "Jouer" sur un personnage existant → EnterWorld.

**Critères de succès** :
- ✅ Un personnage humanoïde gris (Y Bot) est visible à la position du joueur, **debout sur le sol**.
- ✅ Il joue une animation de marche **en boucle, en permanence** (pas figé).
- ✅ Le terrain reste visible (pas de régression cf. CLAUDE.md).
- ✅ Caméra orbitale toujours fonctionnelle.
- ✅ Aucune erreur Vulkan validation layer dans les logs.

**Critère de fallback** :
- Si y_bot.glb est renommé/supprimé, le client doit afficher le cube placeholder + un log warning. Pas de crash.

Capturer un screenshot (`Win+Shift+S`) du résultat, à joindre à la PR.

- [ ] **Step 15.6 — Commit**

```bash
git add src/client/app/Engine.cpp src/client/app/Engine.h
git commit -m "$(cat <<'EOF'
feat(client): replace cube avatar with skinned humanoid Y Bot (15/17)

Sous-projet A étape 15 : intégration runtime skinné dans Engine.cpp.
Post-EnterWorld, le cube avatar_placeholder.mesh est remplacé par Y Bot
qui joue son clip "Walking" en boucle permanente. Le bobY synthétique
de l'OrbitalCameraController n'est plus appliqué — c'est la skinning
animation qui produit le mouvement.

Fallback : si y_bot.glb absent ou load échoue, le cube est conservé
avec un log warning. Pas de crash.

Smoke test : screenshot joint à la PR.

Déploiement : ✅ client uniquement, pas de redéploiement serveur.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 16 — Intégration `Engine.cpp` éditeur (debug avatar)

**Files:** Modify `src/client/app/Engine.cpp:3525-3548`.

- [ ] **Step 16.1 — Localiser le bloc éditeur debug avatar**

Lire `src/client/app/Engine.cpp` autour de la ligne 3525-3548. C'est le bloc gardé par `if (m_worldEditorExe)` qui draw l'avatar humanoïde de debug en mode éditeur. Aujourd'hui il dessine le cube via `GeometryPass`.

- [ ] **Step 16.2 — Mirroir de la logique client**

Ajouter avant ou autour de ce bloc, la même init paresseuse + draw que dans Task 15.3 — mais gardée par `if (m_worldEditorExe)`. Le `m_skinnedAvatarReady` peut être partagé entre les deux modes (un seul Y Bot loadé suffit).

```cpp
if (m_worldEditorExe && m_skinnedAvatarReady && m_playerSkinnedMesh) {
    // [duplicate the sample + record block from Task 15.3, using the editor's
    //  view/proj/model matrices and render targets]
}
```

- [ ] **Step 16.3 — Build + smoke test éditeur**

Run :
```powershell
.\build\Release\lcdlln_world_editor.exe
```
Vérifier : Y Bot animé est visible dans la viewport de l'éditeur (à la place du cube debug). Capturer un screenshot.

- [ ] **Step 16.4 — Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "feat(editor): skinned Y Bot avatar in world editor viewport (16/17)"
```

---

## Task 17 — Update CODEBASE_MAP.md

**Files:** Modify `CODEBASE_MAP.md`.

- [ ] **Step 17.1 — Ajouter une nouvelle section après §14**

Insérer entre la section "14. Vue 3ème personne" et la section "15. Menu pause in-game" :

```markdown
## 14.5 Runtime skinning + animation (sous-projet A — chantier 2026-05-18)

Premier sous-projet de la décomposition A→K (animation + races + animaux).
Remplace le cube `avatar_placeholder.mesh` par un humanoïde Mixamo skinné
qui joue une animation en boucle permanente. Voir
`docs/superpowers/specs/2026-05-18-skinning-animation-foundations-design.md`
pour le contexte complet.

### Format runtime : glTF 2.0 binaire (.glb)

Asset Mixamo téléchargé en FBX Binary → converti par
`tools/asset_pipeline/fbx_to_gltf.ps1` (qui appelle `FBX2glTF.exe`,
fork Godot v0.9.7, gitignored) → `.glb` final dans
`game/data/models/<category>/<entity>/<entity>.glb`.

Parsing au runtime via **cgltf** (single-header MIT, vendored dans
`external/cgltf/cgltf.h`).

### Fichiers clés

| Fichier | Rôle |
|---|---|
| `external/cgltf/cgltf.h` | Parser glTF single-header MIT (v1.14 pinné). |
| `src/shared/math/Quat.h/.cpp` | Type quaternion + slerp (nouveau). |
| `src/client/render/skinned/Skeleton.h/.cpp` | Bones + parent index + bind transforms. |
| `src/client/render/skinned/AnimationClip.h/.cpp` | Keyframes T/R/S par bone + interp. |
| `src/client/render/skinned/AnimationSampler.h/.cpp` | Sample(t)+ComputeGlobal+ComputeFinal. |
| `src/client/render/skinned/SkinnedMesh.h/.cpp` | VkBuffers vertex/index + ref Skeleton. |
| `src/client/render/skinned/SkinnedMeshLoader.h/.cpp` | cgltf → SkinnedMeshCpuData → GPU upload. |
| `src/client/render/skinned/SkinnedRenderer.h/.cpp` | Pipeline Vulkan dédié skinné. |
| `game/data/shaders/skinned_gbuffer.vert` | Applique skinning par matrices d'os, output identique à gbuffer_geometry.vert. |
| `game/data/models/avatars/y_bot/y_bot.glb` | Premier humanoïde de référence (Mixamo Y Bot + clip "Walking"). |
| `tools/asset_pipeline/` | Scripts PowerShell de download FBX2glTF + conversion FBX→glb. |

### Convention winding

Le pipeline `SkinnedRenderer` utilise **`frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE`**
parce que la spec glTF mandate CCW pour les faces front. C'est **différent**
du `GeometryPass` actuel (CW, correct pour le cube avatar_placeholder.mesh
qui est CW dans son fichier). Cf. CLAUDE.md section "Convention winding /
face culling" : chaque pipeline a sa propre convention selon la source du mesh.

### Limites (à enrichir dans sous-projets B/C)

- Une seule animation ("Walking") jouée en boucle permanente, pas de state machine.
- Pas de variantes raciales (Y Bot affiché pour toutes les races).
- Pas de remote players visibles animés (avatar local uniquement).
- Pas de texture diffuse (couleur unie via avatar_skin placeholder).
- Cf. section 8 du spec pour la liste exhaustive du hors-périmètre A.
```

- [ ] **Step 17.2 — Mettre à jour §14 (cube → humanoïde)**

Remplacer dans §14 la ligne :
```
| `game/data/meshes/avatar_placeholder.mesh` | Cube 0.5×1.8×0.5 m (pieds Y=0), placeholder visuel pour l'avatar. ... |
```
par :
```
| `game/data/meshes/avatar_placeholder.mesh` | Cube 0.5×1.8×0.5 m — **fallback** si chargement du modèle skinné échoue. L'avatar par défaut est désormais `game/data/models/avatars/y_bot/y_bot.glb` (cf. §14.5). |
```

Dans la sous-section "Limites assumées" du §14, **retirer** ou **adoucir** :
- ❌ ~~"Avatar = cube monochrome (pas encore de mesh humanoïde texturé)."~~
- ✅ Remplacer par : "Avatar = humanoïde Y Bot animé en boucle Walking (sous-projet A). Texture diffuse, variantes raciales, et state machine sont les sous-projets C et B respectivement."

- [ ] **Step 17.3 — Commit**

```bash
git add CODEBASE_MAP.md
git commit -m "$(cat <<'EOF'
docs(map): add §14.5 skinning runtime + update §14 avatar (17/17)

Clôture le sous-projet A. CODEBASE_MAP.md documente :
- nouveau runtime skinné + animation (8 fichiers .h/.cpp)
- convention winding CCW pour pipeline skinné (vs CW GeometryPass)
- asset pipeline FBX→glTF
- limites assumées (renvoyées vers sous-projets B/C)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Vérification finale & PR

- [ ] **Tous les tests verts**

```powershell
ctest --test-dir build --output-on-failure
```
Attendu : tous les tests existants + 5 nouveaux (`quat_tests`, `skeleton_tests`, `animation_clip_tests`, `animation_sampler_tests`, `skinned_mesh_loader_tests`) → Passed.

- [ ] **Pas de warning Vulkan validation layer**

Lancer le client en debug, surveiller la console — aucun `[VK_VALIDATION_ERROR]` ou `[VK_VALIDATION_WARNING]` lié au nouveau pipeline.

- [ ] **Vérification smoke test**

- Client : Y Bot animé visible post-EnterWorld ✅
- Éditeur : Y Bot animé visible dans la viewport ✅
- Fallback : renommer `y_bot.glb` → cube revient + log warning ✅
- Terrain : toujours visible (pas de régression winding) ✅

- [ ] **Créer la PR**

Description PR doit inclure :
- Lien vers le spec `docs/superpowers/specs/2026-05-18-skinning-animation-foundations-design.md`
- Screenshots client + éditeur avec Y Bot animé
- **Ligne déploiement** : `**Déploiement** : ✅ client uniquement, pas de redéploiement serveur.`
- Liste des 5 nouveaux tests verts
- Note explicite : "Sous-projet A de la décomposition A→K. Sous-projets B (state machine locomotion) et C (variantes raciales) peuvent maintenant être brainstormés en parallèle."

---

## Périmètre explicitement hors plan (renvoi sous-projets ultérieurs)

Rappel des sous-projets qui dépendent de A et le suivent :

| Sous-projet | Périmètre |
|---|---|
| **B** | State machine Idle/Walk/Run, transitions/blending, surface-aware speed (eau/sable/neige), saut hauteur/longueur, remote players animés |
| **C** | 6 squelettes raciaux, sélection conditionnelle race→modèle, code couleur placeholder par race |
| **D** | Preview 3D dans CharacterCreate + sliders morpho/taille |
| **E** | Actions sociales (saluer, s'asseoir, s'allonger, mourir, ouvrir conteneur, manger/boire) |
| **F** | Attach points armes/sac à dos, cycles d'attaque 1H/2H, casting magie |
| **G** | Cycle nage + détection in-water |
| **H** | Montures (perso ↔ cheval) |
| **I** | Squelettes animaux (cheval, vache, cochon, oiseau, serpent, poisson, dragon…) |
| **J** | IA d'ambiance "ils sont en vie" (look-at, wander, idle micro-anims) |
| **K** | Convention assets transverse "1 dossier par modèle" (déjà appliquée dans A pour avatars, à étendre) |
