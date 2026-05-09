// src/client/world/surface/SurfaceQueryService.cpp
#include "src/client/world/surface/SurfaceQueryService.h"
#include "src/client/world/surface/SurfaceTable.h"
#include "src/client/world/StreamCache.h"
#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/LayerPalette.h"
#include "src/client/world/terrain/SplatMap.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <algorithm>

namespace engine::world::surface
{
    bool SurfaceQueryService::Init(const SurfaceTable& table,
                                    engine::world::StreamCache& cache,
                                    const engine::core::Config& cfg,
                                    const engine::world::terrain::LayerPalette& palette) noexcept
    {
        m_table   = &table;
        m_cache   = &cache;
        m_cfg     = &cfg;
        m_palette = &palette;
        return true;
    }

    SurfaceQueryResult SurfaceQueryService::Query(engine::math::Vec3 worldPos) const
    {
        SurfaceQueryResult fallback{ SurfaceType::Dirt, {} };
        if (!m_table || !m_cache || !m_cfg || !m_palette) return fallback;

        // 1. worldPos.xz → (chunkCoord, localCellX, localCellZ).
        const auto coord = engine::world::WorldToGlobalChunkCoord(worldPos.x, worldPos.z);

        // ChunkBounds → cellule splat locale. ChunkSize en mètres = engine::world::kChunkSize.
        // localCell = (worldOffset / chunkSize) * splatResolution.
        const auto bounds = engine::world::ChunkBounds(coord);
        const float chunkSizeX = bounds.maxX - bounds.minX;
        const float chunkSizeZ = bounds.maxZ - bounds.minZ;
        if (chunkSizeX <= 0.0f || chunkSizeZ <= 0.0f) return fallback;

        const float fx = (worldPos.x - bounds.minX) / chunkSizeX;
        const float fz = (worldPos.z - bounds.minZ) / chunkSizeZ;
        const int splatRes = static_cast<int>(engine::world::terrain::kSplatResolution);
        int localCellX = static_cast<int>(fx * static_cast<float>(splatRes));
        int localCellZ = static_cast<int>(fz * static_cast<float>(splatRes));
        localCellX = std::clamp(localCellX, 0, splatRes - 1);
        localCellZ = std::clamp(localCellZ, 0, splatRes - 1);

        // 2. Charger splat.bin (cache → disk → nullptr).
        auto splat = m_cache->LoadSplatMap(*m_cfg, coord.x, coord.z);
        if (!splat)
        {
            // Throttle warn : 1× par (coord) par session.
            const int64_t key = (static_cast<int64_t>(coord.x) << 32)
                              | static_cast<uint32_t>(coord.z);
            if (m_warnedChunks.insert(key).second)
            {
                LOG_WARN(World, "[SurfaceQuery] splat absent for chunk ({},{}) → fallback Dirt",
                    coord.x, coord.z);
            }
            return fallback;
        }

        // 3. Lire les 8 poids à (localCellX, localCellZ). Tie-break : plus petit index.
        const auto layerCount = static_cast<size_t>(splat->layerCount);
        const size_t cellOffset = (static_cast<size_t>(localCellZ) * splat->resolution
                                 + static_cast<size_t>(localCellX)) * layerCount;
        uint8_t maxWeight = 0;
        size_t maxLayer = 0;
        for (size_t i = 0; i < layerCount; ++i)
        {
            const uint8_t w = splat->weights[cellOffset + i];
            if (w > maxWeight)
            {
                maxWeight = w;
                maxLayer = i;
            }
        }

        // 4-5. layer→SurfaceType via palette.
        SurfaceQueryResult r;
        r.base = m_palette->GetSurfaceTypeForLayer(static_cast<uint8_t>(maxLayer));
        // 6. modifiers neutres en M100.11.
        return r;
    }
}
