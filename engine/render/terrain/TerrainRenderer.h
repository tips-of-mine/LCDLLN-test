#pragma once

#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/render/terrain/TerrainMesh.h"
#include "engine/render/terrain/TerrainSplatting.h"
#include "engine/render/terrain/TerrainHoleMask.h"
#include "engine/render/terrain/TerrainCliffMesh.h"
#include "engine/render/FrameGraph.h"
#include "engine/math/Frustum.h"
#include "engine/math/Math.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace engine::core { class Config; }

namespace engine::render::terrain
{
    /// World-space bounding data for one terrain patch.
    /// Used for LOD distance calculation and frustum culling.
    struct TerrainPatchInfo
    {
        float originX  = 0.0f; ///< World X of patch corner (min X)
        float originZ  = 0.0f; ///< World Z of patch corner (min Z)
        float centerX  = 0.0f; ///< World X of patch centre
        float centerZ  = 0.0f; ///< World Z of patch centre
        float minY     = 0.0f; ///< Min world Y over all patch vertices
        float maxY     = 0.0f; ///< Max world Y over all patch vertices
    };

    /// Terrain renderer: heightmap-based ground mesh with distance LOD and geomorphing.
    ///
    /// Architecture:
    ///  - One shared patch vertex buffer (17×17 local XZ vertices) + 5 per-LOD index buffers.
    ///  - CPU-side LOD selection (distance camera→patch centre) and frustum culling per patch.
    ///  - Per-patch push constants (origin, morph factor, LOD level).
    ///  - Per-frame UBO (view-proj matrices, camera position, terrain world parameters).
    ///  - Writes into GBuffer-compatible attachments (A/B/C/Velocity/Depth).
    ///
    /// This class is self-contained and does NOT modify Engine.cpp or DeferredPipeline.
    class TerrainRenderer
    {
    public:
        /// SPIR-V loader function (same signature as DeferredPipeline::ShaderLoaderFn).
        using ShaderLoaderFn = std::function<std::vector<uint32_t>(const char* spvPath)>;

        /// Camera-to-patch-centre distance thresholds for LOD selection (metres).
        /// LOD 0 used below kLodDistances[0], LOD N used below kLodDistances[N], etc.
        static constexpr float kLodDistances[kTerrainLodCount] = {
            50.f, 200.f, 500.f, 1000.f, 4000.f
        };

        TerrainRenderer() = default;
        TerrainRenderer(const TerrainRenderer&) = delete;
        TerrainRenderer& operator=(const TerrainRenderer&) = delete;

