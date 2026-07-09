# Chantier 2 SP1 — groundwork avatar modulaire — plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Composer/rendre/échanger plusieurs parties skinnées partageant un squelette (avatar modulaire), démontré avec des placeholders procéduraux et une commande de test.

**Architecture:** Un `ModularAvatar` (client) tient un squelette partagé + une carte slot→partie (`SkinnedMesh*`). Le rendu calcule les matrices d'os UNE fois et appelle `SkinnedRenderer::Record` pour chaque partie active. Les placeholders sont des boîtes skinnées à 100% sur un os, générées par code (aucun asset).

**Tech Stack:** C++17/20, Vulkan (via `SkinnedRenderer` existant), cgltf (loader existant), CMake + ctest.

## Global Constraints

- **Client-rendu uniquement** pour SP1 (rendu + placeholders). La commande de test peut être soit admin-via-master (log), soit un flag debug strictement client (cf. Task 4 — décision au début de Task 4).
- **Pas de toolchain locale** : compilation + ctest via CI. `ModularAvatar`/`PlaceholderPart` sont **pures (données CPU, pas de Vulkan/ImGui)** → testables par **ctest sur Linux**. L'intégration Engine + rendu = vérif **CI Windows + en jeu**.
- **PascalCase** nouveau code ; commentaires français.
- **Ne PAS toucher** `frontFace`/`cullMode` d'aucun pipeline (garde CLAUDE.md). On réutilise `SkinnedRenderer` tel quel.
- Nouveau `.cpp` → l'ajouter à `CMakeLists.txt` racine (moteur/engine_core) ET, pour les tests, à la cible de test skinnée existante (chercher dans `CMakeLists.txt` racine ET `src/CMakeLists.txt` — piège doublon).
- Skeleton partagé : toute partie doit avoir `skeleton.bones.size()` == celui du Body ET même ordre de noms, sinon rejet + log.

## Prérequis

SP1 étend le rendu avatar que #960 modifie. Base la branche sur #960 (déjà fait :
`claude/ch2-sp1-modular-avatar` part de la HEAD de `claude/character-window`).
Quand #960 merge dans `main`, rebaser sur `origin/main`.

---

## File Structure

- **Create** `src/client/render/skinned/ModularAvatar.h` / `.cpp` — composant slots+parties.
- **Create** `src/client/render/skinned/PlaceholderPart.h` / `.cpp` — génération de parties boîte skinnées à un os.
- **Create** `src/client/render/skinned/tests/ModularAvatarTests.cpp` — tests unitaires.
- **Create** `src/client/render/skinned/tests/PlaceholderPartTests.cpp` — tests unitaires.
- **Modify** `src/client/app/Engine.h` — membre `ModularAvatar m_modularAvatar;` + parties placeholder.
- **Modify** `src/client/app/Engine.cpp` — Body = mesh courant ; boucle `Record` sur `ActiveParts()` ; commande `/modular`.
- **Modify** `CMakeLists.txt` (+ `src/CMakeLists.txt` si la cible test y est) — nouveaux sources + tests.

---

## Task 1 : `ModularAvatar` (logique pure) + tests

**Files:**
- Create: `src/client/render/skinned/ModularAvatar.h`, `ModularAvatar.cpp`
- Create/Test: `src/client/render/skinned/tests/ModularAvatarTests.cpp`
- Modify: `CMakeLists.txt` (source engine_core + test)

**Interfaces:**
- Produces :
  - `enum class EquipVisualSlot { Body=0, Head, Chest, Legs, Feet, Hands, Weapon, Offhand, Count };`
  - `class ModularAvatar` avec :
    - `void SetBody(const SkinnedMesh* body)` — définit le corps + le squelette de référence.
    - `bool SetPart(EquipVisualSlot slot, const SkinnedMesh* mesh)` — pose (mesh) / retire (nullptr) une partie ; retourne false + log si squelette incompatible (bones.size / ordre noms). `Body` refusé ici (utiliser SetBody).
    - `void ClearPart(EquipVisualSlot slot)`.
    - `std::vector<const SkinnedMesh*> ActiveParts() const` — Body d'abord (s'il existe), puis les slots occupés dans l'ordre de l'enum.
    - `const Skeleton* SharedSkeleton() const` — squelette du Body, ou nullptr.
    - `bool HasBody() const`.

