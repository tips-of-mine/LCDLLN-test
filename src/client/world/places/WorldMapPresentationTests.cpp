// Carte du monde — tests des règles de présentation (libellés au survol,
// déroulé du parchemin). Fonctions pures de WorldMapPresentation.h.

#include "src/client/world/places/WorldMapPresentation.h"

#include <cmath>
#include <iostream>

namespace
{
	int s_failCount = 0;

	/// Enregistre un échec si \p ok est faux.
	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	/// Enregistre un échec si \p actual s'écarte de \p expected de plus de 0,001.
	void CheckNear(float actual, float expected, const char* msg)
	{
		if (std::fabs(actual - expected) > 0.001f)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << " : " << actual << " attendu " << expected << std::endl;
		}
	}
}

int main()
{
	using namespace engine::client;

	// Régions : curseur dans la région, centre découvert, vue éloignée.
	Check(!WorldMapRegionNameVisible(1.0f, false, true), "region non survolee : pas de nom");
	Check(WorldMapRegionNameVisible(1.0f, true, true), "region survolee en vue d'ensemble : nom");
	Check(WorldMapRegionNameVisible(2.5f, true, true), "region survolee a zoom 2.5 : nom");
	Check(!WorldMapRegionNameVisible(2.6f, true, true), "region survolee en vue rapprochee : pas de nom");
	Check(!WorldMapRegionNameVisible(1.0f, true, false), "region non decouverte : pas de nom");
	Check(!WorldMapRegionNameVisible(150.0f, true, true), "ouverture (2 km visibles) : pas de nom de region");

	// Déroulé : 0 au départ, 1 à la fin, monotone, borné.
	CheckNear(WorldMapUnrollFraction(0.0f, kWorldMapUnrollSeconds), 0.0f, "deroule : depart");
	CheckNear(WorldMapUnrollFraction(kWorldMapUnrollSeconds, kWorldMapUnrollSeconds), 1.0f, "deroule : fin");
	CheckNear(WorldMapUnrollFraction(10.0f, kWorldMapUnrollSeconds), 1.0f, "deroule : borne a 1");
	CheckNear(WorldMapUnrollFraction(-1.0f, kWorldMapUnrollSeconds), 0.0f, "deroule : borne a 0");
	CheckNear(WorldMapUnrollFraction(0.5f, 1.0f), 0.875f, "deroule : ease-out a mi-course");
	CheckNear(WorldMapUnrollFraction(0.1f, 0.0f), 1.0f, "duree nulle : immediat");
	{
		float prev = -1.0f;
		bool monotone = true;
		for (int i = 0; i <= 35; ++i)
		{
			const float f = WorldMapUnrollFraction(static_cast<float>(i) * 0.01f, kWorldMapUnrollSeconds);
			monotone = monotone && f >= prev;
			prev = f;
		}
		Check(monotone, "deroule : croissant");
	}

	// Hauteur : de minH à fullH ; minH plus grand que fullH borné.
	CheckNear(WorldMapUnrollHeight(0.0f, 60.0f, 600.0f), 60.0f, "hauteur : depart = en-tete");
	CheckNear(WorldMapUnrollHeight(1.0f, 60.0f, 600.0f), 600.0f, "hauteur : fin = pleine");
	CheckNear(WorldMapUnrollHeight(0.5f, 60.0f, 600.0f), 330.0f, "hauteur : mi-course");
	CheckNear(WorldMapUnrollHeight(0.0f, 800.0f, 600.0f), 600.0f, "hauteur : minH borne a fullH");

	if (s_failCount == 0)
		std::cout << "[OK] world_map_presentation_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
