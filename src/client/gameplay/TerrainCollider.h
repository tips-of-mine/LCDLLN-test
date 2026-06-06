#pragma once

#include "src/client/gameplay/CharacterController.h"  // for IWorldCollider

namespace engine::world::terrain
{
    class IHeightField;  // forward (Phase 2 : source de hauteur abstraite)
}

namespace engine::world::water
{
    struct WaterScene;  // forward (nage : QueryWater)
}

namespace engine::gameplay
{

/// Implementation de IWorldCollider pour le terrain heightmap (B.1).
///
/// Bind sur une ou deux sources `IHeightField` (Phase 2, chantier C) : un
/// champ chunke prioritaire (residents) et un champ heightmap de repli. Les
/// requetes de hauteur passent par `GroundHeightAt` qui aiguille chunk ->
/// heightmap -> sol plat. `SweepCapsule` realise un sweep vertical approxime
/// contre le sol echantillonne par `GroundHeightAt` : si la capsule traverse le
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

    /// Bind les sources de hauteur (non-owning). `chunkField` est prioritaire
    /// quand il est resident (zones chunkees embarquees) ; `heightmapField`
    /// sert de repli (heightmap legacy). L'un ou l'autre peut etre `nullptr`
    /// (collider non-bound -> sol plat a Y=0). Remplace
    /// `BindTerrain(TerrainRenderer*)` (Phase 2, chantier C).
    void BindHeightFields(const engine::world::terrain::IHeightField* chunkField,
                          const engine::world::terrain::IHeightField* heightmapField);

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
    /// Non-owning. Source de hauteur prioritaire (chunks residents, grille 256).
    /// Lifetime garanti par l'appelant. nullptr = pas de source chunkee.
    const engine::world::terrain::IHeightField* m_chunkField = nullptr;
    /// Non-owning. Source de hauteur de repli (heightmap legacy). Lifetime
    /// garanti par l'appelant. nullptr = pas de repli (sol plat a Y=0).
    const engine::world::terrain::IHeightField* m_heightmapField = nullptr;
    /// Non-owning. Scene d'eau pour QueryWater (nage). nullptr = pas d'eau.
    const engine::world::water::WaterScene* m_water = nullptr;
};

}  // namespace engine::gameplay
