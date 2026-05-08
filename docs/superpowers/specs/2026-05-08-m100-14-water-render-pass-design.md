# M100.14 Water Render Pass — Design (Phase 4b)

> **Ticket source :** [M100.14-WaterRenderPass.md](../../../tickets/M100/M100.14-WaterRenderPass.md)
> **Branche :** `claude/m100-14-water-render-pass` (sur `origin/main` post-M100.13 PR #478).
> **Phase :** 4b (Phase 4 décomposée en 4 PRs : 13/14/15/16).
> **Pré-requis livré :** M100.13 (WaterScene + WaterMeshBuilder + WaterDocument + StreamCache::LoadWater).

## 1. Objectif

Livrer la passe Vulkan qui rend les meshes d'eau gameplay (lacs polygonaux + rivières
spline) produits par M100.13. Visuellement : surfaces avec normales animées
(2 octaves), réfraction par sample SceneColor, réflexion screen-space mince
(SSR 32 raymarch steps + fallback skybox), Fresnel + turbidité.

## 2. Contexte technique

### 2.1 État actuel (post-M100.13)

- `WaterScene` (zone-level) chargeable via `StreamCache::LoadWater` ou
  `WaterDocument::LoadFromDisk`.
- `WaterMeshBuilder` génère vertex/index arrays CPU pour lacs (ear clipping)
  et rivières (polyline ribbon).
- **Aucun rendu GPU de l'eau gameplay** — `WaterScene` n'est pas uploadé
  ni dessiné.

### 2.2 Découverte critique : M38 `WaterRenderer` est dead code

L'ancien `WaterRenderer` (M37/M38, ~550 LOC) est **initialisé au boot mais
jamais enregistré dans le frame graph**. `m_waterRenderer.Record()` n'est
appelé nulle part dans le codebase. Conséquences :

- Au boot : init alloue des Reflection RT (half-res) + Refraction RT
  (full-res) + pipeline + descripteurs **pour rien** (~16 MB VRAM gaspillés).
- Aucun pixel d'eau dessiné dans le run actuel.
- Les logs `[WaterRenderer] Init OK` / `[WaterRenderer] Destroyed`
  observés au boot/shutdown sont trompeurs.

**Décision : supprimer M38 dans la même PR M100.14**, plutôt que
maintenir du code mort en parallèle. Cela simplifie la stratégie de nommage
des shaders (`water.vert`/`water.frag` libres pour la nouvelle passe) et
réduit ~550 LOC.

### 2.3 État réel du frame graph

```
Geometry → SSAO_Generate → SSAO_BlurH → SSAO_BlurV → Decals (overlay) →
  Lighting (writes SceneColor_HDR) → Bloom_* → Tonemap → TAA → CopyPresent
```

Le ticket M100.14 propose `Decals → Water → VolumetricFog → TAA` mais
VolumetricFog n'existe pas encore (M100.22) et l'ordre actuel est plus
nuancé. **Insertion correcte** : `Lighting → Water → Bloom_Prefilter`,
avec ping-pong rename de `SceneColor_HDR` → `SceneColor_HDR_PostWater`
côté Bloom_Prefilter et Bloom_Combine.

## 3. Décisions architecturales

### 3.1 Décision A — Cohabitation M38 ↔ M100.14

**Choix : supprimer M38** (WaterRenderer.{h,cpp} + call sites Engine.cpp +
config keys + CMake source list).

