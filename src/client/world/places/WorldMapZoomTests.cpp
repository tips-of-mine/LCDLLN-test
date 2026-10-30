// FIX.11 — Tests du zoom de la carte du monde (fonctions pures).

#include "src/client/world/places/WorldMapZoom.h"

#include <cmath>
#include <iostream>

namespace
{
	int s_failCount = 0;

	void CheckNear(float actual, float expected, const char* msg)
	{
		if (std::fabs(actual - expected) > 0.01f)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << " : " << actual << " attendu " << expected << std::endl;
		}
	}
}

int main()
{
	using namespace engine::client;
	const float world = 300000.0f; // monde de 300 km de côté

	const float maxZoom = WorldMapMaxZoom(world, 250.0f);
	CheckNear(maxZoom, 1200.0f, "zoom max : 250 m visibles");
	CheckNear(WorldMapZoomForView(world, 2000.0f, maxZoom), 150.0f, "zoom initial : 2 km visibles");
	CheckNear(WorldMapVisibleMeters(world, 150.0f), 2000.0f, "2 km visibles a zoom 150");
	CheckNear(WorldMapZoomForView(world, 100.0f, maxZoom), 1200.0f, "vue plus petite que le minimum : bornee au zoom max");
	CheckNear(WorldMapZoomForView(world, 900000.0f, maxZoom), 1.0f, "vue plus grande que le monde : zoom 1");

	// Petit monde (zone historique de 2 km) : la carte entière tient déjà dans 2 km.
	CheckNear(WorldMapZoomForView(1500.0f, 2000.0f, WorldMapMaxZoom(1500.0f, 250.0f)), 1.0f, "petit monde : zoom 1");
	CheckNear(WorldMapMaxZoom(1500.0f, 250.0f), 6.0f, "petit monde : zoom max 6");

	// Valeurs invalides : repli sur 1.
	CheckNear(WorldMapMaxZoom(0.0f, 250.0f), 1.0f, "taille nulle");
	CheckNear(WorldMapZoomForView(world, 0.0f, maxZoom), 1.0f, "vue nulle");

	// Marqueurs : taille figée au-delà de z = 8.
	CheckNear(WorldMapMarkerZoom(150.0f), 8.0f, "marqueurs bornes a 8");
	CheckNear(WorldMapMarkerZoom(3.0f), 3.0f, "marqueurs inchanges sous 8");

	if (s_failCount == 0)
		std::cout << "[OK] world_map_zoom_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
