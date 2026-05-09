// src/client/world/surface/SurfaceType.h
#pragma once

#include <cstdint>
#include <string_view>

namespace engine::world::surface
{
    /// Surfaces reconnues par le pipeline gameplay (M100.11).
    /// Ordre figé. Tout futur ajout va AVANT `_Count`. Aucune renumérotation.
    enum class SurfaceType : uint16_t
    {
        Dirt = 0,
        Grass,
        Mud,
        Sand,
        Rock,
        Snow,
        ShallowWater,
        DeepWater,
        LavaCooled,
        WheatField,
        CornField,
        Road,
        Bridge,
        _Count
    };

    /// Renvoie le nom canonique ("Dirt", "Grass", ..., "Bridge").
    /// Pour `_Count` ou cast invalide : renvoie "_Invalid".
    std::string_view ToString(SurfaceType t) noexcept;

    /// Parse le nom canonique. True + sortie écrite si match exact.
    /// False sinon (out non touché).
    bool ParseSurfaceType(std::string_view s, SurfaceType& out) noexcept;
}