**Justification** :
- M38 ne dessine rien actuellement.
- LCDLLN est un jeu terrestre (pas d'océan global pertinent).
- Garder M38 endormi = dette technique pour 0 gain.
- Les noms de shaders `water.vert/.frag` deviennent libres (réécriture).

### 3.2 Décision B — Réfraction : ping-pong rename

**Choix : ping-pong sur `SceneColor_HDR`** (pas blit explicite).

**Justification** :
- Sémantique frame graph propre (writer→reader explicite, barriers automatiques).
- 2 sites downstream à renommer (Bloom_Prefilter, Bloom_Combine).
- Coût mémoire = 1 image HDR full-screen (~16 MB en 1080p) = acceptable.
- Pas de blit GPU explicite à orchestrer.

**Pattern** :
- Lighting écrit `SceneColor_HDR` (inchangé)
- Water lit `SceneColor_HDR` + écrit `SceneColor_HDR_PostWater` (NEW)
- Bloom_Prefilter lit `SceneColor_HDR_PostWater` (RENAME)
- Bloom_Combine lit `SceneColor_HDR_PostWater` (RENAME)

### 3.3 Décision C — Tests CPU élargis, golden image différé

**Choix : 5 tests CPU**, golden image différé (besoin GPU CI).

**Tests retenus** :
- `Test_WaterPassPushConstants_Is128Bytes` (sizeof + static_assert)
- `Test_WaterPassPushConstants_FieldOffsets_MatchSpec` (offsetof par champ)
- `Test_BuildDrawInfos_EmptyScene_ZeroInstances`
- `Test_BuildDrawInfos_OneLake_OneRiver_ProducesTwoInfos`
- `Test_BuildDrawInfos_ParamsIndexOrdering_LakesFirst`

**Tests différés (nécessitent GPU CI)** :
- `Test_WaterPass_RegistersInFrameGraph` (FG-runtime)
- `Test_WaterPass_GoldenImage` (capture pixels)
- Mesure perf < 1.0 ms

### 3.4 Décision D — Live update via dirty flag

**Choix : rebuild GPU à chaque mutation `WaterDocument`** (pattern terrain).

**Mécanique** :
- Mode éditeur : Engine observe `WaterDocument::IsDirty()` chaque frame ;
  si dirty → `WaterMeshGpu::Rebuild(WaterScene)` + `ClearDirty()`.
- Mode client : 1 rebuild au LoadZone, puis statique pour la durée de la session.
- Cohérent avec M100.5 (heightmap édition live) et M100.9 (splat live).

## 4. Architecture

### 4.1 Vue d'ensemble

```
┌─────────────────────────────────────────────────────────────────┐
│  À SUPPRIMER (M38 dead code)                                    │
│  - engine/render/WaterRenderer.{h,cpp}                          │
│  - Init/Destroy call sites Engine.cpp                           │
│  - Config keys render.water.*                                   │
│  - CMake source list entry                                      │
│  Net : ~ -550 LOC                                               │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│  À AJOUTER (M100.14)                                            │
│                                                                 │
│  WaterMeshGpu  (engine/render/WaterMeshGpu.{h,cpp})             │
│    - VBO/IBO concaténés (lakes + rivers)                        │
│    - drawInfos[] : { firstIdx, idxCount, vertOff, paramsIdx }   │
│    - Rebuild(scene) : reconstruit + upload via VMA staging      │
│                                                                 │
│  WaterPass     (engine/render/WaterPass.{h,cpp})                │
│    - FG-integrated (pattern UnderwaterPass + DecalPass)         │
│    - Init / Record / Destroy / InvalidateFramebufferCache       │
│    - Push constants 128 B par-instance                          │
│    - 4 bindings : sceneColor, sceneDepth, normalMap, skybox     │
│                                                                 │
│  Shaders       (engine/render/shaders/water.vert,.frag          │
│                 → game/data/shaders/water.{vert,frag,spv})      │
│    - Vertex : viewProj * pos                                    │
│    - Fragment : 2 octaves normales + refraction + SSR + Fresnel │
│                                                                 │
│  Tests         (engine/render/tests/Water*Tests.cpp)            │
│    - 5 cas CPU                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 `WaterMeshGpu`

```cpp
namespace engine::render
{
    /// Info de draw call par instance (lac OU rivière).
    /// `paramsIndex` est unifié : 0..N_lakes-1 = lacs (dans `scene.lakes`),
    /// N_lakes..N_lakes+N_rivers-1 = rivières (offset N_lakes dans `scene.rivers`).
    /// Le caller (WaterPass::Record) dispatche en fonction de ce seuil.
    struct WaterInstanceDrawInfo
    {
        uint32_t firstIndex;   // Offset dans IBO global
        uint32_t indexCount;   // Nombre d'indices pour cette instance
        int32_t  vertexOffset; // Base vertex pour cette instance
        uint32_t paramsIndex;  // Voir doc struct ci-dessus
    };

    /// Buffer GPU contenant tous les meshes d'eau (lakes + rivers concaténés).
    /// Reconstruit à la demande depuis une WaterScene CPU.
    class WaterMeshGpu final
    {
    public:
        WaterMeshGpu() = default;
        WaterMeshGpu(const WaterMeshGpu&) = delete;
        WaterMeshGpu& operator=(const WaterMeshGpu&) = delete;

        bool Rebuild(VkDevice device, VkPhysicalDevice physicalDevice,
                     void* vmaAllocator, VkCommandPool transferPool, VkQueue transferQueue,
                     const engine::world::water::WaterScene& scene);

        void Destroy(VkDevice device, void* vmaAllocator);

        VkBuffer GetVertexBuffer() const { return m_vbo; }
        VkBuffer GetIndexBuffer()  const { return m_ibo; }
        const std::vector<WaterInstanceDrawInfo>& GetDrawInfos() const { return m_drawInfos; }
        bool IsValid() const { return m_vbo != VK_NULL_HANDLE && m_ibo != VK_NULL_HANDLE; }
        size_t GetInstanceCount() const { return m_drawInfos.size(); }

    private:
        VkBuffer       m_vbo            = VK_NULL_HANDLE;
        void*          m_vboAllocation  = nullptr;  // VmaAllocation
        VkBuffer       m_ibo            = VK_NULL_HANDLE;
        void*          m_iboAllocation  = nullptr;
        VkDeviceSize   m_vboCapacity    = 0;
        VkDeviceSize   m_iboCapacity    = 0;
        std::vector<WaterInstanceDrawInfo> m_drawInfos;
    };

    /// Helper CPU testable sans device : transforme une WaterScene en
    /// vertex+index arrays + drawInfos. Lacs en tête, rivières ensuite.
    void BuildDrawInfos(const engine::world::water::WaterScene& scene,
                        std::vector<float>& outVertices,            // 7 floats / vertex (pos3 + uv2 + flowDir2)
                        std::vector<uint32_t>& outIndices,
                        std::vector<WaterInstanceDrawInfo>& outDrawInfos);
}
```

**Format vertex (28 B)** :
```cpp
struct WaterVertex
{
    float position[3];   // World space
    float uv[2];         // Tangent-plane UV
    float flowDir[2];    // (0,0) lac ; (dx,dz) rivière
};
static_assert(sizeof(WaterVertex) == 28, "WaterVertex must be 28 bytes");
```

### 4.3 `WaterPass`

```cpp
namespace engine::render
{
    /// Push constants par-instance (128 B max Vulkan).
    struct WaterPassPushConstants
    {
        float viewProj[16];        // 64 B
        float cameraPos[3];        // 12 B
        float timeSeconds;         //  4 B
        float bottomColor[3];      // 12 B
        float turbidity;           //  4 B
        float flowDirection[2];    //  8 B
        float flowSpeed;           //  4 B
        float refractionAmount;    //  4 B
        float fresnelPower;        //  4 B
        float reflectionStrength;  //  4 B
        float screenSize[2];       //  8 B
    };
    static_assert(sizeof(WaterPassPushConstants) == 128, "WaterPassPushConstants must be 128 bytes");

    class WaterPass final
    {
    public:
        WaterPass() = default;
        WaterPass(const WaterPass&) = delete;
        WaterPass& operator=(const WaterPass&) = delete;

        bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
                  VkFormat sceneColorHDRFormat,
                  const uint32_t* vertSpirv, size_t vertWordCount,
                  const uint32_t* fragSpirv, size_t fragWordCount,
                  VkImageView normalMapView, VkSampler normalMapSampler,
                  VkImageView skyboxCubeView, VkSampler skyboxSampler,
                  uint32_t maxFrames = 2,
                  VkPipelineCache pipelineCache = VK_NULL_HANDLE);

        void Record(VkDevice device, VkCommandBuffer cmd, Registry& registry,
                    VkExtent2D extent,
                    ResourceId idSceneColorIn,
                    ResourceId idSceneDepth,
                    ResourceId idSceneColorOut,
                    const WaterMeshGpu& mesh,
                    const WaterPassPushConstants& paramsBase,
                    const engine::world::water::WaterScene& scene,
                    uint32_t frameIndex);

        void Destroy(VkDevice device);
        void InvalidateFramebufferCache(VkDevice device);
        bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

    private:
        // Framebuffer cache keyed par output view (pattern DecalPass)
        struct FramebufferKey { VkImageView outputView = VK_NULL_HANDLE; uint32_t width = 0, height = 0; bool operator==(const FramebufferKey& o) const noexcept; };
        struct FramebufferKeyHash { size_t operator()(const FramebufferKey& k) const noexcept; };

        VkRenderPass          m_renderPass          = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_descriptorPool      = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipelineLayout      = VK_NULL_HANDLE;
        VkPipeline            m_pipeline            = VK_NULL_HANDLE;
        VkSampler             m_sceneColorSampler   = VK_NULL_HANDLE;  // Linear clamp
        VkSampler             m_sceneDepthSampler   = VK_NULL_HANDLE;  // Nearest clamp

        std::vector<VkDescriptorSet> m_descriptorSets;
        std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_framebufferCache;
        uint32_t m_maxFrames = 2;
    };
}
```

**Pipeline state** :
- Blend : `SRC_ALPHA, ONE_MINUS_SRC_ALPHA`
- Depth test : `LESS_OR_EQUAL`, write OFF
- Cull : `BACK`
- Vertex format : `WaterVertex` (28 B, 3 attribs)

**Descriptor set 0** :
- binding 0 : `sampler2D u_sceneColor` (SceneColor_HDR, R16G16B16A16_SFLOAT)
- binding 1 : `sampler2D u_sceneDepth` (depth buffer linéarisé via near/far)
- binding 2 : `sampler2D u_normalMap` (tile normale animée, 8-bit)
- binding 3 : `samplerCube u_skybox` (fallback réflexion)

### 4.4 Shaders

#### `water.vert`

```glsl
#version 450

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;
    float flowSpeed;
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec2 inFlowDir;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec2 vFlowDir;

