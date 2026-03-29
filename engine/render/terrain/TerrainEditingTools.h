#pragma once

#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/render/terrain/TerrainSplatting.h"

#include <cstdint>
#include <string>

#include <vulkan/vulkan_core.h>

namespace engine::core    { class Config; }

namespace engine::render::terrain
{
    /// Brush operations supported by the terrain editor.
    enum class BrushOp : uint32_t
    {
        Raise   = 0, ///< Raise height by strength × kernel weight.
        Lower   = 1, ///< Lower height by strength × kernel weight.
        Smooth  = 2, ///< Gaussian-weighted average of neighbouring pixels within the brush.
        Flatten = 3, ///< Lerp toward a target normalised height.
    };

    /// Parameters shared by all brush operations.
    struct BrushParams
    {
        float radius        = 10.0f; ///< World-space brush radius in metres.
        float strength      = 0.10f; ///< Effect magnitude per call [0, 1].
        float falloff       = 1.0f;  ///< Falloff exponent: 1=linear, 2=quadratic, …
        float flattenTarget = 0.0f;  ///< Target normalised height [0, 1] used by Flatten.
    };

    /// Binary magic for exported splat map files ("SLAP", little-endian).
    static constexpr uint32_t kSplatFileMagic = 0x50414C53u;

    /// CPU-side terrain editing tools: heightmap brush and splat paint.
    ///
    /// Usage pattern:
    ///   1. Obtain a TerrainRenderer that has been successfully Init'd.
    ///   2. Call Init() passing the renderer's mutable CPU data + Vulkan context.
    ///   3. Each editor frame: call ApplyBrush() / PaintSplat() with world-space coords.
    ///   4. After editing: call FlushHeightmap() / FlushSplatMap() to push to GPU.
    ///   5. To persist: call SaveHeightmap() / SaveSplatMap() with content-relative paths.
    ///
    /// Thread safety: all methods are NOT thread-safe. Call from the main/render thread only.
    class TerrainEditingTools final
    {
    public:
        TerrainEditingTools() = default;
        TerrainEditingTools(const TerrainEditingTools&) = delete;
        TerrainEditingTools& operator=(const TerrainEditingTools&) = delete;

        /// Initialises the editing tools.
        ///
        /// \param heightmap        Mutable CPU heightmap owned by TerrainRenderer.
        ///                         Must remain valid for the lifetime of this object.
        /// \param splatting        TerrainSplatting instance that holds a CPU splat copy.
        ///                         Must remain valid for the lifetime of this object.
        /// \param terrainOriginX   World X of the terrain corner (config: terrain.origin_x).
        /// \param terrainOriginZ   World Z of the terrain corner (config: terrain.origin_z).
        /// \param terrainWorldSize Total world extent in metres (config: terrain.world_size).
        /// \param heightScale      Max world height in metres (config: terrain.height_scale).
        /// \return true on success.
        bool Init(HeightmapData*    heightmap,
                  TerrainSplatting* splatting,
                  float terrainOriginX,
                  float terrainOriginZ,
                  float terrainWorldSize,
                  float heightScale);

        /// Releases all internal state. Calling Init() again is safe afterwards.
        void Shutdown();

        /// Returns true if Init() succeeded and Shutdown() has not been called.
        bool IsValid() const { return m_initialized; }

        // ── Heightmap brush ───────────────────────────────────────────────────────

        /// Applies a heightmap brush at a world-space position.
        ///
        /// Modifies the CPU heightmap in place. Sets the dirty-heightmap flag.
        /// Call FlushHeightmap() after all brush strokes to upload the result to the GPU.
        ///
        /// \param worldX   World X centre of the brush stroke.
        /// \param worldZ   World Z centre of the brush stroke.
        /// \param op       Brush operation (Raise / Lower / Smooth / Flatten).
        /// \param params   Brush configuration (radius, strength, falloff, flattenTarget).
        void ApplyBrush(float worldX, float worldZ,
                        BrushOp op, const BrushParams& params);

        // ── Splat paint ───────────────────────────────────────────────────────────

