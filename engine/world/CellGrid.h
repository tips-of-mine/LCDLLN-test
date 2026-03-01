#pragma once

/**
 * @file CellGrid.h
 * @brief Spatial partition: 64m cells per zone, mapping cell -> entityIds (M13.2).
 */

#include "engine/world/World.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::world {

/** @brief Cell size in meters (M13.2). */
constexpr int32_t kCellSize = 64;

/** @brief Cells per zone axis (ZoneSize / CellSize). */
constexpr int32_t kCellsPerZone = kZoneSize / kCellSize;

/** @brief Cell coordinate within zone (local cell indices [0, kCellsPerZone)). */
struct CellCoord {
    int32_t zoneX = 0;
    int32_t zoneZ = 0;
    int32_t cellX = 0;
    int32_t cellZ = 0;

    bool operator==(const CellCoord& o) const noexcept {
        return zoneX == o.zoneX && zoneZ == o.zoneZ && cellX == o.cellX && cellZ == o.cellZ;
    }
};

} // namespace engine::world

namespace std {
template<>
struct hash<engine::world::CellCoord> {
    size_t operator()(const engine::world::CellCoord& c) const noexcept {
        return static_cast<size_t>(c.zoneX) * 13131u + static_cast<size_t>(c.zoneZ) * 7171u
             + static_cast<size_t>(c.cellX) * 64u + static_cast<size_t>(c.cellZ);
    }
};
} // namespace std

namespace engine::world {

/**
 * @brief Converts world position (meters) to cell coordinates.
 *
 * @param worldX World X in meters.
 * @param worldZ World Z in meters.
 * @return       CellCoord for the cell containing (worldX, worldZ).
 */
CellCoord WorldToCellCoord(float worldX, float worldZ);

/**
 * @brief Fills the list of cell coordinates in a square radius around center (e.g. radius 3 = 7x7).
 *
 * @param center Center cell.
 * @param radius Half-side in cells (e.g. 3 for 7x7).
 * @param out    Output list (appended, not cleared).
 */
void GetCellsInRadius(const CellCoord& center, int32_t radius, std::vector<CellCoord>& out);

/**
 * @brief Cell grid for one zone: maintains cell -> entity ids and entity -> cell.
 */
class CellGrid {
public:
    /**
     * @brief Constructs a grid for the given zone.
     */
    CellGrid(int32_t zoneX = 0, int32_t zoneZ = 0);

    /** @brief Inserts an entity at the given world position (must be in this zone). */
    void Insert(uint32_t entityId, float worldX, float worldZ);

    /** @brief Removes an entity from the grid. */
    void Remove(uint32_t entityId);

    /** @brief Updates entity position; no-op if not present. */
    void UpdatePosition(uint32_t entityId, float worldX, float worldZ);

    /** @brief Returns entity ids in the given cell (zone-local cellX, cellZ in [0, kCellsPerZone)). */
    std::vector<uint32_t> GetEntityIdsInCell(int32_t cellX, int32_t cellZ) const;

private:
    int32_t m_zoneX = 0;
    int32_t m_zoneZ = 0;
    std::unordered_map<uint32_t, CellCoord> m_entityToCell;
    std::unordered_map<CellCoord, std::vector<uint32_t>> m_cellToEntities;

    void AddToCell(const CellCoord& c, uint32_t entityId);
    void RemoveFromCell(const CellCoord& c, uint32_t entityId);
};

} // namespace engine::world