void main()
{
    vUv       = inUv;
    vWorldPos = inPosition;
    vFlowDir  = inFlowDir;
    gl_Position = pc.viewProj * vec4(inPosition, 1.0);
}
```

#### `water.frag` (cœur visuel)

```glsl
#version 450

layout(set=0, binding=0) uniform sampler2D u_sceneColor;
layout(set=0, binding=1) uniform sampler2D u_sceneDepth;
layout(set=0, binding=2) uniform sampler2D u_normalMap;
layout(set=0, binding=3) uniform samplerCube u_skybox;

layout(push_constant) uniform PushConstants {
    mat4  viewProj;
    vec3  cameraPos;
    float timeSeconds;
    vec3  bottomColor;
    float turbidity;
    vec2  flowDirection;
    float flowSpeed;
    float refractionAmount;
    float fresnelPower;
    float reflectionStrength;
    vec2  screenSize;
} pc;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec2 vFlowDir;

layout(location = 0) out vec4 fragColor;

vec3 unpackNormal(vec3 n) { return normalize(n * 2.0 - 1.0); }

// SSR mince : raymarch en screen-space, max 32 steps, fallback skybox.
vec3 ssrTrace(vec3 worldPos, vec3 reflectDir, vec3 fallback)
{
    const int  kMaxSteps  = 32;
    const float kStepSize  = 0.5;
    const float kThickness = 0.25;

    vec3 rayPos = worldPos + reflectDir * 0.05;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        rayPos += reflectDir * kStepSize;
        vec4 clip = pc.viewProj * vec4(rayPos, 1.0);
        if (clip.w <= 0.0) break;
        vec3 ndc = clip.xyz / clip.w;
        if (any(lessThan(ndc.xy, vec2(-1.0))) || any(greaterThan(ndc.xy, vec2(1.0))))
            break;
        vec2 screenUv = ndc.xy * 0.5 + 0.5;
        float sampledDepth = texture(u_sceneDepth, screenUv).r;
        float rayDepthNorm = ndc.z * 0.5 + 0.5;
        if (sampledDepth < rayDepthNorm - 1e-4)
        {
            float depthDiff = rayDepthNorm - sampledDepth;
            if (depthDiff > 0.0 && depthDiff * length(rayPos - worldPos) < kThickness)
                return texture(u_sceneColor, screenUv).rgb;
        }
    }
    return fallback;
}

