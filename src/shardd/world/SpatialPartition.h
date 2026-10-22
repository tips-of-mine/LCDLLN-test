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

	/// Demi-côté (m) de la grille unique du monde « Royaumes » : couvre toute la
	/// carte du lore (cellules E-19 … E20 et S14 … N18, ±205 km) avec une marge.
	/// 2 × 210 km / 100 m = 4200 cases par axe (tient dans un int16).
	inline constexpr float kCellWorldHalfExtentMeters = 210000.0f;

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

		/// Initialise la grille d'une carte carrée de côté 2 × \p halfExtentMeters,
		/// centrée sur (0,0). Défaut : une zone historique (\ref kZoneSizeMeters m de
		/// côté, plage [−5000, 5000[). Le monde « Royaumes » (cellules de 10 km) passe
		/// \ref kCellWorldHalfExtentMeters : une seule grille pour toute la carte.
		/// Stockage CREUX : seules les cases de 100 m occupées consomment de la mémoire.
		bool Init(float halfExtentMeters = static_cast<float>(kZoneSizeMeters) * 0.5f);

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

		/// Demi-côté (m) de la carte couverte (plage acceptée [−h, h[ sur X et Z).
		float HalfExtentMeters() const { return m_halfExtentMeters; }
		/// Nombre de cases (100 m) par axe.
		int AxisCount() const { return m_axisCount; }
		/// Nombre de cases actuellement occupées (stockage creux).
		size_t OccupiedCellCount() const { return m_cells.size(); }

	private:
		/// Vrai si la case est dans [0, m_axisCount) sur les deux axes.
		bool IsValidCell(const CellCoord& cell) const;
		/// Clé de stockage d'une case valide.
		uint32_t CellKey(const CellCoord& cell) const;

		/// Case → entités présentes (une case vidée est retirée).
		std::unordered_map<uint32_t, std::vector<EntityId>> m_cells;
		std::unordered_map<EntityId, CellCoord> m_entityCells;
		float m_halfExtentMeters = static_cast<float>(kZoneSizeMeters) * 0.5f;
		int m_axisCount = kCellGridAxisCount;
		bool m_initialized = false;
	};

	/// Compute entering and leaving cells between two interest sets.
	void ComputeInterestDiff(
		const std::vector<CellCoord>& previousCells,
		const std::vector<CellCoord>& currentCells,
		InterestDiff& outDiff);
}