        /// Initialises the terrain renderer.
        ///
        /// Config keys read:
        ///   render.terrain.grass_mask_visual_strength (float, default 0.35) — multiplie le masque herbe en fragment (0 = off).
        ///   terrain.world_size             (float, default 1024.0) – total terrain world extent (metres)
        ///   terrain.height_scale           (float, default 200.0)  – max height in world units
        ///   terrain.origin_x               (float, default -512.0) – world X of terrain corner
        ///   terrain.origin_z               (float, default -512.0) – world Z of terrain corner
        ///   terrain.splat.tiling_grass     (float, default 8.0)    – metres per tile, grass layer
        ///   terrain.splat.tiling_dirt      (float, default 8.0)    – metres per tile, dirt layer
        ///   terrain.splat.tiling_rock      (float, default 16.0)   – metres per tile, rock layer
        ///   terrain.splat.tiling_snow      (float, default 12.0)   – metres per tile, snow layer
        ///
        /// \param heightmapRelPath   Content-relative path to the .r16h file
        ///                           (e.g. "terrain/heightmap.r16h"). If the file is absent,
        ///                           Init returns false gracefully (no crash).
        /// \param splatmapRelPath    Content-relative path to a SLAP splat file (`kTerrainSplatFileMagic`).
        ///                           Empty or fichier invalide → splat par défaut (herbe) générée dans `TerrainSplatting`.
        /// \param grassMaskRelPath   Content-relative path to a GRMS grass/detail mask (`kGrassMaskFileMagic`).
        ///                           Même résolution que la splat CPU ; absent ou invalide → masque nul (pas d’effet).
        /// \param holeMaskRelPath    Content-relative path to the .hmask file
        ///                           (e.g. "terrain/holemask.hmask"). If absent, a fully-solid
        ///                           fallback is used (no holes, no change to visible terrain).
        /// \param cliffMeshRelPaths  Content-relative paths to .cliff mesh files
        ///                           (e.g. {"terrain/cliff_a.cliff", "terrain/cliff_b.cliff"}).
        ///                           May be empty. Missing files are skipped with a warning.
        /// \param fmtA/B/C/Vel/Depth GBuffer attachment formats (must match GeometryPass).
        /// \param queue              Graphics queue used for one-time GPU uploads.
        /// \param terrainWorldSizeMetersOverride Si renseigné (>0), remplace la clé config `terrain.world_size` pour l’étendue monde (m).
        bool Init(VkDevice device, VkPhysicalDevice physDev,
                  const engine::core::Config& config,
                  const std::string& heightmapRelPath,
                  const std::string& splatmapRelPath,
                  const std::string& grassMaskRelPath,
                  const std::string& holeMaskRelPath,
                  const std::vector<std::string>& cliffMeshRelPaths,
                  VkFormat fmtA, VkFormat fmtB, VkFormat fmtC,
                  VkFormat fmtVelocity, VkFormat fmtDepth,
                  VkQueue queue, uint32_t queueFamilyIndex,
                  ShaderLoaderFn loadSpirv,
                  std::optional<float> terrainWorldSizeMetersOverride = std::nullopt);

        /// Destroys all GPU resources. Safe to call when not initialised.
        void Destroy(VkDevice device);

        /// Records terrain draw calls into cmd.
        ///
        /// Performs:
        ///   1. Updates the per-frame UBO (viewProj, cameraPos, terrain params).
        ///   2. For each patch: distance-based LOD selection + morph factor + frustum cull.
        ///   3. Begins the terrain render pass (clears GBuffer attachments).
        ///   4. Draws visible patches grouped by LOD.
        ///   5. Ends the render pass.
        ///
        /// \param prevViewProjMat4  Column-major mat4 from previous frame (for TAA velocity).
        /// \param viewProjMat4      Column-major mat4 for current frame.
        void Record(VkDevice device, VkCommandBuffer cmd,
                    engine::render::Registry& registry,
                    VkExtent2D extent,
                    engine::render::ResourceId idA,
                    engine::render::ResourceId idB,
                    engine::render::ResourceId idC,
                    engine::render::ResourceId idVelocity,
                    engine::render::ResourceId idDepth,
                    const float* prevViewProjMat4,
                    const float* viewProjMat4,
                    const engine::math::Vec3& cameraPos,
                    const engine::math::Frustum& frustum);

        /// Destroys cached framebuffers. Call before resizing GBuffer images.
        void InvalidateFramebufferCache(VkDevice device);

        /// Returns true if Init succeeded and Destroy has not been called.
        bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; }

        /// Returns the CPU hole mask data for navmesh / collision queries (M34.3).
        /// IsHole(qx, qz) returns true when quad (qx, qz) is a hole (unwalkable).
        const HoleMaskData& GetHoleMaskData() const { return m_holeMaskData; }

        /// Masque herbe / détail surface (R8, UV alignés sur la splat). Ticket 010.
        HoleMaskData&       GetMutableGrassMaskData()       { return m_grassMaskData; }
        const HoleMaskData& GetGrassMaskData() const        { return m_grassMaskData; }

        /// Recrée la texture GPU du masque herbe à partir du buffer CPU et met à jour le descriptor (binding 8).
        bool ReuploadGrassMaskFromCpuData(VkDevice device, VkPhysicalDevice physDev,
                                          VkQueue queue, uint32_t queueFamilyIndex);

