#include "src/client/world/cells/CellWorldFrame.h"

#include <algorithm>
#include <cmath>

namespace engine::world::cells
{
	void RenderFrame::SetOriginCell(CellCoord cell)
	{
		m_originCell = cell;
		const CellPoint c = CellCenter(cell);
		m_originX = c.x;
		m_originZ = c.z;
	}

	CellCoord RenderFrame::CellOfRender(double renderX, double renderZ) const
	{
		return CellFromPosition(renderX + m_originX, renderZ + m_originZ);
	}

	CellPoint RenderFrame::CellCenterRender(CellCoord cell) const
	{
		const CellPoint c = CellCenter(cell);
		return { c.x - m_originX, c.z - m_originZ };
	}

	CellPoint RenderFrame::RebaseDelta(CellCoord newOrigin) const
	{
		const CellPoint c = CellCenter(newOrigin);
		return { m_originX - c.x, m_originZ - c.z };
	}

	void GeneratedCellSet::Add(CellCoord cell)
	{
		if (!IsInLoreGrid(cell)) return;
		if (m_ids.insert(CellZoneId(cell)).second) m_cells.push_back(cell);
	}

	bool GeneratedCellSet::Contains(CellCoord cell) const
	{
		// Hors carte du lore : jamais générée (et CellZoneId exige |n|,|e| < 500).
		if (!IsInLoreGrid(cell)) return false;
		return m_ids.count(CellZoneId(cell)) != 0;
	}

	bool GeneratedCellSet::ContainsGlobal(double globalX, double globalZ) const
	{
		if (!std::isfinite(globalX) || !std::isfinite(globalZ)) return false;
		if (std::fabs(globalX) > 1.0e7 || std::fabs(globalZ) > 1.0e7) return false;
		return Contains(CellFromPosition(globalX, globalZ));
	}

	void SelectCellsInRange(double globalX, double globalZ, double radiusMeters,
		const GeneratedCellSet& generated, std::vector<CellCoord>& out)
	{
		out.clear();
		struct Candidate { CellCoord cell; double distance; };
		std::vector<Candidate> found;
		for (const CellCoord& cell : generated.Cells())
		{
			const double d = DistanceToCell(cell, globalX, globalZ);
			if (d <= radiusMeters) found.push_back({ cell, d });
		}
		// Tri stable par distance puis par coordonnées : ordre déterministe.
		std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
			if (a.distance != b.distance) return a.distance < b.distance;
			if (a.cell.n != b.cell.n) return a.cell.n < b.cell.n;
			return a.cell.e < b.cell.e;
		});
		for (const Candidate& c : found) out.push_back(c.cell);
	}

	bool IsMoveAllowed(const GeneratedCellSet& generated, double fromX, double fromZ, double toX, double toZ)
	{
		if (generated.Empty()) return true;
		if (!generated.ContainsGlobal(fromX, fromZ)) return true;
		return generated.ContainsGlobal(toX, toZ);
	}
}
