/**
 * @file navmesh_baker.cpp
 * @brief M11.3: Bake navmesh per chunk with Recast; extract portals (border edges); write navmesh.bin + portals.bin.
 *
 * Usage: navmesh_baker <chunkDir>
 *   chunkDir e.g. build/zone_0/chunks/chunk_0_0 — must contain chunk.meta (bounds); writes navmesh.bin and portals.bin there.
 * Or: navmesh_baker <minX> <minZ> <maxX> <maxZ> <outputDir>
 *   Bakes a single chunk with given bounds (meters) and writes navmesh.bin + portals.bin into outputDir.
 */

#include <Recast.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

/* Zone builder chunk.meta format (bounds + flags) for reading bounds when only chunkDir is given. */
static bool ReadChunkMetaBounds(const std::string& path, float& minX, float& minZ, float& maxX, float& maxZ) {
    constexpr uint32_t kChunkMetaMagic = 0x4D4E4843u;
#ifdef _WIN32
    std::FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
#else
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#endif
    uint32_t magic = 0, version = 0;
    float bmin[3], bmax[3];
    uint32_t flags = 0;
    if (std::fread(&magic, 1, 4, f) != 4 || std::fread(&version, 1, 4, f) != 4 ||
        std::fread(bmin, 4, 3, f) != 3 || std::fread(bmax, 4, 3, f) != 3 || std::fread(&flags, 1, 4, f) != 4) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    if (magic != kChunkMetaMagic || version != 1u) return false;
    minX = bmin[0]; minZ = bmin[2]; maxX = bmax[0]; maxZ = bmax[2];
    return true;
}

/* Binary format must match engine/world/NavMeshFormat.h */
namespace format {
constexpr uint32_t kNavMeshBinMagic = 0x4D56414Eu;
constexpr uint32_t kNavMeshBinVersion = 1u;
constexpr uint32_t kPortalsBinMagic = 0x4C545250u;
constexpr uint32_t kPortalsBinVersion = 1u;
enum class PortalNeighborSide : uint8_t { MinX = 0, MaxX = 1, MinZ = 2, MaxZ = 3 };
struct NavMeshVertex { float x = 0.f, y = 0.f, z = 0.f; };
struct Portal { float ax, ay, az, bx, by, bz; PortalNeighborSide side = PortalNeighborSide::MinX; };
} // namespace format

static bool WriteNavMeshBin(const std::string& path,
    const std::vector<format::NavMeshVertex>& verts,
    const std::vector<std::vector<uint32_t>>& polys) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "navmesh_baker: cannot write " << path << "\n"; return false; }
    uint32_t magic = format::kNavMeshBinMagic, version = format::kNavMeshBinVersion;
    uint32_t numVerts = static_cast<uint32_t>(verts.size());
    uint32_t numPolys = static_cast<uint32_t>(polys.size());
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&numVerts), 4);
    for (const auto& v : verts)
        out.write(reinterpret_cast<const char*>(&v), sizeof(format::NavMeshVertex));
    out.write(reinterpret_cast<const char*>(&numPolys), 4);
    for (const auto& p : polys) {
        uint8_t n = static_cast<uint8_t>(p.size());
        if (n > 12) n = 12;
        out.write(reinterpret_cast<const char*>(&n), 1);
        out.write(reinterpret_cast<const char*>(p.data()), n * 4u);
    }
    return true;
}

static bool WritePortalsBin(const std::string& path, const std::vector<format::Portal>& portals) {
    std::ofstream out(path, std::ios::binary);
    if (!out) { std::cerr << "navmesh_baker: cannot write " << path << "\n"; return false; }
    uint32_t magic = format::kPortalsBinMagic, version = format::kPortalsBinVersion;
    uint32_t n = static_cast<uint32_t>(portals.size());
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&n), 4);
    for (const auto& p : portals) {
        out.write(reinterpret_cast<const char*>(&p.ax), 4);
        out.write(reinterpret_cast<const char*>(&p.ay), 4);
        out.write(reinterpret_cast<const char*>(&p.az), 4);
        out.write(reinterpret_cast<const char*>(&p.bx), 4);
        out.write(reinterpret_cast<const char*>(&p.by), 4);
        out.write(reinterpret_cast<const char*>(&p.bz), 4);
        out.write(reinterpret_cast<const char*>(&p.side), 1);
    }
    return true;
}

