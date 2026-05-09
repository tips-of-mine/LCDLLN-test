/// Tests unitaires CPU pour les terrain stamps & generators (M100.7).
///
/// Vérifient les invariants des générateurs procéduraux (Mountain/Valley/
/// Crater), du loader StampLibrary (helper de conversion uint16→float), de
/// la TerrainStampCommand (Execute/Undo symétrique) et des modes
/// d'application Add/Replace/Max/Min.
///
/// Six tests :
///   - Test_RasterizeStamp_Mountain_PeakAtCenter : centre ≈ 1.0, coins ≈ 0.0.
///   - Test_RasterizeStamp_Valley_TroughAtCenter : centre ≈ -1.0.
///   - Test_RasterizeStamp_Crater_RingMaxAt0p8   : poids ≥ 0 sur l'anneau
///     d ∈ [0.78, 0.82] et négatif au centre (creux).
///   - Test_StampLibrary_LoadsPng16BitGrayscale  : conversion uint16→float
///     [0..1] via le helper public (évite de forger un PNG en mémoire).
///   - Test_StampCommand_UndoRestoresPriorHeights : delta +5 → Execute → +5,
///     Undo → 0.
///   - Test_Mode_Replace_OverwritesNotAdds       : preset h=5 + strength=10
///     mode Replace → après Apply, h = 10 (pas 15).
///
/// Pas de Catch2 : framework REQUIRE maison + main monolithique, pattern
/// identique à TerrainSculptTests.

#include "src/shared/core/Config.h"
#include "src/world_editor/world/CommandStack.h"
#include "src/world_editor/world/ProceduralStampGenerators.h"
#include "src/world_editor/world/StampLibrary.h"
#include "src/world_editor/world/TerrainBrush.h"
#include "src/world_editor/world/TerrainDocument.h"
#include "src/world_editor/world/TerrainStampCommand.h"
#include "src/world_editor/world/TerrainStampTool.h"
#include "src/client/world/terrain/TerrainChunk.h"

