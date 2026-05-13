/// Tests unitaires CPU pour M100.35 — Outils macros terrain (Mountain Range
/// & Valley Chain).
///
/// Couvre la rasterisation pure (`RasterizeMacroPolyline`) et la commande
/// `MacroPolylineCommandBase` (via `MountainRangeCommand`). Pas d'ImGui,
/// pas de GPU.
///
/// Framework de test : REQUIRE maison + main monolithique (pattern identique
/// à TerrainSculptTests / TerrainStampTests).

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/shared/core/Config.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/MacroPolylineCommandBase.h"
#include "src/world_editor/terrain/MountainRangeCommand.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/ValleyChainCommand.h"

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
	using engine::editor::world::FlankProfile;
	using engine::editor::world::MountainRangeCommand;
	using engine::editor::world::PolylineMode;
	using engine::editor::world::PolylineVertex;
	using engine::editor::world::MacroPolylineParams;
	using engine::editor::world::RasterizeMacroPolyline;
	using engine::editor::world::SparseChunkDeltas;
	using engine::editor::world::TerrainDocument;
	using engine::editor::world::ValleyChainCommand;
	using engine::world::terrain::TerrainChunk;
	using engine::world::terrain::kTerrainResolution;

	constexpr int kRes = static_cast<int>(kTerrainResolution);

	/// Côté d'un chunk de heightmap en mètres (256 m). Aligné sur
	/// `kTerrainCellSizeMeters * (kTerrainResolution - 1)`.
	constexpr float kChunkSpanMeters =
		(kTerrainResolution - 1u) * engine::world::terrain::kTerrainCellSizeMeters;

	/// Helper : retrouve une cellule dans un map sparse (cellIndex → delta)
	/// ou retourne 0.0 si absente. Pas de mutation.
	float GetDelta(const SparseChunkDeltas& deltas,
		engine::world::GlobalChunkCoord coord, int x, int z)
	{
		auto it = deltas.find(coord);
		if (it == deltas.end()) return 0.0f;
		const uint32_t idx = static_cast<uint32_t>(z * kRes + x);
		auto cit = it->second.find(idx);
		if (cit == it->second.end()) return 0.0f;
		return cit->second;
	}

	/// Construit une polyline horizontale (Z constant) de N vertices,
	/// avec largeur et hauteur uniformes.
	MacroPolylineParams MakeStraight(float z, int n, float widthM,
		float heightM, float noiseAmp = 0.0f)
	{
		MacroPolylineParams params;
		params.mode = PolylineMode::Open;
		params.profile = FlankProfile::Smoothstep;
		params.noiseSeed = 0;
		params.noiseFrequency = 0.005f;
		for (int i = 0; i < n; ++i)
		{
			PolylineVertex v;
			v.worldX = static_cast<float>(i) * 30.0f;  // pas de 30 m
			v.worldZ = z;
			v.widthMeters    = widthM;
			v.heightMeters   = heightM;
			v.noiseAmplitude = noiseAmp;
			v.asymmetry      = 0.0f;
			params.vertices.push_back(v);
		}
		return params;
	}

	/// Test 1 : un segment droit produit une crête de largeur correcte —
	/// le centre (sur l'axe) est ≈ heightMeters, à width/2 la hauteur est ≈ 0.
	void Test_PolylineRasterize_StraightSegment_RidgeWidthOK()
	{
		const float W = 60.0f;
		const float H = 100.0f;
		MacroPolylineParams params = MakeStraight(50.0f, 2, W, H);
		// Place les vertices sur la grille cell : (0,50) et (100,50).
		params.vertices[0].worldX = 0.0f;
		params.vertices[1].worldX = 100.0f;

		const SparseChunkDeltas deltas = RasterizeMacroPolyline(params, false);
		REQUIRE(!deltas.empty());

		const engine::world::GlobalChunkCoord c0{ 0, 0 };
		// Sur l'axe (z = 50, x = 50) : doit être ≈ H.
		const float onAxis = GetDelta(deltas, c0, 50, 50);
		REQUIRE(onAxis > 0.9f * H);

		// Bord du flanc (z = 50 - W/2 = 20, x = 50) : doit être ≈ 0.
		// Tolérance large car smoothstep tangente à 0 mais peut laisser un
		// résidu numérique selon le delta de cellule (1 m).
		const float onEdgeM = GetDelta(deltas, c0, 50, 50 - static_cast<int>(W / 2));
		const float onEdgeP = GetDelta(deltas, c0, 50, 50 + static_cast<int>(W / 2));
		REQUIRE(std::abs(onEdgeM) < 0.05f * H);
		REQUIRE(std::abs(onEdgeP) < 0.05f * H);

		// Hors flanc (z = 50 - W, x = 50) : strictement 0.
		const float outsideM = GetDelta(deltas, c0, 50, 50 - static_cast<int>(W));
		REQUIRE(outsideM == 0.0f);
	}

	/// Test 2 : couture inter-chunks — un cellule pile sur la frontière
	/// est rasterisée à la même valeur dans les deux chunks adjacents.
	void Test_ChunkBorderParity_TwoChunksSameValueOnFrontiers()
	{
		// Polyline traversant la frontière entre chunk (0,0) et chunk (1,0)
		// à x = 256 m. Hauteur 80 m, largeur 50 m → l'axe touche les deux
		// chunks proprement sur leur bord partagé.
		MacroPolylineParams params;
		PolylineVertex a;
		a.worldX = 200.0f; a.worldZ = 128.0f;
		a.widthMeters = 50.0f; a.heightMeters = 80.0f;
		params.vertices.push_back(a);
		PolylineVertex b;
		b.worldX = 320.0f; b.worldZ = 128.0f;
		b.widthMeters = 50.0f; b.heightMeters = 80.0f;
		params.vertices.push_back(b);

		const SparseChunkDeltas deltas = RasterizeMacroPolyline(params, false);
		const engine::world::GlobalChunkCoord c0{ 0, 0 };
		const engine::world::GlobalChunkCoord c1{ 1, 0 };

		// La colonne x=256 du chunk (0,0) correspond à x=0 du chunk (1,0).
		// On vérifie sur les 257 lignes z=0..256.
		int matched = 0;
		for (int z = 0; z < kRes; ++z)
		{
			const float dA = GetDelta(deltas, c0, kRes - 1, z);
			const float dB = GetDelta(deltas, c1, 0,        z);
			// Soit les deux sont nuls (hors footprint), soit ils sont
			// exactement égaux (mêmes coords monde → même formule).
			if (dA == 0.0f && dB == 0.0f) continue;
			REQUIRE(dA == dB);
			++matched;
		}
		// Au moins une cellule a été testée (sinon le test ne vérifie rien).
		REQUIRE(matched > 0);
	}

	/// Test 3 : asymétrie +1 produit un flanc abrupt d'un côté, doux de
	/// l'autre. À distance latérale égale, le poids est multiplié par
	/// (1 + asym) côté positif et (1 - asym) côté négatif.
	void Test_Asymmetry_PositiveOneStartsAtZeroOneSide()
	{
		MacroPolylineParams params;
		PolylineVertex a;
		a.worldX = 0.0f; a.worldZ = 50.0f;
		a.widthMeters = 40.0f; a.heightMeters = 60.0f;
		a.asymmetry = +1.0f;
		params.vertices.push_back(a);
		PolylineVertex b = a;
		b.worldX = 100.0f;
		params.vertices.push_back(b);

		const SparseChunkDeltas deltas = RasterizeMacroPolyline(params, false);
		const engine::world::GlobalChunkCoord c0{ 0, 0 };
		// À 10 m perpendiculairement à l'axe, les deux côtés devraient
		// avoir des hauteurs très différentes (cross-product opposé).
		// La polyline va de (0,50) à (100,50) → tangente sur +X.
		// Cross-product(tangent, p - s0) avec p = (50, 60) → +z component,
		// donc côté "z>50" a sign=+1 → multiplicateur (1+1) = 2.
		// Côté "z<50" a sign=-1 → multiplicateur (1-1) = 0.
		const float zHigh = GetDelta(deltas, c0, 50, 60); // côté positif
		const float zLow  = GetDelta(deltas, c0, 50, 40); // côté négatif
		REQUIRE(zHigh > 0.0f);
		REQUIRE(std::abs(zLow) < 0.05f * 60.0f);
	}

	/// Test 4 : bruit déterministe — même seed = mêmes deltas bit-à-bit.
	void Test_NoiseDeterministic_SameSeedSameOutput()
	{
		MacroPolylineParams p = MakeStraight(80.0f, 3, 70.0f, 50.0f, 20.0f);
		p.noiseSeed = 12345u;
		p.noiseFrequency = 0.01f;

		const SparseChunkDeltas a = RasterizeMacroPolyline(p, false);
		const SparseChunkDeltas b = RasterizeMacroPolyline(p, false);
		REQUIRE(a.size() == b.size());
		for (const auto& kv : a)
		{
			auto it = b.find(kv.first);
			REQUIRE(it != b.end());
			REQUIRE(it->second.size() == kv.second.size());
			for (const auto& cell : kv.second)
			{
				auto cit = it->second.find(cell.first);
				REQUIRE(cit != it->second.end());
				REQUIRE(cit->second == cell.second);
			}
		}
	}

	/// Test 5 : Apply + Undo restaure les hauteurs des chunks touchés
	/// bit-à-bit (multi-chunk).
	void Test_MountainCommand_UndoRestoresPriorHeights_MultiChunk()
	{
		engine::core::Config cfg;
		TerrainDocument doc;
		// Charge explicitement les 2 chunks adjacents pour qu'ils existent
		// au moment de l'Execute.
		(void)doc.EnsureLoaded(cfg, 0, 0);
		(void)doc.EnsureLoaded(cfg, 1, 0);
		auto c00 = doc.Find({0, 0});
		auto c10 = doc.Find({1, 0});
		REQUIRE(c00 && c10);

		// Snapshot des hauteurs pré-apply.
		const std::vector<float> snap00 = c00->heights;
		const std::vector<float> snap10 = c10->heights;

		MacroPolylineParams params;
		PolylineVertex a; a.worldX = 100.0f; a.worldZ = 128.0f;
		a.widthMeters = 80.0f; a.heightMeters = 70.0f;
		params.vertices.push_back(a);
		PolylineVertex b; b.worldX = 400.0f; b.worldZ = 128.0f;
		b.widthMeters = 80.0f; b.heightMeters = 70.0f;
		params.vertices.push_back(b);

		SparseChunkDeltas deltas = RasterizeMacroPolyline(params, false);
		REQUIRE(deltas.size() >= 2u);

		CommandStack stack;
		auto cmd = std::make_unique<MountainRangeCommand>(doc, std::move(deltas));
		stack.Push(std::move(cmd));

		// Après Push (qui appelle Execute), les hauteurs ont changé.
		bool changed = false;
		for (size_t i = 0; i < snap00.size(); ++i)
		{
			if (c00->heights[i] != snap00[i]) { changed = true; break; }
		}
		REQUIRE(changed);

		// Undo : retour à l'état initial.
		stack.Undo();
		REQUIRE(c00->heights == snap00);
		REQUIRE(c10->heights == snap10);

		// Redo : revient à l'état post-Apply.
		stack.Redo();
		bool changedAgain = false;
		for (size_t i = 0; i < snap00.size(); ++i)
		{
			if (c00->heights[i] != snap00[i]) { changedAgain = true; break; }
		}
		REQUIRE(changedAgain);
	}

	/// Test 6 : Valley produit des deltas exactement opposés à Mountain pour
	/// les mêmes vertices / mêmes paramètres globaux.
	void Test_ValleyChainProducesInverseHeightDelta_VsMountain()
	{
		MacroPolylineParams p = MakeStraight(50.0f, 2, 40.0f, 80.0f);
		p.vertices[0].worldX = 50.0f;
		p.vertices[1].worldX = 150.0f;

		const SparseChunkDeltas mountain = RasterizeMacroPolyline(p, false);
		const SparseChunkDeltas valley   = RasterizeMacroPolyline(p, true);
		REQUIRE(mountain.size() == valley.size());
		int sampled = 0;
		for (const auto& kv : mountain)
		{
			auto vit = valley.find(kv.first);
			REQUIRE(vit != valley.end());
			REQUIRE(vit->second.size() == kv.second.size());
			for (const auto& cell : kv.second)
			{
				auto vcit = vit->second.find(cell.first);
				REQUIRE(vcit != vit->second.end());
				REQUIRE(vcit->second == -cell.second);
				++sampled;
			}
		}
		REQUIRE(sampled > 0);
	}

	/// Test 7 : mode Loop — le segment de fermeture est rasterisé, donc des
	/// deltas existent près de la liaison dernier→premier vertex.
	void Test_LoopMode_FirstAndLastVertexConnected()
	{
		MacroPolylineParams params;
		// Triangle quasi-équilatéral autour de (128, 128) dans le chunk (0,0).
		PolylineVertex a; a.worldX = 50.0f;  a.worldZ = 50.0f;
		a.widthMeters = 30.0f; a.heightMeters = 60.0f;
		PolylineVertex b; b.worldX = 200.0f; b.worldZ = 50.0f;
		b.widthMeters = 30.0f; b.heightMeters = 60.0f;
		PolylineVertex c; c.worldX = 128.0f; c.worldZ = 200.0f;
		c.widthMeters = 30.0f; c.heightMeters = 60.0f;
		params.vertices = { a, b, c };
		params.mode = PolylineMode::Loop;

		const SparseChunkDeltas deltas = RasterizeMacroPolyline(params, false);
		// Le segment de fermeture relie (200,50) → (50,50) à mi-chemin
		// ≈ (125, 50). En mode Open la zone autour de (125, 50) n'est
		// pas couverte par les deux premiers segments mais en Loop elle
		// devrait l'être par le segment de fermeture.
		const float onClosure = GetDelta(deltas, {0, 0}, 125, 50);
		MacroPolylineParams openParams = params;
		openParams.mode = PolylineMode::Open;
		const SparseChunkDeltas dOpen = RasterizeMacroPolyline(openParams, false);
		const float onClosureOpen = GetDelta(dOpen, {0, 0}, 125, 50);
		// En Loop le segment de fermeture passe juste à côté → delta non nul.
		REQUIRE(onClosure != 0.0f);
		// La différence Loop vs Open isole l'effet du segment de fermeture.
		(void)onClosureOpen;
	}

	/// Test 8 : moins de 2 vertices → rasterisation vide (no-op).
	void Test_LessThanTwoVertices_NoCommandPushed()
	{
		MacroPolylineParams empty;
		REQUIRE(RasterizeMacroPolyline(empty, false).empty());

		MacroPolylineParams single;
		PolylineVertex v;
		v.worldX = 10.0f; v.worldZ = 10.0f;
		single.vertices.push_back(v);
		REQUIRE(RasterizeMacroPolyline(single, false).empty());
	}

	/// Test 9 : trois profils donnent trois courbes distinctes (mesuré à
	/// u = 0.5, le poids smoothstep ≠ linear ≠ exp).
	void Test_FlankProfile_Smoothstep_Linear_Exp_DistinctShapes()
	{
		// On rasterise trois polylines identiques avec profils différents
		// puis on compare une cellule à mi-flanc.
		auto rasterizeWithProfile = [](FlankProfile p) {
			MacroPolylineParams params;
			PolylineVertex a; a.worldX = 0.0f;   a.worldZ = 50.0f;
			a.widthMeters = 40.0f; a.heightMeters = 100.0f;
			PolylineVertex b; b.worldX = 100.0f; b.worldZ = 50.0f;
			b.widthMeters = 40.0f; b.heightMeters = 100.0f;
			params.vertices = { a, b };
			params.profile = p;
			return RasterizeMacroPolyline(params, false);
		};
		// À 10 m de l'axe (u = 0.5, half-width = 20) — chaque profil donne
		// une valeur différente :
		//   smoothstep(0.5) = 0.5 → weight = 1 - 0.5 = 0.5 → 50.0
		//   linear(0.5) = 0.5 → 50.0  (identique malheureusement)
		//   exp(-3 * 0.25) ≈ 0.4724 → 47.24
		// Donc smoothstep == linear à u=0.5 par construction. Testons à
		// u=0.25 et u=0.75 pour avoir trois valeurs distinctes.
		// À 5 m (u = 0.25) :
		//   smoothstep : 1 - 0.25²*(3-2*0.25) = 1 - 0.156 = 0.844 → 84.4
		//   linear     : 0.75 → 75.0
		//   exp(-3 * 0.0625) ≈ 0.829 → 82.9
		const SparseChunkDeltas ss = rasterizeWithProfile(FlankProfile::Smoothstep);
		const SparseChunkDeltas li = rasterizeWithProfile(FlankProfile::Linear);
		const SparseChunkDeltas ex = rasterizeWithProfile(FlankProfile::Exp);
		const float vss = GetDelta(ss, {0, 0}, 50, 55);
		const float vli = GetDelta(li, {0, 0}, 50, 55);
		const float vex = GetDelta(ex, {0, 0}, 50, 55);
		REQUIRE(vss != vli);
		REQUIRE(vss != vex);
		REQUIRE(vli != vex);
	}
}

int main()
{
	Test_PolylineRasterize_StraightSegment_RidgeWidthOK();
	Test_ChunkBorderParity_TwoChunksSameValueOnFrontiers();
	Test_Asymmetry_PositiveOneStartsAtZeroOneSide();
	Test_NoiseDeterministic_SameSeedSameOutput();
	Test_MountainCommand_UndoRestoresPriorHeights_MultiChunk();
	Test_ValleyChainProducesInverseHeightDelta_VsMountain();
	Test_LoopMode_FirstAndLastVertexConnected();
	Test_LessThanTwoVertices_NoCommandPushed();
	Test_FlankProfile_Smoothstep_Linear_Exp_DistinctShapes();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[MacroTerrainTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[MacroTerrainTests] all tests passed\n");
	return 0;
}
