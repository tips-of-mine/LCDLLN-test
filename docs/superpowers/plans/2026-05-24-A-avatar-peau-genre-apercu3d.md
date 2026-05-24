# Avatar : peau par genre + visible en jeu + aperçu 3D — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Corriger l'affichage de la peau de l'avatar par genre en jeu (peau féminine + peau visible), rendre l'avatar réellement en 3D dans l'écran de création, et améliorer le vocabulaire du sélecteur.

**Architecture:** Une **fonction pure** de routage de matériaux (peau vs habit par sous-maillage), partagée entre le rendu in-world et un **nouveau rendu d'aperçu forward dédié** dans `RacePreviewViewport`. Le rendu in-world est refactoré pour utiliser cette fonction + une instrumentation de diagnostic. L'aperçu sert aussi d'outil d'observation pour les bugs #1/#2.

**Tech Stack:** C++20, Vulkan 1.x, CMake (lib `engine_core` + helper de test `lcdlln_add_simple_test`), GLSL → SPIR-V via `tools/compile_game_shaders.ps1` (glslangValidator), framework de tests minimal custom (`REQUIRE` + `main`).

---

## ⚠️ Contrainte impérative : pas de build local

- **Aucun build/compilation possible dans l'environnement de l'implémenteur.** `cmake`/`glslangValidator`/MSVC sont absents.
- Chaque étape « Vérification » ci-dessous **ne peut PAS être exécutée localement**. Elle est :
  - **compilée par la CI** au push de la PR (build GitHub, **compile-only**, pas de `ctest`), et
  - **validée au runtime par l'utilisateur** (client Windows/Vulkan) une fois la PR buildée.
