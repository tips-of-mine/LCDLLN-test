// M100.21 — Tests collecteur d'influences (portée, troncature, flexion). Headless.

#include "src/client/world/foliage/EntityInfluenceCollector.h"

#include <cstdio>
#include <vector>

using namespace engine::world::foliage;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	void Test_Collector_FiltersEntitiesByRange30m()
	{
		std::vector<EntityCandidate> ents;
		ents.push_back({ 5.0f, 0.0f, 1.0f, 1.5f });    // dist 5 → gardé
		ents.push_back({ 29.0f, 0.0f, 1.0f, 1.5f });   // dist 29 → gardé
		ents.push_back({ 31.0f, 0.0f, 1.0f, 1.5f });   // dist 31 → rejeté
		ents.push_back({ 0.0f, 100.0f, 1.0f, 1.5f });  // loin → rejeté
		auto inf = CollectEntityInfluences(0.0f, 0.0f, ents);
		REQUIRE(inf.size() == 2);
	}

	void Test_Collector_TruncatesAt32()
	{
		std::vector<EntityCandidate> ents;
		for (int i = 0; i < 50; ++i)
			ents.push_back({ static_cast<float>(i) * 0.5f, 0.0f, 1.0f, 1.5f }); // tous < 30 m (i<60)
		auto inf = CollectEntityInfluences(0.0f, 0.0f, ents);
		REQUIRE(inf.size() == 32);
		// Les plus proches d'abord : la première influence est la plus proche.
		if (inf.size() == 32) REQUIRE(inf[0].positionX <= inf[31].positionX);
	}

	void Test_Shader_FlexionMonotoneWithDistance()
	{
		EntityInfluence inf; inf.positionX = 0.0f; inf.positionZ = 0.0f; inf.radiusMeters = 2.0f; inf.falloffPower = 1.5f;
		const float fNear = ComputeFlexionMagnitude(inf, 0.2f, 0.0f, 1.0f); // proche du centre
		const float fFar = ComputeFlexionMagnitude(inf, 1.8f, 0.0f, 1.0f);  // proche du bord
		const float fOut = ComputeFlexionMagnitude(inf, 2.5f, 0.0f, 1.0f);  // hors rayon
		REQUIRE(fNear > fFar);
		REQUIRE(fFar > 0.0f);
		REQUIRE(fOut == 0.0f);
		// Hauteur 0 → pas de flexion (racines fixes).
		REQUIRE(ComputeFlexionMagnitude(inf, 0.2f, 0.0f, 0.0f) == 0.0f);
	}
}

int main()
{
	Test_Collector_FiltersEntitiesByRange30m();
	Test_Collector_TruncatesAt32();
	Test_Shader_FlexionMonotoneWithDistance();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] EntityInfluenceTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] EntityInfluenceTests: %d échec(s)\n", g_failed);
	return g_failed;
}