- [ ] **Step 1 : Écrire le header**

`ModularAvatar.h` :

```cpp
#pragma once
#include <cstdint>
#include <vector>

namespace engine::render::skinned
{
struct SkinnedMesh;
struct Skeleton;

/// Slots visuels d'équipement (SP1 : le pipeline, pas tous peuplés d'assets).
enum class EquipVisualSlot : uint32_t
{
    Body = 0, Head, Chest, Legs, Feet, Hands, Weapon, Offhand, Count
};

/// Avatar composé de plusieurs parties skinnées partageant UN squelette.
/// Le rendu calcule les matrices d'os une fois puis dessine chaque partie active.
class ModularAvatar
{
public:
    void SetBody(const SkinnedMesh* body);
    bool SetPart(EquipVisualSlot slot, const SkinnedMesh* mesh);
    void ClearPart(EquipVisualSlot slot);

    std::vector<const SkinnedMesh*> ActiveParts() const;
    const Skeleton* SharedSkeleton() const;
    bool HasBody() const { return m_body != nullptr; }

private:
    /// true si `mesh` partage le squelette du Body (même nb de bones + mêmes noms/ordre).
    bool SkeletonCompatible(const SkinnedMesh* mesh) const;

    const SkinnedMesh* m_body = nullptr;
    const SkinnedMesh* m_parts[static_cast<size_t>(EquipVisualSlot::Count)] = {};
};
}
```

- [ ] **Step 2 : Écrire les tests (ils échoueront tant que .cpp absent)**

