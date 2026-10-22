#include "src/client/world/cells/CellHeightFields.h"

#include <cmath>
#include <utility>

namespace engine::world::cells
{
	void CellHeightFields::Put(CellHeightTile tile)
	{
		const uint32_t key = CellZoneId(tile.cell);
		m_tiles[key] = std::move(tile);
	}

	void CellHeightFields::Remove(CellCoord cell)
	{
		if (!IsInLoreGrid(cell)) return;
		m_tiles.erase(CellZoneId(cell));
	}

	bool CellHeightFields::Has(CellCoord cell) const
	{
		return Find(cell) != nullptr;
	}

	const CellHeightTile* CellHeightFields::Find(CellCoord cell) const
	{
		if (!IsInLoreGrid(cell)) return nullptr;
		const auto it = m_tiles.find(CellZoneId(cell));
		return it != m_tiles.end() ? &it->second : nullptr;
	}

	bool CellHeightFields::SampleGlobal(double globalX, double globalZ, float& outHeight) const
	{
		if (!std::isfinite(globalX) || !std::isfinite(globalZ)) return false;
		if (std::fabs(globalX) > 1.0e7 || std::fabs(globalZ) > 1.0e7) return false;
		const CellHeightTile* tile = Find(CellFromPosition(globalX, globalZ));
		if (tile == nullptr || tile->worldSize <= 0.0) return false;
		// UV calculés en double (position globale jusqu'à ±200 km), puis même
		// filtrage bilinéaire que TerrainRenderer::SampleHeightAtWorldXZ :
		// « le joueur marche sur ce qu'il voit ».
		const float u = static_cast<float>((globalX - tile->minX) / tile->worldSize);
		const float v = static_cast<float>((globalZ - tile->minZ) / tile->worldSize);
		outHeight = tile->data.SampleBilinearNorm(u, v) * static_cast<float>(tile->heightScale);
		return true;
	}

	float CellHeightFields::HeightAt(float renderX, float renderZ) const
	{
		if (m_frame == nullptr) return 0.0f;
		float h = 0.0f;
		return SampleGlobal(m_frame->ToGlobalX(renderX), m_frame->ToGlobalZ(renderZ), h) ? h : 0.0f;
	}

	bool CellHeightFields::IsLoadedAt(float renderX, float renderZ) const
	{
		if (m_frame == nullptr) return false;
		const double gx = m_frame->ToGlobalX(renderX);
		const double gz = m_frame->ToGlobalZ(renderZ);
		if (!std::isfinite(gx) || !std::isfinite(gz)) return false;
		if (std::fabs(gx) > 1.0e7 || std::fabs(gz) > 1.0e7) return false;
		return Find(CellFromPosition(gx, gz)) != nullptr;
	}

	void CellHeightFields::ListCells(std::vector<CellCoord>& out) const
	{
		out.clear();
		for (const auto& [key, tile] : m_tiles)
		{
			(void)key;
			out.push_back(tile.cell);
		}
	}
}