- L'implémenteur : écrit le code, **relit attentivement**, commite. Ne **jamais** prétendre qu'un test « passe » ou qu'un rendu est « vérifié ».
- **Shaders** : l'implémenteur écrit le GLSL ; la génération des `.spv` se fait via `tools/compile_game_shaders.ps1` **côté utilisateur** (Vulkan SDK requis). Mentionner explicitement dans la PR que les `.spv` des nouveaux shaders doivent être régénérés et committés par l'utilisateur (ou par l'implémenteur s'il obtient l'outil).

## Découpage en 2 PR (recommandé)

- **PR 1 = Phase 1 (Tâches 1–7)** : fonction pure + test + refactor in-world + diagnostic + vocabulaire. Indépendamment livrable ; donne un **retour de diagnostic** immédiat (logs) sur #1/#2 et corrige le vocabulaire. Faible risque.
- **PR 2 = Phase 2 (Tâches 8–14)** : aperçu 3D forward. Plus gros, isolé, dépend de la fonction pure de la Phase 1.

Les deux sont **client uniquement** → ✅ pas de redéploiement serveur.

---

# PHASE 1 — Fonction pure, refactor in-world, diagnostic, vocabulaire

## Task 1: Fonction pure de routage de matériaux

**Files:**
- Create: `src/client/render/skinned/AvatarMaterialRouting.h`
- Create: `src/client/render/skinned/AvatarMaterialRouting.cpp`

- [ ] **Step 1: Écrire le header**

Fichier `src/client/render/skinned/AvatarMaterialRouting.h` :

```cpp
#pragma once

#include "src/client/render/skinned/SkinnedMeshLoader.h"  // SkinnedSubMesh

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render::skinned
{
/// Construit la table d'index de matériau par sous-maillage de l'avatar.
///
/// Chaque sous-maillage dont le `materialName` (après suppression des espaces de
/// début/fin) figure dans `bodyMaterialNames` reçoit `bodyMaterialId` (la PEAU) ;
/// tous les autres reçoivent `outfitMaterialId` (l'HABIT). Le matching est
/// sensible à la casse (noms glTF), insensible aux espaces parasites.
///
/// \param submeshes        Sous-maillages du mesh (parallèle au résultat).
/// \param bodyMaterialNames Noms de matériaux glTF considérés comme peau.
/// \param bodyMaterialId    Id matériau peau (selon le genre). 0 = aucun.
/// \param outfitMaterialId  Id matériau habit (défaut).
/// \return Vecteur parallèle à `submeshes`. **VIDE** si `bodyMaterialId == 0`
///         ou si `submeshes` est vide — l'appelant retombe alors sur le
///         mono-draw habit (comportement historique).
///
/// Fonction pure : aucune dépendance Vulkan, aucun effet de bord.
std::vector<uint32_t> BuildSubmeshMaterialIndices(
    const std::vector<SkinnedSubMesh>& submeshes,
    const std::vector<std::string>&    bodyMaterialNames,
    uint32_t                           bodyMaterialId,
    uint32_t                           outfitMaterialId);
}  // namespace engine::render::skinned
```

- [ ] **Step 2: Écrire l'implémentation**

Fichier `src/client/render/skinned/AvatarMaterialRouting.cpp` :

```cpp
#include "src/client/render/skinned/AvatarMaterialRouting.h"

#include <cctype>

namespace engine::render::skinned
{
namespace
{
    /// Supprime les espaces (et caractères blancs) de début et de fin.
    std::string Trim(const std::string& s)
    {
        std::size_t b = 0, e = s.size();
        while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    }
}  // namespace

std::vector<uint32_t> BuildSubmeshMaterialIndices(
    const std::vector<SkinnedSubMesh>& submeshes,
    const std::vector<std::string>&    bodyMaterialNames,
    uint32_t                           bodyMaterialId,
    uint32_t                           outfitMaterialId)
{
    std::vector<uint32_t> out;
    if (bodyMaterialId == 0u || submeshes.empty())
        return out;  // vide -> l'appelant fait un mono-draw habit

    out.reserve(submeshes.size());
    for (const auto& sub : submeshes)
    {
        const std::string name = Trim(sub.materialName);
        bool isBody = false;
        for (const auto& bn : bodyMaterialNames)
        {
            if (Trim(bn) == name) { isBody = true; break; }
        }
        out.push_back(isBody ? bodyMaterialId : outfitMaterialId);
    }
    return out;
}
}  // namespace engine::render::skinned
```

- [ ] **Step 3: Commit**

```bash
git add src/client/render/skinned/AvatarMaterialRouting.h src/client/render/skinned/AvatarMaterialRouting.cpp
git commit -m "feat(avatar): fonction pure de routage de materiaux peau/habit"
```

## Task 2: Test unitaire de la fonction pure

**Files:**
- Create: `src/client/render/skinned/tests/AvatarMaterialRoutingTests.cpp`

- [ ] **Step 1: Écrire le test (échoue tant que la fonction n'existe pas / régressions)**

Fichier `src/client/render/skinned/tests/AvatarMaterialRoutingTests.cpp` :

```cpp
// Tests for BuildSubmeshMaterialIndices — pure logic, no Vulkan.

#include "src/client/render/skinned/AvatarMaterialRouting.h"

#include <cstdio>
#include <string>
#include <vector>

using engine::render::skinned::BuildSubmeshMaterialIndices;
using engine::render::skinned::SkinnedSubMesh;

namespace
{
    int g_failed = 0;
    #define REQUIRE(cond) do { \
        if (!(cond)) { std::fprintf(stderr, "[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failed; } \
    } while (0)

    SkinnedSubMesh Sub(const char* name)
    {
        SkinnedSubMesh s;
        s.firstIndex = 0;
        s.indexCount = 3;
        s.materialName = name;
        return s;
    }

    // Le sous-maillage peau reçoit l'id peau, l'habit reçoit l'id habit.
    void Test_BodyNameMatched_GetsBodyId()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("MI_Ranger"), Sub("MI_Regular_Male") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Male", "MI_Regular_Female" }, 42u, 7u);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0] == 7u);   // habit
        REQUIRE(out[1] == 42u);  // peau
    }

    // Aucun nom ne matche -> tout habit (cas du bug #2 si les noms ne matchent pas).
    void Test_NoBodyMatch_AllOutfit()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("Alpha_Body"), Sub("Alpha_Joints") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Male" }, 42u, 7u);
        REQUIRE(out.size() == 2);
        REQUIRE(out[0] == 7u);
        REQUIRE(out[1] == 7u);
    }

    // bodyMaterialId == 0 -> vide (l'appelant fera un mono-draw habit).
    void Test_BodyIdZero_ReturnsEmpty()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("MI_Regular_Male") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Male" }, 0u, 7u);
        REQUIRE(out.empty());
    }

    // submeshes vide -> vide.
    void Test_EmptySubmeshes_ReturnsEmpty()
    {
        std::vector<SkinnedSubMesh> empty;
        auto out = BuildSubmeshMaterialIndices(empty, { "MI_Regular_Male" }, 42u, 7u);
        REQUIRE(out.empty());
    }

    // Espaces parasites autour du nom -> matche quand même.
    void Test_TrimWhitespace_StillMatches()
    {
        std::vector<SkinnedSubMesh> subs = { Sub("  MI_Regular_Female  ") };
        auto out = BuildSubmeshMaterialIndices(subs, { "MI_Regular_Female" }, 42u, 7u);
        REQUIRE(out.size() == 1);
        REQUIRE(out[0] == 42u);
    }
}  // namespace

int main()
{
    Test_BodyNameMatched_GetsBodyId();
    Test_NoBodyMatch_AllOutfit();
    Test_BodyIdZero_ReturnsEmpty();
    Test_EmptySubmeshes_ReturnsEmpty();
    Test_TrimWhitespace_StillMatches();
    return g_failed == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Vérification (pas de build local)**

Ne peut pas être exécuté localement. Relire le test : signatures cohérentes avec `AvatarMaterialRouting.h` (Task 1) ; champs `SkinnedSubMesh` = `firstIndex`/`indexCount`/`materialName` (cf. `src/client/render/skinned/SkinnedMeshLoader.h:51-56`). Compile + exécution déléguées à la CI (compile) puis à l'utilisateur (`avatar_material_routing_tests`).

- [ ] **Step 3: Commit**

```bash
git add src/client/render/skinned/tests/AvatarMaterialRoutingTests.cpp
git commit -m "test(avatar): couverture de BuildSubmeshMaterialIndices"
```

## Task 3: Enregistrer la source + le test dans CMake

**Files:**
- Modify: `CMakeLists.txt` (bloc sources `engine_core`, après `SkinnedRenderer.cpp` ~ligne 538)
- Modify: `src/CMakeLists.txt` (après `skinned_mesh_loader_tests` ~ligne 687)

- [ ] **Step 1: Ajouter la source à `engine_core`**

Dans `CMakeLists.txt`, juste après la ligne `src/client/render/skinned/SkinnedRenderer.cpp` :

```cmake
  src/client/render/skinned/SkinnedRenderer.cpp
  src/client/render/skinned/AvatarMaterialRouting.cpp
```

> `AvatarMaterialRouting` est **client-only** (rendu) : ne PAS l'ajouter à la liste `server_app` (server_app ne linke pas engine_core).

- [ ] **Step 2: Ajouter le test**

Dans `src/CMakeLists.txt`, après le bloc `lcdlln_add_simple_test(skinned_mesh_loader_tests ...)` (~ligne 687) :

```cmake
  # Routage de materiaux peau/habit de l'avatar (fonction pure, sans Vulkan).
  lcdlln_add_simple_test(avatar_material_routing_tests
    ${CMAKE_SOURCE_DIR}/src/client/render/skinned/tests/AvatarMaterialRoutingTests.cpp)
```

- [ ] **Step 3: Vérification (pas de build local)**

Relire : `lcdlln_add_simple_test` linke `engine_core` (cf. `cmake/LCDLLNHelpers.cmake:29-35`) qui contient désormais `AvatarMaterialRouting.cpp`. Compilation déléguée CI.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt
git commit -m "build(avatar): compile AvatarMaterialRouting + test dans engine_core"
```

## Task 4: Refactorer le rendu in-world pour utiliser la fonction pure

**Files:**
- Modify: `src/client/app/Engine.cpp` (include en tête + routage inline ~lignes 4745-4758)

- [ ] **Step 1: Ajouter l'include**

En tête de `src/client/app/Engine.cpp`, à côté des autres includes skinned (rechercher `#include "src/client/render/skinned/SkinnedRenderer.h"` et ajouter en dessous) :

```cpp
#include "src/client/render/skinned/AvatarMaterialRouting.h"
```

- [ ] **Step 2: Remplacer le routage inline**

Remplacer le bloc actuel (de `std::vector<uint32_t> submeshMaterialIndices;` jusqu'à la fin du `if (...) { for (...) { ... } }`, ~lignes 4745-4758) par :

```cpp
                                                        // Routage peau/habit par sous-maillage (fonction pure
                                                        // partagee avec l'apercu de creation). Vide si pas de
                                                        // materiau peau ou pas de sous-maillages -> mono-draw habit.
                                                        std::vector<uint32_t> submeshMaterialIndices =
                                                            engine::render::skinned::BuildSubmeshMaterialIndices(
                                                                m_currentSkinnedMesh->submeshes,
                                                                m_avatarBodyMaterialNames,
                                                                bodyMaterialId,
                                                                skinnedMaterialIndex);
```

> Garder le type **non-const** : la lambda du FrameGraph capture `submeshMaterialIndices = std::move(submeshMaterialIndices)` (inchangé, juste en dessous).

- [ ] **Step 3: Vérification (pas de build local)**

Relire : `bodyMaterialId` et `skinnedMaterialIndex` sont déjà déclarés au-dessus (cf. Engine.cpp ~4734 et ~4743) ; `m_avatarBodyMaterialNames` est un membre `std::vector<std::string>`. Comportement **identique** à l'ancien code (même routage) — c'est un refactor sans changement fonctionnel attendu. Compilation déléguée CI.

- [ ] **Step 4: Commit**

```bash
git add src/client/app/Engine.cpp
git commit -m "refactor(avatar): rendu in-world via BuildSubmeshMaterialIndices"
```

## Task 5: Instrumentation de diagnostic peau (in-world)

**Files:**
- Modify: `src/client/app/Engine.h` (nouveau membre)
- Modify: `src/client/app/Engine.cpp` (log après construction de `submeshMaterialIndices`)

- [ ] **Step 1: Ajouter le membre d'état du log**

Dans `src/client/app/Engine.h`, à côté de `std::string m_avatarGender;` (rechercher `m_avatarGender`), ajouter :

```cpp
        /// Genre pour lequel le diagnostic peau a deja ete logge (evite le spam
        /// par frame ; on relogue uniquement au changement de genre).
        std::string m_avatarSkinDiagLoggedGender;
```

- [ ] **Step 2: Logger le diagnostic une fois par changement de genre**

Dans `src/client/app/Engine.cpp`, **juste après** l'affectation de `submeshMaterialIndices` (Task 4, Step 2) et **avant** l'appel à `RecordTerrainChunkBatch`, insérer :

```cpp
                                                        // Diagnostic peau/genre (#1/#2) : logge une seule fois par
                                                        // changement de genre. Revele en clair l'etat runtime :
                                                        // ids materiaux resolus, nb de sous-maillages peau vs habit,
                                                        // et les noms de materiaux reellement charges.
                                                        if (m_avatarSkinDiagLoggedGender != m_avatarGender)
                                                        {
                                                            m_avatarSkinDiagLoggedGender = m_avatarGender;
                                                            std::size_t bodyCount = 0;
                                                            for (uint32_t id : submeshMaterialIndices)
                                                                if (id == bodyMaterialId) ++bodyCount;
                                                            std::string noms;
                                                            for (const auto& sub : m_currentSkinnedMesh->submeshes)
                                                            {
                                                                if (!noms.empty()) noms += ", ";
                                                                noms += sub.materialName.empty() ? "<vide>" : sub.materialName;
                                                            }
                                                            LOG_INFO(Render,
                                                                "[AvatarSkinDiag] genre={} idMale={} idFemale={} bodyId={} habitId={} submeshes={} peau={} noms=[{}]",
                                                                m_avatarGender, m_avatarBodyMaterialIdMale,
                                                                m_avatarBodyMaterialIdFemale, bodyMaterialId,
                                                                skinnedMaterialIndex, m_currentSkinnedMesh->submeshes.size(),
                                                                bodyCount, noms);
                                                            if (bodyMaterialId == 0u)
                                                                LOG_ERROR(Render, "[AvatarSkinDiag] bodyMaterialId=0 -> peau NON routee (texture peau non chargee ?)");
                                                            else if (!submeshMaterialIndices.empty() && bodyCount == 0)
                                                                LOG_WARN(Render, "[AvatarSkinDiag] 0 sous-maillage peau (noms ne matchent pas body_material_names)");
                                                        }
```

- [ ] **Step 3: Vérification (pas de build local)**

Relire : `LOG_INFO/WARN/ERROR(Render, ...)` est le pattern de log utilisé partout dans Engine.cpp ; le format `{}` est celui de spdlog/fmt. Le log ne s'exécute qu'au changement de genre (pas par frame). **À valider par l'utilisateur** : les logs `[AvatarSkinDiag]` apparaissent au lancement et révèlent la cause de #1/#2. Compilation déléguée CI.

- [ ] **Step 4: Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(avatar): log de diagnostic peau/genre (#1/#2)"
```

## Task 6: Vocabulaire du sélecteur — Masculin / Féminin

**Files:**
- Modify: `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp:142-144`

- [ ] **Step 1: Renommer les libellés des RadioButtons**

Remplacer (lignes ~142-144) :

```cpp
			ImGui::RadioButton("Homme", &m_charGender, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Femme", &m_charGender, 1);
```

par :

```cpp
			ImGui::RadioButton("Masculin", &m_charGender, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Féminin", &m_charGender, 1);
```

> Le titre « GENRE » (ligne 140) et le mapping interne (`0 = male`, `1 = female`) restent inchangés. Le fichier est encodé UTF-8 ; « Féminin » (é) utilise un glyphe déjà présent dans la police auth (cf. « Créer »/« Annuler »). À confirmer visuellement par l'utilisateur.

- [ ] **Step 2: Vérification (pas de build local)**

Relire : seuls les libellés changent. Code Windows-only (`#if defined(_WIN32)`) → non compilé sur la CI Linux, validé par l'utilisateur à l'écran de création.

- [ ] **Step 3: Commit**

```bash
git add src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp
git commit -m "feat(avatar): libelles Masculin/Feminin dans le selecteur de genre"
```

## Task 7: Mettre à jour CODEBASE_MAP (Phase 1)

**Files:**
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 1: Documenter le nouveau module + le diagnostic**

Ajouter une entrée décrivant : `src/client/render/skinned/AvatarMaterialRouting.{h,cpp}` (fonction pure de routage peau/habit, partagée in-world/aperçu, testée par `avatar_material_routing_tests`), et le log `[AvatarSkinDiag]` dans `Engine.cpp` (diagnostic peau/genre). Suivre le style des sections existantes du fichier.

- [ ] **Step 2: Commit**

```bash
git add CODEBASE_MAP.md
git commit -m "docs(avatar): CODEBASE_MAP - routage materiaux + diagnostic peau"
```

> **Fin de Phase 1.** Livrable PR 1 (client uniquement). Message PR à inclure : « ✅ client uniquement, pas de redéploiement serveur. Logs `[AvatarSkinDiag]` à relever en jeu pour cibler #1/#2. »

---

# PHASE 2 — Aperçu 3D forward dédié (A3)

> **Note Vulkan / pas de build :** les tâches 8–12 reproduisent des patterns Vulkan **existants** dans le dépôt. Le code de boilerplate (création image/pipeline/render pass) doit être **adapté depuis les fichiers de référence cités** (`SkinnedRenderer.cpp`, `RacePreviewViewport.cpp`) — le reproduire intégralement à l'aveugle (sans compilateur) serait non fiable. Les parties **novatrices** (shaders, logique de draw, câblage) sont données en entier. L'utilisateur valide le rendu après build.

## Task 8: Depth buffer pour l'aperçu

**Files:**
- Modify: `src/client/render/race/RacePreviewViewport.h` (handles depth)
- Modify: `src/client/render/race/RacePreviewViewport.cpp` (Init : créer image+vue depth ; Shutdown : libérer)

- [ ] **Step 1: Déclarer les handles depth**

Dans `RacePreviewViewport.h`, à côté des membres image couleur existants (`m_image`, `m_imageMemory`, `m_imageView`), ajouter :

```cpp
		VkImage        m_depthImage       = VK_NULL_HANDLE;
		VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
		VkImageView    m_depthImageView   = VK_NULL_HANDLE;
```

- [ ] **Step 2: Créer l'image depth dans Init**

Dans `RacePreviewViewport::Init`, **après** la création réussie de l'image couleur + vue (rechercher `vkCreateImageView` de la couleur), créer une image depth `VK_FORMAT_D32_SFLOAT`, usage `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`, mêmes `width`/`height`, tiling OPTIMAL, mémoire `DEVICE_LOCAL` (réutiliser le helper `PickMemoryTypeIndex` déjà présent dans le fichier), puis une `VkImageView` avec `aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT`.

> Pattern à mirrorer : la création de l'image couleur juste au-dessus dans le même fichier (mêmes appels `vkCreateImage` / `vkAllocateMemory` / `vkBindImageMemory` / `vkCreateImageView`), en changeant `format`, `usage` et `aspectMask`.

- [ ] **Step 3: Libérer dans Shutdown**

Dans `RacePreviewViewport::Shutdown`, à côté de la destruction de la vue/image/mémoire couleur, ajouter la destruction symétrique de `m_depthImageView`, `m_depthImage`, `m_depthImageMemory` (avec remise à `VK_NULL_HANDLE`).

- [ ] **Step 4: Vérification (pas de build local)** — relire la symétrie Init/Shutdown (pas de fuite). Compilation déléguée CI.

- [ ] **Step 5: Commit**

```bash
git add src/client/render/race/RacePreviewViewport.h src/client/render/race/RacePreviewViewport.cpp
git commit -m "feat(apercu): depth buffer pour le rendu 3D de l'avatar"
```

## Task 9: Shaders forward de l'aperçu

**Files:**
- Create: `game/data/shaders/skinned_preview.vert`
- Create: `game/data/shaders/skinned_preview.frag`

- [ ] **Step 1: Écrire le vertex shader**

Fichier `game/data/shaders/skinned_preview.vert` (skinning identique à `skinned_gbuffer.vert`, sorties réduites : normale + UV, pas de velocity) :

```glsl
#version 450
// Aperçu de création : skinning forward, sorties minimales (normale monde + UV).
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
    mat4 prevViewProj;  // inutilise ici, conserve pour aligner le layout (144 o)
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

void main() {
    mat4 skin =
        inWeights.x * bones[inBoneIdx.x] +
        inWeights.y * bones[inBoneIdx.y] +
        inWeights.z * bones[inBoneIdx.z] +
        inWeights.w * bones[inBoneIdx.w];
    vec4 posSkinned = skin * vec4(inPosition, 1.0);
    vec3 normalSkinned = mat3(skin) * inNormal;
    mat4 instanceMatrix = mat4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
    vec4 worldPos = instanceMatrix * posSkinned;
    gl_Position = pc.viewProj * worldPos;
    vNormal = mat3(instanceMatrix) * normalSkinned;
    vUv = inUv;
}
```

- [ ] **Step 2: Écrire le fragment shader**

Fichier `game/data/shaders/skinned_preview.frag` (même set bindless 0 que `gbuffer_geometry.frag`, sortie unique éclairée) :

```glsl
#version 450
// Aperçu de création : éclairage directionnel simple, sortie couleur unique.
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUv;

struct MaterialGpuData {
    uint baseColorIndex;
    uint normalIndex;
    uint ormIndex;
    uint flags;
    vec2 tiling;
    vec2 padding;
};

layout(set = 0, binding = 0) uniform sampler2D uTextures[64];
layout(set = 0, binding = 1) readonly buffer MaterialBuffer {
    MaterialGpuData materials[];
} uMaterialBuffer;

layout(push_constant) uniform PushConstants {
    mat4 prevViewProj;
    mat4 viewProj;
    uint materialIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    MaterialGpuData mat = uMaterialBuffer.materials[pc.materialIndex];
    vec2 uv = vUv * mat.tiling;
    vec4 base = texture(uTextures[mat.baseColorIndex], uv);  // sRGB -> linaire au sample
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.4, 0.8, 0.6));  // lumiere cle fixe
    float diff = max(dot(N, L), 0.0);
    float ambient = 0.35;
    vec3 lit = base.rgb * (ambient + diff * 0.85);
    // Attachment couleur en UNORM : on encode gamma pour un rendu correct a l'ecran.
    outColor = vec4(pow(lit, vec3(1.0 / 2.2)), base.a);
}
```

- [ ] **Step 3: Générer les .spv (étape utilisateur)**

⚠️ L'implémenteur ne peut pas compiler. Indiquer dans la PR : exécuter
`powershell -NoProfile -ExecutionPolicy Bypass -File tools/compile_game_shaders.ps1`
(Vulkan SDK requis) pour produire `skinned_preview.vert.spv` et
`skinned_preview.frag.spv`, puis committer les `.spv`.

- [ ] **Step 4: Commit (sources GLSL ; .spv ajoutés ensuite par l'utilisateur)**

```bash
git add game/data/shaders/skinned_preview.vert game/data/shaders/skinned_preview.frag
git commit -m "feat(apercu): shaders forward skinnes (vert + frag)"
```

## Task 10: Pipeline + render pass forward dans RacePreviewViewport

**Files:**
- Modify: `src/client/render/race/RacePreviewViewport.h` (handles pipeline/renderpass/framebuffer/bones)
- Modify: `src/client/render/race/RacePreviewViewport.cpp` (Init : créer ; Shutdown : libérer)
- Modify: signature `Init(...)` pour recevoir `VkPhysicalDevice`, le SPIR-V des 2 shaders, et le `VkDescriptorSetLayout` bindless (set 0). Adapter l'appelant (`AuthImGuiRenderer`/`Engine`).

- [ ] **Step 1: Déclarer les handles**

Dans `RacePreviewViewport.h` :

```cpp
		VkRenderPass     m_renderPass      = VK_NULL_HANDLE;
		VkFramebuffer    m_framebuffer     = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout  = VK_NULL_HANDLE;
		VkPipeline       m_pipeline        = VK_NULL_HANDLE;
		// Bones SSBO (palette de matrices) + descriptor set 1.
		VkBuffer              m_boneBuffer        = VK_NULL_HANDLE;
		VkDeviceMemory        m_boneBufferMemory  = VK_NULL_HANDLE;
		VkDescriptorPool      m_boneDescPool      = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_boneSetLayout     = VK_NULL_HANDLE;
		VkDescriptorSet       m_boneDescSet       = VK_NULL_HANDLE;
		// Instance buffer (1 matrice modele = cadrage de l'avatar).
		VkBuffer       m_instanceBuffer       = VK_NULL_HANDLE;
		VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;
		// Set 0 bindless (materiaux/textures) fourni par l'appelant.
		VkDescriptorSet m_materialDescSet = VK_NULL_HANDLE;
```

- [ ] **Step 2: Render pass + framebuffer (color + depth)**

Dans `Init`, créer un `VkRenderPass` à 2 attachements : couleur `R8G8B8A8_UNORM` (loadOp CLEAR, storeOp STORE, finalLayout `SHADER_READ_ONLY_OPTIMAL`) + depth `D32_SFLOAT` (loadOp CLEAR, storeOp DONT_CARE). Puis un `VkFramebuffer` avec `m_imageView` + `m_depthImageView`.

> Pattern à mirrorer : la création du render pass GBuffer/forward dans `SkinnedRenderer.cpp` (`SkinnedRenderer::Init`) et la convention de layouts de `RacePreviewViewport::Render` (l'image couleur finit en `SHADER_READ_ONLY_OPTIMAL` pour ImGui).

- [ ] **Step 3: Bones SSBO + descriptor set 1**

Créer `m_boneBuffer` (host-visible coherent, taille `256 * sizeof(mat4)`), `m_boneSetLayout` (1 storage buffer, stage VERTEX), `m_boneDescPool`, allouer `m_boneDescSet`, et l'écrire vers `m_boneBuffer`.

> Pattern à mirrorer : `SkinnedRenderer::Init` (création du bone SSBO + layout + pool + set), version **simple slot unique** (pas de ring `kFrameSlots` : l'aperçu rend hors de la boucle FIF principale).

- [ ] **Step 4: Instance buffer (matrice de cadrage)**

Créer `m_instanceBuffer` (host-visible coherent, `sizeof(mat4)`), rempli avec la matrice modèle de cadrage de l'avatar (translation pour poser les pieds + échelle d'import déjà dans `mesh.importTransform`, appliquée dans `Record`). Bindé comme buffer d'instance (locations 3–6).

> Pattern à mirrorer : le « model instance buffer » de `SkinnedRenderer`.

- [ ] **Step 5: Pipeline layout + pipeline graphique**

`m_pipelineLayout` = 2 sets (`set 0` = layout bindless reçu en paramètre ; `set 1` = `m_boneSetLayout`) + push constant range 144 o (VERTEX|FRAGMENT). Pipeline graphique : shaders `skinned_preview.{vert,frag}.spv`, vertex input = format `SkinnedVertex` + instance rows (mirror du vertex input de `SkinnedRenderer`), 1 attachement couleur (blend désactivé), depth test/write ON (`VK_COMPARE_OP_LESS`), `depthBiasEnable = TRUE` + `VK_DYNAMIC_STATE_DEPTH_BIAS`, viewport/scissor dynamiques.

> ⚠️ **Winding (anti-régression terrain, cf. CLAUDE.md)** : le mesh vient d'un fichier (Male_/Female_Ranger). Reprendre **exactement** le `frontFace`/`cullMode` utilisés par `SkinnedRenderer` pour l'avatar in-world (ne PAS copier la convention du terrain). En cas de doute, `cullMode = VK_CULL_MODE_NONE` temporaire pour valider, puis figer le bon `frontFace`.

- [ ] **Step 6: Libérer dans Shutdown** — détruire pipeline, layout, render pass, framebuffer, bone buffer/pool/layout, instance buffer (ordre inverse, remise à `VK_NULL_HANDLE`).

- [ ] **Step 7: Vérification (pas de build local)** — relire les create infos vs les patterns référencés ; symétrie Init/Shutdown. Compilation déléguée CI ; rendu validé par l'utilisateur.

- [ ] **Step 8: Commit**

```bash
git add src/client/render/race/RacePreviewViewport.h src/client/render/race/RacePreviewViewport.cpp
git commit -m "feat(apercu): pipeline forward skinne + render pass color+depth"
```

## Task 11: Implémenter le draw (Render) avec routage de matériaux

**Files:**
- Modify: `src/client/render/race/RacePreviewViewport.cpp` (remplacer le stub `Render`)
- Modify: `RacePreviewViewport.h` (membres : genre + ids matériaux + noms peau)

- [ ] **Step 1: Membres de routage**

Dans `RacePreviewViewport.h` :

```cpp
		std::string              m_gender = "male";
		uint32_t                 m_outfitMaterialId    = 0;
		uint32_t                 m_bodyMaterialIdMale  = 0;
		uint32_t                 m_bodyMaterialIdFemale = 0;
		std::vector<std::string> m_bodyMaterialNames;
```

- [ ] **Step 2: Remplacer le stub `Render` par le vrai rendu**

Dans `RacePreviewViewport::Render`, après les transitions de layout existantes, au lieu du `vkCmdClearColorImage`, faire un vrai render pass. Logique (mirror de `SkinnedRenderer::Record` pour le draw, + la fonction pure pour le routage) :

```cpp
    // 1. Upload des matrices d'os finales dans m_boneBuffer (memcpy host-coherent).
    //    m_finalBoneMatrices est deja calcule par SetMesh/Update.
    // 2. Upload de la matrice d'instance (cadrage) dans m_instanceBuffer.
    // 3. vkCmdBeginRenderPass (m_renderPass, m_framebuffer, clear color+depth).
    // 4. Bind m_pipeline ; set viewport/scissor dynamiques (taille image).
    // 5. Bind descriptor set 0 = m_materialDescSet ; set 1 = m_boneDescSet.
    // 6. Bind vertex buffer (mesh) + instance buffer + index buffer.
    // 7. Routage peau/habit :
    const uint32_t bodyMaterialId =
        (m_gender == "female") ? m_bodyMaterialIdFemale : m_bodyMaterialIdMale;
    const std::vector<uint32_t> submeshMat =
        engine::render::skinned::BuildSubmeshMaterialIndices(
            m_currentMesh->submeshes, m_bodyMaterialNames,
            bodyMaterialId, m_outfitMaterialId);
    // 8. PushConstants : viewProj (camera fixe), prevViewProj = viewProj, materialIndex par draw.
    // 9. Draw : si submeshMat.size() == submeshes.size(), un draw par sous-maillage
    //    (vkCmdSetDepthBias peau via skin_depth_bias_*, materialIndex = submeshMat[s],
    //     vkCmdDrawIndexed(sub.indexCount, 1, sub.firstIndex, 0, 0)).
    //    Sinon mono-draw (materialIndex = m_outfitMaterialId, mesh.indexCount).
    // 10. vkCmdEndRenderPass. (Layout final SHADER_READ_ONLY_OPTIMAL via le render pass.)
```

Le corps détaillé reprend **exactement** la boucle de draw de `SkinnedRenderer::Record` (`src/client/render/skinned/SkinnedRenderer.cpp:655-693`), en remplaçant la cible (render pass de l'aperçu), la source des matrices d'os (`m_finalBoneMatrices`), et la caméra (matrice viewProj fixe de cadrage construite ici). Les valeurs de depth bias sont passées par l'appelant (Task 12) ou lues depuis la config `client.character_creation.skin_depth_bias_*`.

- [ ] **Step 3: Vérification (pas de build local)** — relire la parité avec `SkinnedRenderer::Record` (offsets `firstIndex`, depth bias peau, push constants). Rendu validé par l'utilisateur.

- [ ] **Step 4: Commit**

```bash
git add src/client/render/race/RacePreviewViewport.cpp src/client/render/race/RacePreviewViewport.h
git commit -m "feat(apercu): draw skinne forward avec routage peau/habit + depth bias"
```

## Task 12: Câbler genre + matériaux depuis l'écran de création

**Files:**
- Modify: `src/client/render/race/RacePreviewViewport.h` (méthodes `SetGender`, `SetAvatarMaterials`)
- Modify: `src/client/render/race/RacePreviewViewport.cpp` (implémentations)
- Modify: `src/client/app/Engine.{h,cpp}` (exposer le descriptor set bindless + les ids matériaux + noms peau ; alimenter le viewport)
- Modify: `src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp` (appeler `SetGender` au toggle, à côté du `SetMesh` existant ~ligne 150-152)

- [ ] **Step 1: API du viewport**

Ajouter à `RacePreviewViewport` :

```cpp
		/// Genre actif de l'apercu ("male"/"female") : route la peau au prochain Render.
		void SetGender(const std::string& gender) { m_gender = (gender == "female") ? "female" : "male"; }

		/// Materiaux de l'avatar (set bindless + ids + noms peau), fournis par Engine
		/// apres creation des materiaux au boot. A appeler une fois avant le 1er Render.
		void SetAvatarMaterials(VkDescriptorSet materialSet, uint32_t outfitId,
		                        uint32_t bodyMaleId, uint32_t bodyFemaleId,
		                        const std::vector<std::string>& bodyNames)
		{
			m_materialDescSet = materialSet;
			m_outfitMaterialId = outfitId;
			m_bodyMaterialIdMale = bodyMaleId;
			m_bodyMaterialIdFemale = bodyFemaleId;
			m_bodyMaterialNames = bodyNames;
		}
```

- [ ] **Step 2: Exposer les matériaux depuis Engine**

Dans `Engine`, après la création des matériaux avatar au boot (Engine.cpp ~4144), appeler (là où le `RacePreviewViewport` est accessible — via l'`AuthImGuiRenderer`/`AuthUi`) :

```cpp
m_racePreview->SetAvatarMaterials(
    materialCache.GetDescriptorSet(),
    m_avatarMaterialId,
    m_avatarBodyMaterialIdMale,
    m_avatarBodyMaterialIdFemale,
    m_avatarBodyMaterialNames);
```

> Identifier le point d'accès réel au `RacePreviewViewport` (probablement détenu par `AuthImGuiRenderer`). Si l'objet n'est pas joignable depuis ce bloc, stocker les valeurs dans des membres Engine et les pousser au moment où l'écran de création est monté.

- [ ] **Step 3: Brancher le genre au toggle**

Dans `AuthImGuiCharacterCreate.cpp`, dans le bloc qui pousse l'aperçu au changement (rechercher `m_racePreview->SetMesh(` ~ligne 151), ajouter juste après :

```cpp
				m_racePreview->SetGender(genderStr);
```

(`genderStr` est déjà calculé ligne ~150 : `(m_charGender == 1) ? "female" : "male"`.)

- [ ] **Step 4: Vérification (pas de build local)** — relire le flux : au toggle Masculin/Féminin, `SetMesh` (mesh genré) **et** `SetGender` (peau genrée) sont appelés → le prochain `Render` route la bonne peau. Rendu validé par l'utilisateur.

- [ ] **Step 5: Commit**

```bash
git add src/client/render/race/RacePreviewViewport.h src/client/render/race/RacePreviewViewport.cpp src/client/app/Engine.h src/client/app/Engine.cpp src/client/render/auth/screens/AuthImGuiCharacterCreate.cpp
git commit -m "feat(apercu): cablage genre + materiaux avatar vers l'apercu de creation"
```

## Task 13: Documenter les fonctions (convention repo) + CODEBASE_MAP (Phase 2)

**Files:**
- Modify: `src/client/render/race/RacePreviewViewport.{h,cpp}` (commentaires `///` sur les nouvelles méthodes)
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 1: Documenter**

Ajouter des commentaires `///` (Doxygen, en français) sur les nouvelles méthodes/membres du viewport (rôle, paramètres non-évidents, effet de bord GPU, contraintes thread/timing : « appeler en main thread, après la création des matériaux »).

- [ ] **Step 2: CODEBASE_MAP**

Mettre à jour `CODEBASE_MAP.md` : `RacePreviewViewport` passe du stub au **rendu forward réel** (pipeline + render pass color+depth + bones SSBO + routage peau), shaders `skinned_preview.{vert,frag}`.

- [ ] **Step 3: Commit**

```bash
git add src/client/render/race/RacePreviewViewport.h src/client/render/race/RacePreviewViewport.cpp CODEBASE_MAP.md
git commit -m "docs(apercu): documentation des methodes + CODEBASE_MAP rendu 3D creation"
```

## Task 14: Note de PR Phase 2

- [ ] **Step 1:** Dans la description de PR 2, inclure :
  - ✅ **client uniquement, pas de redéploiement serveur**.
  - ⚠️ Régénérer + committer les `.spv` (`tools/compile_game_shaders.ps1`) si pas déjà fait.
  - Checklist de validation utilisateur : (a) avatar 3D visible dans la création ; (b) bascule Masculin/Féminin change mesh + peau en live ; (c) peau visible (lien avec #2) ; (d) pas de régression terrain (winding).

---

## Self-Review (rempli par l'auteur du plan)

**1. Couverture de la spec :**
- CA1 (peau femelle en jeu) → Tasks 4+5 (routage partagé + diagnostic ciblant la cause ; correctif final guidé par les logs utilisateur). ✔ instrumenté
- CA2 (peau visible en jeu) → Tasks 4+5 idem. ✔ instrumenté
- CA3 (aperçu 3D live) → Tasks 8–12. ✔
- CA4 (vocabulaire) → Task 6. ✔
- CA5 (routage testé) → Tasks 1–3. ✔
- CODEBASE_MAP → Tasks 7 + 13. ✔
- Déploiement client-only → Tasks 7 (note) + 14. ✔

> ⚠️ **Limite assumée** : CA1/CA2 ne sont pas « corrigés » de façon déterministe par ce plan car la cause exige une observation runtime (impossible sans build). Le plan livre l'instrumentation (`[AvatarSkinDiag]`) + l'aperçu (outil d'observation) + un routage durci (trim). Le correctif ciblé suivra une **2ᵉ itération** une fois les logs relevés par l'utilisateur. C'est explicite dans la spec (§2) et accepté par l'utilisateur.

**2. Placeholders :** les tâches Vulkan 8/10/11 référencent des **fichiers existants** comme pattern (autorisé) plutôt que des « Task N » ; les parties novatrices (shaders, routage, câblage) sont en code complet. Pas de « TODO »/« TBD ».

**3. Cohérence des types :** `BuildSubmeshMaterialIndices` (signature identique Tasks 1/2/4/11) ; `SkinnedSubMesh.{firstIndex,indexCount,materialName}` ; push constants 144 o alignées sur les shaders existants ; `m_gender`/`m_materialDescSet` cohérents Tasks 10/11/12.
