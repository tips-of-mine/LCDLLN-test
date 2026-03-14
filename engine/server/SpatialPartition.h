#pragma once

#include "engine/server/ReplicationTypes.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Zone size in meters inherited from the M09.1 world model ticket.
	inline constexpr int kZoneSizeMeters = 4096;

	/// Fixed spatial partition cell size in meters required by M13.2.
	inline constexpr int kCellSizeMeters = 64;

	/// Base client interest radius in cells, resulting in a 7x7 neighborhood.
	inline constexpr int kBaseInterestRadiusCells = 3;

	/// Number of cells per axis for a 4 km zone split into 64 m cells.
	inline constexpr int kCellGridAxisCount = kZoneSizeMeters / kCellSizeMeters;

	/// Discrete cell coordinate inside a zone-local 64 m grid.
	struct CellCoord
	{
		int16_t x = 0;
		int16_t z = 0;

		/// Compare two cell coordinates for exact equality.
		bool operator==(const CellCoord& other) const = default;
	};

	/// Hasher for `CellCoord` when used in unordered containers.
	struct CellCoordHash
	{
		/// Hash the 2D cell coordinate into a size_t value.
		size_t operator()(const CellCoord& coord) const;
	};

	/// Result of diffing two interest cell sets.
	struct InterestDiff
	{
		std::vector<CellCoord> enteringCells;
		std::vector<CellCoord> leavingCells;
	};

	/// Zone-local 64 m cell grid maintaining cell-to-entity membership.
	class CellGrid final
	{
	public:
		/// Construct an empty grid that must be initialized before use.
		CellGrid() = default;

		/// Release all cell mappings on destruction.
		~CellGrid();

		/// Allocate the fixed grid storage for a 4 km zone.
		bool Init();

		/// Release all entity/cell mappings.
		void Shutdown();

		/// Convert a zone-local position in meters to a valid cell coordinate.
		bool TryWorldToCellCoord(float posX, float posZ, CellCoord& outCell) const;

		/// Insert or move one entity into the cell matching the given position.
		bool UpsertEntity(EntityId entityId, float posX, float posZ, CellCoord& outCell);

		/// Remove one entity from its current cell mapping.
		bool RemoveEntity(EntityId entityId);

		/// Build the clamped 7x7 interest cell list around the provided center cell.
		void BuildInterestSet(const CellCoord& centerCell, std::vector<CellCoord>& outCells) const;

		/// Gather the entity ids currently present in the provided cells.
		void GatherEntityIds(const std::vector<CellCoord>& cells, std::vector<EntityId>& outEntityIds) const;

		/// Return true when the grid has been initialized.
		bool IsInitialized() const { return m_initialized; }

	private:
		/// Return the flat storage index for a valid cell coordinate.
		size_t CellIndex(const CellCoord& cell) const;

		std::vector<std::vector<EntityId>> m_cells;
		std::unordered_map<EntityId, CellCoord> m_entityCells;
		bool m_initialized = false;
	};

	/// Compute entering and leaving cells between two interest sets.
	void ComputeInterestDiff(
		const std::vector<CellCoord>& previousCells,
		const std::vector<CellCoord>& currentCells,
		InterestDiff& outDiff);
}
