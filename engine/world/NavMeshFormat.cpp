/**
 * @file NavMeshFormat.cpp
 * @brief Reader for navmesh.bin and portals.bin (M11.3).
 */

#include "engine/world/NavMeshFormat.h"

#include <cstdio>
#include <cstring>

namespace engine::world {

bool ReadNavMeshBin(const std::string& path, NavMeshData& out) {
    out.vertices.clear();
    out.polygons.clear();
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0, numVerts = 0, numPolys = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || std::fread(&version, 1, 4, f) != 4 ||
        std::fread(&numVerts, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    if (magic != kNavMeshBinMagic || version != kNavMeshBinVersion) {
        std::fclose(f);
        return false;
    }
    out.vertices.resize(numVerts);
    if (numVerts > 0 &&
        std::fread(out.vertices.data(), sizeof(NavMeshVertex), numVerts, f) != numVerts) {
        std::fclose(f);
        return false;
    }
    if (std::fread(&numPolys, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    out.polygons.resize(numPolys);
    for (uint32_t p = 0; p < numPolys; ++p) {
        uint8_t nV = 0;
        if (std::fread(&nV, 1, 1, f) != 1 || nV > 12) {
            std::fclose(f);
            return false;
        }
        out.polygons[p].resize(nV);
        if (nV > 0 && std::fread(out.polygons[p].data(), 4, nV, f) != nV) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

bool ReadPortalsBin(const std::string& path, std::vector<NavMeshPortal>& out) {
    out.clear();
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0, numPortals = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || std::fread(&version, 1, 4, f) != 4 ||
        std::fread(&numPortals, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    if (magic != kPortalsBinMagic || version != kPortalsBinVersion) {
        std::fclose(f);
        return false;
    }
    out.resize(numPortals);
    for (uint32_t i = 0; i < numPortals; ++i) {
        if (std::fread(&out[i].ax, 4, 1, f) != 1 ||
            std::fread(&out[i].ay, 4, 1, f) != 1 ||
            std::fread(&out[i].az, 4, 1, f) != 1 ||
            std::fread(&out[i].bx, 4, 1, f) != 1 ||
            std::fread(&out[i].by, 4, 1, f) != 1 ||
            std::fread(&out[i].bz, 4, 1, f) != 1 ||
            std::fread(&out[i].neighborSide, 1, 1, f) != 1) {
            std::fclose(f);
            return false;
        }
    }
    std::fclose(f);
    return true;
}

} // namespace engine::world
