#include "engine/render/terrain/TerrainGrassDetail.h"

#include "engine/core/Log.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace engine::render::terrain
{
    bool TerrainGrassDetail::LoadFromFile(const std::string& fullPath, HoleMaskData& outData)
    {
        outData = {};

        std::ifstream in(fullPath, std::ios::binary);
        if (!in)
        {
            LOG_WARN("TerrainGrassDetail::LoadFromFile: failed to open '{}'", fullPath);
            return false;
        }

        uint32_t magic = 0;
        uint32_t w = 0;
        uint32_t h = 0;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&w), sizeof(w));
        in.read(reinterpret_cast<char*>(&h), sizeof(h));
        if (!in || magic != kGrassMaskFileMagic || w == 0 || h == 0)
        {
            LOG_WARN("TerrainGrassDetail::LoadFromFile: invalid header in '{}'", fullPath);
            return false;
        }

        const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
        std::vector<uint8_t> mask(count);
        in.read(reinterpret_cast<char*>(mask.data()), static_cast<std::streamsize>(count));
        if (!in || in.gcount() != static_cast<std::streamsize>(count))
        {
            LOG_WARN("TerrainGrassDetail::LoadFromFile: truncated data in '{}'", fullPath);
            return false;
        }

        outData.width = w;
        outData.height = h;
        outData.mask = std::move(mask);
        return true;
    }

    void TerrainGrassDetail::GenerateZeros(uint32_t width, uint32_t height, HoleMaskData& outData)
    {
        outData = {};
        if (width == 0 || height == 0)
            return;
        outData.width = width;
        outData.height = height;
        outData.mask.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0u);
    }

    bool TerrainGrassDetail::SaveToFile(const std::string& fullPath, const HoleMaskData& data)
    {
        if (data.width == 0 || data.height == 0 || data.mask.size() != static_cast<size_t>(data.width) * static_cast<size_t>(data.height))
        {
            LOG_WARN("TerrainGrassDetail::SaveToFile: invalid mask dimensions for '{}'", fullPath);
            return false;
        }

        std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            LOG_WARN("TerrainGrassDetail::SaveToFile: failed to open '{}'", fullPath);
            return false;
        }

        const uint32_t magic = kGrassMaskFileMagic;
        const uint32_t w = data.width;
        const uint32_t h = data.height;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&w), sizeof(w));
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(reinterpret_cast<const char*>(data.mask.data()), static_cast<std::streamsize>(data.mask.size()));
        return static_cast<bool>(out);
    }

} // namespace engine::render::terrain
