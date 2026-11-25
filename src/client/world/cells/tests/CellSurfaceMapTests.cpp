// SND.2 — Tests de la carte des sols d'une cellule (CellSurfaceMap).

#include "src/client/world/cells/CellSurfaceMap.h"

#include <cstring>
#include <iostream>
#include <string>
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

	// SND.2b — carte des sols (terrain_ground.gndm) : aller-retour du format,
	// priorité sur le splat, emprise propre, rejets.
	{
		using engine::world::GroundClass;
		using engine::world::GroundMap;
		GroundMap g;
		g.size = 2; g.originX = 1000.0; g.originZ = 2000.0; g.texelM = 50.0;
		g.classes = { static_cast<uint8_t>(GroundClass::PackedEarth), static_cast<uint8_t>(GroundClass::Sand),
			static_cast<uint8_t>(GroundClass::Marble), 200 /* classe d'une version future */ };
		const std::vector<uint8_t> file = engine::world::SaveGroundMap(g);
		GroundMap back; std::string e;
		Check(engine::world::LoadGroundMap(file, back, e) && back.size == 2 && back.classes == g.classes
			&& back.originX == 1000.0 && back.originZ == 2000.0 && back.texelM == 50.0, "gndm : aller-retour");

		CellSurfaceMap s;
		Check(s.BuildFromSlap(MakeSlap()), "splat puis carte des sols");
		Check(s.BuildFromGroundMap(file, e) && s.HasGroundClasses() && !s.Empty(), "carte des sols chargee");
		Check(s.GroundAt(1010, 2010) == GroundClass::PackedEarth, "nord-ouest : terre battue");
		Check(s.GroundAt(1060, 2010) == GroundClass::Sand, "nord-est : sable");
		Check(s.GroundAt(1010, 2060) == GroundClass::Marble, "sud-ouest : marbre");
		Check(s.GroundAt(1060, 2060) == GroundClass::Unknown, "classe inconnue : repli");
		Check(s.GroundAt(999, 2010) == GroundClass::Unknown && s.GroundAt(1100, 2010) == GroundClass::Unknown, "hors emprise");
		Check(s.LayerAt(10, 10, 0, 0, 100) == -1, "couche du splat liberee");

		std::vector<uint8_t> bad2 = file;
		bad2[0] ^= 0xFF;
		CellSurfaceMap t;
		Check(!t.BuildFromGroundMap(bad2, e) && t.Empty(), "gndm : magic faux refuse");
		bad2 = file;
		bad2[4] = 2; // version 2 : inconnue de ce lecteur
		Check(!t.BuildFromGroundMap(bad2, e), "gndm : version future refusee");
		bad2 = file;
		bad2.pop_back();
		Check(!t.BuildFromGroundMap(bad2, e), "gndm : taille incoherente refusee");
		Check(t.BuildFromSlap(MakeSlap()) && !t.BuildFromGroundMap(bad2, e) && t.LayerAt(10, 10, 0, 0, 100) == 0,
			"gndm invalide : le splat reste utilisable");
	}

	if (s_failCount == 0)
		std::cout << "[OK] cell_surface_map_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
