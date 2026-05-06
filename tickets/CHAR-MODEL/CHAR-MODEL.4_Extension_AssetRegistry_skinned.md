# CHAR-MODEL.4 — Extension `AssetRegistry` : `SkinnedMeshAsset`, `SkeletonAsset`, handles stables

## Dépendances
- CHAR-MODEL.1 (`SkinnedMesh.h`, format `.skinmesh`)
- CHAR-MODEL.2 (`Skeleton.h`, format `.skel`)

## Cadrage

Étendre **incrémentalement** `engine/render/AssetRegistry.{h,cpp}` pour
gérer les nouveaux assets `SkinnedMeshAsset` et `SkeletonAsset` avec :
- handles stables (`SkinnedMeshHandle`, `SkeletonHandle`),
- chargement boot-time depuis fichier disque,
- gestion des buffers GPU (vertex/index) propriétaires comme pour les
  meshes statiques,
- libération propre au shutdown.

**Aucune** régression sur les meshes statiques, textures, et meshes
existants. Les API publiques actuelles restent strictement identiques.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/SkinnedMesh.{h,cpp}
ls engine/render/Skeleton.{h,cpp}
ls engine/render/SkinnedVertexInput.h    # CHAR-MODEL.3 livré
grep -n "class AssetRegistry" engine/render/AssetRegistry.h
grep -n "MeshHandle\|TextureHandle" engine/render/AssetRegistry.h
```

---

## Spécification technique

### Nouveaux assets

```cpp
// engine/render/AssetRegistry.h (ajout)
namespace engine::render
{
    struct SkinnedMeshAsset
    {
        VkBuffer  vertexBuffer = VK_NULL_HANDLE;
        VkBuffer  indexBuffer  = VK_NULL_HANDLE;
        void*     vertexAlloc  = nullptr;
        void*     indexAlloc   = nullptr;
        uint32_t  vertexCount  = 0;
        uint32_t  indexCount   = 0;
        uint32_t  indexStride  = 2;             // 2 ou 4

        // Submeshes / LODs : miroir de SkinnedMesh.h
        uint32_t                      submeshCount = 0;
        std::array<SkinnedSubmesh, 16> submeshes{};
        uint32_t                      lodLevelCount = 1;
        std::array<SkinnedMeshLod, 4> lods{};

        // Bornes locales (utiles pour culling)
        engine::math::Vec3 localBoundsMin{};
        engine::math::Vec3 localBoundsMax{};

        // Skeleton requis (résolu par nom au load time)
        AssetId skeletonId = kInvalidAssetId;
    };

    struct SkeletonAsset
    {
        Skeleton skeleton;     // RAM seulement (CPU-side)
        // Pas de buffer GPU pour le squelette : la palette est uploadée par
        // le sampler chaque frame via SkinPaletteBuffer.
    };

    /// Handles stables (mêmes invariants que MeshHandle).
    class SkinnedMeshHandle { /* … */ };
    class SkeletonHandle    { /* … */ };
}
```

### Loaders

```cpp
class AssetRegistry
{
public:
    // Existant (inchangé) : LoadMesh, LoadTexture, etc.

    /// Charge un .skinmesh + le .skel correspondant. Le chemin du squelette
    /// est passé en argument explicite (pas de résolution automatique).
    SkinnedMeshHandle LoadSkinnedMesh(
        std::string_view skinmeshPath,
        SkeletonHandle skeleton);

    SkeletonHandle    LoadSkeleton(std::string_view skelPath);

    // Accès données (read-only)
    const SkinnedMeshAsset* TryGetSkinnedMesh(AssetId id) const;
    const SkeletonAsset*    TryGetSkeleton(AssetId id) const;
};
```

### Cycle de vie

- **Load** : lit le fichier → upload sur GPU via la même mécanique que
  les meshes statiques (staging buffer + transfert). Le `Skeleton` reste
  CPU-side.
- **Reuse** : si le même `skinmeshPath` est demandé deux fois, retourner
  le **même `AssetId`** (cache par chemin canonique). Idem squelette.
- **Shutdown** : libère vertex/index buffers Vulkan.

### Validation cross-asset

À `LoadSkinnedMesh`, vérifier :
- Tous les `jointIndices` du mesh sont < `skeleton.joints.size()` ;
  sinon, log `[asset] skinmesh '%s' references joint %u but skeleton
  '%s' has %u joints` et **rejet** (handle invalide).

### Conventions de chemin

- Skinmesh : `game/data/models/<race>/<race>.skinmesh`
- Skeleton : `game/data/skeletons/<rig>.skel`

Pas de résolution automatique skinmesh→skel : c'est le caller (manifest
race en CHAR-MODEL.15+) qui passe les deux chemins.

### Logs

- `[asset] skinmesh loaded '%s' (%u verts, %u idx, %u submeshes, %u lods)`
- `[asset] skeleton loaded '%s' (%u joints)`
- `[asset] skinmesh load FAILED '%s' : <raison>`

---

## Liste des fichiers

**Modifiés :**
- `engine/render/AssetRegistry.h` (ajout `SkinnedMeshAsset`,
  `SkeletonAsset`, handles, loaders, getters)
- `engine/render/AssetRegistry.cpp` (implémentations)
- `CMakeLists.txt` *(rien à ajouter, AssetRegistry est déjà listé)*

**Créés :**
- `tests/render/AssetRegistry_LoadSkinned_test.cpp`

---

## CMakeLists.txt

Aucune modification : `AssetRegistry.{h,cpp}` est déjà dans `engine_core`.

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Les tests existants sur `MeshHandle` / `TextureHandle` continuent
      de passer **sans modification**.
- [ ] Test `AssetRegistry_LoadSkinned_test` :
      - charge un `.skel` synthétique,
      - charge un `.skinmesh` synthétique référençant le skel,
      - vérifie que `TryGetSkinnedMesh` renvoie un asset valide,
      - vérifie cache (même chemin → même `AssetId`),
      - charge un `.skinmesh` avec un `jointIndex` invalide → handle
        invalide, log d'erreur émis.
- [ ] Au shutdown, aucune fuite Vulkan (vérifier via validation layers
      en debug).
- [ ] Aucune signature publique préexistante de `AssetRegistry` n'est
      modifiée.

---

## Anti-objectifs

- **Ne pas** modifier les structures `MeshAsset` / `TextureAsset`
  existantes.
- **Ne pas** introduire de hot-reload (boot-time uniquement).
- **Ne pas** dépendre d'animation : aucun appel à `AnimationClip` ici.
- **Ne pas** lier l'AssetRegistry au pipeline rendu : le câblage final
  est CHAR-MODEL.5.
- **Ne pas** mettre la palette de skinning dans `SkeletonAsset` (la
  palette est par-instance, pas par-asset).
