#include "src/client/gameplay/TerrainCollider.h"
#include "src/client/render/terrain/TerrainRenderer.h"

#include <algorithm>
#include <cmath>

namespace engine::gameplay
{

TerrainCollider::TerrainCollider() = default;
TerrainCollider::~TerrainCollider() = default;

/// Bind / debind le pointeur terrain (non-owning).
void TerrainCollider::BindTerrain(const engine::render::terrain::TerrainRenderer* terrainRenderer)
{
    m_terrain = terrainRenderer;
}

/// Echantillonne la hauteur du sol en metres a (worldX, worldZ).
/// Delegue a `TerrainRenderer::SampleHeightAtWorldXZ` qui gere :
///   - origine monde (config `terrain.origin_x/z`, defaut -512),
///   - taille monde (config `terrain.world_size`, defaut 1024),
///   - height_scale (config, defaut 200),
///   - interpolation bilineaire,
///   - edge-clamp hors-terrain.
/// Retourne 0.0 si aucun terrain n'est bind (placeholder flat).
float TerrainCollider::GroundHeightAt(float worldX, float worldZ) const
{
    if (m_terrain == nullptr) return 0.0f;
    return m_terrain->SampleHeightAtWorldXZ(worldX, worldZ);
}

/// Sweep vertical approxime contre le sol terrain (MVP B.1).
///
/// Strategie : on echantillonne le sol a l'XZ d'arrivee, et si le centre
/// de la capsule passe d'au-dessus a en-dessous du sol pendant le sweep,
/// on calcule la fraction du sweep ou la traversee a lieu (interpolation
/// lineaire en supposant le sol plat entre start et end). Approximation
/// valable pour pentes faibles et sweeps courts, ce qui est le cas
/// typique du CharacterController (dt <= 16 ms, deplacement < 1 m).
///
/// Le radius / height de la capsule sont ignores en B.1 : le centre de la
/// capsule est traite comme un point, donc l'altitude "au sol" est celle
/// du sol nu (pas de demi-hauteur). Le CharacterController est cense
/// positionner le centre de la capsule a sol + height/2 ; sa logique de
/// snap au sol fonctionne correctement avec cette convention.
///
/// outHit.normal est mis a (0,1,0) par defaut (sol horizontal). On
/// pourrait derive la normale a partir du gradient du heightmap dans une
/// version future, mais pour B.1 c'est suffisant.
bool TerrainCollider::SweepCapsule(const Capsule& /*capsule*/,
                                   const engine::math::Vec3& startCenter,
                                   const engine::math::Vec3& endCenter,
                                   SweepHit& outHit) const
{
    // Valeurs par defaut "pas de hit".
    outHit.hit      = false;
    outHit.fraction = 1.0f;
    outHit.normal   = engine::math::Vec3{0.0f, 1.0f, 0.0f};

    const float startY    = startCenter.y;
    const float endY      = endCenter.y;
    const float endGround = GroundHeightAt(endCenter.x, endCenter.z);

    // Le sweep traverse-t-il le sol ? On considere uniquement le cas
    // "descend a travers le sol" (gravity-driven). Les remontees (start
    // sous le sol) ne sont PAS traitees ici : si le centre commence deja
    // sous le sol, on suppose que le CharacterController a deja corrige sa
    // position au tick precedent (cas de bord rare).
    if (endY < endGround && startY >= endGround)
    {
        const float deltaY = endY - startY;  // < 0 (descente)
        if (std::fabs(deltaY) > 1e-6f)
        {
            // Fraction lineaire du sweep ou l'on touche le sol :
            //   center(t) = start + t * (end - start) avec t in [0..1]
            //   center(t).y == endGround  =>  t = (endGround - startY) / deltaY
            const float t = (endGround - startY) / deltaY;
            outHit.fraction = std::clamp(t, 0.0f, 1.0f);
        }
        else
        {
            // deltaY ~ 0 (sweep horizontal) mais on a traverse : touche immediate.
            outHit.fraction = 0.0f;
        }
        outHit.hit = true;
    }

    return outHit.hit;
}

}  // namespace engine::gameplay
