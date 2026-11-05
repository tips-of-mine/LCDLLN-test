#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace engine::world::cells
{
	namespace
	{
		/// Lit exactement 3 chiffres décimaux à partir de \p s[pos].
		bool Read3Digits(std::string_view s, size_t pos, int& out)
		{
			if (pos + 3 > s.size()) return false;
			int v = 0;
			for (size_t i = pos; i < pos + 3; ++i)
			{
				if (s[i] < '0' || s[i] > '9') return false;
				v = v * 10 + (s[i] - '0');
			}
			out = v;
			return true;
		}

		/// Format commun : <ns><DDD>_<ew><DDD> avec les lettres fournies.
		std::string Format(CellCoord c, char north, char south, char east, char west)
		{
			char buf[32];
			std::snprintf(buf, sizeof(buf), "%c%03d_%c%03d",
				c.n >= 0 ? north : south, std::abs(c.n),
				c.e >= 0 ? east : west, std::abs(c.e));
			return buf;
		}

		/// Décode le format commun à partir de \p offset (préfixe déjà vérifié).
		std::optional<CellCoord> Parse(std::string_view s, size_t offset, char north, char south, char east, char west)
		{
			if (s.size() != offset + 9 || s[offset + 4] != '_') return std::nullopt;
			const char ns = s[offset], ew = s[offset + 5];
			if ((ns != north && ns != south) || (ew != east && ew != west)) return std::nullopt;
			int n = 0, e = 0;
			if (!Read3Digits(s, offset + 1, n) || !Read3Digits(s, offset + 6, e)) return std::nullopt;
			// « s000 » / « w000 » n'existent pas : le zéro s'écrit toujours n/e.
			if ((ns == south && n == 0) || (ew == west && e == 0)) return std::nullopt;
			return CellCoord{ ns == north ? n : -n, ew == east ? e : -e };
		}

		/// Description d'une carte du registre (cf. CellMap). Indexée par la valeur
		/// de l'énumération : l'ordre doit suivre celui de CellMap.
		struct MapInfo
		{
			std::string_view worldId;   ///< sous-dossier world/<id>/
			std::string_view dirPrefix; ///< préfixe des dossiers zones/<prefix>…
			uint32_t zoneIdBase;        ///< base des identifiants de zone des cellules
			uint32_t mapId;             ///< identifiant de carte (grille d'intérêt)
			int minN, maxN, minE, maxE; ///< bornes de la grille (indices inclus)
		};

		constexpr MapInfo kMaps[kCellMapCount] = {
			{ "royaumes", "cell_", kCellZoneIdBase, kCellWorldMapId, kMinN, kMaxN, kMinE, kMaxE },
			{ "enfers", "enfers_", kEnfersZoneIdBase, kEnfersMapId, kEnfersMinN, kEnfersMaxN, kEnfersMinE, kEnfersMaxE },
		};

		/// Entrée du registre de la carte \p map.
		const MapInfo& Info(CellMap map)
		{
			return kMaps[static_cast<size_t>(map)];
		}
	}

	CellCoord CellFromPosition(double x, double z)
	{
		const int e = static_cast<int>(std::floor((x + kCellHalfMeters) / kCellSizeMeters));
		const int k = static_cast<int>(std::floor((z + kCellHalfMeters) / kCellSizeMeters));
		return { -k, e };
	}

	CellPoint CellCenter(CellCoord c)
	{
		return { c.e * kCellSizeMeters, -c.n * kCellSizeMeters };
	}

	CellPoint CellMinCorner(CellCoord c)
	{
		const CellPoint ctr = CellCenter(c);
		return { ctr.x - kCellHalfMeters, ctr.z - kCellHalfMeters };
	}

	CellPoint ToLocal(CellCoord c, double x, double z)
	{
		const CellPoint ctr = CellCenter(c);
		return { x - ctr.x, z - ctr.z };
	}

	CellPoint ToGlobal(CellCoord c, double localX, double localZ)
	{
		const CellPoint ctr = CellCenter(c);
		return { ctr.x + localX, ctr.z + localZ };
	}

	bool IsInLoreGrid(CellCoord c, CellMap map)
	{
		const MapInfo& m = Info(map);
		return c.n >= m.minN && c.n <= m.maxN && c.e >= m.minE && c.e <= m.maxE;
	}

	std::string CellDirName(CellCoord c, CellMap map)
	{
		return std::string(Info(map).dirPrefix) + Format(c, 'n', 's', 'e', 'w');
	}

	std::optional<CellCoord> ParseCellDirName(std::string_view s, CellMap map)
	{
		const std::string_view prefix = Info(map).dirPrefix;
		if (s.substr(0, prefix.size()) != prefix) return std::nullopt;
		return Parse(s, prefix.size(), 'n', 's', 'e', 'w');
	}

	std::optional<CellMap> CellMapOfDirName(std::string_view s)
	{
		for (int i = 0; i < kCellMapCount; ++i)
		{
			const CellMap map = static_cast<CellMap>(i);
			if (ParseCellDirName(s, map).has_value()) return map;
		}
		return std::nullopt;
	}

	std::string LoreCellId(CellCoord c)
	{
		return Format(c, 'N', 'S', 'E', 'W');
	}

	std::optional<CellCoord> ParseLoreCellId(std::string_view s)
	{
		return Parse(s, 0, 'N', 'S', 'E', 'W');
	}

	uint32_t CellZoneId(CellCoord c, CellMap map)
	{
		return Info(map).zoneIdBase + static_cast<uint32_t>(c.n + 500) * 1000u + static_cast<uint32_t>(c.e + 500);
	}

	std::optional<CellMap> CellMapOfZoneId(uint32_t zoneId)
	{
		for (int i = 0; i < kCellMapCount; ++i)
		{
			const MapInfo& m = kMaps[i];
			if (zoneId >= m.zoneIdBase && zoneId < m.zoneIdBase + kCellZoneIdSpan) return static_cast<CellMap>(i);
		}
		return std::nullopt;
	}

	bool IsCellZoneId(uint32_t zoneId)
	{
		return CellMapOfZoneId(zoneId).has_value();
	}

	std::optional<CellCoord> CellFromZoneId(uint32_t zoneId)
	{
		const std::optional<CellMap> map = CellMapOfZoneId(zoneId);
		if (!map) return std::nullopt;
		const uint32_t v = zoneId - Info(*map).zoneIdBase;
		return CellCoord{ static_cast<int>(v / 1000u) - 500, static_cast<int>(v % 1000u) - 500 };
	}

	double DistanceToCellBorder(double x, double z)
	{
		const CellPoint l = ToLocal(CellFromPosition(x, z), x, z);
		return std::min(kCellHalfMeters - std::fabs(l.x), kCellHalfMeters - std::fabs(l.z));
	}

	double DistanceToCell(CellCoord c, double x, double z)
	{
		const CellPoint l = ToLocal(c, x, z);
		const double dx = std::max(0.0, std::fabs(l.x) - kCellHalfMeters);
		const double dz = std::max(0.0, std::fabs(l.z) - kCellHalfMeters);
		return std::sqrt(dx * dx + dz * dz);
	}

	bool IsCellWorld(std::string_view activeZone)
	{
		return activeZone == kCellWorldName;
	}

	std::string_view WorldIdOf(CellMap map)
	{
		return Info(map).worldId;
	}

	std::optional<CellMap> CellMapFromWorldId(std::string_view worldId)
	{
		for (int i = 0; i < kCellMapCount; ++i)
			if (kMaps[i].worldId == worldId) return static_cast<CellMap>(i);
		return std::nullopt;
	}

	uint32_t MapIdOf(CellMap map)
	{
		return Info(map).mapId;
	}

	std::optional<CellMap> CellMapFromMapId(uint32_t mapId)
	{
		for (int i = 0; i < kCellMapCount; ++i)
			if (kMaps[i].mapId == mapId) return static_cast<CellMap>(i);
		return std::nullopt;
	}

	CellMap CellMapOfZoneOr(uint32_t zoneId, CellMap fallback)
	{
		return CellMapOfZoneId(zoneId).value_or(fallback);
	}

	uint32_t MapIdOfZone(uint32_t zoneId)
	{
		const std::optional<CellMap> map = CellMapOfZoneId(zoneId);
		return map ? MapIdOf(*map) : zoneId;
	}

	bool SameMap(uint32_t zoneA, uint32_t zoneB)
	{
		return MapIdOfZone(zoneA) == MapIdOfZone(zoneB);
	}

	bool ZoneDirBelongsToWorld(std::string_view dirName, std::string_view activeZone)
	{
		const bool isCellDir = CellMapOfDirName(dirName).has_value();
		return IsCellWorld(activeZone) ? isCellDir : !isCellDir;
	}

	uint32_t MapCellZoneId(const MapCell& region)
	{
		return CellZoneId(region.cell, region.map);
	}

	std::optional<MapCell> MapCellFromZoneId(uint32_t zoneId)
	{
		const std::optional<CellMap> map = CellMapOfZoneId(zoneId);
		const std::optional<CellCoord> cell = CellFromZoneId(zoneId);
		if (!map || !cell) return std::nullopt;
		return MapCell{ *cell, *map };
	}

	std::string MapCellLabel(const MapCell& region)
	{
		if (region.map == CellMap::Royaumes) return LoreCellId(region.cell);
		std::string label(WorldIdOf(region.map));
		for (char& ch : label)
			if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
		return label + ":" + LoreCellId(region.cell);
	}
}
