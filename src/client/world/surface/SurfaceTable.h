// src/client/world/surface/SurfaceTable.h
#pragma once

#include "src/client/world/surface/SurfaceType.h"

#include <array>
#include <filesystem>
#include <string>

namespace engine::world::surface
{
    struct SurfaceTableEntry
    {
        SurfaceType type = SurfaceType::Dirt;
        float baseSpeed = 1.0f;     // multiplier vs Dirt baseline 1.0
        std::string audioStep;
        std::string visualTag;
    };

    /// Charge une fois `assets/gameplay/surface_table.json` au boot.
    /// Format : { "version":1, "surfaces":[ {type, baseSpeed, audioStep, visualTag}, ... 13 ] }.
    /// Validation : exactement 13 entrées, types uniques, baseSpeed >= 0.
    class SurfaceTable
    {
    public:
        bool LoadFromJson(const std::filesystem::path& path, std::string& outError);

        /// Précondition : t < SurfaceType::_Count. Aucun bounds check release.
        const SurfaceTableEntry& Get(SurfaceType t) const noexcept;

        bool IsLoaded() const noexcept { return m_loaded; }

    private:
        std::array<SurfaceTableEntry, static_cast<size_t>(SurfaceType::_Count)> m_entries{};
        bool m_loaded = false;
    };
}
