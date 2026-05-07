// engine/world/surface/SurfaceQueryService.h
#pragma once

#include "engine/math/Math.h"
#include "engine/world/surface/SurfaceType.h"

#include <cstdint>
#include <unordered_set>

namespace engine::core { class Config; }
namespace engine::world { class StreamCache; }
namespace engine::world::terrain { struct LayerPalette; }

namespace engine::world::surface
{
    class SurfaceTable;

    struct SurfaceModifiers
    {
        bool slippery = false;
        bool wet = false;
        bool frozen = false;
        bool seasonalSnow = false;
        float speedMultiplier = 1.0f;
        float audioPitchShift = 1.0f;
    };

    struct SurfaceQueryResult
    {
        SurfaceType base = SurfaceType::Dirt;
        SurfaceModifiers modifiers{};
    };

    /// Résout `worldPos` → `(SurfaceType, SurfaceModifiers)` à partir de la
    /// splat-map dominante locale. Lecture via `StreamCache::LoadSplatMap`,
    /// résolution layer→type via `LayerPalette`. Si splat indisponible :
    /// fallback `{Dirt, {}}` + warn une fois par chunk.
    /// Modifiers neutres en M100.11 (M100.26 les calculera depuis météo/saison).
    ///
    /// Contrainte thread : `Query` doit être appelée depuis le main thread
    /// uniquement. `m_warnedChunks` n'est pas thread-safe (mutation `mutable`
    /// dans une méthode `const` sans verrou).
    class SurfaceQueryService
    {
    public:
        bool Init(const SurfaceTable& table,
                  engine::world::StreamCache& cache,
                  const engine::core::Config& cfg,
                  const engine::world::terrain::LayerPalette& palette) noexcept;

        SurfaceQueryResult Query(engine::math::Vec3 worldPos) const;

    private:
        // m_table est stocké pour M100.26 (computation des modifiers depuis
        // météo/saison). Inutilisé en M100.11 — Query renvoie toujours des
        // modifiers neutres. Le null-guard inclut m_table pour cohérence.
        const SurfaceTable*                              m_table = nullptr;
        engine::world::StreamCache*                      m_cache = nullptr;
        const engine::core::Config*                      m_cfg = nullptr;
        const engine::world::terrain::LayerPalette*      m_palette = nullptr;
        // Throttle warn : 1 par (chunkX, chunkZ) pour la session.
        // Encodage clé : (int64_t(chunkX) << 32) | uint32_t(chunkZ).
        // Non thread-safe (cf. contrainte thread sur la classe).
        mutable std::unordered_set<int64_t>              m_warnedChunks;
    };
}
