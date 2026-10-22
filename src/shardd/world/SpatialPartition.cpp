#include "src/shardd/world/SpatialPartition.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Return true when the given interest set already contains the cell.
		bool ContainsCell(const std::vector<CellCoord>& cells, const CellCoord& target)
		{
			for (const CellCoord& cell : cells)
			{
				if (cell == target)
				{
					return true;
				}
			}
			return false;
		}

		/// Return true when the id already exists in the output entity list.
		bool ContainsEntityId(const std::vector<EntityId>& entityIds, EntityId entityId)
		{
			return std::find(entityIds.begin(), entityIds.end(), entityId) != entityIds.end();
		}
	}

	size_t CellCoordHash::operator()(const CellCoord& coord) const
	{
		const uint32_t ux = static_cast<uint16_t>(coord.x);
		const uint32_t uz = static_cast<uint16_t>(coord.z);
		return static_cast<size_t>((ux << 16) ^ uz);
	}

	CellGrid::~CellGrid()
	{
		Shutdown();
	}

	bool CellGrid::Init(float halfExtentMeters)
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[CellGrid] Init ignored: already initialized");
			return true;
		}

		// Étendue : multiple de la case, au moins une case, au plus int16 cases par axe.
		// Bornes vérifiées AVANT la conversion entière (NaN / valeur énorme → cast indéfini).
		constexpr float kMaxHalfExtentMeters = 16000.0f * static_cast<float>(kCellSizeMeters);
		if (!(halfExtentMeters > 0.0f) || halfExtentMeters > kMaxHalfExtentMeters)
		{
			LOG_ERROR(Net, "[CellGrid] Init FAILED: invalid half extent {:.1f} m", halfExtentMeters);
			return false;
		}
		const int halfCells = static_cast<int>(std::ceil(halfExtentMeters / static_cast<float>(kCellSizeMeters)));
		m_halfExtentMeters = static_cast<float>(halfCells * kCellSizeMeters);
		m_axisCount = halfCells * 2;
		m_cells.clear();
		m_entityCells.clear();
		m_initialized = true;
		LOG_INFO(Net, "[CellGrid] Init OK (extent_m={:.0f}, cell_m={}, axis_count={}, storage=sparse)",
			m_halfExtentMeters * 2.0f,
			kCellSizeMeters,
			m_axisCount);
		return true;
	}

	bool CellGrid::IsValidCell(const CellCoord& cell) const
	{
		return cell.x >= 0 && cell.z >= 0
			&& cell.x < m_axisCount
			&& cell.z < m_axisCount;
	}

	uint32_t CellGrid::CellKey(const CellCoord& cell) const
	{
		return static_cast<uint32_t>(cell.z) * static_cast<uint32_t>(m_axisCount)
			+ static_cast<uint32_t>(cell.x);
	}

	void CellGrid::Shutdown()
	{
		if (!m_initialized && m_cells.empty() && m_entityCells.empty())
		{
			return;
		}

		const size_t entityCount = m_entityCells.size();
		m_cells.clear();
		m_entityCells.clear();
		m_initialized = false;
		LOG_INFO(Net, "[CellGrid] Destroyed (tracked_entities={})", entityCount);
	}

	bool CellGrid::TryWorldToCellCoord(float posX, float posZ, CellCoord& outCell) const
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[CellGrid] TryWorldToCellCoord FAILED: grid not initialized");
			return false;
		}

		// Convention : la carte est centrée sur (0,0) (zone historique : cf. Engine.cpp
		// B.1 spawn ; monde « Royaumes » : centre de l'Oracle). La grille indexe ses
		// cases dans [0, m_axisCount) : on translate donc les coords par +demi-étendue.
		// Plage acceptée : [-demi-étendue, +demi-étendue). Au-delà : hors carte (rejet).
		const float halfZoneMeters = m_halfExtentMeters;
		const float extentMeters = m_halfExtentMeters * 2.0f;
		const float gridX = posX + halfZoneMeters;
		const float gridZ = posZ + halfZoneMeters;
		if (!(gridX >= 0.0f) || !(gridZ >= 0.0f) || gridX >= extentMeters || gridZ >= extentMeters)
		{
			LOG_WARN(Net, "[CellGrid] TryWorldToCellCoord FAILED: out of zone position ({:.2f}, {:.2f}) (plage acceptee [-{:.0f}, +{:.0f}))",
				posX, posZ, halfZoneMeters, halfZoneMeters);
			return false;
		}

		// Bornage défensif : l'arrondi flottant près du bord ne doit pas produire m_axisCount.
		outCell.x = static_cast<int16_t>(std::min(m_axisCount - 1,
			static_cast<int>(std::floor(gridX / static_cast<float>(kCellSizeMeters)))));
		outCell.z = static_cast<int16_t>(std::min(m_axisCount - 1,
			static_cast<int>(std::floor(gridZ / static_cast<float>(kCellSizeMeters)))));
		return true;
	}

	bool CellGrid::UpsertEntity(EntityId entityId, float posX, float posZ, CellCoord& outCell)
	{
		if (!TryWorldToCellCoord(posX, posZ, outCell))
		{
			LOG_WARN(Net, "[CellGrid] UpsertEntity FAILED (entity_id={}, pos=({:.2f}, {:.2f}))", entityId, posX, posZ);
			return false;
		}

		auto existingIt = m_entityCells.find(entityId);
		if (existingIt != m_entityCells.end())
		{
			if (existingIt->second == outCell)
			{
				return true;
			}

			const auto previousIt = m_cells.find(CellKey(existingIt->second));
			if (previousIt != m_cells.end())
			{
				std::vector<EntityId>& previousCellEntities = previousIt->second;
				previousCellEntities.erase(
					std::remove(previousCellEntities.begin(), previousCellEntities.end(), entityId),
					previousCellEntities.end());
				if (previousCellEntities.empty())
				{
					m_cells.erase(previousIt); // stockage creux : case vide libérée
				}
			}
		}

		m_entityCells[entityId] = outCell;
		std::vector<EntityId>& targetCellEntities = m_cells[CellKey(outCell)];
		if (!ContainsEntityId(targetCellEntities, entityId))
		{
			targetCellEntities.push_back(entityId);
		}

		LOG_DEBUG(Net, "[CellGrid] Entity mapped (entity_id={}, cell={}, {})", entityId, outCell.x, outCell.z);
		return true;
	}

	bool CellGrid::RemoveEntity(EntityId entityId)
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[CellGrid] RemoveEntity ignored: grid not initialized");
			return false;
		}

		const auto it = m_entityCells.find(entityId);
		if (it == m_entityCells.end())
		{
			LOG_WARN(Net, "[CellGrid] RemoveEntity ignored: unknown entity_id={}", entityId);
			return false;
		}

		const auto cellIt = m_cells.find(CellKey(it->second));
		if (cellIt != m_cells.end())
		{
			std::vector<EntityId>& cellEntities = cellIt->second;
			cellEntities.erase(
				std::remove(cellEntities.begin(), cellEntities.end(), entityId),
				cellEntities.end());
			if (cellEntities.empty())
			{
				m_cells.erase(cellIt); // stockage creux : case vide libérée
			}
		}
		m_entityCells.erase(it);
		LOG_INFO(Net, "[CellGrid] Entity removed (entity_id={})", entityId);
		return true;
	}

	void CellGrid::BuildInterestSet(const CellCoord& centerCell, std::vector<CellCoord>& outCells) const
	{
		outCells.clear();
		if (!m_initialized)
		{
			LOG_WARN(Net, "[CellGrid] BuildInterestSet ignored: grid not initialized");
			return;
		}

		if (!IsValidCell(centerCell))
		{
			LOG_WARN(Net, "[CellGrid] BuildInterestSet ignored: invalid center cell ({}, {})", centerCell.x, centerCell.z);
			return;
		}

		for (int dz = -kBaseInterestRadiusCells; dz <= kBaseInterestRadiusCells; ++dz)
		{
			for (int dx = -kBaseInterestRadiusCells; dx <= kBaseInterestRadiusCells; ++dx)
			{
				CellCoord cell{};
				cell.x = static_cast<int16_t>(centerCell.x + dx);
				cell.z = static_cast<int16_t>(centerCell.z + dz);
				if (IsValidCell(cell))
				{
					outCells.push_back(cell);
				}
			}
		}
	}

	void CellGrid::GatherEntityIds(const std::vector<CellCoord>& cells, std::vector<EntityId>& outEntityIds) const
	{
		outEntityIds.clear();
		if (!m_initialized)
		{
			LOG_WARN(Net, "[CellGrid] GatherEntityIds ignored: grid not initialized");
			return;
		}

		for (const CellCoord& cell : cells)
		{
			if (!IsValidCell(cell))
			{
				continue;
			}

			const auto cellIt = m_cells.find(CellKey(cell));
			if (cellIt == m_cells.end())
			{
				continue; // case vide (stockage creux)
			}
			for (EntityId entityId : cellIt->second)
			{
				if (!ContainsEntityId(outEntityIds, entityId))
				{
					outEntityIds.push_back(entityId);
				}
			}
		}
	}

	void ComputeInterestDiff(
		const std::vector<CellCoord>& previousCells,
		const std::vector<CellCoord>& currentCells,
		InterestDiff& outDiff)
	{
		outDiff.enteringCells.clear();
		outDiff.leavingCells.clear();

		for (const CellCoord& cell : currentCells)
		{
			if (!ContainsCell(previousCells, cell))
			{
				outDiff.enteringCells.push_back(cell);
			}
		}

		for (const CellCoord& cell : previousCells)
		{
			if (!ContainsCell(currentCells, cell))
			{
				outDiff.leavingCells.push_back(cell);
			}
		}
	}
}
