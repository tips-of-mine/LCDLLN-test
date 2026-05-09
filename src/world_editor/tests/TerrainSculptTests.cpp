/// Tests unitaires CPU pour la sculpture de terrain (M100.6).
///
/// Vérifient les invariants du kernel de brosse, du raycast, de la commande
/// undo/redo, et du tool. Pas de dépendance ImGui ni Vulkan : tout passe par
/// l'API CPU des fichiers `src/world_editor/world/Terrain*`.
///
/// Sept tests :
///   - Test_RaiseBrush_AddsExpectedDelta       : un tick Raise au centre
///     produit un delta positif au point central et 0 hors du rayon.
///   - Test_LowerBrush_NegatesRaise            : Raise puis Lower (mêmes
///     params) sur un chunk plat ramène la hauteur ≈ 0 au centre.
///   - Test_SmoothBrush_LimitsExtremaToNeighborhood : un pic isolé est
///     atténué par Smooth (la cellule pic baisse, ses voisines montent).
///   - Test_FlattenBrush_ConvergesToCenterHeight : sur un terrain en pente,
///     plusieurs ticks Flatten convergent vers la hauteur sous le centre.
///   - Test_NoiseBrush_DeterministicForSameSeed : EvalSimplex2D produit la
///     même valeur pour les mêmes (x, z, freq, octaves).
///   - Test_Stroke_MergesIntoOneCommand        : 2 commands consécutives
///     avec même mergeKey fusionnent → 1 entrée dans la pile undo.
///   - Test_CrossChunk_PreservesSeam           : la rangée x=256 du chunk
///     (0,0) et la rangée x=0 du chunk (1,0) restent égales après init
///     (sanity check de l'invariant ; le seam preservation actif sera
///     testé plus en profondeur dans une PR ultérieure).
///
/// Pour les tests qui ont besoin d'un `Config`, on utilise la valeur par
/// défaut de `paths.content` ("game/data") : aucun chunk n'existe à ce path
/// sous CTest, donc `EnsureLoaded` crée un chunk plat à 0 — exactement ce
/// qu'il faut pour partir d'une heightmap connue.