        // ── M34.4: terrain editing accessors ─────────────────────────────────────

        /// Returns a mutable reference to the CPU heightmap.
        /// Used by TerrainEditingTools to apply brush operations.
        HeightmapData& GetMutableHeightmapData() { return m_heightmapData; }

        /// Returns the GPU heightmap resources.
        /// Pass HeightmapGpu::image to TerrainEditingTools::FlushHeightmap().
        const HeightmapGpu& GetHeightmapGpu() const { return m_heightmapGpu; }

        /// Returns the TerrainSplatting instance for editing and re-upload.
        TerrainSplatting& GetSplatting() { return m_splatting; }

        /// Returns the world X origin of the terrain (config: terrain.origin_x).
        float GetTerrainOriginX()   const { return m_terrainOriginX;   }
        /// Returns the world Z origin of the terrain (config: terrain.origin_z).
        float GetTerrainOriginZ()   const { return m_terrainOriginZ;   }
        /// Returns the total world extent of the terrain in metres (config: terrain.world_size).
        float GetTerrainWorldSize() const { return m_terrainWorldSize; }
        /// Returns the maximum height in world units (config: terrain.height_scale).
        float GetHeightScale()      const { return m_heightScale;      }

        /// Echantillonne la hauteur du terrain (en metres monde) a la position
        /// horizontale (worldX, worldZ). Filtrage bilineaire ; retombe sur 0
        /// si la heightmap n'est pas chargee. Utilisable par la collision
        /// camera-sol et le snap au sol des avatars.
        float SampleHeightAtWorldXZ(float worldX, float worldZ) const;

    private:
        // ── Push constants ────────────────────────────────────────────────────────
        // All stages, 16 bytes total.
        // offset  0: float patchOriginX
        // offset  4: float patchOriginZ
        // offset  8: float morphFactor   [0,1]
        // offset 12: int   lodLevel      [0, kTerrainLodCount-1]
        struct PushConstants
        {
            float   patchOriginX = 0.0f;
            float   patchOriginZ = 0.0f;
            float   morphFactor  = 0.0f;
            int32_t lodLevel     = 0;
        };

        // ── Per-frame UBO (set=0, binding=2) ─────────────────────────────────────
        // std140 layout using vec4 packing (no scalar arrays), 192 bytes total.
        //   mat4  viewProj        offset   0  (64 bytes)
        //   mat4  prevViewProj    offset  64  (64 bytes)
        //   vec4  cameraPos       offset 128  (xyz = position, w = unused)
        //   vec4  terrainParams   offset 144  (x=terrainSize, y=heightScale, z=vertStepWorld, w=grassMaskVisualStrength)
        //   vec4  terrainOrigin   offset 160  (x=originX, y=originZ, z=unused, w=unused)
        //   vec4  layerTiling     offset 176  (x=grass, y=dirt, z=rock, w=snow tiling metres/tile)
        //                         total  192
        struct FrameUbo
        {
            float viewProj[16];      // offset   0
            float prevViewProj[16];  // offset  64
            float cameraPos[4];      // offset 128  (xyz + w=0)
            float terrainParams[4];  // offset 144  (x=size, y=heightScale, z=vertStepWorld, w=grassMaskStrength)
            float terrainOrigin[4];  // offset 160  (x=originX, y=originZ, z=0, w=0)
            float layerTiling[4];    // offset 176  (x=grass, y=dirt, z=rock, w=snow tiling)
        };                           //         192

        // ── Cliff per-frame UBO (cliff pipeline, set=0, binding=0) ───────────
        // Minimal UBO for cliff pipeline: only view-proj matrices.
        //   mat4  viewProj      offset  0  (64 bytes)
        //   mat4  prevViewProj  offset 64  (64 bytes)
        //                       total 128
        struct CliffFrameUbo
        {
            float viewProj[16];     // offset  0
            float prevViewProj[16]; // offset 64
        };                          //       128