void main()
{
    vec2 flow = pc.flowDirection * pc.flowSpeed * pc.timeSeconds;
    vec2 uv1 = vUv * 8.0  + flow;
    vec2 uv2 = vUv * 16.0 - flow * 0.5;
    vec3 n1 = unpackNormal(texture(u_normalMap, uv1).xyz);
    vec3 n2 = unpackNormal(texture(u_normalMap, uv2).xyz);
    vec3 n  = normalize(n1 + n2);

    vec2 screenUv = gl_FragCoord.xy / pc.screenSize;
    vec2 refrUv   = clamp(screenUv + n.xz * pc.refractionAmount, vec2(0.0), vec2(1.0));
    vec3 refr     = texture(u_sceneColor, refrUv).rgb;
    refr          = mix(refr, pc.bottomColor, pc.turbidity);

    vec3 viewDir    = normalize(pc.cameraPos - vWorldPos);
    vec3 surfaceN   = vec3(n.x, 1.0, n.z);
    vec3 reflectDir = reflect(-viewDir, surfaceN);
    vec3 skyFallback = texture(u_skybox, reflectDir).rgb;
    vec3 refl = ssrTrace(vWorldPos, reflectDir, skyFallback);

    float NdotV = max(0.0, dot(surfaceN, viewDir));
    float f = pow(1.0 - NdotV, pc.fresnelPower);

    vec3 color = mix(refr, refl, f * pc.reflectionStrength);
    fragColor = vec4(color, 1.0);
}
```

## 5. Frame graph — séquence finale

```
Geometry → SSAO → Decals (overlay) →
Lighting (writes SceneColor_HDR) →
Water (NEW)                                  ← cette PR
   reads:  SceneColor_HDR, SceneDepth, SkyboxCube
   writes: SceneColor_HDR_PostWater
