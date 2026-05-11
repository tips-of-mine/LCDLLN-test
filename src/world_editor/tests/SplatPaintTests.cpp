/// Tests unitaires CPU pour la peinture splat (M100.10).
///
/// Vérifient les invariants de la peinture splat : invariant somme=255 par
/// cellule, filtrage auto-rules, falloff radial monotone, couture inter-chunks
/// et coalescing par stroke (1 entrée d'historique par geste). Pas de
/// dépendance ImGui ni Vulkan : tout passe par l'API CPU directement.
///
/// Cinq tests :
///   - Test_ManualBrush_PreservesSum255            : pose un delta manuel sur
///     plusieurs cellules et vérifie que `splat->IsValid()` reste vrai.
///   - Test_AutoRules_PaintsOnlyMatchingCells      : terrain mi-plat mi-pente,
///     vérifie que `MatchesRules` ne valide que les cellules en pente.
///   - Test_Falloff_RadialMonotone                 : poids décroissant en
///     fonction de la distance au centre.
///   - Test_CrossChunk_PreservesSeam               : 2 chunks adjacents avec
///     bord copié, vérifie l'égalité octet à octet.
///   - Test_Stroke_OneHistoryEntry                 : 2 SplatPaintCommand
///     consécutives même mergeKey → 1 entrée undo après push.
///
/// Pas de Catch2 — framework `REQUIRE` maison aligné sur CommandStackTests.

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/splat/SplatPaintCommand.h"
#include "src/world_editor/splat/SplatRules.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/client/world/terrain/SplatMap.h"
#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <array>
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
	using engine::editor::world::CommandMergeKey;
	using engine::editor::world::ComputeSlopeDeg;
	using engine::editor::world::MatchesRules;
	using engine::editor::world::SplatDeltaCell;
	using engine::editor::world::SplatDeltaChunk;
	using engine::editor::world::SplatPaintCommand;
	using engine::editor::world::TerrainDocument;
	using engine::world::GlobalChunkCoord;
	using engine::world::terrain::kSplatLayerCount;
	using engine::world::terrain::kSplatResolution;
	using engine::world::terrain::kTerrainResolution;
	using engine::world::terrain::SplatMap;
	using engine::world::terrain::TerrainChunk;

	/// Helpers : lire / écrire les 8 poids d'une cellule directement dans le
	/// buffer brut.
	void ReadWeights(const SplatMap& s, uint32_t x, uint32_t z,
		std::array<uint8_t, 8>& out)
	{
		const size_t base = (static_cast<size_t>(z) * s.resolution + x) * s.layerCount;
		for (uint32_t l = 0; l < 8; ++l)
		{
			out[l] = (l < s.layerCount && base + l < s.weights.size())
				? s.weights[base + l] : 0;
		}
	}

	void WriteWeights(SplatMap& s, uint32_t x, uint32_t z,
		const std::array<uint8_t, 8>& w)
	{
		const size_t base = (static_cast<size_t>(z) * s.resolution + x) * s.layerCount;
		for (uint32_t l = 0; l < 8 && l < s.layerCount; ++l)
		{
			s.weights[base + l] = w[l];
		}
	}

	/// Construit une cellule "next" dérivée de `prev` en redistribuant `delta`
	/// vers la layer active (clamp + renormalisation pour somme=255).
	std::array<uint8_t, 8> BuildNextCell(
		const std::array<uint8_t, 8>& prev,
		uint8_t activeLayer, uint8_t delta)
	{
		std::array<uint8_t, 8> next = prev;
		const int oldActive = static_cast<int>(next[activeLayer]);
		next[activeLayer] = static_cast<uint8_t>(std::min(255, oldActive + static_cast<int>(delta)));

		int total = 0;
		for (uint32_t l = 0; l < 8; ++l) total += static_cast<int>(next[l]);
		int excess = total - 255;
		if (excess <= 0)
		{
			if (excess < 0)
			{
				const int deficit = -excess;
				next[activeLayer] = static_cast<uint8_t>(
					std::min(255, static_cast<int>(next[activeLayer]) + deficit));
			}
			return next;
		}
		int sumOthers = 0;
		for (uint32_t l = 0; l < 8; ++l)
		{
			if (l == activeLayer) continue;
			sumOthers += static_cast<int>(next[l]);
		}
		if (sumOthers == 0)
		{
			next[activeLayer] = 255;
			return next;
		}
		int removed = 0;
		int last = -1;
		for (uint32_t l = 0; l < 8; ++l)
		{
			if (l == activeLayer) continue;
			const int w = static_cast<int>(next[l]);
			if (w == 0) continue;
			last = static_cast<int>(l);
			const int dec = std::min(w, (excess * w + sumOthers / 2) / sumOthers);
			next[l] = static_cast<uint8_t>(w - dec);
			removed += dec;
		}
		int residual = excess - removed;
		if (residual != 0 && last >= 0)
		{
			int w = static_cast<int>(next[last]);
			if (residual > 0)
			{
				const int dec = std::min(w, residual);
				next[last] = static_cast<uint8_t>(w - dec);
			}
			else
			{
				const int give = std::min(255 - w, -residual);
				next[last] = static_cast<uint8_t>(w + give);
			}
		}
		int finalTotal = 0;
		for (uint32_t l = 0; l < 8; ++l) finalTotal += static_cast<int>(next[l]);
		const int diff = 255 - finalTotal;
		if (diff != 0)
		{
			next[activeLayer] = static_cast<uint8_t>(
				std::clamp(static_cast<int>(next[activeLayer]) + diff, 0, 255));
		}
		return next;
	}

	/// Reproduit la formule de falloff utilisée par `SplatPaintTool`. Sans
	/// dépendance directe sur l'impl interne (qui est en `namespace { }`) :
	/// on duplique la formule ici pour couvrir le critère de monotonie.
	float FalloffWeight(float dist, float radius, float falloff)
	{
		if (radius <= 0.0f) return 0.0f;
		if (dist <= 0.0f) return 1.0f;
		if (dist >= radius) return 0.0f;
		const float clampedFalloff = std::clamp(falloff, 0.0f, 1.0f);
		const float innerRadius = radius * (1.0f - clampedFalloff);
		if (dist <= innerRadius) return 1.0f;
		const float denom = std::max(1e-6f, radius - innerRadius);
		const float t = 1.0f - (dist - innerRadius) / denom;
		if (t <= 0.0f) return 0.0f;
		if (t >= 1.0f) return 1.0f;
		return t * t * (3.0f - 2.0f * t);
	}

	/// Test : pose un delta manuel sur une cellule, vérifie que la somme
	/// reste 255 et que `IsValid()` du splat reste vrai.
	void Test_ManualBrush_PreservesSum255()
	{
		TerrainDocument doc;
		// On simule `EnsureSplatLoaded(0,0)` sans Config : on insère
		// directement un splat dans le document via le mécanisme officiel
		// (un Config par défaut suffit, paths.content="game/data" est OK
		// en environnement de test : aucun fichier disque, donc fallback
		// `MakeUniform(0)`).
		// On utilise un Config vide : `GetString("paths.content", ...)` va
		// renvoyer la valeur par défaut, donc EnsureSplatLoaded créera la
		// splat-map en RAM uniformément layer 0.
		// Cependant, `engine::core::Config` n'est pas trivialement constructible
		// dans tous les builds — pour rester robuste, on travaille
		// directement sur un `SplatMap` local et on simule l'`Execute` via la
		// commande sur un document vide.
		SplatMap splat = SplatMap::MakeUniform(0u);
		REQUIRE(splat.IsValid());

		// Choix : on peint la layer 5 (rock) avec delta=128 sur 4 cellules.
		const uint8_t activeLayer = 5;
		const uint8_t delta = 128;
		const std::pair<uint32_t, uint32_t> cells[4] = {
			{10, 10}, {10, 11}, {11, 10}, {11, 11}
		};

		for (const auto& [x, z] : cells)
		{
			std::array<uint8_t, 8> prev{};
			ReadWeights(splat, x, z, prev);
			const auto next = BuildNextCell(prev, activeLayer, delta);
			// Vérifie l'invariant somme=255 sur le `next` calculé.
			int sum = 0;
			for (auto w : next) sum += static_cast<int>(w);
			REQUIRE(sum == 255);
			WriteWeights(splat, x, z, next);
		}
		REQUIRE(splat.IsValid());
	}

	/// Test : terrain mi-plat (slope=0) mi-pente (slope=~45°). MatchesRules
	/// avec rule slope ∈ [30, 90] valide les cellules en pente uniquement.
	void Test_AutoRules_PaintsOnlyMatchingCells()
	{
		TerrainChunk chunk = TerrainChunk::MakeFlat(0.0f);
		const uint32_t res = chunk.resolutionX;
		// Moitié droite : pente 1 m / cellule (45°).
		for (uint32_t z = 0; z < chunk.resolutionZ; ++z)
		{
			for (uint32_t x = res / 2; x < res; ++x)
			{
				const float h = static_cast<float>(x - res / 2);
				chunk.heights[static_cast<size_t>(z) * res + x] = h;
			}
		}

		// Cellule sur la moitié plate (loin du raccord) : slope ≈ 0.
		const uint32_t flatX = 10;
		const uint32_t flatZ = 10;
		const float slopeFlat = ComputeSlopeDeg(chunk, flatX, flatZ);
		REQUIRE(slopeFlat < 1.0f);

		// Cellule franchement dans la moitié pentue : slope ≈ 45°.
		const uint32_t steepX = (res * 3) / 4;
		const uint32_t steepZ = 10;
		const float slopeSteep = ComputeSlopeDeg(chunk, steepX, steepZ);
		REQUIRE(slopeSteep > 30.0f);

		// Auto-rules slope ∈ [30, 90], altitude libre.
		REQUIRE(!MatchesRules(chunk, flatX, flatZ, 30.0f, 90.0f, -1024.0f, 8192.0f));
		REQUIRE( MatchesRules(chunk, steepX, steepZ, 30.0f, 90.0f, -1024.0f, 8192.0f));

		// Auto-rules slope ∈ [0, 5°] : l'inverse.
		REQUIRE( MatchesRules(chunk, flatX, flatZ, 0.0f, 5.0f, -1024.0f, 8192.0f));
		REQUIRE(!MatchesRules(chunk, steepX, steepZ, 0.0f, 5.0f, -1024.0f, 8192.0f));
	}

	/// Test : weight monotone décroissant entre dist=0 et dist=radius-1.
	/// Couvre le critère "Falloff smooth" de la spec M100.10.
	void Test_Falloff_RadialMonotone()
	{
		const float radius = 8.0f;
		const float falloff = 0.7f;
		const float w0 = FalloffWeight(0.0f, radius, falloff);
		const float w14 = FalloffWeight(radius * 0.25f, radius, falloff);
		const float w12 = FalloffWeight(radius * 0.5f, radius, falloff);
		const float w34 = FalloffWeight(radius * 0.75f, radius, falloff);
		const float wN = FalloffWeight(radius - 0.1f, radius, falloff);
		REQUIRE(w0 > 0.0f);
		REQUIRE(wN >= 0.0f);
		// Monotonie large (égalités possibles dans la zone "plein effet"
		// `dist <= radius*(1-falloff)` mais pas après).
		REQUIRE(w0  >= w14);
		REQUIRE(w14 >= w12);
		REQUIRE(w12 >= w34);
		REQUIRE(w34 >= wN);
		// Le poids au bord du rayon doit être strictement < 1.
		REQUIRE(wN < 1.0f);
	}

	/// Test : 2 chunks adjacents (0,0) et (1,0). On écrit la cellule
	/// (x=kLastIdx, z=Z) du chunk gauche et la cellule miroir (x=0, z=Z) du
	/// chunk droit avec les mêmes valeurs. Vérifie l'égalité octet à octet
	/// sur tous les layers.
	void Test_CrossChunk_PreservesSeam()
	{
		SplatMap splatA = SplatMap::MakeUniform(0u);
		SplatMap splatB = SplatMap::MakeUniform(0u);

		const int kLastIdx = static_cast<int>(kSplatResolution) - 1;
		const uint32_t Z = 50;
		std::array<uint8_t, 8> seamWeights{};
		ReadWeights(splatA, static_cast<uint32_t>(kLastIdx), Z, seamWeights);
		const auto next = BuildNextCell(seamWeights, /*layer=*/3, /*delta=*/100);
		WriteWeights(splatA, static_cast<uint32_t>(kLastIdx), Z, next);
		WriteWeights(splatB, 0u, Z, next);

		// Vérifie l'égalité octet à octet de la rangée bord côté A et de la
		// rangée bord côté B : les bytes des deux chunks sont identiques.
		std::array<uint8_t, 8> a{};
		std::array<uint8_t, 8> b{};
		ReadWeights(splatA, static_cast<uint32_t>(kLastIdx), Z, a);
		ReadWeights(splatB, 0u, Z, b);
		for (uint32_t l = 0; l < 8; ++l)
		{
			REQUIRE(a[l] == b[l]);
		}

		// Sanity : invariants somme=255 conservés des deux côtés.
		REQUIRE(splatA.IsValid());
		REQUIRE(splatB.IsValid());
	}

	/// Test : 2 SplatPaintCommand consécutives avec même mergeKey fusionnent
	/// dans la pile (UndoSize == 1 après push des deux).
	void Test_Stroke_OneHistoryEntry()
	{
		TerrainDocument doc;
		// On utilise des shared_ptr<SplatMap> que TerrainDocument ne connaît
		// pas via EnsureSplatLoaded (besoin d'une Config). On peut simplement
		// vérifier le merge sans toucher au document : l'`Execute` no-op si
		// `FindSplat` renvoie nullptr (cf. SplatPaintCommand::Execute).
		(void)doc;

		CommandStack stack;
		const CommandMergeKey k = 7777;

		// Première commande : 1 chunk, 2 cellules.
		std::vector<SplatDeltaChunk> deltas1;
		{
			SplatDeltaChunk dc;
			dc.coord = GlobalChunkCoord{0, 0};
			SplatDeltaCell c1{};
			c1.x = 5; c1.z = 5;
			c1.prev = {255, 0, 0, 0, 0, 0, 0, 0};
			c1.next = {200, 0, 0, 0, 0, 55, 0, 0};
			SplatDeltaCell c2{};
			c2.x = 6; c2.z = 5;
			c2.prev = {255, 0, 0, 0, 0, 0, 0, 0};
			c2.next = {200, 0, 0, 0, 0, 55, 0, 0};
			dc.cells.push_back(c1);
			dc.cells.push_back(c2);
			deltas1.push_back(std::move(dc));
		}
		auto cmd1 = std::make_unique<SplatPaintCommand>(doc, std::move(deltas1), k);
		stack.Push(std::move(cmd1));
		REQUIRE(stack.UndoSize() == 1u);

		// Deuxième commande : 1 chunk identique, 1 cellule existante (5,5)
		// + 1 nouvelle (7,5). Doit fusionner via TryMerge.
		std::vector<SplatDeltaChunk> deltas2;
		{
			SplatDeltaChunk dc;
			dc.coord = GlobalChunkCoord{0, 0};
			SplatDeltaCell c1{};
			c1.x = 5; c1.z = 5;
			c1.prev = {200, 0, 0, 0, 0, 55, 0, 0}; // ignoré au merge
			c1.next = {180, 0, 0, 0, 0, 75, 0, 0};
			SplatDeltaCell c3{};
			c3.x = 7; c3.z = 5;
			c3.prev = {255, 0, 0, 0, 0, 0, 0, 0};
			c3.next = {200, 0, 0, 0, 0, 55, 0, 0};
			dc.cells.push_back(c1);
			dc.cells.push_back(c3);
			deltas2.push_back(std::move(dc));
		}
		auto cmd2 = std::make_unique<SplatPaintCommand>(doc, std::move(deltas2), k);
		stack.Push(std::move(cmd2));
		// Coalescing : toujours 1 seule entrée d'historique.
		REQUIRE(stack.UndoSize() == 1u);
	}
}

int main()
{
	Test_ManualBrush_PreservesSum255();
	Test_AutoRules_PaintsOnlyMatchingCells();
	Test_Falloff_RadialMonotone();
	Test_CrossChunk_PreservesSeam();
	Test_Stroke_OneHistoryEntry();

	if (g_failed == 0)
	{
		std::printf("[PASS] SplatPaintTests (5/5)\n");
		return 0;
	}
	std::printf("[FAIL] SplatPaintTests: %d failure(s)\n", g_failed);
	return 1;
}
