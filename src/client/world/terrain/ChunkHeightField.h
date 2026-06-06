#pragma once
#include "src/client/world/terrain/IHeightField.h"
#include <memory>

namespace engine::core { class Config; }
namespace engine::world { class StreamCache; }

namespace engine::world::terrain
{
    struct TerrainChunk;

    /// `IHeightField` adossé aux chunks `terrain.bin` streamés (grille 256 m).
    /// Échantillonne UNIQUEMENT des chunks DÉJÀ résidents (lookup LRU pur, 0 I/O).
    /// Main-thread (StreamCache main-thread-only). Aujourd'hui : aucun terrain.bin
    /// livré → IsLoadedAt toujours faux → repli heightmap (comportement inchangé).
    class ChunkHeightField final : public IHeightField
    {
    public:
        /// \param cache  Cache LRU des blobs `terrain.bin` (non-owning). Si
        ///        nullptr, IsLoadedAt est toujours faux et HeightAt retombe sur 0.
        /// \param config Réservé (cohérence d'API / futurs paramètres de zone) ;
        ///        non utilisé dans le chemin résident pur (aucune I/O).
        ChunkHeightField(engine::world::StreamCache* cache, const engine::core::Config* config);

        float HeightAt(float worldX, float worldZ) const override;
        bool  IsLoadedAt(float worldX, float worldZ) const override;

    private:
        /// Retourne le chunk terrain résident couvrant (worldX, worldZ) via un
        /// lookup cache PUR (aucune I/O disque), ou nullptr si absent du cache.
        std::shared_ptr<const TerrainChunk> ResidentChunkAt(float worldX, float worldZ) const;

        engine::world::StreamCache* m_cache  = nullptr;
        const engine::core::Config* m_config = nullptr;
    };
}
