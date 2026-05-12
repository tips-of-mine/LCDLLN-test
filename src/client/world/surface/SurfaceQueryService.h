// src/client/world/surface/SurfaceQueryService.h
#pragma once

#include "src/shared/math/Math.h"
#include "src/client/world/surface/SurfaceType.h"

#include <cstdint>
#include <unordered_set>

namespace engine::core { class Config; }
namespace engine::world { class StreamCache; }
namespace engine::world::terrain { struct LayerPalette; }
namespace engine::world::water { class WaterSampler; }

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

        /// Branche optionnellement un `WaterSampler` (M100.15) pour overrider
        /// le résultat splat-map vers `ShallowWater` (depth < 1 m) ou
        /// `DeepWater` (depth >= 1 m) si le point est dans un volume d'eau.
        /// Passer `nullptr` désactive l'override (comportement M100.11 pur).
        void SetWaterSampler(const engine::world::water::WaterSampler* sampler) noexcept;

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
        const engine::world::water::WaterSampler*        m_waterSampler = nullptr;

        /// M100.15 — applique l'override Shallow/DeepWater au résultat splat
        /// si un sampler est branché ET que le point est dans l'eau. Sinon
        /// retourne `r` tel quel.
        SurfaceQueryResult ApplyWaterOverride(SurfaceQueryResult r,
                                              engine::math::Vec3 worldPos) const noexcept;
    };
}