/** @brief Convert Recast poly mesh to our format and collect boundary edges as portals. */
static void ConvertPolyMeshToOutput(const rcPolyMesh& mesh,
    float minX, float minZ, float maxX, float maxZ,
    std::vector<format::NavMeshVertex>& outVerts,
    std::vector<std::vector<uint32_t>>& outPolys,
    std::vector<format::Portal>& outPortals) {
    const float cs = mesh.cs;
    const float ch = mesh.ch;
    const int nvp = mesh.nvp;
    outVerts.resize(mesh.nverts);
    for (int i = 0; i < mesh.nverts; ++i) {
        outVerts[i].x = mesh.bmin[0] + mesh.verts[i * 3 + 0] * cs;
        outVerts[i].y = mesh.bmin[1] + mesh.verts[i * 3 + 1] * ch;
        outVerts[i].z = mesh.bmin[2] + mesh.verts[i * 3 + 2] * cs;
    }
    const unsigned short invalid = 0xffff;
    outPolys.clear();
    outPortals.clear();
    const float eps = 0.01f;
    for (int p = 0; p < mesh.npolys; ++p) {
        const unsigned short* poly = &mesh.polys[p * nvp * 2];
        std::vector<uint32_t> indices;
        for (int k = 0; k < nvp && poly[k] != invalid; ++k)
            indices.push_back(static_cast<uint32_t>(poly[k]));
        if (indices.size() < 3) continue;
        outPolys.push_back(std::move(indices));
        /* Portal edges: for each poly edge that lies on chunk boundary, add a portal. */
        std::vector<uint32_t>& ind = outPolys.back();
        for (size_t e = 0; e < ind.size(); ++e) {
            uint32_t i0 = ind[e];
            uint32_t i1 = ind[(e + 1) % ind.size()];
            float ax = outVerts[i0].x, ay = outVerts[i0].y, az = outVerts[i0].z;
            float bx = outVerts[i1].x, by = outVerts[i1].y, bz = outVerts[i1].z;
            format::PortalNeighborSide side = format::PortalNeighborSide::MinX;
            bool onBorder = false;
            if (std::fabs(ax - minX) < eps && std::fabs(bx - minX) < eps) { side = format::PortalNeighborSide::MinX; onBorder = true; }
            if (std::fabs(ax - maxX) < eps && std::fabs(bx - maxX) < eps) { side = format::PortalNeighborSide::MaxX; onBorder = true; }
            if (std::fabs(az - minZ) < eps && std::fabs(bz - minZ) < eps) { side = format::PortalNeighborSide::MinZ; onBorder = true; }
            if (std::fabs(az - maxZ) < eps && std::fabs(bz - maxZ) < eps) { side = format::PortalNeighborSide::MaxZ; onBorder = true; }
            if (onBorder) {
                format::Portal port;
                port.ax = ax; port.ay = ay; port.az = az;
                port.bx = bx; port.by = by; port.bz = bz;
                port.side = side;
                outPortals.push_back(port);
            }
        }
    }
}