        // ── Framebuffer cache ─────────────────────────────────────────────────────
        struct FramebufferKey
        {
            VkRenderPass renderPass   = VK_NULL_HANDLE;
            VkImageView  views[5]     = {};
            uint32_t     width        = 0;
            uint32_t     height       = 0;
            bool operator==(const FramebufferKey& o) const;
        };
        struct FramebufferKeyHash
        {
            size_t operator()(const FramebufferKey& k) const;
        };

        // ── Vulkan objects — terrain pipeline ─────────────────────────────────────
        VkRenderPass          m_renderPass    = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_descSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_descPool      = VK_NULL_HANDLE;
        VkDescriptorSet       m_descSet       = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
        VkPipeline            m_pipeline      = VK_NULL_HANDLE;

        /// HOST_COHERENT UBO buffer updated once per Record() call.
        VkBuffer              m_uboBuffer     = VK_NULL_HANDLE;
        VkDeviceMemory        m_uboMemory     = VK_NULL_HANDLE;

        HeightmapGpu          m_heightmapGpu;
        NormalMapGpu          m_normalMapGpu;
        TerrainMeshGpu        m_meshGpu;
        HeightmapData         m_heightmapData; ///< CPU copy for patch bound calculation
        TerrainSplatting      m_splatting;     ///< Splat map + texture arrays (M34.2)

        // ── M34.3: Hole mask ──────────────────────────────────────────────────────
        HoleMaskData          m_grassMaskData; ///< CPU grass/detail mask (same resolution as splat)
        HoleMaskGpu           m_grassMaskGpu;   ///< GPU R8_UNORM (binding 8)

        HoleMaskData          m_holeMaskData; ///< CPU hole mask for navmesh queries
        HoleMaskGpu           m_holeMaskGpu;  ///< GPU R8_UNORM texture (binding 7)

        float                 m_grassMaskVisualStrength = 0.0f; ///< `render.terrain.grass_mask_visual_strength` (0 = off)

        // ── M34.3: Cliff pipeline ─────────────────────────────────────────────────
        VkDescriptorSetLayout m_cliffDescSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool      m_cliffDescPool      = VK_NULL_HANDLE;
        VkDescriptorSet       m_cliffDescSet       = VK_NULL_HANDLE;
        VkPipelineLayout      m_cliffPipelineLayout = VK_NULL_HANDLE;
        VkPipeline            m_cliffPipeline      = VK_NULL_HANDLE;
        VkBuffer              m_cliffUboBuffer     = VK_NULL_HANDLE;
        VkDeviceMemory        m_cliffUboMemory     = VK_NULL_HANDLE;

        /// Cliff albedo placeholder texture (1×1 solid rock colour).
        VkImage               m_cliffAlbedoImage   = VK_NULL_HANDLE;
        VkImageView           m_cliffAlbedoView    = VK_NULL_HANDLE;
        VkDeviceMemory        m_cliffAlbedoMemory  = VK_NULL_HANDLE;
        VkSampler             m_cliffAlbedoSampler = VK_NULL_HANDLE;

        /// Loaded and uploaded cliff meshes (one entry per .cliff file).
        std::vector<CliffMeshGpu> m_cliffMeshes;

        std::vector<TerrainPatchInfo> m_patches;
        std::unordered_map<FramebufferKey, VkFramebuffer, FramebufferKeyHash> m_fbCache;

        // ── Terrain world parameters ──────────────────────────────────────────────
        float    m_terrainOriginX    = 0.0f;
        float    m_terrainOriginZ    = 0.0f;
        float    m_terrainWorldSize  = 0.0f; ///< Total world extent (square)
        float    m_heightScale       = 0.0f; ///< World units for max height
        float    m_vertStepWorld     = 0.0f; ///< World units per local vertex step at LOD 0
        uint32_t m_patchCountX       = 0;
        uint32_t m_patchCountZ       = 0;
    };

} // namespace engine::render::terrain
