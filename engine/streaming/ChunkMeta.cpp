/**
 * @file ChunkMeta.cpp
 * @brief chunk.meta reader (M10.5).
 */

#include "engine/streaming/ChunkMeta.h"

#include <cstdio>
#include <cstring>

namespace engine::streaming {

const char* ChunkAssetFileName(ChunkAssetType t) noexcept {
    switch (t) {
        case ChunkAssetType::Geo:       return "geo.pak";
        case ChunkAssetType::Tex:       return "tex.pak";
        case ChunkAssetType::Instances: return "instances.bin";
        case ChunkAssetType::Nav:       return "navmesh.bin";
        case ChunkAssetType::Probes:    return "probes.bin";
    }
    return "geo.pak";
}

bool ChunkMeta::HasAsset(ChunkAssetType t) const noexcept {
    const size_t i = static_cast<size_t>(t);
    return i < kChunkMetaSlotCount && slots[i].present != 0;
}

uint64_t ChunkMeta::GetAssetSize(ChunkAssetType t) const noexcept {
    const size_t i = static_cast<size_t>(t);
    return i < kChunkMetaSlotCount ? slots[i].size : 0;
}

bool ReadChunkMeta(const std::string& path, ChunkMeta& out) {
    std::memset(&out, 0, sizeof(out));
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f)
        return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0;
    bool ok = (std::fread(&magic, 1, sizeof(magic), f) == sizeof(magic) &&
               std::fread(&version, 1, sizeof(version), f) == sizeof(version));
    if (!ok || magic != kChunkMetaMagic || version != kChunkMetaVersion) {
        std::fclose(f);
        return false;
    }
    for (size_t i = 0; i < kChunkMetaSlotCount && ok; ++i) {
        ok = (std::fread(&out.slots[i].present, 1, sizeof(out.slots[i].present), f) == sizeof(out.slots[i].present) &&
             (std::fread(out.slots[i].reserved, 1, sizeof(out.slots[i].reserved), f) == sizeof(out.slots[i].reserved)) &&
             (std::fread(&out.slots[i].size, 1, sizeof(out.slots[i].size), f) == sizeof(out.slots[i].size));
    }
    std::fclose(f);
    return ok;
}

} // namespace engine::streaming
