#pragma once

#include "engine/render/terrain/TerrainHoleMask.h"

#include <string>

namespace engine::render::terrain
{
    /// Fichier masque herbe / détail surface (ticket 010), **même résolution et UV** que la splat map.
    /// Binaire little-endian :
    ///   magic  : uint32 = kGrassMaskFileMagic ('GRMS')
    ///   width  : uint32 (doit égaler la largeur splat CPU)
    ///   height : uint32
    ///   data   : uint8[width * height] — R8, 0 = pas d’effet, 255 = effet maximal (teinte herbe).
    inline constexpr uint32_t kGrassMaskFileMagic = 0x47524D53u;

    class TerrainGrassDetail
    {
    public:
        TerrainGrassDetail() = delete;

        static bool LoadFromFile(const std::string& fullPath, HoleMaskData& outData);
        static void GenerateZeros(uint32_t width, uint32_t height, HoleMaskData& outData);
        static bool SaveToFile(const std::string& fullPath, const HoleMaskData& data);
    };

} // namespace engine::render::terrain
