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

	bool IsInLoreGrid(CellCoord c)
	{
		return c.n >= kMinN && c.n <= kMaxN && c.e >= kMinE && c.e <= kMaxE;
	}

	std::string CellDirName(CellCoord c)
	{
		return "cell_" + Format(c, 'n', 's', 'e', 'w');
	}

	std::optional<CellCoord> ParseCellDirName(std::string_view s)
	{
		if (s.substr(0, 5) != "cell_") return std::nullopt;
		return Parse(s, 5, 'n', 's', 'e', 'w');
	}

	std::string LoreCellId(CellCoord c)
	{
		return Format(c, 'N', 'S', 'E', 'W');
	}

	std::optional<CellCoord> ParseLoreCellId(std::string_view s)
	{
		return Parse(s, 0, 'N', 'S', 'E', 'W');
	}

	uint32_t CellZoneId(CellCoord c)
	{
		return kCellZoneIdBase + static_cast<uint32_t>(c.n + 500) * 1000u + static_cast<uint32_t>(c.e + 500);
	}

	bool IsCellZoneId(uint32_t zoneId)
	{
		return zoneId >= kCellZoneIdBase && zoneId < kCellZoneIdBase + 1000000u;
	}

	std::optional<CellCoord> CellFromZoneId(uint32_t zoneId)
	{
		if (!IsCellZoneId(zoneId)) return std::nullopt;
		const uint32_t v = zoneId - kCellZoneIdBase;
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

	uint32_t MapIdOfZone(uint32_t zoneId)
	{
		return IsCellZoneId(zoneId) ? kCellWorldMapId : zoneId;
	}

	bool SameMap(uint32_t zoneA, uint32_t zoneB)
	{
		return MapIdOfZone(zoneA) == MapIdOfZone(zoneB);
	}

	bool ZoneDirBelongsToWorld(std::string_view dirName, std::string_view activeZone)
	{
		const bool isCellDir = ParseCellDirName(dirName).has_value();
		return IsCellWorld(activeZone) ? isCellDir : !isCellDir;
	}
}
