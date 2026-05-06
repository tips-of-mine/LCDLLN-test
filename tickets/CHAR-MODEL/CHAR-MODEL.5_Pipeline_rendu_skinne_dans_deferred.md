# CHAR-MODEL.5 — Pipeline de rendu skinné intégré au deferred

## Dépendances
- CHAR-MODEL.3 (shader skin, vertex input, palette buffer)
- CHAR-MODEL.4 (`AssetRegistry` étendu)

## Cadrage

Câbler le rendu skinné **dans le deferred renderer existant** :
- nouveau type `SkinnedRenderable` collecté chaque frame ;
- extension du `GeometryPass` (sans casser le passage statique) pour
  émettre les batchs skinnés avec le pipeline créé en CHAR-MODEL.3 ;
- compatibilité **CSM** (Cascaded Shadow Maps) : les renderables skinnés
  participent aux shadow passes via un pipeline shadow-skinné dédié ;
- compatibilité **G-Buffer** (sortie identique aux meshes statiques :
  worldPos, worldNormal, albedo, roughness/metalness).

À l'issue de ce ticket, un test end-to-end charge un `.skinmesh + .skel`
en bind pose, et le voit s'afficher correctement dans la scène avec
ombres CSM.

---

## Pré-requis vérifiables

```bash
git status
ls engine/render/GeometryPass.{h,cpp}
ls engine/render/CascadedShadowMaps.{h,cpp}
ls engine/render/DeferredPipeline.{h,cpp}
grep -n "class GeometryPass" engine/render/GeometryPass.h
ls engine/render/SkinPaletteBuffer.h    # CHAR-MODEL.3
ls engine/render/AssetRegistry.h | xargs grep -l "SkinnedMeshHandle"
```

---

## Spécification technique

### Nouveau type renderable

```cpp
// engine/render/SkinnedRenderable.h (nouveau)
namespace engine::render
{
    /// Une instance skinnée à dessiner cette frame.
    struct SkinnedRenderable
    {
        SkinnedMeshHandle  mesh;
        SkeletonHandle     skeleton;
        engine::math::Mat4 modelMatrix;
        uint32_t           skinPaletteOffset;   // dans SkinPaletteBuffer (en mat4)
        uint32_t           skinPaletteCount;    // = skeleton.joints.size()
        uint32_t           materialOverrideId;  // 0 = matériau par défaut, voir CHAR-MODEL.23
        uint32_t           lodLevel;            // 0..3 (déterminé par culling/SSE)
    };
}
```

### Collecte par frame

`engine/render/SceneRenderQueue` (existant — vérifier le nom exact ; sinon
l'extension va au plus proche du collecteur scène existant) :

```cpp
class SceneRenderQueue
{
public:
    // Existant inchangé.
    void AddSkinned(const SkinnedRenderable& r);
    std::span<const SkinnedRenderable> SkinnedRenderables() const;
};
```

### Extension `GeometryPass`

```cpp
class GeometryPass
{
public:
    // Existant inchangé.

    /// Initialise le pipeline skinné en plus du pipeline statique.
    void InitSkinned(VkRenderPass rp, VkDescriptorSetLayout skinSetLayout,
                     const SkinnedVertexInputDesc& vid);

    /// Appelé après le draw statique. Bind le pipeline skinné, itère sur
    /// les SkinnedRenderable, drawIndexed par submesh × LOD.
    void DrawSkinned(VkCommandBuffer cmd,
                     std::span<const SkinnedRenderable> renderables,
                     const SkinPaletteBuffer& paletteBuffer);
};
```

Implementation : un `VkPipeline` skinné distinct, partage le même
`VkRenderPass` que le pass géométrie statique. Le frag shader est
**partagé** (sortie G-Buffer identique).

### Shadow pass

`CascadedShadowMaps.cpp` : nouveau pipeline shadow-skinné qui réutilise
le même vertex shader avec un define `#define SHADOW_PASS 1` (ou un
fichier shader dédié `skinned_shadow.vert.glsl`). Sortie : depth seulement.

### Frame flow

Pseudocode dans le frame graph existant :

```
BeginFrame
  └ skinPaletteBuffer.BeginFrame()
  └ pour chaque entité animée : sampler.Sample → ComputeSkinPalette
                              → offset = paletteBuffer.Append(palette)
                              → renderQueue.AddSkinned({...offset})
ShadowPass (CSM)
  └ DrawStaticShadow(...)
  └ DrawSkinnedShadow(skinned, paletteBuffer)
GeometryPass (G-Buffer)
  └ DrawStatic(...)
  └ DrawSkinned(skinned, paletteBuffer)
LightingPass …
```

### Performance

- Tri par `SkinnedMeshAsset.id` puis par matériau pour minimiser les
  binds.
- Une `vkCmdBindDescriptorSets` skin-set par instance suffit (offset
  dynamique sur le SSBO palette).

---

## Liste des fichiers

**Créés :**
- `engine/render/SkinnedRenderable.h`
- `engine/render/shaders/skinned_shadow.vert.glsl`

**Modifiés :**
- `engine/render/GeometryPass.{h,cpp}` (ajout `InitSkinned`, `DrawSkinned`)
- `engine/render/CascadedShadowMaps.{h,cpp}` (pipeline shadow-skin + draw)
- `engine/render/DeferredPipeline.{h,cpp}` (orchestration : crée le
  set layout skin, initialise les passes skinned)
- `engine/render/SceneRenderQueue.{h,cpp}` *(ou équivalent existant — à confirmer dans le repo)*
- `CMakeLists.txt` (nouveaux fichiers headers ajoutés à `engine_core`)
- `scripts/compile_game_shaders.ps1` (ajout du shader shadow skinné)

**Créés (tests) :**
- `tests/render/SkinnedDeferred_RenderToOffscreen_test.cpp` (rend un
  cube skinné avec 1 light directionnel + CSM, vérifie un nb minimum
  de pixels couverts dans le G-Buffer et dans la shadow map).

---

## CMakeLists.txt

```cmake
target_sources(engine_core PRIVATE
    engine/render/SkinnedRenderable.h
)
```

---

## Critères d'acceptation

- [ ] Build Windows + Linux propre.
- [ ] Les meshes statiques continuent d'être rendus identiques (test de
      régression visuelle existant doit passer).
- [ ] Test `SkinnedDeferred_RenderToOffscreen_test` : un cube skinné
      (1 os, bind pose) est visible dans le G-Buffer et projette une
      ombre dans la shadow map CSM.
- [ ] La validation Vulkan (debug layers) ne produit aucune erreur ni
      warning sur les frames de test.
- [ ] Performance : 100 instances skinnées (60 os chacune) tiennent en
      ≤ 4 ms GPU sur un GPU desktop standard (à mesurer avec
      `vkCmdWriteTimestamp`).

---

## Anti-objectifs

- **Ne pas** introduire d'animation (le test rend en bind pose).
- **Ne pas** modifier le format `.skinmesh` ou `.skel`.
- **Ne pas** changer les sets descriptors 0/1/2 du `GeometryPass`
  statique — le set skin est **set 3**.
- **Ne pas** dupliquer le frag shader G-Buffer (réutiliser celui des
  meshes statiques).
- **Ne pas** créer un sampler/blender d'animation (Phase 1).