/** @brief Run Recast pipeline for chunk bounds (world units) and fill outVerts, outPolys, outPortals. */
static bool BakeChunk(rcContext& ctx,
    float minX, float minZ, float maxX, float maxZ,
    std::vector<format::NavMeshVertex>& outVerts,
    std::vector<std::vector<uint32_t>>& outPolys,
    std::vector<format::Portal>& outPortals) {
    const float bmin[3] = { minX, 0.f, minZ };
    const float bmax[3] = { maxX, 1.f, maxZ };
    const float cellSize = 2.f;
    const float cellHeight = 0.5f;
    int sizeX = (int)((bmax[0] - bmin[0]) / cellSize);
    int sizeZ = (int)((bmax[2] - bmin[2]) / cellSize);
    if (sizeX < 1) sizeX = 1;
    if (sizeZ < 1) sizeZ = 1;

    float verts[12] = {
        minX, 0.f, minZ,
        maxX, 0.f, minZ,
        maxX, 0.f, maxZ,
        minX, 0.f, maxZ
    };
    int tris[6] = { 0, 1, 2, 0, 2, 3 };
    unsigned char areas[2] = { RC_WALKABLE_AREA, RC_WALKABLE_AREA };

    rcHeightfield* hf = rcAllocHeightfield();
    if (!hf) { std::cerr << "navmesh_baker: rcAllocHeightfield failed\n"; return false; }
    if (!rcCreateHeightfield(ctx, *hf, sizeX, sizeZ, bmin, bmax, cellSize, cellHeight)) {
        rcFreeHeightField(hf);
        return false;
    }
    if (!rcRasterizeTriangles(&ctx, verts, 4, tris, areas, 2, *hf)) {
        rcFreeHeightField(hf);
        return false;
    }

    rcCompactHeightfield* chf = rcAllocCompactHeightfield();
    if (!chf) { rcFreeHeightField(hf); return false; }
    if (!rcBuildCompactHeightfield(&ctx, 3, 1, *hf, *chf)) {
        rcFreeCompactHeightfield(chf);
        rcFreeHeightField(hf);
        return false;
    }
    rcFreeHeightField(hf);

    if (!rcErodeWalkableArea(&ctx, 0, *chf)) {
        rcFreeCompactHeightfield(chf);
        return false;
    }
    if (!rcBuildDistanceField(&ctx, *chf)) {
        rcFreeCompactHeightfield(chf);
        return false;
    }
    if (!rcBuildRegions(&ctx, *chf, 0, 8, 20)) {
        rcFreeCompactHeightfield(chf);
        return false;
    }
    rcContourSet* cset = rcAllocContourSet();
    if (!cset) { rcFreeCompactHeightfield(chf); return false; }
    if (!rcBuildContours(&ctx, *chf, 1.5f, 12, *cset, RC_CONTOUR_TESS_WALL_EDGES)) {
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        return false;
    }
    rcPolyMesh* mesh = rcAllocPolyMesh();
    if (!mesh) {
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        return false;
    }
    if (!rcBuildPolyMesh(&ctx, *cset, 6, *mesh)) {
        rcFreePolyMesh(mesh);
        rcFreeContourSet(cset);
        rcFreeCompactHeightfield(chf);
        return false;
    }
    rcFreeContourSet(cset);
    rcFreeCompactHeightfield(chf);

    ConvertPolyMeshToOutput(*mesh, minX, minZ, maxX, maxZ, outVerts, outPolys, outPortals);
    rcFreePolyMesh(mesh);
    return true;
}

static void PrintUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <chunkDir>\n";
    std::cerr << "   or: " << prog << " <minX> <minZ> <maxX> <maxZ> <outputDir>\n";
    std::cerr << "Writes navmesh.bin and portals.bin (M11.3).\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    float minX = 0.f, minZ = 0.f, maxX = 256.f, maxZ = 256.f;
    std::string outDir;
    if (argc == 2) {
        outDir = argv[1];
        std::string metaPath = outDir + (outDir.empty() || (outDir.back() != '/' && outDir.back() != '\\') ? "/" : "") + "chunk.meta";
        if (!ReadChunkMetaBounds(metaPath, minX, minZ, maxX, maxZ)) {
            minX = 0.f; minZ = 0.f; maxX = 256.f; maxZ = 256.f;
        }
    } else if (argc >= 6) {
        minX = (float)std::atof(argv[1]);
        minZ = (float)std::atof(argv[2]);
        maxX = (float)std::atof(argv[3]);
        maxZ = (float)std::atof(argv[4]);
        outDir = argv[5];
    } else {
        PrintUsage(argv[0]);
        return 1;
    }
    if (outDir.empty()) { std::cerr << "navmesh_baker: output path empty\n"; return 1; }
    std::string base = outDir;
    if (!base.empty() && base.back() != '/' && base.back() != '\\') base += "/";
    std::string navPath = base + "navmesh.bin";
    std::string prtPath = base + "portals.bin";

    rcContext ctx(false);
    std::vector<format::NavMeshVertex> verts;
    std::vector<std::vector<uint32_t>> polys;
    std::vector<format::Portal> portals;
    if (!BakeChunk(ctx, minX, minZ, maxX, maxZ, verts, polys, portals)) {
        std::cerr << "navmesh_baker: Recast bake failed\n";
        return 1;
    }
    if (!WriteNavMeshBin(navPath, verts, polys)) return 1;
    if (!WritePortalsBin(prtPath, portals)) return 1;
    std::cout << "navmesh_baker: wrote " << verts.size() << " verts, " << polys.size() << " polys, "
              << portals.size() << " portals -> " << navPath << ", " << prtPath << "\n";
    return 0;
}
