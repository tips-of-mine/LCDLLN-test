#pragma once

#include "src/shared/network/ReplicationTypes.h"
#include "src/client/world/WorldModel.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Zone size in meters — same value as \ref engine::world::kZoneSize (single source of truth).
	inline constexpr int kZoneSizeMeters = engine::world::kZoneSize;

	/// Fixed spatial partition cell size in meters — same as \ref engine::world::kSpatialCellSizeMeters.
	inline constexpr int kCellSizeMeters = engine::world::kSpatialCellSizeMeters;

	/// Base client interest radius in cells, resulting in a 7x7 neighborhood.
	inline constexpr int kBaseInterestRadiusCells = 3;

	/// Number of cells per axis for the zone split into \ref kCellSizeMeters cells.
	inline constexpr int kCellGridAxisCount = kZoneSizeMeters / kCellSizeMeters;

	static_assert(kZoneSizeMeters % kCellSizeMeters == 0,
	              "Zone size must be a multiple of spatial cell size (interest grid)");

	/// Discrete cell coordinate inside a zone-local spatial grid (\ref kCellSizeMeters m).
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

	/// Zone-local cell grid (\ref kCellSizeMeters m) maintaining cell-to-entity membership.
	class CellGrid final
	{
	public:
		/// Construct an empty grid that must be initialized before use.
		CellGrid() = default;

		/// Release all cell mappings on destruction.
		~CellGrid();

		/// Allocate the fixed grid storage for one zone (\ref kZoneSizeMeters m side).
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