        /// Paints a splat layer weight at a world-space position.
        ///
        /// Increases the weight of `layer` within the brush radius, then renormalises
        /// all four layer weights so their sum equals 1 per pixel.
        /// Sets the dirty-splatmap flag.
        /// Call FlushSplatMap() after all paint strokes.
        ///
        /// \param worldX   World X centre of the brush stroke.
        /// \param worldZ   World Z centre of the brush stroke.
        /// \param layer    Layer index [0, kSplatLayerCount). 0=grass,1=dirt,2=rock,3=snow.
        /// \param params   Brush configuration (radius, strength, falloff).
        void PaintSplat(float worldX, float worldZ,
                        uint32_t layer, const BrushParams& params);

        // ── GPU upload ────────────────────────────────────────────────────────────

        /// Uploads the full CPU heightmap to the provided GPU R16_UNORM image via staging.
        ///
        /// The image must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL before the call.
        /// It is returned to that layout on success. Clears the dirty-heightmap flag.
        ///
        /// \param heightmapImage  VkImage owned by TerrainRenderer (HeightmapGpu::image).
        /// \return true on success.
        bool FlushHeightmap(VkDevice device, VkPhysicalDevice physDev,
                            VkQueue queue, uint32_t queueFamilyIndex,
                            VkImage heightmapImage);

        /// Uploads the full CPU splat map to the GPU via TerrainSplatting::ReuploadSplatMap().
        ///
        /// Clears the dirty-splatmap flag on success.
        /// \return true on success.
        bool FlushSplatMap(VkDevice device, VkPhysicalDevice physDev,
                           VkQueue queue, uint32_t queueFamilyIndex);

        // ── Save / export ─────────────────────────────────────────────────────────

        /// Exports the CPU heightmap to a .r16h binary file.
        ///
        /// File format (all little-endian):
        ///   magic  : uint32 = kHeightmapMagic (0x504D4148, "HAMP")
        ///   width  : uint32
        ///   height : uint32
        ///   data   : uint16[width * height]  (row-major, z * width + x)
        ///
        /// Parent directories are created automatically if missing.
        ///
        /// \param config   Used to resolve paths.content.
        /// \param relPath  Content-relative path (e.g. "terrain/heightmap.r16h").
        /// \return true on success.
        bool SaveHeightmap(const engine::core::Config& config,
                           const std::string& relPath);

        /// Exports the CPU splat map to a .rgba8 binary file.
        ///
        /// File format (all little-endian):
        ///   magic  : uint32 = kSplatFileMagic (0x50414C53, "SLAP")
        ///   width  : uint32
        ///   height : uint32
        ///   data   : uint8[width * height * 4]  (RGBA, row-major)
        ///
        /// Parent directories are created automatically if missing.
        ///
        /// \param config   Used to resolve paths.content.
        /// \param relPath  Content-relative path (e.g. "terrain/splatmap.rgba8").
        /// \return true on success.
        bool SaveSplatMap(const engine::core::Config& config,
                          const std::string& relPath);

        // ── Dirty flags ───────────────────────────────────────────────────────────

        /// Returns true if the CPU heightmap has been modified since the last flush/clear.
        bool IsDirtyHeightmap() const { return m_dirtyHeightmap; }
        /// Returns true if the CPU splat map has been modified since the last flush/clear.
        bool IsDirtySplatMap()  const { return m_dirtySplatMap;  }
        /// Resets both dirty flags without uploading.
        void ClearDirtyFlags()  { m_dirtyHeightmap = false; m_dirtySplatMap = false; }

    private:
        /// Returns the brush kernel weight [0, 1] for a pixel at world-distance `dist`
        /// from the brush centre. Returns 0 when dist >= radius.
        float ComputeKernel(float dist, float radius, float falloff) const;

        /// Converts world X/Z to heightmap pixel coordinates (may be out of bounds).
        void WorldToHeightmapPixel(float worldX, float worldZ,
                                   int32_t& outPx, int32_t& outPz) const;

        /// Converts world X/Z to splat map pixel coordinates (may be out of bounds).
        void WorldToSplatPixel(float worldX, float worldZ,
                               int32_t& outPx, int32_t& outPz) const;

        HeightmapData*    m_heightmap        = nullptr;
        TerrainSplatting* m_splatting        = nullptr;
        float             m_terrainOriginX   = 0.0f;
        float             m_terrainOriginZ   = 0.0f;
        float             m_terrainWorldSize = 0.0f;
        float             m_heightScale      = 0.0f;
        bool              m_initialized      = false;
        bool              m_dirtyHeightmap   = false;
        bool              m_dirtySplatMap    = false;
    };

} // namespace engine::render::terrain
