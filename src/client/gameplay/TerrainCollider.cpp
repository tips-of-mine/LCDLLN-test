#include "src/client/gameplay/TerrainCollider.h"
#include "src/client/render/terrain/TerrainRenderer.h"
#include "src/client/world/water/WaterSurfaces.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

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
/// Strategie : on echantillonne le sol a l'XZ d'arrivee, et si la base
/// de la capsule (centre - height/2) passe d'au-dessus a en-dessous du
/// sol pendant le sweep, on calcule la fraction du sweep ou la traversee
/// a lieu (interpolation lineaire en supposant le sol plat entre start
/// et end). Approximation valable pour pentes faibles et sweeps courts,
/// ce qui est le cas typique du CharacterController (dt <= 16 ms,
/// deplacement < 1 m).
///
/// La capsule est traitee comme une "boite verticale" : on suit la
/// position de sa base (centerY - halfHeight) au lieu de son centre.
/// Equivalent : on detecte la traversee quand centerY descend sous
/// (groundHeight + halfHeight). Le CharacterController positionne le
/// centre a sol + halfHeight au repos ; le centre = halfHeight de marge
/// pour la collision descendante.
///
/// outHit.normal est mis a (0,1,0) par defaut (sol horizontal). On
/// pourrait derive la normale a partir du gradient du heightmap dans une
/// version future, mais pour B.1 c'est suffisant.
bool TerrainCollider::SweepCapsule(const Capsule& capsule,
                                   const engine::math::Vec3& startCenter,
                                   const engine::math::Vec3& endCenter,
                                   SweepHit& outHit) const
{
    // Valeurs par defaut "pas de hit".
    outHit.hit      = false;
    outHit.fraction = 1.0f;
    outHit.normal   = engine::math::Vec3{0.0f, 1.0f, 0.0f};

    // Seuil de centre auquel la base de la capsule touche le sol.
    // halfHeight = 0 pour une capsule par defaut (back-compat tests qui
    // utilisent Capsule{} et comparent le centre au sol nu).
    const float halfHeight = capsule.height * 0.5f;

    const float startY       = startCenter.y;
    const float endY         = endCenter.y;
    const float endGround    = GroundHeightAt(endCenter.x, endCenter.z);
    const float endThreshold = endGround + halfHeight;

    // Le sweep traverse-t-il le seuil "base touche le sol" ? On considere
    // uniquement le cas "descend a travers le sol" (gravity-driven). Les
    // remontees (start sous le seuil) ne sont PAS traitees ici : si le
    // centre commence deja sous le seuil, on suppose que le
    // CharacterController a deja corrige sa position au tick precedent
    // (cas de bord rare).
    if (endY < endThreshold && startY >= endThreshold)
    {
        const float deltaY = endY - startY;  // < 0 (descente)
        if (std::fabs(deltaY) > 1e-6f)
        {
            // Fraction lineaire du sweep ou la base touche le sol :
            //   center(t) = start + t * (end - start) avec t in [0..1]
            //   center(t).y == endThreshold  =>  t = (endThreshold - startY) / deltaY
            const float t = (endThreshold - startY) / deltaY;
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

/// Nage : detecte l'immersion a `worldCenter` (centre capsule ~ hauteur du
/// bassin). Parcourt les lacs de la scene d'eau et fait un point-in-polygon
/// (ray-casting) dans le plan XZ ; retient la surface la plus haute couvrant
/// (x,z). `inWater` ssi la surface depasse le centre (eau au-dessus du bassin).
bool TerrainCollider::QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const
{
    out = WaterQuery{};
    if (m_water == nullptr)
        return false;

    const float px = worldCenter.x;
    const float pz = worldCenter.z;
    bool found = false;
    float bestSurfaceY = 0.0f;

    for (const auto& lake : m_water->lakes)
    {
        const std::vector<engine::math::Vec3>& poly = lake.polygon;
        const std::size_t n = poly.size();
        if (n < 3)
            continue;
        bool inside = false;
        for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        {
            const float zi = poly[i].z, zj = poly[j].z;
            if ((zi > pz) != (zj > pz))
            {
                const float xCross = (poly[j].x - poly[i].x) * (pz - zi) / (zj - zi) + poly[i].x;
                if (px < xCross)
                    inside = !inside;
            }
        }
        if (inside && (!found || lake.waterLevelY > bestSurfaceY))
        {
            found = true;
            bestSurfaceY = lake.waterLevelY;
        }
    }

    if (!found)
        return false;

    out.surfaceY = bestSurfaceY;
    out.depth    = bestSurfaceY - worldCenter.y;  // > 0 si centre (bassin) immerge
    out.inWater  = out.depth > 0.0f;
    return out.inWater;
}

}  // namespace engine::gameplay