`tests/ModularAvatarTests.cpp` — suivre le style des tests skinnés existants
(chercher un `*Tests.cpp` du dossier pour le framework : assert maison ou gtest).
Construire des `SkinnedMesh` CPU minimaux (skeleton avec N bones nommés ; pas
besoin de buffers GPU pour ces tests — n'utiliser que `.skeleton`). Cas :

```cpp
// Body pose le squelette ; ActiveParts contient le Body.
// SetPart avec squelette compatible (mêmes noms/ordre) -> true, apparait dans ActiveParts.
// SetPart avec bones.size() different -> false, pas ajoute.
// SetPart avec noms/ordre differents -> false.
// ClearPart retire la partie.
// ActiveParts ordre : Body d'abord puis slots par ordre d'enum.
// SetPart(Body,...) -> refuse (utiliser SetBody).
```

Écrire des helpers `MakeSkel({"root","head"})` qui remplissent `SkinnedMesh.skeleton.bones[].name`.

- [ ] **Step 3 : Implémenter `ModularAvatar.cpp`**

```cpp
#include "src/client/render/skinned/ModularAvatar.h"
#include "src/client/render/skinned/SkinnedMesh.h"
#include "src/shared/core/Log.h"

namespace engine::render::skinned
{
void ModularAvatar::SetBody(const SkinnedMesh* body) { m_body = body; }

bool ModularAvatar::SkeletonCompatible(const SkinnedMesh* mesh) const
{
    if (m_body == nullptr || mesh == nullptr) return false;
    const auto& a = m_body->skeleton.bones;
    const auto& b = mesh->skeleton.bones;
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].name != b[i].name) return false;
    return true;
}

bool ModularAvatar::SetPart(EquipVisualSlot slot, const SkinnedMesh* mesh)
{
    if (slot == EquipVisualSlot::Body || slot >= EquipVisualSlot::Count) return false;
    if (mesh != nullptr && !SkeletonCompatible(mesh))
    {
        LOG_WARN(Render, "[ModularAvatar] partie rejetee : squelette incompatible (slot={})",
            static_cast<uint32_t>(slot));
        return false;
    }
    m_parts[static_cast<size_t>(slot)] = mesh;
    return true;
}

void ModularAvatar::ClearPart(EquipVisualSlot slot)
{
    if (slot != EquipVisualSlot::Body && slot < EquipVisualSlot::Count)
        m_parts[static_cast<size_t>(slot)] = nullptr;
}

std::vector<const SkinnedMesh*> ModularAvatar::ActiveParts() const
{
    std::vector<const SkinnedMesh*> out;
    if (m_body) out.push_back(m_body);
    for (size_t i = 0; i < static_cast<size_t>(EquipVisualSlot::Count); ++i)
        if (m_parts[i]) out.push_back(m_parts[i]);
    return out;
}

const Skeleton* ModularAvatar::SharedSkeleton() const
{
    return m_body ? &m_body->skeleton : nullptr;
}
}
```

> `EquipVisualSlot::Count` comparaisons : `slot >= EquipVisualSlot::Count` nécessite
> que l'enum soit comparable — ajouter `static_cast<uint32_t>` si le compilateur
> refuse la comparaison directe sur `enum class`.

- [ ] **Step 4 : CMake — ajouter source + test**

Ajouter `src/client/render/skinned/ModularAvatar.cpp` à la liste engine_core.
Ajouter `ModularAvatarTests.cpp` à la cible de tests skinnés (repérer comment les
autres `skinned/tests/*Tests.cpp` sont enregistrés ; `add_test` associé).

- [ ] **Step 5 : Commit**

```bash
git add src/client/render/skinned/ModularAvatar.h src/client/render/skinned/ModularAvatar.cpp \
        src/client/render/skinned/tests/ModularAvatarTests.cpp CMakeLists.txt
git commit -m "feat(ch2-sp1): ModularAvatar (slots + parties partageant un squelette) + tests"
```

**Vérification :** CI Linux (ctest) — les tests `ModularAvatar*` passent. CI Windows compile.

---

## Task 2 : `PlaceholderPart` (génération procédurale) + tests

**Files:**
- Create: `src/client/render/skinned/PlaceholderPart.h`, `PlaceholderPart.cpp`
- Create/Test: `src/client/render/skinned/tests/PlaceholderPartTests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes : `Skeleton`, `SkinnedVertex`, `SkinnedMeshCpuData` (SkinnedMeshLoader.h).
- Produces :
  - `SkinnedMeshCpuData MakePlaceholderBoxPart(const Skeleton& skel, int boneIndex, float halfExtentM, uint32_t materialTagIndexUnused = 0)` — une boîte (24 sommets, 36 indices) centrée sur la position bind-globale de l'os `boneIndex`, tous les sommets **100% weight sur `boneIndex`** (boneIndices[0]=boneIndex, weights[0]=1, autres 0), 1 submesh couvrant tout. `skeleton`/`clips` copiés du `skel` fourni (pour partager la pose).

- [ ] **Step 1 : Header**

```cpp
#pragma once
#include "src/client/render/skinned/SkinnedMeshLoader.h" // SkinnedMeshCpuData
namespace engine::render::skinned
{
struct Skeleton;
/// Génère une boîte skinnée à 100% sur `boneIndex`, centrée sur la position
/// bind-globale de cet os. Sert de PARTIE placeholder (aucun asset). Le squelette
/// est copié pour partager la pose de l'avatar.
SkinnedMeshCpuData MakePlaceholderBoxPart(const Skeleton& skel, int boneIndex, float halfExtentM);
}
```

- [ ] **Step 2 : Tests**

`tests/PlaceholderPartTests.cpp` :

```cpp
// Skeleton 2 os : root (identite), head (translation connue).
// MakePlaceholderBoxPart(skel, headIndex, 0.1f) :
//  - vertices.size()==24, indices.size()==36, submeshes.size()==1.
//  - tous les vertices : boneIndices[0]==headIndex, weights[0]==1.0f, weights[1..3]==0.
//  - la position moyenne des 24 sommets ≈ translation bind-globale de head (a epsilon).
//  - skeleton copie (bones.size == skel.bones.size).
```

Pour la position bind-globale : `bindGlobal = inverse(bone.inverseBindGlobal)` ;
la translation = colonne de translation. (Vérifier l'API `Mat4::Inverse`/accès
translation dans `Math.h`.)

- [ ] **Step 3 : Implémentation**

Construire 24 `SkinnedVertex` (boîte : 6 faces × 4 sommets, normales par face),
positions = centre(os) ± halfExtent sur x/y/z, uv basiques (0/1), `boneIndices={boneIndex,0,0,0}`,
`weights={1,0,0,0}`. 36 indices (2 triangles/face). Un `SkinnedSubMesh{0,36,""}`.
Copier `skel` dans `cpu.skeleton`. Laisser `clips` vide (la pose vient du Body au rendu).

Centre = translation de `inverse(skel.bones[boneIndex].inverseBindGlobal)`.

- [ ] **Step 4 : CMake source + test.**

- [ ] **Step 5 : Commit**

```bash
git add src/client/render/skinned/PlaceholderPart.h src/client/render/skinned/PlaceholderPart.cpp \
        src/client/render/skinned/tests/PlaceholderPartTests.cpp CMakeLists.txt
git commit -m "feat(ch2-sp1): PlaceholderPart (boite skinnee a un os) + tests"
```

**Vérification :** CI Linux (ctest) `PlaceholderPart*` passent. CI Windows compile.

---

## Task 3 : Intégration Engine — avatar modulaire + boucle de rendu

**Files:**
- Modify: `src/client/app/Engine.h`, `src/client/app/Engine.cpp`

**Interfaces:**
- Consumes : `ModularAvatar` (Task 1), `SkinnedRenderer::Record` (existant), `finals`
  (existant, `Engine.cpp` ~5508).

- [ ] **Step 1 : Engine.h — membres**

```cpp
engine::render::skinned::ModularAvatar m_modularAvatar;
// Parties placeholder possédées (uploadées GPU) pour le test /modular (Task 4).
std::vector<std::unique_ptr<engine::render::skinned::SkinnedMesh>> m_placeholderParts;
```
Include `ModularAvatar.h`.

- [ ] **Step 2 : Engine.cpp — Body = mesh courant**

Là où l'avatar local est résolu (EnterWorld, `m_currentSkinnedMesh` posé) et à
chaque changement de mesh : `m_modularAvatar.SetBody(m_currentSkinnedMesh);`.

- [ ] **Step 3 : Engine.cpp — boucle de Record**

Au draw avatar (~5598-5615) : `finals` est déjà calculé. Remplacer le `Record`
unique par une boucle sur les parties actives, MÊME `finals`/model. Le Body garde
son routage matériau (submeshMaterialIndices) ; les placeholders utilisent le
chemin mono-draw (submeshes 1 entrée, materialIndex par défaut) :

```cpp
for (const engine::render::skinned::SkinnedMesh* part : m_modularAvatar.ActiveParts())
{
    const bool isBody = (part == m_currentSkinnedMesh);
    m_skinnedRenderer.Record(m_vkDeviceContext.GetDevice(), innerCmd, m_vkSwapchain.GetExtent(),
        m_pipeline->GetGeometryPass().GetRenderPassLoad(), VK_NULL_HANDLE,
        rs.prevViewProjMatrix.m, rs.viewProjMatrix.m,
        *part, finals, materialCache.GetDescriptorSet(), finalModelMat.m,
        isBody ? skinnedMaterialIndex : 0u,
        isBody ? submeshMaterialIndices : std::vector<uint32_t>{},
        isBody ? bodyMaterialId : 0u,
        m_avatarSkinDepthBiasConstant, m_avatarSkinDepthBiasSlope);
}
```

> Le lambda capture actuel `submeshMaterialIndices = std::move(...)` doit rester
> valide dans la boucle : ne pas `move` avant la boucle ; capturer par copie ou
> restructurer. Vérifier la capture exacte à l'implémentation.

Fallback : si `ActiveParts()` est vide (Body pas encore posé), ne rien dessiner
(comportement actuel préservé quand `m_currentSkinnedMesh` nul).

- [ ] **Step 4 : Commit**

```bash
git add src/client/app/Engine.h src/client/app/Engine.cpp
git commit -m "feat(ch2-sp1): avatar local rendu via ModularAvatar (boucle Record parties)"
```

**Vérification :** CI (Windows) + en jeu — l'avatar s'affiche/anime EXACTEMENT
comme avant (seul le Body est actif ; aucune régression visuelle). Linux compile.

---

## Task 4 : Commande de test `/modular` (swap live)

**Décision d'ouverture (choisir au début de la task) :** admin-via-master (log,
convention repo) OU flag debug `client.debug.modular_test` (strictement client).
Reco SP1 : **flag debug client** — plus simple, purement visuel, zéro serveur.
Documenter le choix dans le commit.

**Files:**
- Modify: `src/client/app/Engine.cpp` (+ éventuel handler master si admin choisi).

- [ ] **Step 1 : À l'ouverture de la fenêtre / au boot, générer 2 parties placeholder**

Depuis le squelette du Body : pour un slot de démo (ex. `Head`), créer 2 variantes
via `MakePlaceholderBoxPart(skel, headBoneIndex, 0.12f)` et `(…, 0.18f)`, les
**Upload** en `SkinnedMesh` (host-visible) stockés dans `m_placeholderParts`.
Repérer `headBoneIndex` via `skeleton.FindBoneIndex("mixamorig:Head")` (nom réel
à confirmer dans le squelette Ranger ; fallback : un os plausible).

- [ ] **Step 2 : Commande `/modular <slot> <A|B|off>`**

Dans le dispatcher de commandes chat (là où `/skills` etc. sont gérés) :
`/modular head A` → `m_modularAvatar.SetPart(Head, partA)` ; `B` → partB ;
`off` → `ClearPart(Head)`. Logguer. (Si admin : router via master + audit ;
si debug flag : n'exécuter que si `client.debug.modular_test`.)

- [ ] **Step 3 : Commit**

```bash
git add src/client/app/Engine.cpp src/client/app/Engine.h
git commit -m "feat(ch2-sp1): commande /modular (swap live d'une partie placeholder)"
```

**Vérification :** en jeu — `/modular head A` fait apparaître une boîte sur la tête
de l'avatar-monde, qui **suit l'animation** ; `head B` l'échange ; `head off` la
retire. Aucun crash Vulkan. Prouve le pipeline modulaire de bout en bout.

---

## Task 5 (optionnel SP1) : aperçu 3D multi-parties

`RacePreviewViewport::RenderOffscreen` : boucler le draw sur les parties actives
(mêmes `finals`) pour voir l'équipement dans la fenêtre Personnage. Peut être
différé au SP « UI paperdoll » si SP1 se concentre sur l'avatar-monde.

---

## Self-Review (couverture spec)

- Composer plusieurs parties partageant un squelette → Task 1 (`ModularAvatar`).
- Rendu = bones 1x + Record par partie → Task 3.
- Swap live → Task 4.
- Placeholders sans asset → Task 2 (`PlaceholderPart`) + Task 4.
- Compat squelette (rejet) → Task 1 (`SetPart` + tests).
- Testable ctest Linux (logique pure) → Tasks 1-2.
- Client-only / SP suivants hors scope → aucun catalogue/wire/DB/stats/asset dans le plan. ✅
