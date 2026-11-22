// Lot D (bug 3) — mer de repli des cellules non générées (cf. CellSeaFallback.h).
#include "src/client/world/cells/CellSeaFallback.h"

#include <algorithm>
#include <cmath>

namespace engine::world::cells
{
	int FallbackSeaRadiusCells(double viewDistanceM)
	{
		if (!std::isfinite(viewDistanceM) || viewDistanceM <= 0.0)
			return 1;
		const double cellsNeeded = std::ceil(viewDistanceM / kCellSizeMeters);
		const int r = 1 + static_cast<int>(std::min(cellsNeeded, static_cast<double>(kFallbackSeaMaxRadiusCells)));
		return std::clamp(r, 1, kFallbackSeaMaxRadiusCells);
	}

	void SelectFallbackSeaCells(CellCoord center, int radiusCells,
		const std::function<bool(CellCoord)>& isMissing, std::vector<CellCoord>& out)
	{
		out.clear();
		if (!isMissing)
			return;
		const int r = std::clamp(radiusCells, 0, kFallbackSeaMaxRadiusCells);
		for (int n = center.n + r; n >= center.n - r; --n)
			for (int e = center.e - r; e <= center.e + r; ++e)
			{
				const CellCoord c{ n, e };
				if (isMissing(c))
					out.push_back(c);
			}
	}

	engine::world::water::LakeInstance MakeFallbackSeaLake(CellCoord cell, float seaLevelY,
		double originX, double originZ)
	{
		engine::world::water::LakeInstance ocean;
		ocean.name = "ocean_repli_" + LoreCellId(cell);
		ocean.isOcean = true;
		ocean.waterLevelY = seaLevelY;
		// Mêmes valeurs que la mer d'une cellule générée (tools/world_gen/lib/Output.cpp).
		ocean.turbidity = 0.35f;
		ocean.bottomColor = { 0.06f, 0.18f, 0.27f };
		const CellPoint mn = CellMinCorner(cell);
		const float x0 = static_cast<float>(mn.x - kFallbackSeaMarginM - originX);
		const float z0 = static_cast<float>(mn.z - kFallbackSeaMarginM - originZ);
		const float x1 = static_cast<float>(mn.x + kCellSizeMeters + kFallbackSeaMarginM - originX);
		const float z1 = static_cast<float>(mn.z + kCellSizeMeters + kFallbackSeaMarginM - originZ);
		const float y = seaLevelY;
		ocean.polygon = { { x0, y, z0 }, { x1, y, z0 }, { x1, y, z1 }, { x0, y, z1 } };
		return ocean;
	}

	bool FindSeaLevel(const std::vector<engine::world::water::LakeInstance>& lakes, float& outSeaLevelY)
	{
		for (const engine::world::water::LakeInstance& lake : lakes)
			if (lake.isOcean && std::isfinite(lake.waterLevelY))
			{
				outSeaLevelY = lake.waterLevelY;
				return true;
			}
		return false;
	}

	bool IsMoveAllowedAvoiding(const std::function<bool(CellCoord)>& isMissing,
		double fromX, double fromZ, double toX, double toZ)
	{
		if (!isMissing)
			return true;
		if (isMissing(CellFromPosition(fromX, fromZ)))
			return true;
		return !isMissing(CellFromPosition(toX, toZ));
	}
}
