#pragma once

#include "src/client/gameplay/CharacterController.h"  // for IWorldCollider

namespace engine::render::terrain
{
    class TerrainRenderer;  // forward
}

namespace engine::world::water
{
    struct WaterScene;  // forward (nage : QueryWater)
}

namespace engine::gameplay
{

/// Implementation de IWorldCollider pour le terrain heightmap (B.1).
///
/// Bind sur un TerrainRenderer deja initialise ; les requetes de hauteur
/// passent par `TerrainRenderer::SampleHeightAtWorldXZ` (bilineaire, origin /
/// worldSize / heightScale gerees par le renderer). `SweepCapsule` realise
/// un sweep vertical approxime contre le sol : si la capsule traverse le
/// terrain entre `startCenter` et `endCenter`, on renvoie le hit avec la
/// fraction lineaire correspondante (approximation valable pour pentes
/// faibles et pas de simulation court).
///
/// Limitations B.1 :
/// - Pas de collision contre props / batiments (rien dans le monde encore).
/// - Pas de mur invisible : tout est franchissable sauf l'altitude du sol.
/// - QueryWater renvoie toujours `inWater = false` (impl par defaut heritee
///   de IWorldCollider) ; B.2 / B.3 ajoutera la query eau.
class TerrainCollider final : public IWorldCollider
{
public:
    TerrainCollider();
    ~TerrainCollider() override;

    /// Bind sur le terrain (non-owning). Doit etre appele avant toute
    /// requete (`SweepCapsule`, `GroundHeightAt`). Passer `nullptr` pour
    /// debinder (toutes les requetes renvoient alors un sol plat a Y=0).
    void BindTerrain(const engine::render::terrain::TerrainRenderer* terrainRenderer);

    /// IWorldCollider : sweep capsule du centre `startCenter` vers `endCenter`.
    /// Retourne true ssi le sweep traverse le sol pendant le mouvement ;
    /// `outHit.fraction` est calcule par interpolation lineaire en supposant
    /// le sol plat entre les deux extremites (valide pour pentes faibles).
    bool SweepCapsule(const Capsule& capsule,
                      const engine::math::Vec3& startCenter,
                      const engine::math::Vec3& endCenter,
                      SweepHit& outHit) const override;

    /// Helper public : altitude du sol en metres a la position monde
    /// horizontale (worldX, worldZ). Utile pour les tests, le snap au sol
    /// d'un avatar, et la resolution d'un spawn point. Renvoie 0.0 si aucun
    /// terrain n'est bind.
    float GroundHeightAt(float worldX, float worldZ) const;

    /// Bind la scene d'eau (non-owning) pour la nage. nullptr = pas d'eau.
    void BindWater(const engine::world::water::WaterScene* water) { m_water = water; }

    /// IWorldCollider : detecte l'immersion a `worldCenter` (centre capsule ~
    /// hauteur du bassin). Parcourt les lacs de la scene d'eau (point-in-polygon
    /// XZ) ; si le centre est sous la surface d'un lac -> `inWater=true`,
    /// `surfaceY`/`depth` renseignes. Sans scene -> `inWater=false`.
    bool QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const override;

private:
    /// Non-owning. Lifetime garanti par l'appelant (le terrain vit aussi
    /// longtemps que le collider qui s'y refere).
    const engine::render::terrain::TerrainRenderer* m_terrain = nullptr;
    /// Non-owning. Scene d'eau pour QueryWater (nage). nullptr = pas d'eau.
    const engine::world::water::WaterScene* m_water = nullptr;
};

}  // namespace engine::gameplay
