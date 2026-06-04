// M100.28 — Tests zones : round-trip + transition météo au bord + champs quest.

#include "src/client/world/zones/Zones.h"
#include "src/client/world/zones/ZoneQuery.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace engine::world::zones;
using engine::math::Vec3;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Near(float a, float b, float eps = 1e-2f) { return std::fabs(a - b) <= eps; }

	GameplayZone Square(ZoneType type, const char* name, float side)
	{
		GameplayZone z; z.type = type; z.name = name;
		z.polygon = { Vec3(0,0,0), Vec3(side,0,0), Vec3(side,0,side), Vec3(0,0,side) };
		return z;
	}

	void Test_Zones_Roundtrip()
	{
		std::vector<GameplayZone> zones;
		zones.push_back(Square(ZoneType::SafeZone, "spawn", 20.0f));
		GameplayZone wo = Square(ZoneType::WeatherOverride, "storm", 30.0f);
		wo.weatherType = 1; wo.weatherBlendT = 0.4f; wo.transitionMarginMeters = 5.0f;
		zones.push_back(wo);
		GameplayZone qt = Square(ZoneType::QuestTrigger, "q_intro", 10.0f);
		qt.questId = 7777;
		zones.push_back(qt);

		auto bytes = SaveZonesBin(zones);
		std::vector<GameplayZone> out; std::string err;
		REQUIRE(LoadZonesBin(bytes, out, err));
		REQUIRE(out.size() == 3);
		if (out.size() == 3)
		{
			REQUIRE(out[0].type == ZoneType::SafeZone && out[0].name == "spawn");
			REQUIRE(out[0].polygon.size() == 4);
			REQUIRE(out[1].type == ZoneType::WeatherOverride && out[1].weatherType == 1);
			REQUIRE(Near(out[1].weatherBlendT, 0.4f));
		}
	}

	void Test_QuestTrigger_FieldsPreserved()
	{
		GameplayZone qt = Square(ZoneType::QuestTrigger, "q", 10.0f); qt.questId = 4242;
		auto bytes = SaveZonesBin({ qt });
		std::vector<GameplayZone> out; std::string err;
		REQUIRE(LoadZonesBin(bytes, out, err));
		REQUIRE(out.size() == 1);
		if (out.size() == 1)
		{
			REQUIRE(out[0].type == ZoneType::QuestTrigger);
			REQUIRE(out[0].questId == 4242);
		}
	}

	void Test_WeatherOverride_TransitionAtBorder()
	{
		GameplayZone wo = Square(ZoneType::WeatherOverride, "storm", 20.0f);
		wo.transitionMarginMeters = 5.0f;
		REQUIRE(Near(WeatherOverrideBlend(wo, 10.0f, 10.0f), 1.0f)); // centre (dist 10 > margin)
		REQUIRE(Near(WeatherOverrideBlend(wo, 10.0f, 2.0f), 0.4f));  // 2 m du bord → 2/5
		REQUIRE(Near(WeatherOverrideBlend(wo, 10.0f, 0.0f), 0.0f));  // sur le bord
		REQUIRE(WeatherOverrideBlend(wo, 10.0f, -1.0f) == 0.0f);     // dehors
	}
}

int main()
{
	Test_Zones_Roundtrip();
	Test_QuestTrigger_FieldsPreserved();
	Test_WeatherOverride_TransitionAtBorder();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] ZonesTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] ZonesTests: %d échec(s)\n", g_failed);
	return g_failed;
}
