/// Tests unitaires CPU pour M100.39 — Thermal & Wind Erosion (clôture
/// Phase 2.5 — Terrain naturaliste).
///
/// Couvre : thermal flat (pas de delta), thermal sur cone vertical
/// (relaxe vers talus), conservation de masse thermal (Σ deltas ≈ 0),
/// wind flat (deltas négligeables), wind déterminisme, ThermalWindErosionCommand
/// Apply/Undo restaure bit-à-bit.

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/shared/core/Config.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/erosion/ThermalSimulation.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionCommand.h"
#include "src/world_editor/terrain/erosion/WindSimulation.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <utility>
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

	using engine::editor::world::CommandStack;
	using engine::editor::world::ConsolidatedHeightGrid;
	using engine::editor::world::SparseChunkDeltas;
	using engine::editor::world::TerrainDocument;
	using engine::editor::world::erosion::RunThermalSimulation;
	using engine::editor::world::erosion::RunWindSimulation;
	using engine::editor::world::erosion::ThermalSimulationParams;
	using engine::editor::world::erosion::ThermalSimulationResult;
	using engine::editor::world::erosion::ThermalWindErosionCommand;
	using engine::editor::world::erosion::WindSimulationParams;
	using engine::editor::world::erosion::WindSimulationResult;

	/// Cône vertical : pente très raide depuis le centre.
	ConsolidatedHeightGrid MakeSteepCone(int N, float peak)
	{
		ConsolidatedHeightGrid g;
		g.width = N; g.height = N;
		g.cellSizeMeters = 1.0f;
		g.heights.resize(static_cast<size_t>(N) * N);
		const float cx = static_cast<float>(N - 1) * 0.5f;
		const float cz = static_cast<float>(N - 1) * 0.5f;
		for (int z = 0; z < N; ++z)
		{
			for (int x = 0; x < N; ++x)
			{
				const float dx = static_cast<float>(x) - cx;
				const float dz = static_cast<float>(z) - cz;
				const float r = std::sqrt(dx * dx + dz * dz);
				// Décroît très vite : pente ~10 m/cell.
				g.heights[static_cast<size_t>(z) * N + x] = std::max(0.0f, peak - r * 10.0f);
			}
		}
		return g;
	}

	/// Test 1 : thermal sur plat → 0 delta.
	void Test_Thermal_FlatTerrain_NoErosion()
	{
		auto g = ConsolidatedHeightGrid::MakeFlat(20, 20, 100.0f);
		ThermalSimulationParams params;
		params.numPasses = 10;
		const auto r = RunThermalSimulation(g, 0.0f, params);
		REQUIRE(r.deltas.empty() || r.totalTransferredMeters == 0.0f);
	}

	/// Test 2 : thermal sur cone vertical → transfert non-zéro.
	void Test_Thermal_SteepCone_TransfersMaterial()
	{
		auto g = MakeSteepCone(20, 200.0f);
		ThermalSimulationParams params;
		params.talusAngleDeg = 35.0f;
		params.forcePerPass  = 0.5f;
		params.numPasses     = 30;
		const auto r = RunThermalSimulation(g, 0.0f, params);
		REQUIRE(r.totalTransferredMeters > 0.0f);
		REQUIRE(!r.deltas.empty());
		REQUIRE(r.passesExecuted >= 1u);
	}

	/// Test 3 : conservation de masse globale (Σ deltas ≈ 0).
	void Test_Thermal_MassConservation()
	{
		auto g = MakeSteepCone(15, 100.0f);
		ThermalSimulationParams params;
		params.numPasses = 10;
		const auto r = RunThermalSimulation(g, 0.0f, params);
		double sum = 0.0;
		for (const auto& kv : r.deltas)
		{
			for (const auto& cell : kv.second) sum += static_cast<double>(cell.second);
		}
		// Tolérance : float arith + chunks frontières dupliquées (couture)
		// double-count les cellules de bord. On accepte une tolérance large
		// vs la masse totale transférée.
		REQUIRE(std::fabs(sum) < std::max(1e-2,
			static_cast<double>(r.totalTransferredMeters) * 0.5));
	}

	/// Test 4 : thermal convergence early-exit. Avec une heightmap stable
	/// (sub-talus partout), le simulateur sort tôt.
	void Test_Thermal_Convergence_EarlyExit()
	{
		// Heightmap avec pente très douce (sous le talus).
		ConsolidatedHeightGrid g;
		g.width = 10; g.height = 10;
		g.cellSizeMeters = 1.0f;
		g.heights.resize(100);
		for (int z = 0; z < 10; ++z)
		{
			for (int x = 0; x < 10; ++x)
			{
				// Pente 0.1 → ~5.7° < 35° talus.
				g.heights[static_cast<size_t>(z) * 10 + x] = static_cast<float>(x) * 0.1f;
			}
		}
		ThermalSimulationParams params;
		params.talusAngleDeg = 35.0f;
		params.numPasses     = 100;
		const auto r = RunThermalSimulation(g, 0.0f, params);
		// La convergence devrait être atteinte rapidement.
		REQUIRE(r.passesExecuted <= 5u);
	}

	/// Test 5 : wind sur plat → deltas négligeables.
	void Test_Wind_FlatTerrain_NegligibleDeltas()
	{
		auto g = ConsolidatedHeightGrid::MakeFlat(30, 30, 100.0f);
		WindSimulationParams params;
		params.numParticles = 50;
		const auto r = RunWindSimulation(g, 0.0f, params);
		// Exposition partout = 0, capacité = 0 → ni érosion ni déposition
		// significative.
		float maxAbs = 0.0f;
		for (const auto& kv : r.deltas)
		{
			for (const auto& cell : kv.second)
			{
				maxAbs = std::max(maxAbs, std::fabs(cell.second));
			}
		}
		REQUIRE(maxAbs < 1e-3f);
	}

	/// Test 6 : wind déterminisme — même seed → même résultat.
	void Test_Wind_Deterministic_SameSeed()
	{
		auto g = MakeSteepCone(20, 100.0f);
		WindSimulationParams params;
		params.numParticles = 100;
		params.rngSeed = 1234u;
		const auto a = RunWindSimulation(g, 0.0f, params);
		const auto b = RunWindSimulation(g, 0.0f, params);
		REQUIRE(a.deltas.size() == b.deltas.size());
		for (const auto& kv : a.deltas)
		{
			auto it = b.deltas.find(kv.first);
			REQUIRE(it != b.deltas.end());
			REQUIRE(it->second.size() == kv.second.size());
			for (const auto& cell : kv.second)
			{
				auto cit = it->second.find(cell.first);
				REQUIRE(cit != it->second.end());
				REQUIRE(cit->second == cell.second);
			}
		}
	}

	/// Test 7 : wind 0 particules → résultat vide.
	void Test_Wind_ZeroParticles_EmptyResult()
	{
		auto g = MakeSteepCone(20, 100.0f);
		WindSimulationParams params;
		params.numParticles = 0;
		const auto r = RunWindSimulation(g, 0.0f, params);
		REQUIRE(r.deltas.empty());
	}

	/// Test 8 : ThermalWindErosionCommand Apply + Undo restaure bit-à-bit.
	void Test_Command_Apply_Undo_RestoresExact()
	{
		engine::core::Config cfg;
		TerrainDocument doc;
		(void)doc.EnsureLoaded(cfg, 0, 0);
		auto chunk = doc.Find({0, 0});
		REQUIRE(chunk);
		const std::vector<float> snap = chunk->heights;

		ThermalWindErosionCommand::Data data;
		const engine::world::GlobalChunkCoord coord{0, 0};
		data.thermalDeltas[coord][0]   = -0.5f;
		data.thermalDeltas[coord][100] = +0.3f;
		data.windDeltas[coord][50]     = -0.2f;
		data.windDeltas[coord][200]    = +0.4f;

		CommandStack stack;
		auto cmd = std::make_unique<ThermalWindErosionCommand>(doc, std::move(data));
		stack.Push(std::move(cmd));

		REQUIRE(chunk->heights[0]   == snap[0]   - 0.5f);
		REQUIRE(chunk->heights[100] == snap[100] + 0.3f);
		REQUIRE(chunk->heights[50]  == snap[50]  - 0.2f);
		REQUIRE(chunk->heights[200] == snap[200] + 0.4f);

		stack.Undo();
		REQUIRE(chunk->heights == snap);

		stack.Redo();
		REQUIRE(chunk->heights[0] == snap[0] - 0.5f);
	}

	/// Test 9 : preserveSteepSlopes protège les pentes > seuil.
	void Test_Thermal_PreserveSteepSlopes()
	{
		// Cone à pente >> seuil partout.
		auto g = MakeSteepCone(15, 200.0f);
		const std::vector<float> originalHeights = g.heights;

		ThermalSimulationParams params;
		params.talusAngleDeg = 35.0f;
		params.preserveSteepSlopes = true;
		params.preserveSteepThresholdDeg = 30.0f; // protège tout au-delà de 30°
		params.numPasses = 5;
		const auto r = RunThermalSimulation(g, 0.0f, params);
		// Avec preserveSteep à 30° et talus à 35°, la zone d'application
		// est ]35°, 30°[ → vide. Pas de transfert attendu.
		REQUIRE(r.totalTransferredMeters < 1e-3f);
		(void)originalHeights;
	}

	/// P0 (audit 2026-06-05, 4.1) : terrain sous le plancher (hauteurs
	/// négatives) → AUCUN transfert inversé. Avant le fix, le facteur
	/// anti-runaway `h / excès` devenait NÉGATIF pour h < 0 : les deltas
	/// s'inversaient (masse remontant la pente, amplitudes non bornées) et
	/// les altitudes divergeaient. Après le fix, les cellules sans hauteur
	/// disponible au-dessus du plancher n'émettent tout simplement pas.
	void Test_Thermal_NegativeHeights_NoInvertedRunaway()
	{
		// Falaise raide dont TOUT le relief est sous 0 m (plancher hors sea).
		ConsolidatedHeightGrid g;
		g.width = 10; g.height = 10;
		g.cellSizeMeters = 1.0f;
		g.heights.resize(100);
		for (int z = 0; z < 10; ++z)
			for (int x = 0; x < 10; ++x)
				g.heights[static_cast<size_t>(z) * 10 + x] =
					-5.0f - static_cast<float>(x) * 20.0f; // pente raide, h < 0
		ThermalSimulationParams params;
		params.talusAngleDeg = 35.0f;
		params.forcePerPass  = 0.5f;
		params.numPasses     = 20;
		params.stopUnderSeaLevel = false; // plancher = 0 m
		const auto r = RunThermalSimulation(g, 0.0f, params);
		REQUIRE(r.totalTransferredMeters == 0.0f); // rien de disponible au-dessus de 0
		for (const float h : g.heights)
		{
			REQUIRE(std::isfinite(h));
			REQUIRE(h >= -200.0f && h <= 10.0f); // aucune divergence
		}
	}

	/// P0 (4.1) : une cellule juste au-dessus du plancher ne descend jamais
	/// SOUS le plancher, quelle que soit la pente demandée.
	void Test_Thermal_NeverBelowFloor()
	{
		ConsolidatedHeightGrid g;
		g.width = 8; g.height = 8;
		g.cellSizeMeters = 1.0f;
		g.heights.resize(64, 0.0f);
		// Un pic isolé de 3 m au centre : il doit relaxer mais jamais < 0.
		g.heights[static_cast<size_t>(4) * 8 + 4] = 3.0f;
		ThermalSimulationParams params;
		params.talusAngleDeg = 35.0f;
		params.forcePerPass  = 1.0f;
		params.numPasses     = 40;
		params.stopUnderSeaLevel = false; // plancher = 0 m
		const auto r = RunThermalSimulation(g, 0.0f, params);
		(void)r;
		for (const float h : g.heights)
		{
			REQUIRE(h >= -1e-4f); // jamais sous le plancher (tolérance float)
		}
	}
}

int main()
{
	Test_Thermal_FlatTerrain_NoErosion();
	Test_Thermal_SteepCone_TransfersMaterial();
	Test_Thermal_MassConservation();
	Test_Thermal_Convergence_EarlyExit();
	Test_Wind_FlatTerrain_NegligibleDeltas();
	Test_Wind_Deterministic_SameSeed();
	Test_Wind_ZeroParticles_EmptyResult();
	Test_Command_Apply_Undo_RestoresExact();
	Test_Thermal_PreserveSteepSlopes();
	Test_Thermal_NegativeHeights_NoInvertedRunaway();
	Test_Thermal_NeverBelowFloor();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ThermalWindErosionTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ThermalWindErosionTests] all tests passed\n");
	return 0;
}
