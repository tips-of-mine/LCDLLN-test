#pragma once

/**
 * @file InterestSet.h
 * @brief Client interest set: cells in radius (7x7), diff entering/leaving (M13.2).
 */

#include "engine/world/CellGrid.h"

#include <vector>

namespace engine::world {

/** @brief Default interest radius in cells (7x7 = radius 3). */
constexpr int32_t kDefaultInterestRadiusCells = 3;

/**
 * @brief Per-client interest set: set of cells around client position, with diff of cells entering/leaving.
 *
 * Call Update(worldX, worldZ) each frame (or when player moves); then read currentCells, cellsEntered, cellsLeft.
 */
class ClientInterestSet {
public:
    ClientInterestSet() = default;

    /**
     * @brief Updates the interest set from client world position.
     *
     * Computes the new set of cells in radius (default 3 = 7x7), diffs against previous set,
     * and fills currentCells, cellsEntered, cellsLeft.
     *
     * @param worldX     Client world X in meters.
     * @param worldZ     Client world Z in meters.
     * @param radiusCells Half-side in cells (default 3 for 7x7).
     */
    void Update(float worldX, float worldZ, int32_t radiusCells = kDefaultInterestRadiusCells);

    /** @brief Current set of cells in interest (7x7 around center). */
    const std::vector<CellCoord>& CurrentCells() const noexcept { return m_currentCells; }

    /** @brief Cells that entered the interest set this update. */
    const std::vector<CellCoord>& CellsEntered() const noexcept { return m_cellsEntered; }

    /** @brief Cells that left the interest set this update. */
    const std::vector<CellCoord>& CellsLeft() const noexcept { return m_cellsLeft; }

private:
    std::vector<CellCoord> m_currentCells;
    std::vector<CellCoord> m_previousCells;
    std::vector<CellCoord> m_cellsEntered;
    std::vector<CellCoord> m_cellsLeft;

    static bool CellInList(const std::vector<CellCoord>& list, const CellCoord& c);
};

} // namespace engine::world