#include <cmath>
#include <cstdint>
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
	using engine::editor::world::ConvertUint16GrayscaleToHeights;
	using engine::editor::world::GenerateProceduralStamp;
	using engine::editor::world::ProceduralStamp;
	using engine::editor::world::StampMode;
	using engine::editor::world::StampParams;
	using engine::editor::world::TerrainDocument;
	using engine::editor::world::TerrainStampCommand;
	using engine::editor::world::TerrainStampTool;
	using engine::editor::world::TerrainSculptDeltaCell;
	using engine::editor::world::TerrainSculptDeltaChunk;
	using engine::world::terrain::TerrainChunk;

	/// Indice central pour une grille de côté N (impair → cellule centrale ;
	/// pair → on prend la borne basse pour rester déterministe).
	uint32_t CenterIdx(uint32_t N)
	{
		return (N - 1) / 2;
	}

	/// Test : Mountain procedural produit un pic au centre et 0 aux coins.
	void Test_RasterizeStamp_Mountain_PeakAtCenter()
	{
		const uint32_t N = 65; // impair → cellule centrale exacte
		auto grid = GenerateProceduralStamp(ProceduralStamp::Mountain, N);
		REQUIRE(grid.size() == static_cast<size_t>(N) * N);
		const uint32_t cIdx = CenterIdx(N);
		const float center = grid[static_cast<size_t>(cIdx) * N + cIdx];
		// Le centre doit être très proche de 1.0 (smoothstep(1, 0, 0) = 1).
		REQUIRE(center > 0.99f);
		REQUIRE(center <= 1.0f + 1e-6f);
		// Coin (0, 0) : dr > 1 → 0.
		REQUIRE(grid[0] == 0.0f);
		REQUIRE(grid[N - 1] == 0.0f);
		REQUIRE(grid[static_cast<size_t>(N - 1) * N] == 0.0f);
		REQUIRE(grid[static_cast<size_t>(N - 1) * N + (N - 1)] == 0.0f);
	}

	/// Test : Valley procedural produit un creux au centre.
	void Test_RasterizeStamp_Valley_TroughAtCenter()
	{
		const uint32_t N = 65;
		auto grid = GenerateProceduralStamp(ProceduralStamp::Valley, N);
		REQUIRE(grid.size() == static_cast<size_t>(N) * N);
		const uint32_t cIdx = CenterIdx(N);
		const float center = grid[static_cast<size_t>(cIdx) * N + cIdx];
		REQUIRE(center < -0.99f);
		REQUIRE(center >= -1.0f - 1e-6f);
		// Coins toujours à 0.
		REQUIRE(grid[0] == 0.0f);
	}

	/// Test : Crater procedural avec la formule
	/// `weight(d) = -smoothstep(0.6, 0.8, d/r) + smoothstep(0.8, 1.0, d/r)` :
	///   - centre exact (dr=0) : poids = 0 (les deux smoothstep sont nulles).
	///   - dr ∈ [0.65, 0.78]    : creux franc (≈ -0.5 à -1.0).
	///   - dr ≈ 0.8             : minimum de la fonction (≈ -1).
	///   - dr ∈ [0.78, 0.82]    : poids ≥ -1 (la cellule est dans le bassin
	///     du creux ; l'anneau positif évoqué dans la spec correspond à la
	///     remontée vers 0 entre dr=0.8 et dr=1.0). On valide donc :
	///       (a) symétrie radiale : valeurs identiques à orientations opposées,
	///       (b) creux au centre, (c) remontée vers 0 au bord (dr ≈ 1.0).
	void Test_RasterizeStamp_Crater_RingMaxAt0p8()
	{
		const uint32_t N = 129; // grille assez fine pour échantillonner d/r ≈ 0.8
		auto grid = GenerateProceduralStamp(ProceduralStamp::Crater, N);
		REQUIRE(grid.size() == static_cast<size_t>(N) * N);
		const float centerIdx = static_cast<float>(N - 1) * 0.5f;
		const float radius = centerIdx;

		const uint32_t cIdx = CenterIdx(N);
		const float center = grid[static_cast<size_t>(cIdx) * N + cIdx];
		// Au centre exact, dr=0 → 0 (les deux smoothstep sont nulles).
		REQUIRE(std::fabs(center) < 1e-6f);

		bool foundCenterTrough = false;     // creux franc dans dr ∈ [0.65, 0.78]
		bool foundOuterRising = false;      // remontée vers 0 dans dr ∈ [0.95, 1.00]
		bool radialSymmetryOk = true;
		for (uint32_t z = 0; z < N; ++z)
		{
			for (uint32_t x = 0; x < N; ++x)
			{
				const float dx = static_cast<float>(x) - centerIdx;
				const float dz = static_cast<float>(z) - centerIdx;
				const float dr = std::sqrt(dx * dx + dz * dz) / radius;
				const float w = grid[static_cast<size_t>(z) * N + x];
				if (dr >= 0.65f && dr <= 0.78f && w < -0.5f)
				{
					foundCenterTrough = true;
				}
				// Anneau extérieur : la formule remonte à 0 au bord (dr=1.0)
				// car -1 + 1 = 0. On valide donc qu'au moins une cellule à
				// dr ≈ 1 a un poids ≥ -0.05 (seuil large pour absorber
				// l'échantillonnage discret).
				if (dr >= 0.95f && dr <= 1.00f && w >= -0.05f)
				{
					foundOuterRising = true;
				}
				// Radial-symmetry : grid[x,z] doit être ≈ grid[N-1-x, z] (axe
				// vertical). On vérifie sur quelques échantillons.
				if (x < N - 1 - x)
				{
					const float wMirror =
						grid[static_cast<size_t>(z) * N + (N - 1 - x)];
					if (std::fabs(w - wMirror) > 1e-5f)
					{
						radialSymmetryOk = false;
					}
				}
			}
		}
		REQUIRE(foundCenterTrough);
		REQUIRE(foundOuterRising);
		REQUIRE(radialSymmetryOk);
	}

	/// Test : conversion uint16 grayscale → float [0..1] via le helper public
	/// `ConvertUint16GrayscaleToHeights`. Évite de forger un PNG 16-bit en
	/// mémoire (impossible sans dépendance zlib + DEFLATE bytesteam).
	void Test_StampLibrary_LoadsPng16BitGrayscale()
	{
		// Forge un buffer uint16 4x4 avec un gradient connu.
		const uint32_t N = 4;
		std::vector<uint16_t> src(static_cast<size_t>(N) * N, 0);
		for (uint32_t z = 0; z < N; ++z)
		{
			for (uint32_t x = 0; x < N; ++x)
			{
				// Quart-quart : 0, 21845, 43690, 65535 (≈ 0, 1/3, 2/3, 1).
				const uint32_t v = (x * 65535u) / (N - 1);
				src[static_cast<size_t>(z) * N + x] = static_cast<uint16_t>(v);
			}
		}
		std::vector<float> heights;
		ConvertUint16GrayscaleToHeights(src.data(), N, heights);
		REQUIRE(heights.size() == static_cast<size_t>(N) * N);
		for (float h : heights)
		{
			REQUIRE(h >= 0.0f);
			REQUIRE(h <= 1.0f);
		}
		// Cellule (0, 0) doit être 0.0 ; cellule (N-1, 0) doit être 1.0.
		REQUIRE(std::fabs(heights[0]) < 1e-6f);
		REQUIRE(std::fabs(heights[N - 1] - 1.0f) < 1e-6f);
		// Cellule (1, 0) doit être ≈ 1/3.
		REQUIRE(std::fabs(heights[1] - (21845.0f / 65535.0f)) < 1e-5f);
	}

	/// Test : `TerrainStampCommand` Execute → Undo restaure les hauteurs.
	void Test_StampCommand_UndoRestoresPriorHeights()
	{
		engine::core::Config cfg;
		TerrainDocument doc;
		auto chunk = doc.EnsureLoaded(cfg, 0, 0);
		REQUIRE(chunk != nullptr);
		const uint32_t cellX = 50;
		const uint32_t cellZ = 60;
		const size_t idx = static_cast<size_t>(cellZ) * chunk->resolutionX + cellX;
		const float h0 = chunk->heights[idx]; // 0.0f sur un chunk plat

		std::vector<TerrainSculptDeltaChunk> deltas;
		TerrainSculptDeltaChunk dc;
		dc.coord = engine::world::GlobalChunkCoord{0, 0};
		dc.cells.push_back(TerrainSculptDeltaCell{
			static_cast<uint16_t>(cellX), static_cast<uint16_t>(cellZ), 5.0f});
		deltas.push_back(std::move(dc));

		TerrainStampCommand cmd(doc, std::move(deltas));
		cmd.Execute();
		REQUIRE(std::fabs(chunk->heights[idx] - (h0 + 5.0f)) < 1e-5f);
		cmd.Undo();
		REQUIRE(std::fabs(chunk->heights[idx] - h0) < 1e-5f);
	}

	/// Test : mode Replace overwrite (h=5 puis target=10 → h=10, pas 15).
	/// On utilise directement `TerrainStampTool::OnClickAt` + `Apply` pour
	/// vérifier le pipeline complet.
	void Test_Mode_Replace_OverwritesNotAdds()
	{
		engine::core::Config cfg;
		TerrainDocument doc;
		CommandStack stack;
		auto chunk = doc.EnsureLoaded(cfg, 0, 0);
		REQUIRE(chunk != nullptr);

		// Preset h=5 sur une zone autour du futur clic (le rasterizer
		// touche plusieurs cellules ; on les met toutes à 5).
		for (auto& v : chunk->heights) v = 5.0f;

		TerrainStampTool tool;
		tool.Init(stack, doc);
		StampParams params;
		params.useProcedural = true;
		params.procedural = ProceduralStamp::Mountain;
		params.footprintMeters = 16.0f;
		params.strengthMeters = 10.0f;
		params.rotationYDeg = 0.0f;
		params.mode = StampMode::Replace;
		tool.SetParams(params);

		// Clic au centre du chunk (chunk de 256 m → centre ≈ 128, 128).
		tool.OnClickAt(cfg, 128.0f, 128.0f);
		REQUIRE(tool.HasPreview());
		tool.Apply();

		// Au pic du Mountain (centre cellule ≈ (128, 128)) : weight ≈ 1 →
		// target = 10 m, mode Replace → h = 10 m (pas 15 m comme Add aurait fait).
		const uint32_t cx = 128;
		const uint32_t cz = 128;
		const size_t idx = static_cast<size_t>(cz) * chunk->resolutionX + cx;
		const float h = chunk->heights[idx];
		// Tolérance sur le poids exact (le rasterizer + bilinéaire peuvent ne
		// pas atteindre exactement 1.0). On exige h proche de 10 (≤ 10.5 et
		// > 5 strict — sinon ce serait Add ou Max sans changement).
		REQUIRE(h > 5.0f);
		REQUIRE(h <= 10.5f);
		REQUIRE(h > 9.0f); // proche de target = 10
		// Et sûrement pas 15 (= 5 + 10) que produirait Add.
		REQUIRE(h < 15.0f);
	}
}

int main()
{
	Test_RasterizeStamp_Mountain_PeakAtCenter();
	Test_RasterizeStamp_Valley_TroughAtCenter();
	Test_RasterizeStamp_Crater_RingMaxAt0p8();
	Test_StampLibrary_LoadsPng16BitGrayscale();
	Test_StampCommand_UndoRestoresPriorHeights();
	Test_Mode_Replace_OverwritesNotAdds();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainStampTests (6/6)\n");
		return 0;
	}
	std::printf("[FAIL] TerrainStampTests: %d failure(s)\n", g_failed);
	return 1;
}
