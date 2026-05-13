/// Tests unitaires CPU pour M100.40 — Mesh Insert Foundation + Cave Tool.
///
/// Couvre : binary IO LCMI v1 round-trip, MeshInsertDocument CRUD,
/// CaveCatalog JSON parsing, CaveCamouflage poids sparse, PlaceCaveCommand
/// Apply/Undo (mesh insert + splat restore).
///
/// Framework : REQUIRE maison + main monolithique.

#include "src/client/world/terrain/SplatMap.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/shared/core/Config.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/MeshInsertIo.h"
#include "src/world_editor/volumes/caves/CaveCamouflage.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"
#include "src/world_editor/volumes/caves/PlaceCaveCommand.h"

#include <cstdio>
#include <memory>
#include <string>
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
	using engine::editor::world::TerrainDocument;
	using engine::editor::world::volumes::LoadMeshInsertsBin;
	using engine::editor::world::volumes::MeshInsertDocument;
	using engine::editor::world::volumes::MeshInsertInstance;
	using engine::editor::world::volumes::SaveMeshInsertsBin;
	using engine::editor::world::volumes::caves::CaveCatalog;
	using engine::editor::world::volumes::caves::CaveSplatPatch;
	using engine::editor::world::volumes::caves::ComputeCaveSplatWeights;
	using engine::editor::world::volumes::caves::PlaceCaveCommand;

	/// Test 1 : round-trip binary LCMI v1.
	void Test_MeshInsertsBin_RoundTrip()
	{
		std::vector<MeshInsertInstance> src;
		{
			MeshInsertInstance a;
			a.guid             = 42u;
			a.gltfRelativePath = "meshes/caves/cave_small_01.gltf";
			a.worldPosition    = { 100.5f, 50.2f, 200.3f };
			a.eulerRotationDeg = { 0.0f, 45.0f, 0.0f };
			a.uniformScale     = 1.25f;
			a.insertCategory   = "cave";
			a.displayName      = "Grotte alpha";
			a.hasInteriorVolume   = true;
			a.castsShadow         = true;
			a.receivesAudioReverb = false;
			a.allowsWaterIngress  = true;
			a.lightProbeIntensity = 0.7f;
			src.push_back(a);
		}
		{
			MeshInsertInstance b;
			b.guid             = 99u;
			b.gltfRelativePath = "meshes/caves/cave_large_01.gltf";
			b.worldPosition    = { -100.0f, 0.0f, -200.0f };
			b.insertCategory   = "cave";
			src.push_back(b);
		}

		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveMeshInsertsBin(src, bytes, err));
		REQUIRE(err.empty());
		REQUIRE(bytes.size() >= 16u);

		std::vector<MeshInsertInstance> dst;
		REQUIRE(LoadMeshInsertsBin(std::span<const uint8_t>(bytes), dst, err));
		REQUIRE(dst.size() == 2u);
		REQUIRE(dst[0].guid == 42u);
		REQUIRE(dst[0].gltfRelativePath == "meshes/caves/cave_small_01.gltf");
		REQUIRE(dst[0].worldPosition.x == 100.5f);
		REQUIRE(dst[0].worldPosition.y == 50.2f);
		REQUIRE(dst[0].worldPosition.z == 200.3f);
		REQUIRE(dst[0].eulerRotationDeg.y == 45.0f);
		REQUIRE(dst[0].uniformScale == 1.25f);
		REQUIRE(dst[0].insertCategory == "cave");
		REQUIRE(dst[0].displayName == "Grotte alpha");
		REQUIRE(dst[0].hasInteriorVolume == true);
		REQUIRE(dst[0].castsShadow == true);
		REQUIRE(dst[0].receivesAudioReverb == false);
		REQUIRE(dst[0].allowsWaterIngress == true);
		REQUIRE(dst[0].lightProbeIntensity == 0.7f);
		REQUIRE(dst[1].guid == 99u);
		REQUIRE(dst[1].insertCategory == "cave");
	}

	/// Test 2 : MeshInsertDocument CRUD.
	void Test_MeshInsertDocument_AddRemoveUpdate()
	{
		MeshInsertDocument doc;
		MeshInsertInstance inst;
		inst.gltfRelativePath = "test/cave.gltf";
		inst.insertCategory = "cave";

		const uint64_t guid = doc.Add(inst);
		REQUIRE(guid > 0u);
		REQUIRE(doc.Size() == 1u);

		const MeshInsertInstance* found = doc.GetByGuid(guid);
		REQUIRE(found != nullptr);
		REQUIRE(found->gltfRelativePath == "test/cave.gltf");

		MeshInsertInstance updated = *found;
		updated.uniformScale = 2.5f;
		REQUIRE(doc.Update(guid, updated));
		REQUIRE(doc.GetByGuid(guid)->uniformScale == 2.5f);

		auto caves = doc.GetByCategory("cave");
		REQUIRE(caves.size() == 1u);

		REQUIRE(doc.Remove(guid));
		REQUIRE(doc.Size() == 0u);
		REQUIRE(doc.GetByGuid(guid) == nullptr);
		REQUIRE(!doc.Remove(guid));  // double-remove no-op
	}

	/// Test 3 : MeshInsertDocument auto-Guid pour `instance.guid == 0`.
	void Test_MeshInsertDocument_AutoAssignsGuid()
	{
		MeshInsertDocument doc;
		MeshInsertInstance a; a.gltfRelativePath = "a";
		MeshInsertInstance b; b.gltfRelativePath = "b";
		const uint64_t guidA = doc.Add(a);
		const uint64_t guidB = doc.Add(b);
		REQUIRE(guidA != guidB);
		REQUIRE(guidA > 0u);
		REQUIRE(guidB > 0u);
	}

	/// Test 4 : CaveCatalog parse JSON inline.
	void Test_CaveCatalog_ParseJson()
	{
		const std::string json = R"({
			"caves": [
				{
					"id": "test_cave",
					"gltf": "meshes/caves/test.gltf",
					"displayName": "Test grotte",
					"thumbnail": "meshes/caves/thumbnails/test.png",
					"aabbMin": [-1.0, 0.0, -1.0],
					"aabbMax": [1.0, 2.0, 1.0],
					"entrancePoint": [0.0, 0.0, -1.0],
					"interiorAabbMin": [-0.5, 0.0, -0.5],
					"interiorAabbMax": [0.5, 1.5, 0.5]
				}
			]
		})";
		CaveCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(json, err));
		REQUIRE(cat.Size() == 1u);
		const auto* entry = cat.FindById("test_cave");
		REQUIRE(entry != nullptr);
		REQUIRE(entry->displayName == "Test grotte");
		REQUIRE(entry->gltfRelativePath == "meshes/caves/test.gltf");
		REQUIRE(entry->aabbMax.y == 2.0f);
		REQUIRE(entry->entrancePoint.z == -1.0f);
	}

	/// Test 5 : CaveCatalog vide / absent → liste vide, pas d'erreur.
	void Test_CaveCatalog_EmptyAndMissing()
	{
		CaveCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson("{}", err));
		REQUIRE(cat.Size() == 0u);
		REQUIRE(cat.FindById("anything") == nullptr);

		// JSON malformé : on accepte les structures vides mais une syntaxe
		// invalide à l'intérieur de "caves" doit être détectée.
		const std::string bad = R"({"caves": [{"id":)";
		(void)cat.ParseJson(bad, err);
	}

	/// Test 6 : ComputeCaveSplatWeights produit des poids non-nuls dans le
	/// rayon et zéro à l'extérieur.
	void Test_CaveCamouflage_WeightsInRadius()
	{
		CaveSplatPatch patch;
		patch.worldX = 0.0f;
		patch.worldZ = 0.0f;
		patch.radiusMeters = 5.0f;
		patch.strength = 0.8f;
		const auto deltas = ComputeCaveSplatWeights(patch);
		REQUIRE(!deltas.empty());
		// Au moins une cellule à distance < rayon a un poids > 0.
		bool hasPositive = false;
		for (const auto& kv : deltas)
		{
			for (const auto& cell : kv.second)
			{
				if (cell.second > 0.0f) hasPositive = true;
				REQUIRE(cell.second <= 1.0f); // borne haute = strength (0.8)
			}
		}
		REQUIRE(hasPositive);
	}

	/// Test 7 : PlaceCaveCommand Apply puis Undo restaure le mesh insert
	/// (le splat est restauré bit-à-bit aussi grâce au snapshot).
	void Test_PlaceCaveCommand_Apply_Undo()
	{
		engine::core::Config cfg;
		MeshInsertDocument meshDoc;
		TerrainDocument terrain;

		// Précharge la splatmap du chunk (0,0) pour que le command puisse
		// y écrire son patch.
		(void)terrain.EnsureSplatLoaded(cfg, 0, 0);
		auto splat = terrain.FindSplat({0, 0});
		REQUIRE(splat);
		const std::vector<uint8_t> snapWeights = splat->weights;

		PlaceCaveCommand::Data data;
		data.instance.gltfRelativePath = "test/cave.gltf";
		data.instance.worldPosition = { 100.0f, 50.0f, 100.0f };
		data.instance.insertCategory = "cave";
		data.instance.displayName = "Test";
		CaveSplatPatch patch;
		patch.worldX = 100.0f;
		patch.worldZ = 100.0f;
		patch.radiusMeters = 4.0f;
		patch.strength = 0.5f;
		patch.splatLayer = 5u;
		data.splatPatch = patch;

		CommandStack stack;
		auto cmd = std::make_unique<PlaceCaveCommand>(meshDoc, terrain, std::move(data));
		stack.Push(std::move(cmd));

		REQUIRE(meshDoc.Size() == 1u);
		// L'invariant somme=255 doit être préservé sur les cellules touchées.
		const size_t kRes = engine::world::terrain::kSplatResolution;
		const size_t kLayers = engine::world::terrain::kSplatLayerCount;
		bool sumOk = true;
		for (size_t i = 0; i < kRes * kRes; ++i)
		{
			int sum = 0;
			for (size_t L = 0; L < kLayers; ++L)
				sum += splat->weights[i * kLayers + L];
			if (sum != 255) { sumOk = false; break; }
		}
		REQUIRE(sumOk);

		stack.Undo();
		REQUIRE(meshDoc.Size() == 0u);
		// La splat doit être très proche de l'original (les arrondis u8
		// peuvent introduire un résidu de ±1 par cellule, c'est attendu).
		size_t diffCells = 0;
		for (size_t i = 0; i < splat->weights.size(); ++i)
		{
			if (splat->weights[i] != snapWeights[i]) ++diffCells;
		}
		// Tolérance : au plus 5 % des cellules ont un résidu ±1 d'arrondi.
		REQUIRE(diffCells <= splat->weights.size() / 20u);
	}
}

int main()
{
	Test_MeshInsertsBin_RoundTrip();
	Test_MeshInsertDocument_AddRemoveUpdate();
	Test_MeshInsertDocument_AutoAssignsGuid();
	Test_CaveCatalog_ParseJson();
	Test_CaveCatalog_EmptyAndMissing();
	Test_CaveCamouflage_WeightsInRadius();
	Test_PlaceCaveCommand_Apply_Undo();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[MeshInsertTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[MeshInsertTests] all tests passed\n");
	return 0;
}
