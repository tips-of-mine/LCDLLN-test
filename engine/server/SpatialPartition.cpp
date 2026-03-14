#include "engine/server/SpatialPartition.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>

namespace engine::server
{
	namespace
	{
		/// Return true when the cell coordinate is inside the fixed 64x64 grid.
		bool IsValidCell(const CellCoord& cell)
		{
			return cell.x >= 0 && cell.z >= 0
				&& cell.x < kCellGridAxisCount
				&& cell.z < kCellGridAxisCount;
		}

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

	bool CellGrid::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[CellGrid] Init ignored: already initialized");
			return true;
		}

		m_cells.clear();
		m_cells.resize(static_cast<size_t>(kCellGridAxisCount * kCellGridAxisCount));
		m_entityCells.clear();
		m_initialized = true;
		LOG_INFO(Net, "[CellGrid] Init OK (zone_m={}, cell_m={}, axis_count={})",
			kZoneSizeMeters,
			kCellSizeMeters,
			kCellGridAxisCount);
		return true;
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

		if (posX < 0.0f || posZ < 0.0f || posX >= static_cast<float>(kZoneSizeMeters) || posZ >= static_cast<float>(kZoneSizeMeters))
		{
			LOG_WARN(Net, "[CellGrid] TryWorldToCellCoord FAILED: out of zone position ({:.2f}, {:.2f})", posX, posZ);
			return false;
		}

		outCell.x = static_cast<int16_t>(std::floor(posX / static_cast<float>(kCellSizeMeters)));
		outCell.z = static_cast<int16_t>(std::floor(posZ / static_cast<float>(kCellSizeMeters)));
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

			std::vector<EntityId>& previousCellEntities = m_cells[CellIndex(existingIt->second)];
			previousCellEntities.erase(
				std::remove(previousCellEntities.begin(), previousCellEntities.end(), entityId),
				previousCellEntities.end());
		}

		m_entityCells[entityId] = outCell;
		std::vector<EntityId>& targetCellEntities = m_cells[CellIndex(outCell)];
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

		std::vector<EntityId>& cellEntities = m_cells[CellIndex(it->second)];
		cellEntities.erase(
			std::remove(cellEntities.begin(), cellEntities.end(), entityId),
			cellEntities.end());
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

			const std::vector<EntityId>& cellEntities = m_cells[CellIndex(cell)];
			for (EntityId entityId : cellEntities)
			{
				if (!ContainsEntityId(outEntityIds, entityId))
				{
					outEntityIds.push_back(entityId);
				}
			}
		}
	}

	size_t CellGrid::CellIndex(const CellCoord& cell) const
	{
		return static_cast<size_t>(cell.z) * static_cast<size_t>(kCellGridAxisCount)
			+ static_cast<size_t>(cell.x);
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