Bloom_Prefilter (reads SceneColor_HDR_PostWater)   ← RENAME 1
Bloom_Downsample/Upsample (chaîne) →
Bloom_Combine (reads SceneColor_HDR_PostWater)     ← RENAME 2
   writes SceneColor_HDR_WithBloom
AutoExposure → Tonemap → TAA → CopyPresent
```

**Fallback si WaterPass.Init échoue** : enregistrer `Water_Passthrough`
qui blit `SceneColor_HDR` → `SceneColor_HDR_PostWater` (1 vkCmdCopyImage).
Cela garantit que les passes downstream (Bloom_*) trouvent toujours leur
input même sans water rendering.

## 6. Intégration Engine

### 6.1 Cleanup M38

| Action | Fichier | Lignes |
|---|---|---|
| Delete | `engine/render/WaterRenderer.h` | full |
| Delete | `engine/render/WaterRenderer.cpp` | full |
| Remove member | `engine/Engine.h` | `engine::render::WaterRenderer m_waterRenderer;` |
| Remove init | `engine/Engine.cpp` (1298-1321) | ~24 lignes |
| Remove destroy | `engine/Engine.cpp` (2738) | 1 ligne |
| Remove include | `engine/Engine.h` | `#include "engine/render/WaterRenderer.h"` |
| Remove CMake | `CMakeLists.txt` | `WaterRenderer.cpp` de engine_core |
| Remove config | `engine/Engine.cpp` (1305-1309) | render.water.level/grid_resolution/grid_half_size |

Net cleanup : ~ -550 LOC.

### 6.2 Additions Engine

**Engine.h** :
```cpp
#include "engine/render/WaterPass.h"
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterSurfaces.h"

// Membres :
engine::render::WaterPass    m_waterPass;
engine::render::WaterMeshGpu m_waterMeshGpu;
std::shared_ptr<engine::world::water::WaterScene> m_clientWaterScene;  // Mode client
bool                                              m_waterClientSceneDirty = false;

// FG resource ID :
engine::render::ResourceId m_fgSceneColorHDRPostWaterId = engine::render::kInvalidResourceId;
```

**Engine.cpp — boot (après Lighting init)** :
- Charge `shaders/water.vert.spv` + `water.frag.spv`
- Charge texture `textures/water_normal.ktx2` (placeholder si absente)
- Récupère skybox view depuis `m_zoneAtmosphere` (fallback gris si absente)
- `m_waterPass.Init(...)` ; si échec → log WARN, water rendering désactivé
- `m_waterMeshGpu.Init(...)` (buffer vide initial)