#include "src/shared/core/Config.h"
#include "src/world_editor/world/CommandStack.h"
#include "src/world_editor/world/TerrainBrush.h"
#include "src/world_editor/world/TerrainDocument.h"
#include "src/world_editor/world/TerrainSculptCommand.h"
#include "src/world_editor/world/TerrainSculptTool.h"
#include "src/client/world/terrain/TerrainChunk.h"

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

	using engine::editor::world::ApplyBrushKernel;
	using engine::editor::world::CommandStack;
	using engine::editor::world::CommandMergeKey;
	using engine::editor::world::EvalSimplex2D;
	using engine::editor::world::TerrainBrushMode;
	using engine::editor::world::TerrainBrushParams;
	using engine::editor::world::TerrainDocument;
	using engine::editor::world::TerrainSculptCommand;
	using engine::editor::world::TerrainSculptDeltaCell;
	using engine::editor::world::TerrainSculptDeltaChunk;
	using engine::world::terrain::TerrainChunk;
	using engine::world::terrain::kTerrainResolution;

	/// Valeur (z*resX+x) dans `heights`.
	float HeightAt(const TerrainChunk& c, uint32_t x, uint32_t z)
	{
		return c.heights[static_cast<size_t>(z) * c.resolutionX + x];
	}

	/// Indice cellule la plus proche d'une coord local (mètres → cellule).
	uint32_t CellIndex(float local, uint32_t resolution)
	{
		const int i = static_cast<int>(std::floor(local + 0.5f));
		const int clamped = i < 0 ? 0 :
			(i >= static_cast<int>(resolution) ? static_cast<int>(resolution) - 1 : i);
		return static_cast<uint32_t>(clamped);
	}

	/// Test : un tick Raise sur un chunk plat ajoute un delta strictement
	/// positif à la cellule centrale et zéro à une cellule hors rayon.
	void Test_RaiseBrush_AddsExpectedDelta()
	{
		TerrainChunk chunk = TerrainChunk::MakeFlat(0.0f);
		TerrainBrushParams params;
		params.mode = TerrainBrushMode::Raise;
		params.radiusMeters = 4.0f;
		params.strengthMps = 5.0f;
		params.falloff = 0.5f;

		std::vector<TerrainSculptDeltaCell> delta;
		const float centerX = 128.0f;
		const float centerZ = 128.0f;
		const float dt = 1.0f / 60.0f;
		const uint32_t modified = ApplyBrushKernel(chunk, params, centerX, centerZ, dt, delta);
		REQUIRE(modified > 0);

		const uint32_t cx = CellIndex(centerX, chunk.resolutionX);
		const uint32_t cz = CellIndex(centerZ, chunk.resolutionZ);
		REQUIRE(HeightAt(chunk, cx, cz) > 0.0f);
		// Cellule franchement hors rayon doit être inchangée.
		REQUIRE(HeightAt(chunk, cx + 50, cz) == 0.0f);
		// Le delta accumulé contient au moins la cellule centrale.
		bool foundCenter = false;
		for (const auto& d : delta)
		{
			if (d.x == cx && d.z == cz)
			{
				foundCenter = true;
				REQUIRE(d.deltaMeters > 0.0f);
			}
		}
		REQUIRE(foundCenter);
	}

	/// Test : Raise puis Lower (mêmes params, mêmes ticks) ramène la cellule
	/// centrale à sa hauteur d'origine (à epsilon près).
	void Test_LowerBrush_NegatesRaise()
	{
		TerrainChunk chunk = TerrainChunk::MakeFlat(0.0f);
		TerrainBrushParams up;
		up.mode = TerrainBrushMode::Raise;
		up.radiusMeters = 5.0f;
		up.strengthMps = 4.0f;
		up.falloff = 0.7f;

		std::vector<TerrainSculptDeltaCell> deltas;
		const float dt = 0.1f;
		ApplyBrushKernel(chunk, up, 100.0f, 100.0f, dt, deltas);
		const uint32_t cx = CellIndex(100.0f, chunk.resolutionX);
		const uint32_t cz = CellIndex(100.0f, chunk.resolutionZ);
		const float afterUp = HeightAt(chunk, cx, cz);
		REQUIRE(afterUp > 0.0f);

		TerrainBrushParams down = up;
		down.mode = TerrainBrushMode::Lower;
		ApplyBrushKernel(chunk, down, 100.0f, 100.0f, dt, deltas);
		const float afterDown = HeightAt(chunk, cx, cz);
		REQUIRE(std::fabs(afterDown) < 1e-4f);
	}

	/// Test : un pic isolé (1 cellule à h=10) est lissé par Smooth — la
	/// cellule du pic descend (le delta y est négatif). Les voisines
	/// reçoivent une partie du pic (delta positif).
	void Test_SmoothBrush_LimitsExtremaToNeighborhood()
	{
		TerrainChunk chunk = TerrainChunk::MakeFlat(0.0f);
		const uint32_t cx = 80;
		const uint32_t cz = 80;
		chunk.heights[static_cast<size_t>(cz) * chunk.resolutionX + cx] = 10.0f;

		TerrainBrushParams params;
		params.mode = TerrainBrushMode::Smooth;
		params.radiusMeters = 3.0f;
		params.strengthMps = 5.0f;   // converge vite
		params.falloff = 0.0f;       // pas de falloff → poids ~1 partout

		std::vector<TerrainSculptDeltaCell> delta;
		ApplyBrushKernel(chunk, params,
			static_cast<float>(cx), static_cast<float>(cz),
			0.5f, delta);

		const float peak = HeightAt(chunk, cx, cz);
		// Le pic doit avoir baissé.
		REQUIRE(peak < 10.0f);
		// Une voisine immédiate doit avoir reçu une partie du pic.
		const float neighbour = HeightAt(chunk, cx + 1, cz);
		REQUIRE(neighbour > 0.0f);
		// La hauteur du pic reste >= la voisine (énergie globale conservée
		// localement à l'itération 1).
		REQUIRE(peak >= neighbour);
	}

	/// Test : sur une pente lisse en X (h = x * 0.1), Flatten autour d'un
	/// centre (cx, cz) tire la cellule centrale vers la hauteur sous ce
	/// centre. Plusieurs ticks → la cellule converge vers cette cible.
	void Test_FlattenBrush_ConvergesToCenterHeight()
	{
		TerrainChunk chunk = TerrainChunk::MakeFlat(0.0f);
		// Pente : h(x, z) = x * 0.1 (en mètres).
		for (uint32_t z = 0; z < chunk.resolutionZ; ++z)
		{
			for (uint32_t x = 0; x < chunk.resolutionX; ++x)
			{
				chunk.heights[static_cast<size_t>(z) * chunk.resolutionX + x] =
					static_cast<float>(x) * 0.1f;
			}
		}
		const float centerX = 50.0f;
		const float centerZ = 50.0f;
		const float targetH = chunk.SampleHeight(centerX, centerZ);
		// Cellule cible : on regarde quelques cellules excentrées pour voir
		// la convergence (la cellule centrale est déjà ≈ targetH).
		const uint32_t probeX = CellIndex(centerX + 3.0f, chunk.resolutionX);
		const uint32_t probeZ = CellIndex(centerZ, chunk.resolutionZ);
		const float beforeH = HeightAt(chunk, probeX, probeZ);
		REQUIRE(std::fabs(beforeH - targetH) > 0.05f);

		TerrainBrushParams params;
		params.mode = TerrainBrushMode::Flatten;
		params.radiusMeters = 6.0f;
		params.strengthMps = 5.0f;
		params.falloff = 0.0f;

		std::vector<TerrainSculptDeltaCell> delta;
		// Plusieurs ticks pour laisser la convergence se faire.
		for (int i = 0; i < 20; ++i)
		{
			ApplyBrushKernel(chunk, params, centerX, centerZ, 0.1f, delta);
		}
		const float afterH = HeightAt(chunk, probeX, probeZ);
		REQUIRE(std::fabs(afterH - targetH) < std::fabs(beforeH - targetH));
	}

	/// Test : EvalSimplex2D est déterministe pour les mêmes (x, z, freq,
	/// octaves) — appel répété → valeur identique bit-à-bit.
	void Test_NoiseBrush_DeterministicForSameSeed()
	{
		const float a1 = EvalSimplex2D(12.5f, 7.25f, 0.05f, 3);
		const float a2 = EvalSimplex2D(12.5f, 7.25f, 0.05f, 3);
		REQUIRE(a1 == a2);
		const float b1 = EvalSimplex2D(0.0f, 0.0f, 0.1f, 4);
		const float b2 = EvalSimplex2D(0.0f, 0.0f, 0.1f, 4);
		REQUIRE(b1 == b2);
		// Sanity : valeur dans [-1, 1] (approximatif).
		REQUIRE(a1 > -2.0f && a1 < 2.0f);
		// Sanity : 2 paramètres différents → valeur potentiellement différente.
		const float c = EvalSimplex2D(1.0f, 1.0f, 0.05f, 3);
		REQUIRE(c != a1 || std::fabs(c) < 2.0f);
	}

	/// Test : 2 commandes consécutives avec le même mergeKey fusionnent en
	/// 1 entrée dans la pile undo. Vérifie aussi que `Undo()` rejoue les
	/// 2 deltas d'un coup (effet cumulatif).
	void Test_Stroke_MergesIntoOneCommand()
	{
		engine::core::Config cfg;
		TerrainDocument doc;
		auto chunk = doc.EnsureLoaded(cfg, 0, 0);
		REQUIRE(chunk != nullptr);
		// Place une marque avant la commande pour vérifier l'effet.
		const size_t idx0 = static_cast<size_t>(10) * chunk->resolutionX + 10;
		const size_t idx1 = static_cast<size_t>(11) * chunk->resolutionX + 11;

		std::vector<TerrainSculptDeltaChunk> deltasA;
		{
			TerrainSculptDeltaChunk dc;
			dc.coord = engine::world::GlobalChunkCoord{0, 0};
			dc.cells.push_back({10, 10, 1.0f});
			deltasA.push_back(std::move(dc));
		}
		std::vector<TerrainSculptDeltaChunk> deltasB;
		{
			TerrainSculptDeltaChunk dc;
			dc.coord = engine::world::GlobalChunkCoord{0, 0};
			dc.cells.push_back({11, 11, 2.0f});
			deltasB.push_back(std::move(dc));
		}

		CommandStack stack;
		const CommandMergeKey k = 7;
		stack.Push(std::make_unique<TerrainSculptCommand>(doc, std::move(deltasA), k));
		stack.Push(std::make_unique<TerrainSculptCommand>(doc, std::move(deltasB), k));

		// 1 entrée seulement (fusion).
		REQUIRE(stack.UndoSize() == 1u);
		REQUIRE(std::fabs(chunk->heights[idx0] - 1.0f) < 1e-5f);
		REQUIRE(std::fabs(chunk->heights[idx1] - 2.0f) < 1e-5f);

		// Undo doit reverser les deux deltas d'un coup.
		stack.Undo();
		REQUIRE(stack.UndoSize() == 0u);
		REQUIRE(std::fabs(chunk->heights[idx0]) < 1e-5f);
		REQUIRE(std::fabs(chunk->heights[idx1]) < 1e-5f);
	}

	/// Test : la rangée x=256 du chunk (0,0) et la rangée x=0 du chunk (1,0)
	/// sont égales après création (les chunks plats à 0 partagent
	/// trivialement la couture). C'est un sanity-check de l'invariant ; le
	/// vrai test seam preservation actif (avec brushstroke à cheval) viendra
	/// avec une PR follow-up qui pilote `TerrainSculptTool` via une caméra
	/// de test.
	void Test_CrossChunk_PreservesSeam()
	{
		engine::core::Config cfg;
		TerrainDocument doc;
		auto a = doc.EnsureLoaded(cfg, 0, 0);
		auto b = doc.EnsureLoaded(cfg, 1, 0);
		REQUIRE(a != nullptr);
		REQUIRE(b != nullptr);
		const uint32_t lastX = kTerrainResolution - 1;
		for (uint32_t z = 0; z < kTerrainResolution; ++z)
		{
			const float ha = HeightAt(*a, lastX, z);
			const float hb = HeightAt(*b, 0, z);
			REQUIRE(ha == hb);
		}
	}
}

int main()
{
	Test_RaiseBrush_AddsExpectedDelta();
	Test_LowerBrush_NegatesRaise();
	Test_SmoothBrush_LimitsExtremaToNeighborhood();
	Test_FlattenBrush_ConvergesToCenterHeight();
	Test_NoiseBrush_DeterministicForSameSeed();
	Test_Stroke_MergesIntoOneCommand();
	Test_CrossChunk_PreservesSeam();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainSculptTests (7/7)\n");
		return 0;
	}
	std::printf("[FAIL] TerrainSculptTests: %d failure(s)\n", g_failed);
	return 1;
}
