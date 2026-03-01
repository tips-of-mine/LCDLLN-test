/**
 * @file InterestSet.cpp
 * @brief Client interest set: cells in radius, diff entering/leaving (M13.2).
 */

#include "engine/world/InterestSet.h"

#include <algorithm>

namespace engine::world {

bool ClientInterestSet::CellInList(const std::vector<CellCoord>& list, const CellCoord& c) {
    return std::find(list.begin(), list.end(), c) != list.end();
}

void ClientInterestSet::Update(float worldX, float worldZ, int32_t radiusCells) {
    m_previousCells = m_currentCells;
    m_currentCells.clear();
    m_cellsEntered.clear();
    m_cellsLeft.clear();
    CellCoord center = WorldToCellCoord(worldX, worldZ);
    GetCellsInRadius(center, radiusCells, m_currentCells);
    for (const CellCoord& c : m_currentCells) {
        if (!CellInList(m_previousCells, c))
            m_cellsEntered.push_back(c);
    }
    for (const CellCoord& c : m_previousCells) {
        if (!CellInList(m_currentCells, c))
            m_cellsLeft.push_back(c);
    }
}

} // namespace engine::world
