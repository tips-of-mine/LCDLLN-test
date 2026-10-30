// SND.2 — Tests de la carte des sols d'une cellule (CellSurfaceMap).

#include "src/client/world/cells/CellSurfaceMap.h"

#include <cstring>
#include <iostream>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	/// SLAP 2×2 : texel (i, j) = canal dominant (i + 2j).
	std::vector<uint8_t> MakeSlap()
	{
		std::vector<uint8_t> f(12 + 2 * 2 * 4, 0);
		const uint32_t magic = 0x50414C53u, w = 2, h = 2;
		std::memcpy(f.data(), &magic, 4);
		std::memcpy(f.data() + 4, &w, 4);
		std::memcpy(f.data() + 8, &h, 4);
		for (int t = 0; t < 4; ++t)
		{
			uint8_t* px = f.data() + 12 + t * 4;
			px[0] = 20; px[1] = 20; px[2] = 20; px[3] = 20;
			px[t] = 195; // canal t dominant
		}
		return f;
	}
}

int main()
{
	using engine::world::cells::CellSurfaceMap;
	CellSurfaceMap m;
	Check(m.BuildFromSlap(MakeSlap()), "SLAP valide");
	Check(m.Width() == 2 && m.Height() == 2, "dimensions");
	// Cellule [0, 100[ × [0, 100[ : chaque texel couvre 50 m.
	Check(m.LayerAt(10, 10, 0, 0, 100) == 0, "nord-ouest : herbe");
	Check(m.LayerAt(60, 10, 0, 0, 100) == 1, "nord-est : terre");
	Check(m.LayerAt(10, 60, 0, 0, 100) == 2, "sud-ouest : roche");
	Check(m.LayerAt(60, 60, 0, 0, 100) == 3, "sud-est : neige");
	Check(m.LayerAt(-1, 10, 0, 0, 100) == -1 && m.LayerAt(100, 10, 0, 0, 100) == -1, "hors cellule");
	Check(m.LayerAt(5010, 5060, 5000, 5000, 100) == 2, "coin min quelconque");

	std::vector<uint8_t> bad = MakeSlap();
	bad[0] ^= 0xFF;
	CellSurfaceMap b;
	Check(!b.BuildFromSlap(bad) && b.Empty(), "magic faux refuse");
	bad = MakeSlap();
	bad.pop_back();
	Check(!b.BuildFromSlap(bad), "taille incoherente refusee");
	Check(b.LayerAt(1, 1, 0, 0, 100) == -1, "carte vide : -1");

	if (s_failCount == 0)
		std::cout << "[OK] cell_surface_map_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
