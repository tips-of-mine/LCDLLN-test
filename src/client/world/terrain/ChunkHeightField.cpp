#include "src/client/world/terrain/ChunkHeightField.h"

#include "src/client/world/StreamCache.h"
#include "src/client/world/WorldModel.h"                  // WorldToTerrainChunkCoord, kTerrainChunkSizeMeters
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainChunkLoader.h"  // LoadFromCache, MakeTerrainCacheKey

#include <string>

namespace engine::world::terrain
{
    ChunkHeightField::ChunkHeightField(engine::world::StreamCache* cache,
                                       const engine::core::Config* config)
        : m_cache(cache), m_config(config) {}

    std::shared_ptr<const TerrainChunk>
    ChunkHeightField::ResidentChunkAt(float worldX, float worldZ) const
    {
        if (m_cache == nullptr) return nullptr;
        const engine::world::GlobalChunkCoord c =
            engine::world::WorldToTerrainChunkCoord(worldX, worldZ);
        const std::string key = MakeTerrainCacheKey(c.x, c.z);
        std::string err;
        // LoadFromCache = lookup PUR (StreamCache::Lookup + désérialisation) :
        // aucune I/O disque, nullptr si la clé n'est pas résidente.
        return LoadFromCache(*m_cache, key, err);
    }

    bool ChunkHeightField::IsLoadedAt(float worldX, float worldZ) const
    {
        return ResidentChunkAt(worldX, worldZ) != nullptr;
    }

    float ChunkHeightField::HeightAt(float worldX, float worldZ) const
    {
        auto chunk = ResidentChunkAt(worldX, worldZ);
        if (!chunk) return 0.0f;
        const engine::world::GlobalChunkCoord c =
            engine::world::WorldToTerrainChunkCoord(worldX, worldZ);
        // Origine monde du chunk = index * taille de la grille terrain (256 m).
        const float originX = static_cast<float>(c.x)
            * static_cast<float>(engine::world::kTerrainChunkSizeMeters);
        const float originZ = static_cast<float>(c.z)
            * static_cast<float>(engine::world::kTerrainChunkSizeMeters);
        // SampleHeight attend des coords chunk-locales (m) ; bilinéaire + clamp.
        return chunk->SampleHeight(worldX - originX, worldZ - originZ);
    }
}
