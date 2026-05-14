/// Tests unitaires CPU pour M100.44 — VMap Bridge & Phase 11 Validation
/// (clôture Phase 11 « Volumes 3D »).
///
/// Couvre : TransformLocalAabbToWorld (translation / scale / rotation Y),
/// VMapBridge::Build (cave résolue + portail), LCVC v1 round-trip,
/// Phase11Validator (guids dupliqués, gltf vide, template orphelin,
/// difficulty range incohérent, triggerRadius ≤ 0).

#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/bridge/Phase11Validator.h"
#include "src/world_editor/volumes/bridge/VMapBridge.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::volumes::MeshInsertDocument;
	using engine::editor::world::volumes::MeshInsertInstance;
	using engine::editor::world::volumes::caves::CaveCatalog;
	using engine::editor::world::volumes::dungeons::DungeonCatalog;
	using engine::editor::world::volumes::dungeons::DungeonPortalDocument;
	using engine::editor::world::volumes::dungeons::DungeonPortalInstance;
	namespace vb = engine::editor::world::volumes::bridge;

	bool NearF(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

	/// Translation pure : AABB local [-1,1]³ + position (10, 20, 30),
	/// yaw 0, scale 1 → world [9,11]×[19,21]×[29,31].
	void Test_Transform_Translation()
	{
		engine::math::Vec3 outMin, outMax;
		vb::TransformLocalAabbToWorld(
			{ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f },
			{ 10.0f, 20.0f, 30.0f }, 0.0f, 1.0f, outMin, outMax);
		REQUIRE(NearF(outMin.x, 9.0f));
		REQUIRE(NearF(outMax.x, 11.0f));
		REQUIRE(NearF(outMin.y, 19.0f));
		REQUIRE(NearF(outMax.z, 31.0f));
	}

	/// Scale 2× : AABB local [-1,1]³ → world [-2,2]³ (position 0).
	void Test_Transform_Scale()
	{
		engine::math::Vec3 outMin, outMax;
		vb::TransformLocalAabbToWorld(
			{ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f }, 0.0f, 2.0f, outMin, outMax);
		REQUIRE(NearF(outMin.x, -2.0f));
		REQUIRE(NearF(outMax.x, 2.0f));
		REQUIRE(NearF(outMin.z, -2.0f));
		REQUIRE(NearF(outMax.z, 2.0f));
	}

	/// Rotation Y 90° d'une AABB non cubique : x ↔ z s'échangent, l'AABB
	/// englobante reste conservatrice.
	void Test_Transform_RotationY90()
	{
		engine::math::Vec3 outMin, outMax;
		// AABB local : x ∈ [-4, 4], z ∈ [-1, 1].
		vb::TransformLocalAabbToWorld(
			{ -4.0f, 0.0f, -1.0f }, { 4.0f, 2.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f }, 90.0f, 1.0f, outMin, outMax);
		// Après yaw 90° : l'extent x devient ~[-1,1], l'extent z devient ~[-4,4].
		REQUIRE(NearF(outMin.x, -1.0f));
		REQUIRE(NearF(outMax.x, 1.0f));
		REQUIRE(NearF(outMin.z, -4.0f));
		REQUIRE(NearF(outMax.z, 4.0f));
		REQUIRE(NearF(outMin.y, 0.0f));
		REQUIRE(NearF(outMax.y, 2.0f));
	}

	/// VMapBridge::Build : une cave résolue via catalogue + un portail.
	void Test_VMapBridge_Build()
	{
		CaveCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(R"({
			"caves": [
				{ "id": "c1", "gltf": "meshes/caves/c1.gltf",
				  "aabbMin": [-3, 0, -2], "aabbMax": [3, 4, 2] }
			]
		})", err));

		MeshInsertDocument meshDoc;
		MeshInsertInstance inst;
		inst.gltfRelativePath = "meshes/caves/c1.gltf";
		inst.insertCategory   = "cave";
		inst.worldPosition    = { 100.0f, 50.0f, 100.0f };
		inst.uniformScale     = 1.0f;
		meshDoc.Add(inst);

		DungeonPortalDocument portalDoc;
		DungeonPortalInstance portal;
		portal.dungeonTemplateId = "dungeon_x";
		portal.worldPosition     = { 0.0f, 0.0f, 0.0f };
		portal.triggerRadius     = 5.0f;
		portalDoc.Add(portal);

		vb::VMapBridge bridge;
		bridge.SetCaveCatalog(&cat);
		size_t unresolved = 0u;
		bridge.Build(meshDoc, portalDoc, unresolved);

		REQUIRE(bridge.Size() == 2u);
		REQUIRE(unresolved == 0u);
		// Cave proxy : aabb local [-3,3]×[0,4]×[-2,2] + pos (100,50,100).
		const auto& proxies = bridge.Proxies();
		bool foundCave = false, foundPortal = false;
		for (const auto& p : proxies)
		{
			if (p.volumeKind == vb::kVolumeKindCave)
			{
				foundCave = true;
				REQUIRE(NearF(p.worldMin.x, 97.0f));
				REQUIRE(NearF(p.worldMax.x, 103.0f));
				REQUIRE(NearF(p.worldMin.y, 50.0f));
				REQUIRE(NearF(p.worldMax.y, 54.0f));
			}
			else if (p.volumeKind == vb::kVolumeKindDungeonPortal)
			{
				foundPortal = true;
				// cube ±triggerRadius autour de la position.
				REQUIRE(NearF(p.worldMin.x, -5.0f));
				REQUIRE(NearF(p.worldMax.x, 5.0f));
			}
		}
		REQUIRE(foundCave);
		REQUIRE(foundPortal);
	}

	/// VMapBridge : mesh insert sans catalogue résolu → proxy dégénéré +
	/// outUnresolvedCount incrémenté.
	void Test_VMapBridge_UnresolvedProxy()
	{
		MeshInsertDocument meshDoc;
		MeshInsertInstance inst;
		inst.gltfRelativePath = "meshes/caves/ghost.gltf";
		inst.insertCategory   = "cave";
		inst.worldPosition    = { 1.0f, 2.0f, 3.0f };
		meshDoc.Add(inst);

		DungeonPortalDocument portalDoc;
		vb::VMapBridge bridge; // pas de catalogue set
		size_t unresolved = 0u;
		bridge.Build(meshDoc, portalDoc, unresolved);
		REQUIRE(bridge.Size() == 1u);
		REQUIRE(unresolved == 1u);
		// Proxy dégénéré : cube ±0.5 autour de la position.
		REQUIRE(NearF(bridge.Proxies()[0].worldMin.x, 0.5f));
		REQUIRE(NearF(bridge.Proxies()[0].worldMax.x, 1.5f));
	}

	/// LCVC v1 round-trip : la cave est résolue via catalogue → son proxy
	/// porte kVolumeKindCave et survit au round-trip binaire.
	void Test_LCVC_RoundTrip()
	{
		CaveCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(R"({
			"caves": [
				{ "id": "rt", "gltf": "rt.gltf",
				  "aabbMin": [-1, 0, -1], "aabbMax": [1, 2, 1] }
			]
		})", err));

		MeshInsertDocument meshDoc;
		MeshInsertInstance inst;
		inst.gltfRelativePath = "rt.gltf";
		inst.insertCategory   = "cave";
		inst.worldPosition    = { 7.0f, 8.0f, 9.0f };
		inst.uniformScale     = 1.0f;
		meshDoc.Add(inst);
		DungeonPortalDocument portalDoc;

		vb::VMapBridge bridge;
		bridge.SetCaveCatalog(&cat);
		size_t unresolved = 0u;
		bridge.Build(meshDoc, portalDoc, unresolved);
		REQUIRE(unresolved == 0u);

		std::vector<uint8_t> bytes;
		REQUIRE(bridge.Serialize(bytes, err));
		REQUIRE(bytes.size() >= 16u);

		std::vector<vb::VolumeAabbProxy> decoded;
		REQUIRE(vb::VMapBridge::Deserialize(std::span<const uint8_t>(bytes), decoded, err));
		REQUIRE(decoded.size() == 1u);
		REQUIRE(decoded[0].volumeKind == vb::kVolumeKindCave);
		REQUIRE(decoded[0].sourceGuid == meshDoc.All()[0].guid);
	}

	void Test_LCVC_BadMagic()
	{
		std::vector<uint8_t> bytes(16u, 0x00);
		std::vector<vb::VolumeAabbProxy> decoded;
		std::string err;
		REQUIRE(!vb::VMapBridge::Deserialize(std::span<const uint8_t>(bytes), decoded, err));
	}

	/// Validator : document propre → 0 erreur.
	void Test_Validator_CleanDocs()
	{
		MeshInsertDocument meshDoc;
		DungeonPortalDocument portalDoc;
		vb::Phase11Validator validator;
		const auto report = validator.Validate(meshDoc, portalDoc);
		REQUIRE(report.errorCount == 0u);
		// Docs vides → 1 info "aucun volume placé".
		REQUIRE(report.infoCount >= 1u);
	}

	/// Validator : mesh insert avec gltf vide → erreur bloquante.
	void Test_Validator_EmptyGltf()
	{
		MeshInsertDocument meshDoc;
		MeshInsertInstance inst;
		inst.gltfRelativePath = ""; // vide
		inst.insertCategory   = "cave";
		meshDoc.Add(inst);
		DungeonPortalDocument portalDoc;

		vb::Phase11Validator validator;
		const auto report = validator.Validate(meshDoc, portalDoc);
		REQUIRE(report.errorCount >= 1u);
		REQUIRE(report.HasBlockingErrors());
	}

	/// Validator : portail dont le template est absent du catalogue → erreur.
	void Test_Validator_OrphanTemplate()
	{
		DungeonCatalog cat;
		std::string err;
		REQUIRE(cat.ParseJson(R"({"dungeons":[{"id":"known_dungeon"}]})", err));

		MeshInsertDocument meshDoc;
		DungeonPortalDocument portalDoc;
		DungeonPortalInstance portal;
		portal.dungeonTemplateId = "UNKNOWN_dungeon"; // pas dans le catalogue
		portal.triggerRadius     = 3.0f;
		portal.minDifficulty     = 1u;
		portal.maxDifficulty     = 1u;
		portalDoc.Add(portal);

		vb::Phase11Validator validator;
		validator.SetDungeonCatalog(&cat);
		const auto report = validator.Validate(meshDoc, portalDoc);
		REQUIRE(report.HasBlockingErrors());
	}

	/// Validator : triggerRadius ≤ 0 + difficulty range incohérent → erreurs.
	void Test_Validator_BadPortalParams()
	{
		MeshInsertDocument meshDoc;
		DungeonPortalDocument portalDoc;
		DungeonPortalInstance portal;
		portal.dungeonTemplateId = "d";
		portal.triggerRadius     = 0.0f;  // inactivable
		portal.minDifficulty     = 3u;
		portal.maxDifficulty     = 1u;    // incohérent
		portalDoc.Add(portal);

		vb::Phase11Validator validator; // pas de catalogue → warning supplémentaire
		const auto report = validator.Validate(meshDoc, portalDoc);
		REQUIRE(report.errorCount >= 2u); // triggerRadius + difficulty range
	}

	/// Validator : guid dupliqué détecté.
	void Test_Validator_DuplicateGuid()
	{
		MeshInsertDocument meshDoc;
		MeshInsertInstance a;
		a.guid = 100u; a.gltfRelativePath = "x"; a.insertCategory = "cave";
		MeshInsertInstance b;
		b.guid = 100u; b.gltfRelativePath = "y"; b.insertCategory = "cave";
		meshDoc.Add(a);
		meshDoc.Add(b); // même guid forcé
		DungeonPortalDocument portalDoc;

		vb::Phase11Validator validator;
		const auto report = validator.Validate(meshDoc, portalDoc);
		REQUIRE(report.HasBlockingErrors());
	}
}

int main()
{
	Test_Transform_Translation();
	Test_Transform_Scale();
	Test_Transform_RotationY90();
	Test_VMapBridge_Build();
	Test_VMapBridge_UnresolvedProxy();
	Test_LCVC_RoundTrip();
	Test_LCVC_BadMagic();
	Test_Validator_CleanDocs();
	Test_Validator_EmptyGltf();
	Test_Validator_OrphanTemplate();
	Test_Validator_BadPortalParams();
	Test_Validator_DuplicateGuid();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[Phase11BridgeTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[Phase11BridgeTests] all tests passed\n");
	return 0;
}
