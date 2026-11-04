// world_cells_tests — découpage du monde en cellules de 10 km (module pur).
#include "src/shared/world/WorldCells.h"

#include <cmath>
#include <cstdio>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}
	bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }
}

int main()
{
	using namespace engine::world::cells;

	// Origine : centre de l'Oracle.
	Check(CellFromPosition(0.0, 0.0) == CellCoord{ 0, 0 }, "oracle a l'origine");
	Check(CellFromPosition(4999.0, -4999.0) == CellCoord{ 0, 0 }, "bord interieur");
	Check(CellFromPosition(5000.0, 0.0) == CellCoord{ 0, 1 }, "x=5000 -> est");
	Check(CellFromPosition(0.0, -5001.0) == CellCoord{ 1, 0 }, "-Z = nord");
	Check(CellFromPosition(0.0, 5000.0) == CellCoord{ -1, 0 }, "+Z = sud");

	// Hameau de Marn (N003_W009).
	const CellCoord marn{ 3, -9 };
	const CellPoint c = CellCenter(marn);
	Check(Near(c.x, -90000.0) && Near(c.z, -30000.0), "centre N003_W009");
	Check(CellFromPosition(c.x, c.z) == marn, "aller-retour centre");
	const CellPoint mn = CellMinCorner(marn);
	Check(Near(mn.x, -95000.0) && Near(mn.z, -35000.0), "coin min");
	const CellPoint l = ToLocal(marn, -90100.0, -29900.0);
	Check(Near(l.x, -100.0) && Near(l.z, 100.0), "global -> local");
	const CellPoint g = ToGlobal(marn, l.x, l.z);
	Check(Near(g.x, -90100.0) && Near(g.z, -29900.0), "local -> global");

	// Noms.
	Check(CellDirName(marn) == "cell_n003_w009", "nom dossier");
	Check(CellDirName({ 0, 0 }) == "cell_n000_e000", "nom oracle");
	Check(CellDirName({ -9, 11 }) == "cell_s009_e011", "nom sud-est");
	Check(LoreCellId({ -7, 16 }) == "S007_E016", "id lore");
	Check(ParseCellDirName("cell_s009_e011") == CellCoord{ -9, 11 }, "parse dossier");
	Check(!ParseCellDirName("cell_S009_E011").has_value(), "majuscules refusees");
	Check(!ParseCellDirName("cell_s000_e001").has_value(), "s000 refuse");
	Check(!ParseCellDirName("cell_n03_w009").has_value(), "2 chiffres refuses");
	Check(ParseLoreCellId("N011_E008") == CellCoord{ 11, 8 }, "parse lore");
	Check(!ParseLoreCellId("n011_e008").has_value(), "lore minuscules refusees");

	// Identifiants de zone : bijectifs, sans collision avec les zones historiques.
	for (int n = kMinN; n <= kMaxN; ++n)
		for (int e = kMinE; e <= kMaxE; ++e)
		{
			const uint32_t id = CellZoneId({ n, e });
			if (!IsCellZoneId(id) || CellFromZoneId(id) != CellCoord{ n, e })
			{
				Check(false, "zone id bijectif");
				n = kMaxN + 1;
				break;
			}
		}
	Check(!IsCellZoneId(0u) && !IsCellZoneId(1u) && !IsCellZoneId(2u), "zones historiques hors plage");
	Check(!CellFromZoneId(1u).has_value(), "zone 1 n'est pas une cellule");

	Check(IsInLoreGrid({ 23, -19 }) && IsInLoreGrid({ -18, 26 }), "coins du lore");
	Check(IsInLoreGrid({ 19, 12 }) && IsInLoreGrid({ -15, 0 }) && IsInLoreGrid({ 0, 24 }), "terres ajoutees en v1.0.52 dans le lore");
	Check(!IsInLoreGrid({ 24, 0 }) && !IsInLoreGrid({ -19, 0 }) && !IsInLoreGrid({ 0, 27 }) && !IsInLoreGrid({ 0, -20 }), "hors lore");

	Check(Near(DistanceToCellBorder(0.0, 0.0), 5000.0), "distance au bord : centre");
	Check(Near(DistanceToCellBorder(4900.0, 100.0), 100.0), "distance au bord : pres de l'est");
	Check(Near(DistanceToCell({ 0, 0 }, 100.0, -200.0), 0.0), "distance a la cellule : dedans");
	Check(Near(DistanceToCell({ 0, 1 }, 4000.0, 0.0), 1000.0), "distance a la cellule est");
	Check(Near(DistanceToCell({ 1, 1 }, 4000.0, -4000.0), std::sqrt(2.0) * 1000.0), "distance a la cellule diagonale");

	// Une seule carte : toutes les cellules partagent la même carte.
	Check(IsCellWorld("royaumes") && !IsCellWorld("feyhin") && !IsCellWorld(""), "monde en cellules");
	Check(MapIdOfZone(CellZoneId({ 3, -9 })) == kCellWorldMapId, "carte d'une cellule");
	Check(!IsCellZoneId(kCellWorldMapId) && kCellWorldMapId > 2u, "id de carte hors plage");
	Check(MapIdOfZone(1u) == 1u, "carte d'une zone historique");
	Check(SameMap(CellZoneId({ 3, -9 }), CellZoneId({ -9, 11 })), "deux cellules = meme carte");
	Check(!SameMap(CellZoneId({ 3, -9 }), 1u) && !SameMap(1u, 2u) && SameMap(2u, 2u), "zones historiques distinctes");
	Check(ZoneDirBelongsToWorld("cell_n003_w009", "royaumes") && !ZoneDirBelongsToWorld("feyhin", "royaumes"), "dossiers du monde en cellules");
	Check(ZoneDirBelongsToWorld("feyhin", "feyhin") && ZoneDirBelongsToWorld("zone_1", "")
		&& !ZoneDirBelongsToWorld("cell_n003_w009", "feyhin"), "dossiers des zones historiques");

	// Seconde carte : les Enfers (5 cellules en croix, repère propre).
	Check(CellDirName({ 0, 0 }, CellMap::Enfers) == "enfers_n000_e000", "nom dossier enfers");
	Check(CellDirName({ -1, 0 }, CellMap::Enfers) == "enfers_s001_e000", "nom dossier enfers sud");
	Check(ParseCellDirName("enfers_n000_w001", CellMap::Enfers) == CellCoord{ 0, -1 }, "parse dossier enfers");
	Check(!ParseCellDirName("enfers_n000_e000").has_value(), "dossier enfers refuse pour les royaumes");
	Check(!ParseCellDirName("cell_n000_e000", CellMap::Enfers).has_value(), "dossier royaumes refuse pour les enfers");
	Check(CellMapOfDirName("enfers_n001_e000") == CellMap::Enfers && CellMapOfDirName("cell_n001_e000") == CellMap::Royaumes
		&& !CellMapOfDirName("feyhin").has_value(), "carte d'un dossier");
	Check(IsInLoreGrid({ 1, 0 }, CellMap::Enfers) && IsInLoreGrid({ -1, -1 }, CellMap::Enfers)
		&& !IsInLoreGrid({ 2, 0 }, CellMap::Enfers) && !IsInLoreGrid({ 0, -2 }, CellMap::Enfers), "grille des enfers");
	// Identifiants : aller-retour sur les deux cartes, plages disjointes.
	for (int map = 0; map < kCellMapCount; ++map)
		for (int n = -1; n <= 1; ++n)
			for (int e = -1; e <= 1; ++e)
			{
				const CellMap m = static_cast<CellMap>(map);
				const uint32_t id = CellZoneId({ n, e }, m);
				Check(IsCellZoneId(id) && CellFromZoneId(id) == CellCoord{ n, e } && CellMapOfZoneId(id) == m,
					"zone id bijectif (deux cartes)");
				Check(MapCellFromZoneId(id).has_value() && *MapCellFromZoneId(id) == MapCell({ n, e }, m)
					&& MapCellZoneId(MapCell({ n, e }, m)) == id, "region aller-retour");
			}
	Check(CellZoneId({ 0, 0 }, CellMap::Enfers) != CellZoneId({ 0, 0 }), "pas de collision en (0,0)");
	Check(CellZoneId({ 0, 0 }, CellMap::Enfers) >= kEnfersZoneIdBase, "plage enfers");
	Check(MapIdOfZone(CellZoneId({ 0, 1 }, CellMap::Enfers)) == kEnfersMapId, "carte d'une cellule des enfers");
	Check(!SameMap(CellZoneId({ 0, 0 }, CellMap::Enfers), CellZoneId({ 0, 0 })), "enfers et royaumes : cartes differentes");
	Check(SameMap(CellZoneId({ 0, 0 }, CellMap::Enfers), CellZoneId({ 1, 0 }, CellMap::Enfers)), "deux cellules des enfers = meme carte");
	Check(!IsCellZoneId(kEnfersMapId) && !IsCellZoneId(kEnfersZoneIdBase + kCellZoneIdSpan), "hors plage enfers");
	Check(CellMapFromWorldId("enfers") == CellMap::Enfers && CellMapFromWorldId("royaumes") == CellMap::Royaumes
		&& !CellMapFromWorldId("feyhin").has_value() && WorldIdOf(CellMap::Enfers) == "enfers", "identifiants de monde");
	Check(CellMapFromMapId(kEnfersMapId) == CellMap::Enfers && CellMapFromMapId(kCellWorldMapId) == CellMap::Royaumes
		&& !CellMapFromMapId(1u).has_value() && MapIdOf(CellMap::Enfers) == kEnfersMapId, "identifiants de carte");
	Check(CellMapOfZoneOr(1u) == CellMap::Royaumes && CellMapOfZoneOr(CellZoneId({ 0, 0 }, CellMap::Enfers)) == CellMap::Enfers,
		"carte d'une zone avec repli");
	Check(ZoneDirBelongsToWorld("enfers_n000_e000", "royaumes") && !ZoneDirBelongsToWorld("enfers_n000_e000", "feyhin"),
		"dossiers des enfers dans le monde en cellules");
	Check(MapCellLabel(MapCell({ 0, -1 }, CellMap::Enfers)) == "ENFERS:N000_W001" && MapCellLabel(MapCell(3, -9)) == "N003_W009",
		"libelle de region");

	if (g_fail == 0) std::printf("world_cells_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
