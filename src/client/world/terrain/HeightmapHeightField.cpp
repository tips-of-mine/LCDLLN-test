#include "src/client/world/terrain/HeightmapHeightField.h"
#include "src/client/render/terrain/TerrainRenderer.h"

namespace engine::world::terrain
{
    HeightmapHeightField::HeightmapHeightField(
        const engine::render::terrain::TerrainRenderer* renderer)
        : m_renderer(renderer) {}

    float HeightmapHeightField::HeightAt(float worldX, float worldZ) const
    {
        // SampleHeightAtWorldXZ retombe déjà sur 0 si la heightmap n'est pas
        // chargée (pas de NaN). On garde quand même la garde nullptr explicite.
        if (m_renderer == nullptr) return 0.0f;
        return m_renderer->SampleHeightAtWorldXZ(worldX, worldZ);
    }

    bool HeightmapHeightField::IsLoadedAt(float /*worldX*/, float /*worldZ*/) const
    {
        // La heightmap legacy couvre toute l'étendue terrain dès qu'elle est
        // valide : pas de granularité par position (contrairement aux chunks).
        return m_renderer != nullptr && m_renderer->IsValid();
    }
}
