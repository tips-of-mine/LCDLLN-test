#pragma once

/**
 * @file Lod.h
 * @brief LOD distance config and selection by distance; mesh LOD chain (M09.3).
 *
 * Base distances: LOD0 0-25m, LOD1 25-60m, LOD2 60-150m, LOD3 150-400m.
 * Config keys: world.lod_max_0 .. world.lod_max_3 (meters).
 */

#include <cstdint>

namespace engine::world {

/** @brief Number of LOD levels (0..3). */
constexpr unsigned kLodLevelCount = 4u;

/**
 * @brief Returns the LOD level (0..3) for a given distance from camera.
 *
 * Uses config world.lod_max_0 .. world.lod_max_3; defaults 25, 60, 150, 400 m.
 *
 * @param distance Distance from observer in meters.
 * @return LOD index 0 (closest) to 3 (farthest).
 */
unsigned SelectLod(float distance);

/**
 * @brief LOD chain for a mesh: one geometry index per LOD level (M09.3).
 *
 * Indices refer to geometry/mesh handles in the renderer.
 */
struct MeshLodChain {
    uint32_t geometryIndex[4] = { 0, 0, 0, 0 };
};

} // namespace engine::world
