/**
 * @file CellGrid.cpp
 * @brief Cell grid and pos->cell coord (M13.2).
 */

#include "engine/world/CellGrid.h"

#include <cmath>
#include <algorithm>

namespace engine::world {

CellCoord WorldToCellCoord(float worldX, float worldZ) {
    const float zoneXf = std::floor(worldX / static_cast<float>(kZoneSize));
    const float zoneZf = std::floor(worldZ / static_cast<float>(kZoneSize));
    const int32_t zoneX = static_cast<int32_t>(zoneXf);
    const int32_t zoneZ = static_cast<int32_t>(zoneZf);
    const float localX = worldX - zoneX * kZoneSize;
    const float localZ = worldZ - zoneZ * kZoneSize;
    int32_t cellX = static_cast<int32_t>(std::floor(localX / static_cast<float>(kCellSize)));
    int32_t cellZ = static_cast<int32_t>(std::floor(localZ / static_cast<float>(kCellSize)));
    if (cellX >= kCellsPerZone) cellX = kCellsPerZone - 1;
    if (cellZ >= kCellsPerZone) cellZ = kCellsPerZone - 1;
    if (cellX < 0) cellX = 0;
    if (cellZ < 0) cellZ = 0;
    CellCoord c;
    c.zoneX = zoneX;
    c.zoneZ = zoneZ;
    c.cellX = cellX;
    c.cellZ = cellZ;
    return c;
}

void GetCellsInRadius(const CellCoord& center, int32_t radius, std::vector<CellCoord>& out) {
    for (int32_t dz = -radius; dz <= radius; ++dz) {
        for (int32_t dx = -radius; dx <= radius; ++dx) {
            int32_t cx = center.cellX + dx;
            int32_t cz = center.cellZ + dz;
            if (cx >= 0 && cx < kCellsPerZone && cz >= 0 && cz < kCellsPerZone) {
                CellCoord c;
                c.zoneX = center.zoneX;
                c.zoneZ = center.zoneZ;
                c.cellX = cx;
                c.cellZ = cz;
                out.push_back(c);
            }
        }
    }
}

CellGrid::CellGrid(int32_t zoneX, int32_t zoneZ) : m_zoneX(zoneX), m_zoneZ(zoneZ) {}

void CellGrid::AddToCell(const CellCoord& c, uint32_t entityId) {
    m_cellToEntities[c].push_back(entityId);
}

void CellGrid::RemoveFromCell(const CellCoord& c, uint32_t entityId) {
    auto it = m_cellToEntities.find(c);
    if (it == m_cellToEntities.end()) return;
    auto& vec = it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), entityId), vec.end());
    if (vec.empty()) m_cellToEntities.erase(it);
}

void CellGrid::Insert(uint32_t entityId, float worldX, float worldZ) {
    CellCoord c = WorldToCellCoord(worldX, worldZ);
    if (c.zoneX != m_zoneX || c.zoneZ != m_zoneZ) return;
    Remove(entityId);
    m_entityToCell[entityId] = c;
    AddToCell(c, entityId);
}

void CellGrid::Remove(uint32_t entityId) {
    auto it = m_entityToCell.find(entityId);
    if (it == m_entityToCell.end()) return;
    RemoveFromCell(it->second, entityId);
    m_entityToCell.erase(it);
}

void CellGrid::UpdatePosition(uint32_t entityId, float worldX, float worldZ) {
    auto it = m_entityToCell.find(entityId);
    if (it == m_entityToCell.end()) return;
    CellCoord c = WorldToCellCoord(worldX, worldZ);
    if (c.zoneX != m_zoneX || c.zoneZ != m_zoneZ) return;
    if (it->second.cellX == c.cellX && it->second.cellZ == c.cellZ) return;
    RemoveFromCell(it->second, entityId);
    it->second = c;
    AddToCell(c, entityId);
}

std::vector<uint32_t> CellGrid::GetEntityIdsInCell(int32_t cellX, int32_t cellZ) const {
    CellCoord c;
    c.zoneX = m_zoneX;
    c.zoneZ = m_zoneZ;
    c.cellX = cellX;
    c.cellZ = cellZ;
    auto it = m_cellToEntities.find(c);
    if (it == m_cellToEntities.end()) return {};
    return it->second;
}

} // namespace engine::world
