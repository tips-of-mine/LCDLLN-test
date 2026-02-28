#pragma once

/**
 * @file NavMeshFormat.h
 * @brief Binary format for baked navmesh (navmesh.bin) and chunk portals (portals.bin) (M11.3).
 *
 * Navmesh stitch runtime via portals; bake offline with Recast.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace engine::world {

/** @brief navmesh.bin file magic "NAVM". */
constexpr uint32_t kNavMeshBinMagic = 0x4D56414Eu;

/** @brief navmesh.bin format version. */
constexpr uint32_t kNavMeshBinVersion = 1u;

/** @brief portals.bin file magic "PRTL". */
constexpr uint32_t kPortalsBinMagic = 0x4C545250u;

/** @brief portals.bin format version. */
constexpr uint32_t kPortalsBinVersion = 1u;

/** @brief Neighbor side for a portal (chunk boundary). */
enum class PortalNeighborSide : uint8_t {
    MinX = 0,
    MaxX = 1,
    MinZ = 2,
    MaxZ = 3,
};

/** @brief Single vertex in navmesh (world position). */
struct NavMeshVertex {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief One portal edge (boundary between chunks). */
struct NavMeshPortal {
    float ax = 0.f, ay = 0.f, az = 0.f;
    float bx = 0.f, by = 0.f, bz = 0.f;
    PortalNeighborSide neighborSide = PortalNeighborSide::MinX;
};

/** @brief In-memory navmesh: vertices and polygons (each poly = list of vert indices). */
struct NavMeshData {
    std::vector<NavMeshVertex> vertices;
    std::vector<std::vector<uint32_t>> polygons;
};

/**
 * @brief Reads navmesh.bin from path. Returns true on success.
 */
bool ReadNavMeshBin(const std::string& path, NavMeshData& out);

/**
 * @brief Reads portals.bin from path. Returns true on success.
 */
bool ReadPortalsBin(const std::string& path, std::vector<NavMeshPortal>& out);

} // namespace engine::world