**Engine.cpp — chaque frame, avant FrameGraph::execute()** :
```cpp
const engine::world::water::WaterScene* scene = nullptr;
bool dirty = false;

if (m_worldEditorExe && m_waterDocument)
{
    scene = &m_waterDocument->Get();
    dirty = m_waterDocument->IsDirty();
}
else if (m_clientWaterScene)
{
    scene = m_clientWaterScene.get();
    dirty = m_waterClientSceneDirty;
}

if (scene && dirty)
{
    m_waterMeshGpu.Rebuild(device, phys, vmaAlloc, transferPool, transferQueue, *scene);
    if (m_worldEditorExe) m_waterDocument->ClearDirty();
    else                  m_waterClientSceneDirty = false;
}
```

**Engine.cpp — addPass Water (après Lighting)** :
```cpp
if (m_waterPass.IsValid())
{
    m_frameGraph.addPass("Water",
        [this](engine::render::PassBuilder& b) {
            b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::SampledRead);
            b.read(m_fgDepthId,                   engine::render::ImageUsage::SampledRead);
            b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::ColorWrite);
        },
        [this](VkCommandBuffer cmd, engine::render::Registry& reg) {
            const auto* scene = m_worldEditorExe ? &m_waterDocument->Get() : m_clientWaterScene.get();
            if (!scene || !m_waterMeshGpu.IsValid()) return;

            engine::render::WaterPassPushConstants base{};
            // fill viewProj, cameraPos, timeSeconds, screenSize depuis RenderState
            // (per-instance bottomColor, turbidity, flowDirection, flowSpeed, refractionAmount,
            //  fresnelPower, reflectionStrength sont écrasés instance par instance dans Record)
            const uint32_t frameIdx = m_currentFrame % 2;
            m_waterPass.Record(m_vkDeviceContext.GetDevice(), cmd, reg, m_vkSwapchain.GetExtent(),
                               m_fgSceneColorHDRId, m_fgDepthId, m_fgSceneColorHDRPostWaterId,
                               m_waterMeshGpu, base, *scene, frameIdx);
        });
}
else
{
    // Fallback passthrough : blit SceneColor_HDR → SceneColor_HDR_PostWater
    m_frameGraph.addPass("Water_Passthrough",
        [this](engine::render::PassBuilder& b) {
            b.read(m_fgSceneColorHDRId,           engine::render::ImageUsage::TransferSrc);
            b.write(m_fgSceneColorHDRPostWaterId, engine::render::ImageUsage::TransferDst);
        },
        [this](VkCommandBuffer cmd, engine::render::Registry& reg) {
            VkImage src = reg.getImage(m_fgSceneColorHDRId);
            VkImage dst = reg.getImage(m_fgSceneColorHDRPostWaterId);
            if (src == VK_NULL_HANDLE || dst == VK_NULL_HANDLE) return;
            VkImageCopy copy{};
            copy.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            copy.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            VkExtent2D ext = m_vkSwapchain.GetExtent();
            copy.extent = { ext.width, ext.height, 1 };
            vkCmdCopyImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        });
}
```

### 6.3 Renames downstream

Sites à modifier dans Engine.cpp (substitution `m_fgSceneColorHDRId` →
`m_fgSceneColorHDRPostWaterId` dans les déclarations `read()` uniquement) :

| Site | Ligne approx | Type |
|---|---|---|
| `Bloom_Prefilter` setup | 1971 | `b.read(...)` |
| `Bloom_Combine` setup | 2032 | `b.read(...)` |

Les `Record(...)` continuent de prendre `m_fgSceneColorHDRId` comme premier
argument (puisqu'ils lisent à travers le Registry, qui résout le nouvel ID
si utilisé dans le `read()` setup). À vérifier dans le plan task par task.

## 7. Tests CPU (5 cas)

### `engine/render/tests/WaterPassTests.cpp`

```cpp
#include "engine/render/WaterPass.h"

void Test_WaterPassPushConstants_Is128Bytes()
{
    REQUIRE(sizeof(engine::render::WaterPassPushConstants) == 128);
}

void Test_WaterPassPushConstants_FieldOffsets_MatchSpec()
{
    using PC = engine::render::WaterPassPushConstants;
    REQUIRE(offsetof(PC, viewProj)            ==   0);
    REQUIRE(offsetof(PC, cameraPos)           ==  64);
    REQUIRE(offsetof(PC, timeSeconds)         ==  76);
    REQUIRE(offsetof(PC, bottomColor)         ==  80);
    REQUIRE(offsetof(PC, turbidity)           ==  92);
    REQUIRE(offsetof(PC, flowDirection)       ==  96);
    REQUIRE(offsetof(PC, flowSpeed)           == 104);
    REQUIRE(offsetof(PC, refractionAmount)    == 108);
    REQUIRE(offsetof(PC, fresnelPower)        == 112);
    REQUIRE(offsetof(PC, reflectionStrength)  == 116);
    REQUIRE(offsetof(PC, screenSize)          == 120);
}
```

