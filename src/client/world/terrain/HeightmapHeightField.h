#pragma once
#include "src/client/world/terrain/IHeightField.h"

namespace engine::render::terrain { class TerrainRenderer; }  // forward

namespace engine::world::terrain
{
    /// `IHeightField` adossé à la heightmap legacy (`TerrainRenderer`). Non-owning.
    /// Sert de source de repli quand aucun chunk `terrain.bin` n'est résident :
    /// échantillonne la même heightmap que celle réellement rendue, donc « le
    /// joueur marche sur ce qu'il voit » sur le contenu actuel.
    class HeightmapHeightField final : public IHeightField
    {
    public:
        /// \param renderer Source heightmap (non-owning). Peut être nullptr :
        ///        HeightAt retombe alors sur 0 et IsLoadedAt sur false.
        explicit HeightmapHeightField(const engine::render::terrain::TerrainRenderer* renderer);

        float HeightAt(float worldX, float worldZ) const override;
        bool  IsLoadedAt(float worldX, float worldZ) const override;

    private:
        const engine::render::terrain::TerrainRenderer* m_renderer = nullptr;
    };
}
