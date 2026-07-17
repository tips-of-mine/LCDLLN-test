/// Tests unitaires CPU du générateur de bruit des nuages volumétriques
/// (chantier ciel 2026-07-17, PR A).
///
/// Pas de Vulkan ni de GPU — la suite tourne sous ctest Linux. On vérifie :
///   - Déterminisme : même seed → mêmes octets ; seeds différents → données
///     différentes.
///   - Périodicité : TileableWorley et TileablePerlinFbm valent la même
///     chose en x et x+1 (le sampler REPEAT exige un tuilage sans couture).
///   - Tailles/format des textures générées.
///   - Distribution non dégénérée (ni constante, ni saturée).
///
/// Framework : REQUIRE maison + main monolithique (pattern des autres
/// suites du repo).

#include "src/client/render/clouds/CloudNoiseGenerator.h"

#include <cmath>
#include <cstdio>
#include <cstdint>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::render::clouds::CloudNoiseData;
	using engine::render::clouds::GenerateCloudNoise;
	using engine::render::clouds::kBaseNoiseSize;
	using engine::render::clouds::kDetailNoiseSize;
	using engine::render::clouds::TileablePerlinFbm;
	using engine::render::clouds::TileableWorley;

	/// Test : tailles et format des textures générées.
	void Test_Generate_SizesAndFormat()
	{
		const CloudNoiseData data = GenerateCloudNoise(42u);
		const size_t baseExpected =
			static_cast<size_t>(kBaseNoiseSize) * kBaseNoiseSize * kBaseNoiseSize * 4u;
		const size_t detailExpected =
			static_cast<size_t>(kDetailNoiseSize) * kDetailNoiseSize * kDetailNoiseSize * 4u;
		REQUIRE(data.baseRgba.size() == baseExpected);
		REQUIRE(data.detailRgba.size() == detailExpected);
		// Canal A du détail : opaque constant (non utilisé par le shader).
		REQUIRE(data.detailRgba[3] == 255u);
	}

	/// Test : déterminisme — même seed → octets identiques ; seed différent
	/// → au moins un octet différent.
	void Test_Generate_Deterministic()
	{
		const CloudNoiseData a = GenerateCloudNoise(1234u);
		const CloudNoiseData b = GenerateCloudNoise(1234u);
		REQUIRE(a.baseRgba == b.baseRgba);
		REQUIRE(a.detailRgba == b.detailRgba);

		const CloudNoiseData c = GenerateCloudNoise(9999u);
		REQUIRE(a.baseRgba != c.baseRgba);
	}

	/// Test : périodicité des bruits — f(x) == f(x+1) (à epsilon flottant
	/// près) sur un échantillon de points, pour chaque axe.
	void Test_Noises_AreTileable()
	{
		const float kEps = 1e-4f;
		const float pts[][3] = {
			{ 0.13f, 0.57f, 0.91f },
			{ 0.02f, 0.98f, 0.50f },
			{ 0.77f, 0.31f, 0.05f },
		};
		for (const auto& p : pts)
		{
			const float w0 = TileableWorley(p[0], p[1], p[2], 8, 7u);
			REQUIRE(std::fabs(w0 - TileableWorley(p[0] + 1.0f, p[1], p[2], 8, 7u)) < kEps);
			REQUIRE(std::fabs(w0 - TileableWorley(p[0], p[1] + 1.0f, p[2], 8, 7u)) < kEps);
			REQUIRE(std::fabs(w0 - TileableWorley(p[0], p[1], p[2] + 1.0f, 8, 7u)) < kEps);

			const float n0 = TileablePerlinFbm(p[0], p[1], p[2], 4, 4, 7u);
			REQUIRE(std::fabs(n0 - TileablePerlinFbm(p[0] + 1.0f, p[1], p[2], 4, 4, 7u)) < kEps);
			REQUIRE(std::fabs(n0 - TileablePerlinFbm(p[0], p[1] + 1.0f, p[2], 4, 4, 7u)) < kEps);
			REQUIRE(std::fabs(n0 - TileablePerlinFbm(p[0], p[1], p[2] + 1.0f, 4, 4, 7u)) < kEps);
		}
	}

	/// Test : distribution non dégénérée du canal R (forme de base) — ni
	/// constante ni saturée : min/max écartés, moyenne dans une plage large.
	void Test_Generate_NonDegenerateDistribution()
	{
		const CloudNoiseData data = GenerateCloudNoise(42u);
		uint8_t mn = 255u, mx = 0u;
		uint64_t sum = 0u;
		size_t count = 0u;
		for (size_t i = 0; i < data.baseRgba.size(); i += 4u)
		{
			const uint8_t r = data.baseRgba[i];
			mn = (r < mn) ? r : mn;
			mx = (r > mx) ? r : mx;
			sum += r;
			++count;
		}
		REQUIRE(count > 0u);
		const double mean = static_cast<double>(sum) / static_cast<double>(count);
		REQUIRE(mx - mn > 60);           // vraie dynamique
		REQUIRE(mean > 40.0 && mean < 215.0); // ni noir ni blanc uniforme
	}
}

int main()
{
	Test_Generate_SizesAndFormat();
	Test_Generate_Deterministic();
	Test_Noises_AreTileable();
	Test_Generate_NonDegenerateDistribution();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[CloudNoiseGeneratorTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[CloudNoiseGeneratorTests] all tests passed\n");
	return 0;
}