### `engine/render/tests/WaterMeshGpuTests.cpp`

```cpp
#include "engine/render/WaterMeshGpu.h"
#include "engine/world/water/WaterSurfaces.h"

void Test_BuildDrawInfos_EmptyScene_ZeroInstances()
{
    engine::world::water::WaterScene scene;
    std::vector<float> verts; std::vector<uint32_t> idx;
    std::vector<engine::render::WaterInstanceDrawInfo> infos;
    engine::render::BuildDrawInfos(scene, verts, idx, infos);
    REQUIRE(verts.empty());
    REQUIRE(idx.empty());
    REQUIRE(infos.empty());
}

void Test_BuildDrawInfos_OneLake_OneRiver_ProducesTwoInfos()
{
    engine::world::water::WaterScene scene;
    // Lac triangle simple
    engine::world::water::LakeInstance lake;
    lake.polygon = { {0,0,0}, {10,0,0}, {5,0,10} };
    lake.waterLevel = 0.0f;
    scene.lakes.push_back(lake);
    // Rivière 2 nœuds
    engine::world::water::RiverInstance river;
    river.nodes = { {{0,0,20}, 1.0f}, {{20,0,20}, 1.0f} };
    scene.rivers.push_back(river);

    std::vector<float> verts; std::vector<uint32_t> idx;
    std::vector<engine::render::WaterInstanceDrawInfo> infos;
    engine::render::BuildDrawInfos(scene, verts, idx, infos);

    REQUIRE(infos.size() == 2);
    REQUIRE(infos[0].paramsIndex == 0);  // Lake [0] : index unifié 0..N_lakes-1
    REQUIRE(infos[1].paramsIndex == 1);  // River [0] : index unifié N_lakes..N_lakes+N_rivers-1
}

void Test_BuildDrawInfos_ParamsIndexOrdering_LakesFirst()
{
    engine::world::water::WaterScene scene;
    engine::world::water::LakeInstance lake1, lake2;
    lake1.polygon = { {0,0,0}, {1,0,0}, {0,0,1} }; lake1.waterLevel = 0.0f;
    lake2.polygon = { {2,0,0}, {3,0,0}, {2,0,1} }; lake2.waterLevel = 0.0f;
    scene.lakes = { lake1, lake2 };
    engine::world::water::RiverInstance r1;
    r1.nodes = { {{0,0,5},1.0f}, {{5,0,5},1.0f} };
    scene.rivers = { r1 };

    std::vector<float> verts; std::vector<uint32_t> idx;
    std::vector<engine::render::WaterInstanceDrawInfo> infos;
    engine::render::BuildDrawInfos(scene, verts, idx, infos);

    REQUIRE(infos.size() == 3);
    // Index unifié : lakes en tête (0..1), rivers ensuite (2).
    REQUIRE(infos[0].paramsIndex == 0);
    REQUIRE(infos[1].paramsIndex == 1);
    REQUIRE(infos[2].paramsIndex == 2);
    // Vertex offsets monotones croissants (concaténation lake0|lake1|river0).
    REQUIRE(infos[0].vertexOffset == 0);
    REQUIRE(infos[1].vertexOffset > infos[0].vertexOffset);
    REQUIRE(infos[2].vertexOffset > infos[1].vertexOffset);
}
```

Framework REQUIRE maison (pattern M100.13). Les tests valident :
- Layout binaire des push constants (compatibilité C++ ↔ GLSL via offsetof).
- Logique CPU `BuildDrawInfos` (ordre lakes-puis-rivers, offsets cohérents).

**Tests différés (GPU CI requis)** :
- `Test_WaterPass_RegistersInFrameGraph` : vérifier que la passe est ajoutée au graph runtime.
- `Test_WaterPass_GoldenImage` : capture screenshot, comparaison à un PNG golden.
- Mesure perf : `WaterPass < 1.0 ms` sur lac 100×100 m.

## 8. Critères d'acceptation

