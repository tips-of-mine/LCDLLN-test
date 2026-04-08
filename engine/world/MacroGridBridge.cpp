#include "engine/world/MacroGridBridge.h"

#include <algorithm>
#include <cmath>

namespace engine::world
{
	bool ParseMacroCellId(std::string_view s, int32_t& outRow, int32_t& outCol, std::string& err)
	{
		outRow = 0;
		outCol = 0;
		err.clear();
		if (s.size() < 5u || s[0] != 'R')
		{
			err = "macro cell id must start with 'R'";
			return false;
		}
		size_t i = 1u;
		if (i >= s.size() || s[i] < '0' || s[i] > '9')
		{
			err = "expected row digits after 'R'";
			return false;
		}
		int64_t r = 0;
		while (i < s.size() && s[i] >= '0' && s[i] <= '9')
		{
			r = r * 10 + static_cast<int64_t>(s[i] - '0');
			if (r > static_cast<int64_t>(INT32_MAX))
			{
				err = "row index overflow";
				return false;
			}
			++i;
		}
		if (i >= s.size() || s[i] != 'C')
		{
			err = "expected 'C' between row and column";
			return false;
		}
		++i;
		if (i >= s.size() || s[i] < '0' || s[i] > '9')
		{
			err = "expected column digits after 'C'";
			return false;
		}
		int64_t c = 0;
		while (i < s.size() && s[i] >= '0' && s[i] <= '9')
		{
			c = c * 10 + static_cast<int64_t>(s[i] - '0');
			if (c > static_cast<int64_t>(INT32_MAX))
			{
				err = "column index overflow";
				return false;
			}
			++i;
		}
		if (i != s.size())
		{
			err = "trailing characters after macro cell id";
			return false;
		}
		outRow = static_cast<int32_t>(r);
		outCol = static_cast<int32_t>(c);
		return true;
	}

	MacroCellAabb MacroCellAabbMeters(int32_t row, int32_t col, float macroCellSizeMeters)
	{
		MacroCellAabb a;
		if (macroCellSizeMeters <= 0.0f)
			return a;
		a.minX = static_cast<float>(col) * macroCellSizeMeters;
		a.maxX = a.minX + macroCellSizeMeters;
		a.minZ = static_cast<float>(row) * macroCellSizeMeters;
		a.maxZ = a.minZ + macroCellSizeMeters;
		return a;
	}

	void ChunksIntersectingMacroAabb(const MacroCellAabb& macro, float chunkSizeMeters,
	                                 std::vector<GlobalChunkCoord>& outSorted)
	{
		outSorted.clear();
		if (chunkSizeMeters <= 0.0f || macro.maxX <= macro.minX || macro.maxZ <= macro.minZ)
			return;

		const float inv = 1.0f / chunkSizeMeters;
		const int32_t cx0 = static_cast<int32_t>(std::floor(macro.minX * inv));
		const int32_t cx1 = static_cast<int32_t>(std::ceil(macro.maxX * inv)) - 1;
		const int32_t cz0 = static_cast<int32_t>(std::floor(macro.minZ * inv));
		const int32_t cz1 = static_cast<int32_t>(std::ceil(macro.maxZ * inv)) - 1;

		for (int32_t cz = cz0; cz <= cz1; ++cz)
		{
			for (int32_t cx = cx0; cx <= cx1; ++cx)
				outSorted.push_back(GlobalChunkCoord{ cx, cz });
		}

		std::sort(outSorted.begin(), outSorted.end(), [](const GlobalChunkCoord& a, const GlobalChunkCoord& b) {
			if (a.z != b.z)
				return a.z < b.z;
			return a.x < b.x;
		});
	}
}
