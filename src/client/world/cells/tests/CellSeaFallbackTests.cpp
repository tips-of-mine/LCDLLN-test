// cell_sea_fallback_tests — mer de repli des cellules non générées (lot D, bug 3).
#include "src/client/world/cells/CellSeaFallback.h"
#include "src/client/world/cells/CellWorldFrame.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}
	bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }
}

int main()
{
	using namespace engine::world::cells;

	// Rayon : 1 cellule de plus que la distance de vue, borné.
	Check(FallbackSeaRadiusCells(0.0) == 1, "rayon : vue nulle -> 1");
	Check(FallbackSeaRadiusCells(2500.0) == 2, "rayon : 2,5 km -> 2");
	Check(FallbackSeaRadiusCells(10000.0) == 2, "rayon : 10 km -> 2");
	Check(FallbackSeaRadiusCells(20000.0) == 3, "rayon : 20 km -> 3");
	Check(FallbackSeaRadiusCells(1.0e9) == kFallbackSeaMaxRadiusCells, "rayon : borne max");
	Check(FallbackSeaRadiusCells(std::nan("")) == 1, "rayon : NaN -> 1");

	// Monde : seule la cellule N001_W011 est générée ; sa voisine ouest N001_W012
	// (mer, cas signalé à l'ouest de Lhynn) est manquante.
	GeneratedCellSet generated;
	generated.Add({ 1, -11 });
	const auto missing = [&](CellCoord c) { return !generated.Contains(c); };
	std::vector<CellCoord> cells;
	SelectFallbackSeaCells({ 1, -11 }, 1, missing, cells);
	Check(cells.size() == 8u, "8 voisines manquantes autour d'une cellule seule");
	bool hasWest = false, hasSelf = false;
	for (const CellCoord& c : cells)
	{
		hasWest = hasWest || c == CellCoord{ 1, -12 };
		hasSelf = hasSelf || c == CellCoord{ 1, -11 };
	}
	Check(hasWest && !hasSelf, "N001_W012 choisie, la cellule générée non");
	Check(cells.front() == CellCoord{ 2, -12 } && cells.back() == CellCoord{ 0, -10 }, "ordre déterministe (nord-ouest d'abord)");
	std::vector<CellCoord> again;
	SelectFallbackSeaCells({ 1, -11 }, 1, missing, again);
	Check(again == cells, "sélection stable d'une frame à l'autre");
	SelectFallbackSeaCells({ 1, -11 }, 0, missing, cells);
	Check(cells.empty(), "rayon 0 : la cellule du joueur seule (générée)");
	SelectFallbackSeaCells({ 1, -11 }, 2, std::function<bool(CellCoord)>{}, cells);
	Check(cells.empty(), "prédicat vide : rien");

	// Nappe : carré de la cellule + marge, au niveau de la mer, en repère de rendu.
	const CellPoint mn = CellMinCorner({ 1, -12 });
	RenderFrame frame;
	frame.SetOriginCell({ 1, -11 });
	const engine::world::water::LakeInstance lake = MakeFallbackSeaLake({ 1, -12 }, 40.0f, frame.OriginX(), frame.OriginZ());
	Check(lake.isOcean && Near(lake.waterLevelY, 40.0), "nappe : mer au niveau 40 m");
	Check(lake.polygon.size() == 4u, "nappe : 4 sommets");
	if (lake.polygon.size() == 4u)
	{
		Check(Near(lake.polygon[0].x, frame.ToRenderX(mn.x - kFallbackSeaMarginM))
			&& Near(lake.polygon[0].z, frame.ToRenderZ(mn.z - kFallbackSeaMarginM)), "nappe : coin min avec marge");
		Check(Near(lake.polygon[2].x, frame.ToRenderX(mn.x + kCellSizeMeters + kFallbackSeaMarginM))
			&& Near(lake.polygon[2].z, frame.ToRenderZ(mn.z + kCellSizeMeters + kFallbackSeaMarginM)), "nappe : coin max avec marge");
		// La cellule ouest jouxte la cellule d'origine : son bord est à x = -5000 (rendu).
		Check(Near(lake.polygon[1].x, -5000.0 + kFallbackSeaMarginM), "nappe : recouvre la frontière avec la cellule générée");
		Check(Near(lake.polygon[0].y, 40.0) && Near(lake.polygon[3].y, 40.0), "nappe : plate");
	}

	// Niveau de la mer : premier lac `isOcean`, aucun sinon.
	std::vector<engine::world::water::LakeInstance> lakes(2);
	lakes[0].waterLevelY = 12.0f;
	float sea = -1.0f;
	Check(!FindSeaLevel(lakes, sea), "pas de mer : pas de niveau");
	lakes[1].isOcean = true;
	lakes[1].waterLevelY = 40.0f;
	Check(FindSeaLevel(lakes, sea) && Near(sea, 40.0), "niveau de la mer lu sur le lac océan");

	// Mur invisible généralisé.
	const CellPoint c11 = CellCenter({ 1, -11 });
	const CellPoint c12 = CellCenter({ 1, -12 });
	Check(IsMoveAllowedAvoiding(missing, c11.x, c11.z, c11.x + 10.0, c11.z), "dans la cellule générée : libre");
	Check(!IsMoveAllowedAvoiding(missing, c11.x, c11.z, c12.x, c12.z), "entrée dans une cellule manquante : bloquée");
	Check(IsMoveAllowedAvoiding(missing, c12.x, c12.z, c12.x + 5.0, c12.z), "départ déjà hors du monde : jamais figé");
	Check(IsMoveAllowedAvoiding(std::function<bool(CellCoord)>{}, c11.x, c11.z, c12.x, c12.z), "prédicat vide : libre");

	if (g_fail == 0)
		std::printf("cell_sea_fallback_tests OK\n");
	return g_fail == 0 ? 0 : 1;
}