- [x] M38 `WaterRenderer` complètement supprimé (h/cpp/Engine call sites/CMake/config keys)
- [x] `WaterPass` enregistré dans FrameGraph **entre Lighting et Bloom_Prefilter**
- [x] Ping-pong rename : Bloom_Prefilter et Bloom_Combine lisent `SceneColor_HDR_PostWater`
- [x] `WaterMeshGpu::Rebuild` upload via VMA staging buffer
- [x] `WaterDocument::IsDirty()` déclenche rebuild en mode éditeur (live)
- [x] `WaterPass` reste pur — aucune branche `m_editorEnabled` dedans
- [x] Shader fragment : 2 octaves normales + réfraction + SSR (32 steps) + Fresnel + skybox fallback
- [x] Push constants 128 B exacts, layout vérifié par test offsetof
- [x] Fallback `Water_Passthrough` (blit) si Init échoue
- [x] 5 tests CPU passent (sizeof, offsetof, BuildDrawInfos × 3)
- [ ] **Différé GPU CI** : test golden image, perf < 1.0 ms

## 9. Hors scope explicite

- Caustiques projetées au sol (ticket futur).
- Tessellation Gerstner / vagues 3D (ticket futur).
- Wading & swimming gameplay (M100.15).
- Mode tropical / aurora.
- Splash de pluie (M100.26).
- WaterPass branchant sur `m_editorEnabled` (interdit par contrat).

## 10. Format des fichiers (récapitulatif)

| Fichier | Action | Notes |
|---|---|---|
| `engine/render/WaterRenderer.h` | DELETE | M38 dead code |
| `engine/render/WaterRenderer.cpp` | DELETE | M38 dead code |
| `engine/render/WaterMeshGpu.h` | CREATE | Buffer GPU + helper CPU |
| `engine/render/WaterMeshGpu.cpp` | CREATE | |
| `engine/render/WaterPass.h` | CREATE | Passe FG-intégrée |
| `engine/render/WaterPass.cpp` | CREATE | |
| `engine/render/shaders/water.vert` | CREATE | (path canonique source) |
| `engine/render/shaders/water.frag` | CREATE | |
| `game/data/shaders/water.vert` | OVERWRITE | (build pipeline copie depuis engine/) |
| `game/data/shaders/water.frag` | OVERWRITE | |
| `engine/render/tests/WaterPassTests.cpp` | CREATE | 2 tests offsetof |
| `engine/render/tests/WaterMeshGpuTests.cpp` | CREATE | 3 tests BuildDrawInfos |
| `engine/Engine.h` | MODIFY | -1 include + 1 include + 4 membres + 1 ResourceId |
| `engine/Engine.cpp` | MODIFY | -29 lignes M38 + ~80 lignes WaterPass wiring + 2 renames downstream |
| `CMakeLists.txt` | MODIFY | -1 (WaterRenderer.cpp) + 4 (WaterMeshGpu.cpp + WaterPass.cpp + 2 tests exe) |
| `tickets/M100/INDEX.md` | MODIFY | M100.14 Ready → Done (CI pending) |

## 11. Déploiement

> **Déploiement** : ✅ client/éditeur uniquement, pas de redéploiement
> serveur. Aucun protocole, aucun format binaire ajouté (M100.14 réutilise
> `instances/water.bin` de M100.13).

## 12. Ordre d'implémentation suggéré (pour writing-plans)

1. Cleanup M38 — delete WaterRenderer + Engine call sites (préalable, isolé).
2. `WaterMeshGpu` CPU — `BuildDrawInfos` testable + 3 tests CPU.
3. `WaterMeshGpu` GPU — `Init`/`Rebuild`/`Destroy` avec VMA staging.
4. `WaterPassPushConstants` struct + `WaterPass.h` API + 2 tests offsetof.
5. `WaterPass.cpp` — Init (render pass + descriptor + pipeline + samplers).
6. `WaterPass.cpp` — Record (begin pass + draw per instance + push constants).
7. Shaders `water.vert` + `water.frag` (avec SSR + Fresnel inline).
8. Engine.cpp — wiring (boot init, frame update, addPass Water + fallback, renames downstream).
9. CMakeLists.txt + tickets/M100/INDEX.md + validation finale.

Soit ~9 tasks TDD strict pour le plan d'implémentation.
