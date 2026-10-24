// Préréglages de distance de vue (render.quality_preset) : défaut Moyenne,
// distances strictement croissantes de Basse à Ultra, bornage des valeurs hors plage.

#include "src/client/settings/ViewDistancePreset.h"

#include <cstdio>

using engine::client::ResolveViewDistancePreset;

namespace
{
	int g_fail = 0;
	void check(bool cond, const char* msg)
	{
		if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; }
	}
}

int main()
{
	check(engine::client::kDefaultQualityPreset == 1, "defaut = Moyenne");

	for (int p = 0; p < 3; ++p)
	{
		const auto a = ResolveViewDistancePreset(p);
		const auto b = ResolveViewDistancePreset(p + 1);
		check(a.cameraFarM < b.cameraFarM, "camera : croissante");
		check(a.terrainDrawM < b.terrainDrawM, "terrain : croissant");
		check(a.cellRenderRadiusM < b.cellRenderRadiusM, "rayon de rendu : croissant");
		check(a.scatterScale < b.scatterScale, "decor : croissant");
	}
	for (int p = 0; p <= 3; ++p)
	{
		const auto s = ResolveViewDistancePreset(p);
		// Le terrain ne doit pas être dessiné au-delà du plan lointain.
		check(s.terrainDrawM <= s.cameraFarM, "terrain <= plan lointain");
		check(s.scatterScale > 0.0f, "facteur decor > 0");
	}
	check(ResolveViewDistancePreset(-5).cameraFarM == ResolveViewDistancePreset(0).cameraFarM, "borne basse");
	check(ResolveViewDistancePreset(99).cameraFarM == ResolveViewDistancePreset(3).cameraFarM, "borne haute");

	if (g_fail == 0) std::printf("view_distance_preset_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
