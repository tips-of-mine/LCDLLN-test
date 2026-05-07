// engine/world/surface/SurfaceType.cpp
#include "engine/world/surface/SurfaceType.h"

#include <array>

namespace engine::world::surface
{
    namespace
    {
        constexpr std::array<std::string_view, static_cast<size_t>(SurfaceType::_Count)> kNames = {
            "Dirt", "Grass", "Mud", "Sand", "Rock", "Snow",
            "ShallowWater", "DeepWater", "LavaCooled",
            "WheatField", "CornField", "Road", "Bridge"
        };
    }

    std::string_view ToString(SurfaceType t) noexcept
    {
        const auto idx = static_cast<size_t>(t);
        if (idx >= kNames.size()) return "_Invalid";
        return kNames[idx];
    }

    bool ParseSurfaceType(std::string_view s, SurfaceType& out) noexcept
    {
        for (size_t i = 0; i < kNames.size(); ++i)
        {
            if (kNames[i] == s)
            {
                out = static_cast<SurfaceType>(i);
                return true;
            }
        }
        return false;
    }
}
