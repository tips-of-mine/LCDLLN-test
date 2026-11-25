// src/client/world/water/tests/SeaSwellTests.cpp
// EAU.6 — houle de la mer et jet de rive (SeaSwell) + parité avec water.vert.
// Lancé depuis la racine du dépôt (WORKING_DIRECTORY de lcdlln_add_simple_test).
#include "src/client/world/water/SeaSwell.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	namespace w = engine::world::water;

	bool Near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

	void Test_DepthScale_OffAtShore_FullOffshore()
	{
		REQUIRE(Near(w::SeaSwellDepthScale(-3.0f), 0.0f));
		REQUIRE(Near(w::SeaSwellDepthScale(w::kSeaSwellShallowM), 0.0f));
		REQUIRE(Near(w::SeaSwellDepthScale(w::kSeaSwellFullDepthM), 1.0f));
		REQUIRE(Near(w::SeaSwellDepthScale(w::kSeaDeepWaterM), 1.0f));
		REQUIRE(w::SeaSwellDepthScale(2.0f) > 0.0f && w::SeaSwellDepthScale(2.0f) < 1.0f);
	}

	void Test_SwellHeight_BoundedByAmplitude_AndMoves()
	{
		w::SeaSwellParams p;
		p.amplitudeM = 0.35f;
		float maxAbs = 0.0f;
		bool changes = false;
		for (int i = 0; i < 200; ++i)
		{
			const float x = static_cast<float>(i) * 1.7f, z = static_cast<float>(i) * -2.3f;
			const float h0 = w::SeaSwellHeight(x, z, 10.0f, p);
			const float h1 = w::SeaSwellHeight(x, z, 11.0f, p);
			maxAbs = std::fmax(maxAbs, std::fabs(h0));
			if (std::fabs(h1 - h0) > 1e-3f) changes = true;
		}
		REQUIRE(maxAbs <= p.amplitudeM + 1e-4f);
		REQUIRE(maxAbs > 0.05f);   // houle réellement visible
		REQUIRE(changes);          // la surface bouge dans le temps
		p.amplitudeM = 0.0f;
		REQUIRE(Near(w::SeaSwellHeight(3.0f, 4.0f, 5.0f, p), 0.0f));
	}

	void Test_Surge_OnlyNearShore_AndBounded()
	{
		float maxSurge = 0.0f;
		bool rises = false;
		for (int k = 0; k < 90; ++k)
		{
			const float t = static_cast<float>(k) * 0.1f;
			REQUIRE(Near(w::SeaSurge(0.0f, 0.0f, w::kSeaSurfZoneDepthM + 0.1f, t), 0.0f));  // large : rien
			const float s = w::SeaSurge(0.0f, 0.0f, 0.0f, t);
			REQUIRE(s >= 0.0f);
			maxSurge = std::fmax(maxSurge, s);
			if (s > 0.1f) rises = true;
		}
		REQUIRE(maxSurge <= w::kSeaRunUpM + 1e-5f);
		REQUIRE(rises);
	}

	void Test_SurfaceOffset_IsSwellPlusSurge()
	{
		w::SeaSwellParams p;
		const float x = 12.0f, z = -7.0f, t = 33.3f;
		for (float d : { -0.5f, 0.0f, 1.0f, 3.0f, 50.0f })
			REQUIRE(Near(w::SeaSurfaceOffset(x, z, d, t, p),
				w::SeaSwellHeight(x, z, t, p) * w::SeaSwellDepthScale(d) + w::SeaSurge(x, z, d, t)));
	}

	void Test_BreakIndex_OnePerPeriod_AtShorePeak()
	{
		// Le sommet du jet de rive au rivage tombe sur un changement d'index.
		const float tPeak = w::kSeaSurfPeriodS * 0.25f;
		REQUIRE(w::SeaSurfBreakIndex(tPeak - 0.05f) + 1 == w::SeaSurfBreakIndex(tPeak + 0.05f));
		REQUIRE(Near(std::sin(w::SeaSurfPhase(0.0f, tPeak)), 1.0f, 1e-4f));
		int changes = 0;
		long long last = w::SeaSurfBreakIndex(0.0f);
		for (int k = 1; k <= 900; ++k)
		{
			const long long i = w::SeaSurfBreakIndex(static_cast<float>(k) * 0.1f);
			if (i != last) { ++changes; last = i; }
		}
		REQUIRE(changes == 10);  // 90 s / 9 s
	}

	/// Lit `const float NOM = valeur;` (scalaires) et `const float NOM[4] = float[4](a, b, c, d);`.
	std::map<std::string, std::vector<float>> ReadShaderConstants(const std::string& path)
	{
		std::map<std::string, std::vector<float>> out;
		std::ifstream in(path);
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string src = ss.str();
		const std::regex scalar(R"(const\s+float\s+(\w+)\s*=\s*([-0-9.]+)\s*;)");
		for (auto it = std::sregex_iterator(src.begin(), src.end(), scalar); it != std::sregex_iterator(); ++it)
			out[(*it)[1].str()] = { std::stof((*it)[2].str()) };
		const std::regex array(R"(const\s+float\s+(\w+)\[4\]\s*=\s*float\[4\]\(([^)]*)\)\s*;)");
		for (auto it = std::sregex_iterator(src.begin(), src.end(), array); it != std::sregex_iterator(); ++it)
		{
			std::vector<float> v;
			std::stringstream items((*it)[2].str());
			std::string item;
			while (std::getline(items, item, ','))
				v.push_back(std::stof(item));
			out[(*it)[1].str()] = v;
		}
		return out;
	}

	void Test_Parity_WithWaterVertShader()
	{
		const auto c = ReadShaderConstants("game/data/shaders/water.vert");
		REQUIRE(!c.empty());
		auto scalarIs = [&](const char* name, float expected) {
			const auto it = c.find(name);
			const bool ok = it != c.end() && it->second.size() == 1 && Near(it->second[0], expected);
			if (!ok) std::fprintf(stderr, "  parite water.vert : %s\n", name);
			REQUIRE(ok);
		};
		auto arrayIs = [&](const char* name, const float (&expected)[4]) {
			const auto it = c.find(name);
			bool ok = it != c.end() && it->second.size() == 4;
			for (int i = 0; ok && i < 4; ++i) ok = Near(it->second[static_cast<size_t>(i)], expected[i]);
			if (!ok) std::fprintf(stderr, "  parite water.vert : %s\n", name);
			REQUIRE(ok);
		};
		scalarIs("kSeaRunUpM", w::kSeaRunUpM);
		scalarIs("kSeaSurfPeriodS", w::kSeaSurfPeriodS);
		scalarIs("kSeaSurfPhasePerMeter", w::kSeaSurfPhasePerMeter);
		scalarIs("kSeaSurfZoneDepthM", w::kSeaSurfZoneDepthM);
		scalarIs("kSeaSwellShallowM", w::kSeaSwellShallowM);
		scalarIs("kSeaSwellFullDepthM", w::kSeaSwellFullDepthM);
		scalarIs("kSwellWeightSum", w::kSeaSwellWeightSum);
		arrayIs("kSwellLen", w::kSeaSwellLengthM);
		arrayIs("kSwellWeight", w::kSeaSwellWeight);
		arrayIs("kSwellAngle", w::kSeaSwellAngle);
	}

	void Test_Parity_WithWaterFragShader()
	{
		// water.frag recalcule la phase du jet de rive (écume des vagues de bord).
		const auto c = ReadShaderConstants("game/data/shaders/water.frag");
		auto scalarIs = [&](const char* name, float expected) {
			const auto it = c.find(name);
			const bool ok = it != c.end() && it->second.size() == 1 && Near(it->second[0], expected);
			if (!ok) std::fprintf(stderr, "  parite water.frag : %s\n", name);
			REQUIRE(ok);
		};
		scalarIs("kSeaSwellReferenceAmplitudeM", w::kSeaSwellReferenceAmplitudeM);
		scalarIs("kSeaSurfPeriodS", w::kSeaSurfPeriodS);
		scalarIs("kSeaSurfPhasePerMeter", w::kSeaSurfPhasePerMeter);
		scalarIs("kSeaSurfZoneDepthM", w::kSeaSurfZoneDepthM);
	}
}

int main()
{
	Test_DepthScale_OffAtShore_FullOffshore();
	Test_SwellHeight_BoundedByAmplitude_AndMoves();
	Test_Surge_OnlyNearShore_AndBounded();
	Test_SurfaceOffset_IsSwellPlusSurge();
	Test_BreakIndex_OnePerPeriod_AtShorePeak();
	Test_Parity_WithWaterVertShader();
	Test_Parity_WithWaterFragShader();

	if (g_failed == 0)
	{
		std::printf("All SeaSwell tests passed.\n");
		return 0;
	}
	std::fprintf(stderr, "%d test(s) failed.\n", g_failed);
	return 1;
}
