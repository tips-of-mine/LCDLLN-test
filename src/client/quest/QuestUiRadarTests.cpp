// Tests du helper pur QuestUi WorldToRadarUv — projection d'une position
// monde (plan XZ) vers les coordonnées UV du radar minimap SP3, centré sur
// le joueur. Fonction libre, sans dépendance ImGui ni Config, donc testable
// directement (voir doc dans QuestUi.h).

#include "src/client/quest/QuestUi.h"

#include <cmath>
#include <iostream>

using engine::client::WorldToRadarUv;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* label)
	{
		if (!condition)
		{
			std::cerr << "[FAIL] " << label << "\n";
			++g_failures;
		}
	}

	bool NearlyEqual(float a, float b, float epsilon = 0.0001f)
	{
		return std::abs(a - b) <= epsilon;
	}
}

int main()
{
	// Joueur en (0,0), rayon 60 : POI au centre exact -> (0.5,0.5), pas off-radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = true;
		WorldToRadarUv(0.0f, 0.0f, 0.0f, 0.0f, 60.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.5f), "POI(0,0) -> u==0.5");
		Check(NearlyEqual(v, 0.5f), "POI(0,0) -> v==0.5");
		Check(!offRadar, "POI(0,0) -> pas off-radar");
	}

	// Joueur en (0,0), rayon 60 : POI (30,0) -> u=0.75, pas off-radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = true;
		WorldToRadarUv(30.0f, 0.0f, 0.0f, 0.0f, 60.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.75f), "POI(30,0) -> u==0.75");
		Check(NearlyEqual(v, 0.5f), "POI(30,0) -> v==0.5");
		Check(!offRadar, "POI(30,0) -> pas off-radar");
	}

	// Joueur en (0,0), rayon 60 : POI (120,0) -> off-radar, u clampé <= 1.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = false;
		WorldToRadarUv(120.0f, 0.0f, 0.0f, 0.0f, 60.0f, u, v, offRadar);
		Check(offRadar, "POI(120,0) -> off-radar");
		Check(u <= 1.0f, "POI(120,0) -> u clampe <= 1");
		Check(NearlyEqual(u, 1.0f), "POI(120,0) -> u clampe == 1 (borne du cadre)");
	}

	// Joueur décentré (10,-5) : le POI au même point que le joueur reste au centre du radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = true;
		WorldToRadarUv(10.0f, -5.0f, 10.0f, -5.0f, 60.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.5f), "POI==joueur decentre -> u==0.5");
		Check(NearlyEqual(v, 0.5f), "POI==joueur decentre -> v==0.5");
		Check(!offRadar, "POI==joueur decentre -> pas off-radar");
	}

	// Garde radiusM <= 0 : pas de division par zéro, sort au centre en off-radar.
	{
		float u = 0.0f;
		float v = 0.0f;
		bool offRadar = false;
		WorldToRadarUv(5.0f, 5.0f, 0.0f, 0.0f, 0.0f, u, v, offRadar);
		Check(NearlyEqual(u, 0.5f), "radiusM<=0 -> u==0.5 (garde)");
		Check(NearlyEqual(v, 0.5f), "radiusM<=0 -> v==0.5 (garde)");
		Check(offRadar, "radiusM<=0 -> off-radar force a true");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
