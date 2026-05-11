#pragma once
// GridVisitor : pattern Visitor pour parcourir les entites dans un AoI
// autour d'une position. Compose CellGrid::BuildInterestSet (7x7 cells)
// + CellGrid::GatherEntityIds. Header-only template.
//
// Pattern WoW-like adapte LCDLLN : le visiteur est un foncteur ou une
// lambda invoque pour chaque EntityId dans le voisinage. Le caller
// resout l'objet derriere l'id via ObjectAccessor (Wave 19).
//
// Limite Wave 18 : utilise le radius par defaut kBaseInterestRadiusCells
// (3 cells = 7x7 = 700m d'AoI avec kSpatialCellSizeMeters=100m). Pour un
// AoI plus fin (ex: /say a 30m), il faudra une grille a cellule plus
// petite ou un filtrage radial par distance euclidienne du caller.

#include "src/shardd/world/SpatialPartition.h"
#include "src/shared/network/ReplicationTypes.h"

#include <utility>
#include <vector>

namespace engine::server
{
	/// Visite toutes les entites dans le voisinage 7x7 cells autour de
	/// (centerX, centerZ). \p fn est invoque une fois par EntityId trouve.
	///
	/// \param grid CellGrid initialisee. Si non initialisee, no-op.
	/// \param centerX position X world-space en metres.
	/// \param centerZ position Z world-space en metres.
	/// \param fn foncteur invocable avec (EntityId). Peut etre lambda.
	///
	/// \remark Order de visite non garanti (depend de l'ordre des cells
	///         dans BuildInterestSet + l'ordre interne de chaque cell).
	/// \remark Si (centerX, centerZ) tombe hors de la zone, no-op silent.
	template<typename Fn>
	void GridVisit(const CellGrid& grid, float centerX, float centerZ, Fn&& fn)
	{
		CellCoord center{};
		if (!grid.TryWorldToCellCoord(centerX, centerZ, center))
			return;

		std::vector<CellCoord> cells;
		grid.BuildInterestSet(center, cells);

		std::vector<EntityId> ids;
		grid.GatherEntityIds(cells, ids);

		for (auto id : ids)
			fn(id);
	}

	/// Variante non-template (type-erased via std::function-like) — utile
	/// pour les call-sites qui n'ont pas de raison d'inline. Implementee
	/// inline en termes de GridVisit<Fn>. Conserve l'API symmetrique
	/// avec un foncteur stateful (GridNotifier).
	///
	/// \remark Si tu peux utiliser le template, prefer GridVisit<Fn> qui
	///         n'a aucun cout d'indirection.
	template<typename Visitor>
	void GridVisitWithVisitor(const CellGrid& grid, float centerX, float centerZ, Visitor& visitor)
	{
		GridVisit(grid, centerX, centerZ, [&visitor](EntityId id) {
			visitor.Visit(id);
		});
	}
}
