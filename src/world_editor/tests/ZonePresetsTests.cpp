/// Tests unitaires CPU pour M100.46 — Zone Presets Library.
///
/// Couvre les incréments 1 (data : JSON / validation / registry), 2a
/// (OperationParams + CustomizationApplier), 2b (Reset + dispatcher
/// `place_*` + executor), 2c (place_arch), 2d (mountain_macro /
/// valley_macro / lake_polygon / river_manual). L'UI (incrément 3)
/// vient avec ses propres tests.

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/shared/core/Config.h"
#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/arches/ArchCatalog.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/overhangs/OverhangCatalog.h"
#include "src/world_editor/water/HeightGridAssembly.h"
#include "src/world_editor/water/WaterDocument.h"
#include "src/world_editor/zone_presets/CustomizationApplier.h"
#include "src/world_editor/zone_presets/OperationDispatcher.h"
#include "src/world_editor/zone_presets/OperationParams.h"
#include "src/world_editor/zone_presets/WorldMapEditDocumentReset.h"
#include "src/world_editor/zone_presets/ZonePreset.h"
#include "src/world_editor/zone_presets/ZonePresetExecutor.h"
#include "src/world_editor/zone_presets/ZonePresetIo.h"
#include "src/world_editor/zone_presets/ZonePresetRegistry.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

	namespace zp = engine::editor::world::zone_presets;

	const char* kValidPreset = R"({
		"version": 1,
		"id": "test_zone",
		"displayName": { "fr": "Zone test", "en": "Test zone" },
		"description": { "fr": "Une zone de test.", "en": "A test zone." },
		"thumbnail": "thumbnails/test_zone.png",
		"tags": ["forest", "humid"],
		"estimatedExecutionSeconds": 42,
		"operations": [
			{
				"type": "mountain_macro",
				"preset": "pre_alpine",
				"polyline": [[1000, 4000], [3000, 4000]],
				"heightMeters": 600,
				"affectedBy": ["relief"]
			},
			{
				"type": "hydraulic_erosion",
				"preset": "realistic",
				"numDroplets": 80000,
				"rngSeed": "global",
				"affectedBy": ["water_density", "dryness"]
			}
		],
		"decoration": []
	})";

	std::filesystem::path MakeTempDir(const char* tag)
	{
		const auto base = std::filesystem::temp_directory_path()
			/ ("lcdlln_m10046_" + std::string(tag));
		std::error_code ec;
		std::filesystem::remove_all(base, ec);
		std::filesystem::create_directories(base / "editor" / "zone_presets", ec);
		return base;
	}

	void WriteFile(const std::filesystem::path& p, const std::string& content)
	{
		std::ofstream f(p, std::ios::binary | std::ios::trunc);
		f.write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	// --- ZonePresetIo -------------------------------------------------------

	void Test_ParsesValidPreset()
	{
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(kValidPreset, preset, err));
		REQUIRE(preset.version == 1);
		REQUIRE(preset.id == "test_zone");
		REQUIRE(preset.displayName.fr == "Zone test");
		REQUIRE(preset.displayName.en == "Test zone");
		REQUIRE(preset.description.fr == "Une zone de test.");
		REQUIRE(preset.thumbnailPath == "thumbnails/test_zone.png");
		REQUIRE(preset.tags.size() == 2u);
		REQUIRE(preset.tags[0] == "forest");
		REQUIRE(preset.estimatedExecutionSeconds == 42.0f);
		REQUIRE(preset.operations.size() == 2u);
		REQUIRE(preset.operations[0].type == "mountain_macro");
		REQUIRE(preset.operations[0].toolPresetId == "pre_alpine");
		REQUIRE(preset.operations[0].affectedBy.size() == 1u);
		REQUIRE(preset.operations[0].affectedBy[0] == "relief");
		REQUIRE(preset.operations[1].type == "hydraulic_erosion");
		REQUIRE(preset.operations[1].affectedBy.size() == 2u);
		REQUIRE(preset.decorationEntryCount == 0u);
	}

	void Test_RejectsMissingId()
	{
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(!zp::ParseZonePresetJson(R"({"operations":[]})", preset, err));
		REQUIRE(!err.empty());
	}

	void Test_RejectsMissingOperations()
	{
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(!zp::ParseZonePresetJson(R"({"id":"x"})", preset, err));
	}

	/// Le rawJson d'une opération conserve l'objet complet (le futur
	/// OperationDispatcher en extraira les params typés).
	void Test_OperationRawJsonPreserved()
	{
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(kValidPreset, preset, err));
		REQUIRE(preset.operations[0].rawJson.front() == '{');
		REQUIRE(preset.operations[0].rawJson.back() == '}');
		REQUIRE(preset.operations[0].rawJson.find("\"mountain_macro\"") != std::string::npos);
		REQUIRE(preset.operations[0].rawJson.find("polyline") != std::string::npos);
	}

	/// Une section decoration non vide est comptée (Validate la rejettera).
	void Test_DecorationCountedWhenPresent()
	{
		const char* withDeco = R"({
			"id": "d", "operations": [{"type":"coastline"}],
			"decoration": [ {"type":"future_a"}, {"type":"future_b"} ]
		})";
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(withDeco, preset, err));
		REQUIRE(preset.decorationEntryCount == 2u);
	}

	// --- ZonePreset::Validate ----------------------------------------------

	void Test_ValidateAcceptsCleanPreset()
	{
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(kValidPreset, preset, err));
		REQUIRE(preset.Validate(err));
		REQUIRE(err.empty());
	}

	void Test_ValidateRejectsUnknownToolId()
	{
		const char* bad = R"({"id":"x","operations":[{"type":"nonexistent_tool"}]})";
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(bad, preset, err));
		REQUIRE(!preset.Validate(err));
		REQUIRE(err.find("nonexistent_tool") != std::string::npos);
	}

	void Test_ValidateRejectsUnknownAffectedBy()
	{
		const char* bad = R"({"id":"x","operations":[
			{"type":"mountain_macro","affectedBy":["relief","bogus_tag"]}]})";
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(bad, preset, err));
		REQUIRE(!preset.Validate(err));
		REQUIRE(err.find("bogus_tag") != std::string::npos);
	}

	void Test_ValidateRejectsEmptyOperations()
	{
		zp::ZonePreset preset;
		preset.id = "x";
		std::string err;
		REQUIRE(!preset.Validate(err));  // operations vide
	}

	/// La décoration est réservée Phase 13 : un preset MVP avec une
	/// section decoration non vide est rejeté par Validate.
	void Test_ValidateRejectsNonEmptyDecoration()
	{
		const char* withDeco = R"({
			"id": "d", "operations": [{"type":"coastline"}],
			"decoration": [ {"type":"future_thing"} ]
		})";
		zp::ZonePreset preset;
		std::string err;
		REQUIRE(zp::ParseZonePresetJson(withDeco, preset, err));
		REQUIRE(!preset.Validate(err));
		REQUIRE(err.find("decoration") != std::string::npos);
	}

	void Test_KnownTypeHelpers()
	{
		REQUIRE(zp::IsKnownOperationType("mountain_macro"));
		REQUIRE(zp::IsKnownOperationType("place_dungeon"));
		REQUIRE(!zp::IsKnownOperationType("teleporter"));
		REQUIRE(zp::IsKnownAffectedByTag("relief"));
		REQUIRE(zp::IsKnownAffectedByTag("water_density"));
		REQUIRE(zp::IsKnownAffectedByTag("dryness"));
		REQUIRE(!zp::IsKnownAffectedByTag("humidity"));
	}

	void Test_LocalizedStringFallback()
	{
		zp::LocalizedString s;
		s.fr = "bonjour";
		s.en = "hello";
		REQUIRE(s.Get("fr") == "bonjour");
		REQUIRE(s.Get("en") == "hello");
		REQUIRE(s.Get("de") == "bonjour");  // fallback fr

		zp::LocalizedString frOnly;
		frOnly.fr = "salut";
		REQUIRE(frOnly.Get("en") == "salut");  // fallback fr quand en vide
	}

	// --- ZonePresetRegistry -------------------------------------------------

	void Test_RegistryLoadsValidPresetsSkipsBad()
	{
		const auto root = MakeTempDir("registry");
		const auto dir = root / "editor" / "zone_presets";
		WriteFile(dir / "good_a.json",
			R"({"id":"good_a","operations":[{"type":"coastline"}],"decoration":[]})");
		WriteFile(dir / "good_b.json",
			R"({"id":"good_b","operations":[{"type":"river_network","affectedBy":["water_density"]}]})");
		// type inconnu → rejeté par Validate
		WriteFile(dir / "bad_type.json",
			R"({"id":"bad","operations":[{"type":"warp_drive"}]})");
		// JSON malformé → rejeté au parse
		WriteFile(dir / "corrupt.json", R"({ not json )");

		auto& reg = zp::ZonePresetRegistry::Instance();
		reg.ResetForTesting();
		const size_t loaded = reg.LoadFromContentPath(root.string());
		REQUIRE(loaded == 2u);
		REQUIRE(reg.Size() == 2u);
		REQUIRE(reg.FindById("good_a") != nullptr);
		REQUIRE(reg.FindById("good_b") != nullptr);
		REQUIRE(reg.FindById("bad") == nullptr);
		// Tri stable par id.
		REQUIRE(reg.Presets()[0].id == "good_a");
		REQUIRE(reg.Presets()[1].id == "good_b");

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		reg.ResetForTesting();
	}

	void Test_RegistryMissingDirIsNotAnError()
	{
		auto& reg = zp::ZonePresetRegistry::Instance();
		reg.ResetForTesting();
		const auto missing = std::filesystem::temp_directory_path()
			/ "lcdlln_m10046_does_not_exist_zzz";
		REQUIRE(reg.LoadFromContentPath(missing.string()) == 0u);
		REQUIRE(reg.Size() == 0u);
		reg.ResetForTesting();
	}

	// --- OperationParams (incrément 2a) ------------------------------------

	bool NearEq(double a, double b) { return std::fabs(a - b) < 1e-6; }

	/// Parse scalaires (nombre, bool, string) + saute les clés
	/// structurelles type/affectedBy. P1 (audit 2026-06-05, 6.4) : "preset"
	/// N'est PLUS filtrée — `MaybeApplyToolPreset` (OperationDispatcher) la
	/// lit via `GetString("preset")` ; la filtrer rendait l'overlay de
	/// tool-preset silencieusement inopérant.
	void Test_OpParams_ParsesScalars()
	{
		const std::string raw = R"({
			"type": "hydraulic_erosion", "preset": "realistic",
			"affectedBy": ["water_density"],
			"numDroplets": 80000, "rngSeed": "global",
			"carvingEnabled": true, "windEnabled": false
		})";
		const auto p = zp::OperationParams::Parse(raw);
		// type/affectedBy ne sont PAS dans les params ; "preset" y est (6.4).
		REQUIRE(!p.Has("type"));
		std::string presetId;
		REQUIRE(p.GetString("preset", presetId));
		REQUIRE(presetId == "realistic");
		REQUIRE(!p.Has("affectedBy"));
		double n = 0.0;
		REQUIRE(p.GetNumber("numDroplets", n));
		REQUIRE(NearEq(n, 80000.0));
		std::string s;
		REQUIRE(p.GetString("rngSeed", s));
		REQUIRE(s == "global");
		bool b = false;
		REQUIRE(p.GetBool("carvingEnabled", b) && b == true);
		REQUIRE(p.GetBool("windEnabled", b) && b == false);
		// mauvais type → false, out inchangé
		double dummy = -1.0;
		REQUIRE(!p.GetNumber("rngSeed", dummy));
		REQUIRE(NearEq(dummy, -1.0));
	}

	/// Les listes de coordonnées [[x,z],…] et [x,y,z] sont aplaties.
	void Test_OpParams_FlattensCoordLists()
	{
		const std::string raw = R"({
			"type": "mountain_macro",
			"polyline": [[1000, 4000], [3000, 4200], [5000, 4000]],
			"worldPosition": [3200, 0, 800]
		})";
		const auto p = zp::OperationParams::Parse(raw);
		std::vector<double> poly;
		REQUIRE(p.GetNumberList("polyline", poly));
		REQUIRE(poly.size() == 6u);  // 3 paires aplaties
		REQUIRE(NearEq(poly[0], 1000.0));
		REQUIRE(NearEq(poly[3], 4200.0));
		std::vector<double> pos;
		REQUIRE(p.GetNumberList("worldPosition", pos));
		REQUIRE(pos.size() == 3u);
		REQUIRE(NearEq(pos[2], 800.0));
	}

	/// ScaleNumber multiplie un scalaire, no-op sur clé absente/non-nombre.
	void Test_OpParams_ScaleNumber()
	{
		auto p = zp::OperationParams::Parse(R"({"heightMeters": 600, "label": "x"})");
		p.ScaleNumber("heightMeters", 2.0);
		double h = 0.0;
		REQUIRE(p.GetNumber("heightMeters", h));
		REQUIRE(NearEq(h, 1200.0));
		// no-op sur clé absente et sur clé non-numérique (pas de crash)
		p.ScaleNumber("absent", 3.0);
		p.ScaleNumber("label", 3.0);
		std::string s;
		REQUIRE(p.GetString("label", s) && s == "x");
	}

	/// Objet vide / malformé → params vides, pas de crash.
	void Test_OpParams_TolerantToMalformed()
	{
		REQUIRE(zp::OperationParams::Parse("").Size() == 0u);
		REQUIRE(zp::OperationParams::Parse("not json").Size() == 0u);
		REQUIRE(zp::OperationParams::Parse("{}").Size() == 0u);
	}

	// --- CustomizationApplier (incrément 2a) -------------------------------

	void Test_Customization_IsNeutral()
	{
		zp::CustomizationParams def;
		REQUIRE(def.IsNeutral());
		zp::CustomizationParams tweaked;
		tweaked.reliefMultiplier = 2.0f;
		REQUIRE(!tweaked.IsNeutral());
	}

	/// relief ×2 double heightMeters/depthMeters d'une op taguée "relief".
	void Test_Customization_ReliefScalesHeights()
	{
		auto p = zp::OperationParams::Parse(
			R"({"heightMeters": 600, "depthMeters": 80, "widthMeters": 1200})");
		zp::CustomizationParams custom;
		custom.reliefMultiplier = 2.0f;
		zp::ApplyCustomization(p, { "relief" }, custom);
		double h = 0.0, d = 0.0, w = 0.0;
		REQUIRE(p.GetNumber("heightMeters", h) && NearEq(h, 1200.0));
		REQUIRE(p.GetNumber("depthMeters", d) && NearEq(d, 160.0));
		// widthMeters n'est pas affecté par relief.
		REQUIRE(p.GetNumber("widthMeters", w) && NearEq(w, 1200.0));
	}

	/// Une op SANS le tag pertinent n'est pas touchée.
	void Test_Customization_UnaffectedOpUnchanged()
	{
		auto p = zp::OperationParams::Parse(R"({"heightMeters": 600})");
		zp::CustomizationParams custom;
		custom.reliefMultiplier = 0.5f;
		// affectedBy ne contient pas "relief" → heightMeters inchangé.
		zp::ApplyCustomization(p, { "dryness" }, custom);
		double h = 0.0;
		REQUIRE(p.GetNumber("heightMeters", h) && NearEq(h, 600.0));
	}

	/// water_density et dryness ciblent leurs propres champs.
	void Test_Customization_WaterAndDryness()
	{
		auto p = zp::OperationParams::Parse(
			R"({"numDroplets": 80000, "evaporationRate": 0.02, "windStrength": 0.4})");
		zp::CustomizationParams custom;
		custom.waterDensityMultiplier = 2.0f;
		custom.drynessMultiplier      = 1.5f;
		zp::ApplyCustomization(p, { "water_density", "dryness" }, custom);
		double dr = 0.0, ev = 0.0, ws = 0.0;
		REQUIRE(p.GetNumber("numDroplets", dr) && NearEq(dr, 160000.0));
		REQUIRE(p.GetNumber("evaporationRate", ev) && NearEq(ev, 0.03));
		REQUIRE(p.GetNumber("windStrength", ws) && NearEq(ws, 0.6));
	}

	/// Curseurs à 1.0 → params identiques (exécution « preset brut »).
	void Test_Customization_NeutralLeavesParamsIdentical()
	{
		auto p = zp::OperationParams::Parse(R"({"heightMeters": 600, "numDroplets": 80000})");
		zp::ApplyCustomization(p, { "relief", "water_density" }, zp::CustomizationParams{});
		double h = 0.0, n = 0.0;
		REQUIRE(p.GetNumber("heightMeters", h) && NearEq(h, 600.0));
		REQUIRE(p.GetNumber("numDroplets", n) && NearEq(n, 80000.0));
	}

	// --- WorldMapEditDocumentReset (incrément 2b) --------------------------

	namespace ew = engine::editor::world;
	namespace vol = engine::editor::world::volumes;

	/// Reset vide les 4 documents en mémoire.
	void Test_WorldMapEditDocumentReset_ClearsAllDocs()
	{
		ew::TerrainDocument terrain;
		ew::WaterDocument   water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		// Pré-remplissage minimal.
		vol::MeshInsertInstance mi;
		mi.gltfRelativePath = "x"; mi.insertCategory = "cave";
		meshDoc.Add(mi);
		vol::dungeons::DungeonPortalInstance dp;
		dp.dungeonTemplateId = "y";
		portalDoc.Add(dp);
		REQUIRE(meshDoc.Size() == 1u);
		REQUIRE(portalDoc.Size() == 1u);

		zp::ResetEditedZoneDocuments(terrain, water, meshDoc, portalDoc);
		REQUIRE(meshDoc.Size() == 0u);
		REQUIRE(portalDoc.Size() == 0u);
		// terrain/water n'ont pas de Size() public mais leur Reset est appelé.
	}

	// --- OperationDispatcher (incrément 2b) --------------------------------

	zp::DispatchContext MakeMinimalCtx(
		ew::TerrainDocument& t, ew::WaterDocument& w,
		vol::MeshInsertDocument& m,
		vol::dungeons::DungeonPortalDocument& d,
		vol::caves::CaveCatalog& cv, vol::overhangs::OverhangCatalog& oh,
		vol::arches::ArchCatalog& ar,
		vol::dungeons::DungeonCatalog& dc)
	{
		return zp::DispatchContext{ t, w, m, d, cv, oh, ar, dc };
	}

	/// place_cave : trouve l'entrée catalogue, construit une commande
	/// PlaceCaveCommand qui exécutée pousse l'instance dans le doc.
	void Test_Dispatcher_PlaceCave_BuildsCommand()
	{
		vol::caves::CaveCatalog caveCat;
		std::string err;
		REQUIRE(caveCat.ParseJson(R"({
			"caves":[{"id":"c1","gltf":"caves/c1.gltf","displayName":"Petite grotte",
				"aabbMin":[-2,0,-2],"aabbMax":[2,3,2],"entrancePoint":[0,0,-2]}]
		})", err));
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePresetOperation op;
		op.type    = "place_cave";
		op.rawJson = R"({
			"type":"place_cave",
			"catalogId":"c1",
			"worldPosition":[100,50,200],
			"rotationY":45,
			"autoSnapToGround":true
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);

		stack.Push(std::move(cmd));
		REQUIRE(meshDoc.Size() == 1u);
		REQUIRE(meshDoc.All()[0].insertCategory == "cave");
		REQUIRE(meshDoc.All()[0].gltfRelativePath == "caves/c1.gltf");
		// Snap au sol : y = 50 - entrancePoint.y(=0) = 50 (entrancePoint.y
		// est 0 dans le catalogue ci-dessus).
		REQUIRE(NearEq(meshDoc.All()[0].worldPosition.y, 50.0));
	}

	/// place_cave avec catalogId introuvable → Failed.
	void Test_Dispatcher_PlaceCave_UnknownCatalog()
	{
		vol::caves::CaveCatalog caveCat;
		std::string err;
		(void)caveCat.ParseJson(R"({"caves":[]})", err);
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "place_cave";
		op.rawJson = R"({"type":"place_cave","catalogId":"ghost","worldPosition":[0,0,0]})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
		REQUIRE(cmd == nullptr);
	}

	/// place_dungeon : catalogId du dungeon catalog, instance créée.
	void Test_Dispatcher_PlaceDungeon_BuildsCommand()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		std::string err;
		REQUIRE(dgCat.ParseJson(R"({
			"dungeons":[{"id":"dt","displayName":"Donjon test","requiredLevel":15,
				"minDifficulty":1,"maxDifficulty":2}]
		})", err));

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePresetOperation op;
		op.type    = "place_dungeon";
		op.rawJson = R"({
			"type":"place_dungeon","catalogId":"dt",
			"worldPosition":[800,0,800],"rotationY":90,
			"triggerRadius":4
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		stack.Push(std::move(cmd));
		REQUIRE(portalDoc.Size() == 1u);
		REQUIRE(portalDoc.All()[0].dungeonTemplateId == "dt");
		REQUIRE(NearEq(portalDoc.All()[0].triggerRadius, 4.0));
		REQUIRE(portalDoc.All()[0].requiredLevel == 15u); // depuis le catalog
	}

	/// place_arch : géométrie dérivée (midpoint, yaw, scale) à partir
	/// des deux piliers + résolution du catalog. Mirror de ArchTool::Place.
	void Test_Dispatcher_PlaceArch_DerivesGeometry()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		std::string err;
		// Arche native : pillarA=[-2,0,0], pillarB=[2,0,0] → span natif = 4 m.
		REQUIRE(arCat.ParseJson(R"({
			"arches":[{"id":"a1","gltf":"arches/a1.gltf","displayName":"Arche test",
				"archAnchorA":[-2.0,0.0,0.0],"archAnchorB":[2.0,0.0,0.0],
				"archHeight":3.0,"aabbMin":[-3,0,-1],"aabbMax":[3,4,1]}]
		})", err));

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePresetOperation op;
		op.type    = "place_arch";
		// Monde : A=(0,10,0), B=(8,10,0) → span = 8 m, scale = 8/4 = 2,
		// midpoint = (4,10,0), yaw = atan2(0,8) = 0 deg.
		op.rawJson = R"({
			"type":"place_arch","catalogId":"a1",
			"pillarA":[0,10,0],"pillarB":[8,10,0]
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		stack.Push(std::move(cmd));
		REQUIRE(meshDoc.Size() == 1u);
		const auto& inst = meshDoc.All()[0];
		REQUIRE(inst.insertCategory == "arch");
		REQUIRE(NearEq(inst.worldPosition.x, 4.0));    // midpoint
		REQUIRE(NearEq(inst.worldPosition.y, 10.0));
		REQUIRE(NearEq(inst.worldPosition.z, 0.0));
		REQUIRE(NearEq(inst.uniformScale, 2.0));        // 8 / 4
		REQUIRE(std::fabs(inst.eulerRotationDeg.y) < 0.1f); // yaw ≈ 0 deg
	}

	// --- Incrément 2d : mountain_macro / valley_macro / lake_polygon /
	//                    river_manual

	/// mountain_macro : polyline → rasterisation → MountainRangeCommand
	/// avec deltas non vides (au moins un chunk touché).
	void Test_Dispatcher_MountainMacro_RasterizesAndBuildsCommand()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePresetOperation op;
		op.type    = "mountain_macro";
		// Polyline traversant 2-3 chunks (kChunkSize = 500 m). Hauteur 100 m,
		// largeur 400 m → assure une rasterisation non vide.
		op.rawJson = R"({
			"type":"mountain_macro",
			"polyline":[[200,200],[700,200],[1200,200]],
			"widthMeters":400,
			"heightMeters":100
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);
		// On ne pousse pas la commande (Execute toucherait des chunks que le
		// TerrainDocument doit charger via Config — non disponible en test).
		// L'objectif est de valider le wiring dispatcher → rasterizer →
		// constructeur de commande.
	}

	/// mountain_macro avec polyline trop courte → Failed.
	void Test_Dispatcher_MountainMacro_RejectsBadPolyline()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "mountain_macro";
		op.rawJson = R"({"type":"mountain_macro","polyline":[[100,100]],"widthMeters":200,"heightMeters":50})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
		REQUIRE(cmd == nullptr);
	}

	/// valley_macro : même polyline que mountain_macro, mais le rasterizer
	/// est invoqué avec `invert=true`. On vérifie que la commande retournée
	/// est bien un ValleyChainCommand (label "Valley Chain").
	void Test_Dispatcher_ValleyMacro_BuildsValleyCommand()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "valley_macro";
		op.rawJson = R"({
			"type":"valley_macro",
			"polyline":[[300,300],[800,400],[1300,500]],
			"widthMeters":300,
			"heightMeters":80
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);
		REQUIRE(std::string(cmd->GetLabel()) == "Valley Chain");
	}

	/// lake_polygon : polygone fermé → LakeInstance avec polygon Y=waterLevel,
	/// AddLakeCommand exécutée → lac présent dans WaterDocument.
	void Test_Dispatcher_LakePolygon_BuildsCommand()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePresetOperation op;
		op.type    = "lake_polygon";
		op.rawJson = R"({
			"type":"lake_polygon",
			"polygon":[[100,100],[200,100],[200,200],[100,200]],
			"waterLevel":12.5
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);

		stack.Push(std::move(cmd));
		REQUIRE(water.Get().lakes.size() == 1u);
		const auto& lake = water.Get().lakes[0];
		REQUIRE(lake.polygon.size() == 4u);
		REQUIRE(NearEq(lake.waterLevelY, 12.5));
		REQUIRE(NearEq(lake.polygon[0].y, 12.5)); // Y rempli depuis waterLevel
	}

	/// lake_polygon avec polygon de 2 points → Failed (besoin ≥ 3 points).
	void Test_Dispatcher_LakePolygon_RejectsDegenerate()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "lake_polygon";
		op.rawJson = R"({"type":"lake_polygon","polygon":[[0,0],[100,0]],"waterLevel":5})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
	}

	/// river_manual : polyline → RiverInstance avec nodes width/depth par
	/// défaut, Y = 0 (MVP).
	void Test_Dispatcher_RiverManual_BuildsCommand()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePresetOperation op;
		op.type    = "river_manual";
		op.rawJson = R"({
			"type":"river_manual",
			"polyline":[[100,200],[500,250],[900,200]],
			"widthMeters":6,
			"depthMeters":2
		})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);

		stack.Push(std::move(cmd));
		REQUIRE(water.Get().rivers.size() == 1u);
		const auto& river = water.Get().rivers[0];
		REQUIRE(river.nodes.size() == 3u);
		REQUIRE(NearEq(river.nodes[1].widthMeters, 6.0));
		REQUIRE(NearEq(river.nodes[1].depthMeters, 2.0));
		REQUIRE(NearEq(river.nodes[1].position.y, 0.0)); // Y = 0 en MVP
	}

	/// `splat_paint` reste Unsupported après l'incrément 2e (action ponctuelle,
	/// pas une op batch déterministe). Ne crash pas, ne lève rien.
	void Test_Dispatcher_UnsupportedType_GracefulSkip()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "splat_paint";
		op.rawJson = R"({"type":"splat_paint","layerIndex":2,"strength":0.5})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Unsupported);
		REQUIRE(cmd == nullptr);
	}

	// --- Helper partagé : BuildGridFromLoadedChunks --------------------

	/// `BuildGridFromLoadedChunks` doit produire une grille bien
	/// dimensionnée (2×(kRes-1)+1 par axe) avec heights initiales à 0
	/// quand aucun chunk n'existe sur disque (EnsureLoaded crée des
	/// chunks plats par défaut).
	void Test_HeightGridAssembly_BuildsFlat()
	{
		engine::core::Config cfg;
		ew::TerrainDocument terrain;

		const auto grid = ew::BuildGridFromLoadedChunks(terrain, cfg);

		const int kRes = static_cast<int>(engine::world::terrain::kTerrainResolution);
		const int expectedDim = 2 * (kRes - 1) + 1;
		REQUIRE(grid.width  == expectedDim);
		REQUIRE(grid.height == expectedDim);
		REQUIRE(grid.heights.size() ==
			static_cast<size_t>(expectedDim) * expectedDim);
		REQUIRE(NearEq(grid.cellSizeMeters,
			engine::world::terrain::kTerrainCellSizeMeters));
		REQUIRE(grid.originCellX == 0);
		REQUIRE(grid.originCellZ == 0);

		// Toutes les heights sont à 0 : EnsureLoaded crée des chunks plats
		// quand aucun terrain.bin n'existe sur disque.
		for (float h : grid.heights)
		{
			REQUIRE(NearEq(h, 0.0));
		}

		// EnsureLoaded a chargé 2x2 chunks = 4.
		REQUIRE(terrain.LoadedChunkCount() == 4u);
	}

	/// Hauteurs non nulles : on injecte une valeur dans un chunk déjà chargé
	/// avant l'appel et on vérifie qu'elle se retrouve dans la grille.
	void Test_HeightGridAssembly_PreservesNonZeroHeights()
	{
		engine::core::Config cfg;
		ew::TerrainDocument terrain;
		// Pré-charge le chunk (0,0) et injecte une hauteur connue.
		auto chunk = terrain.EnsureLoaded(cfg, 0, 0);
		REQUIRE(chunk);
		const int kRes = static_cast<int>(engine::world::terrain::kTerrainResolution);
		// Cellule (10, 20) du chunk → height 42.0.
		chunk->heights[static_cast<size_t>(20) * kRes + 10] = 42.0f;

		const auto grid = ew::BuildGridFromLoadedChunks(terrain, cfg);
		// Mapping : cellule (10, 20) du chunk (0,0) → cellule (10, 20)
		// de la grille (chunks (0,0) occupent baseX=0, baseZ=0).
		REQUIRE(NearEq(grid.Get(10, 20), 42.0));
	}

	// --- Incrément 2e : hydraulic_erosion / thermal_wind_erosion /
	//                    river_network / coastline (config-required)

	/// Les 4 ops simulation refusent gracieusement quand `Config` est absent
	/// du DispatchContext (impossible de charger les chunks). Pas de crash,
	/// renvoie Failed + log warning.
	void Test_Dispatcher_HydraulicErosion_RequiresConfig()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "hydraulic_erosion";
		op.rawJson = R"({"type":"hydraulic_erosion","numDroplets":5000,"rngSeed":"global"})";

		// MakeMinimalCtx ne renseigne pas Config (defaults nullptr).
		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
		REQUIRE(cmd == nullptr);
	}

	/// `thermal_wind_erosion` avec aucune passe activée → Failed (avant même
	/// le check Config — gating au plus tôt).
	void Test_Dispatcher_ThermalWindErosion_RejectsAllDisabled()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "thermal_wind_erosion";
		op.rawJson = R"({"type":"thermal_wind_erosion","thermalEnabled":false,"windEnabled":false})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
	}

	/// `river_network` sans sources → Failed (validation paramètres avant
	/// même de toucher Config).
	void Test_Dispatcher_RiverNetwork_RejectsEmptySources()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "river_network";
		op.rawJson = R"({"type":"river_network","sources":[],"carvingEnabled":true})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
	}

	/// `coastline` sans Config → Failed.
	void Test_Dispatcher_Coastline_RequiresConfig()
	{
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		zp::ZonePresetOperation op;
		op.type    = "coastline";
		op.rawJson = R"({"type":"coastline","seaLevelMeters":30,"beachEnabled":true,"cliffsEnabled":true})";

		const auto ctx = MakeMinimalCtx(terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat);
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Failed);
	}

	// --- Happy path : ops sim avec Config + chunks préchargés -------------
	//
	// Ces tests valident le wiring complet :
	//   BuildGridFromLoadedChunks → Run*Simulation → command builder.
	// Petits paramètres (50 gouttes, lifetime 10) pour rester < 100 ms.

	/// Helper : remplit les 4 chunks (0,0)..(1,1) d'une pente diagonale
	/// pour que les sims aient quelque chose à éroder / suivre.
	void InjectSlopedHeights(ew::TerrainDocument& terrain,
		const engine::core::Config& cfg, float maxHeight)
	{
		const int kRes = static_cast<int>(engine::world::terrain::kTerrainResolution);
		for (int cz = 0; cz < 2; ++cz)
		{
			for (int cx = 0; cx < 2; ++cx)
			{
				auto chunk = terrain.EnsureLoaded(cfg, cx, cz);
				REQUIRE(chunk);
				for (int iz = 0; iz < kRes; ++iz)
				{
					for (int ix = 0; ix < kRes; ++ix)
					{
						const float gx = static_cast<float>(cx * (kRes - 1) + ix);
						const float gz = static_cast<float>(cz * (kRes - 1) + iz);
						const float totalDim = static_cast<float>(2 * (kRes - 1));
						// Pente diagonale 0..maxHeight.
						chunk->heights[static_cast<size_t>(iz) * kRes + ix] =
							maxHeight * (gx + gz) / (2.0f * totalDim);
					}
				}
			}
		}
	}

	/// hydraulic_erosion happy path : Config + chunks pentus → dispatcher
	/// renvoie Ok et la commande "Hydraulic Erosion" est construite.
	void Test_Dispatcher_HydraulicErosion_OkWithConfig()
	{
		engine::core::Config cfg;
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		InjectSlopedHeights(terrain, cfg, 100.0f);

		zp::ZonePresetOperation op;
		op.type    = "hydraulic_erosion";
		// Petits params pour test rapide. rngSeed fixe pour déterminisme.
		op.rawJson = R"({"type":"hydraulic_erosion","numDroplets":50,"maxLifetimeSteps":10,"rngSeed":7})";

		const zp::DispatchContext ctx{
			terrain, water, meshDoc, portalDoc,
			caveCat, ohCat, arCat, dgCat, &cfg };
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);
		REQUIRE(std::string(cmd->GetLabel()) == "Hydraulic Erosion");
	}

	/// thermal_wind_erosion happy path : valide le wiring du wrapper
	/// `ThermalWindErosionParams` (incrément 2e+ : preset overlay) sans
	/// preset, défauts struct + JSON.
	void Test_Dispatcher_ThermalWindErosion_OkWithConfig()
	{
		engine::core::Config cfg;
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		InjectSlopedHeights(terrain, cfg, 80.0f);

		zp::ZonePresetOperation op;
		op.type    = "thermal_wind_erosion";
		// Thermal seulement (rapide). 3 passes pour test minimal.
		op.rawJson = R"({"type":"thermal_wind_erosion","thermalEnabled":true,"windEnabled":false,"numPasses":3})";

		const zp::DispatchContext ctx{
			terrain, water, meshDoc, portalDoc,
			caveCat, ohCat, arCat, dgCat, &cfg };
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);
		REQUIRE(std::string(cmd->GetLabel()) == "Thermal/Wind Erosion");
	}

	/// coastline happy path : sea level réglé + smoothing seul (cliffs
	/// désactivés pour rapidité). Vérifie l'insertion du LakeInstance
	/// océan + construction de la commande.
	void Test_Dispatcher_Coastline_OkWithConfig()
	{
		engine::core::Config cfg;
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		InjectSlopedHeights(terrain, cfg, 80.0f);

		zp::ZonePresetOperation op;
		op.type    = "coastline";
		op.rawJson = R"({"type":"coastline","seaLevelMeters":40,"beachEnabled":true,"cliffsEnabled":false})";

		const zp::DispatchContext ctx{
			terrain, water, meshDoc, portalDoc,
			caveCat, ohCat, arCat, dgCat, &cfg };
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);
		REQUIRE(std::string(cmd->GetLabel()) == "Coastline");

		// Push pour valider que Execute() ne crash pas (côté coastline
		// le LakeInstance océan est inséré ET le sea level écrit dans
		// WaterDocument).
		stack.Push(std::move(cmd));
		REQUIRE(NearEq(water.GetOcean().seaLevelMeters, 40.0));
		REQUIRE(water.Get().lakes.size() == 1u);
		REQUIRE(water.Get().lakes[0].isOcean);
	}

	/// river_network happy path : 2 sources sur terrain pentu → commande
	/// construite. Petit minFlowThresholdCells pour que les rivières
	/// ne soient pas rejetées sur ce mini terrain.
	void Test_Dispatcher_RiverNetwork_OkWithConfig()
	{
		engine::core::Config cfg;
		vol::caves::CaveCatalog caveCat;
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;

		InjectSlopedHeights(terrain, cfg, 200.0f);

		zp::ZonePresetOperation op;
		op.type    = "river_network";
		// Deux sources, threshold bas pour ne pas rejeter, pas de carving
		// (sinon la commande applique des deltas qui demandent un chunk
		// dirty path).
		op.rawJson = R"({"type":"river_network","sources":[[50,50],[100,100]],"minFlowThresholdCells":10,"carvingEnabled":false})";

		const zp::DispatchContext ctx{
			terrain, water, meshDoc, portalDoc,
			caveCat, ohCat, arCat, dgCat, &cfg };
		std::unique_ptr<ew::ICommand> cmd;
		const auto rc = zp::DispatchOperation(op, zp::CustomizationParams{}, ctx, cmd);
		REQUIRE(rc == zp::DispatchResult::Ok);
		REQUIRE(cmd != nullptr);
		REQUIRE(std::string(cmd->GetLabel()) == "River Network");
	}

	// --- Executor end-to-end : mini-preset mixte (macro + place + sim) ---

	/// Test d'intégration : un mini ZonePreset construit en mémoire avec
	/// 3 ops de natures différentes (mountain_macro = macro polyline,
	/// place_cave = mesh insert, hydraulic_erosion = sim) exécuté
	/// complètement via le ZonePresetExecutor → CommandStack peuplé +
	/// documents mutés. Couvre le chemin happy path complet.
	void Test_Executor_MixedPresetEndToEnd()
	{
		engine::core::Config cfg;
		vol::caves::CaveCatalog caveCat;
		std::string err;
		REQUIRE(caveCat.ParseJson(R"({
			"caves":[{"id":"c1","gltf":"caves/c1.gltf","displayName":"Petite grotte",
				"aabbMin":[-2,0,-2],"aabbMax":[2,3,2],"entrancePoint":[0,0,-2]}]
		})", err));
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		// Pré-remplis le terrain pour que hydraulic_erosion ait quelque
		// chose à éroder.
		InjectSlopedHeights(terrain, cfg, 100.0f);

		zp::ZonePreset preset;
		preset.id = "e2e_mixed";
		preset.operations.push_back({
			"mountain_macro", "", {},
			R"({"type":"mountain_macro","polyline":[[100,100],[400,150],[700,100]],"widthMeters":200,"heightMeters":50})"
		});
		preset.operations.push_back({
			"place_cave", "", {},
			R"({"type":"place_cave","catalogId":"c1","worldPosition":[300,20,400]})"
		});
		preset.operations.push_back({
			"hydraulic_erosion", "", {},
			R"({"type":"hydraulic_erosion","numDroplets":30,"maxLifetimeSteps":8,"rngSeed":3})"
		});

		zp::ZonePresetExecutor executor;
		const zp::DispatchContext ctx{
			terrain, water, meshDoc, portalDoc,
			caveCat, ohCat, arCat, dgCat, &cfg };

		int callbackCount = 0;
		const auto summary = executor.Execute(preset, zp::CustomizationParams{},
			stack, ctx,
			[&](const zp::ExecutionProgress&) { ++callbackCount; return true; });

		REQUIRE(summary.totalSteps == 3u);
		REQUIRE(summary.commandsPushed == 3u);
		REQUIRE(summary.unsupportedSkipped == 0u);
		REQUIRE(summary.failed == 0u);
		REQUIRE(!summary.wasCancelled);
		REQUIRE(callbackCount == 3);

		// La cave a effectivement été poussée dans MeshInsertDocument.
		REQUIRE(meshDoc.Size() == 1u);
		REQUIRE(meshDoc.All()[0].insertCategory == "cave");
		// Le CommandStack a 3 commandes (= rien skip).
		REQUIRE(stack.UndoSize() == 3u);
	}

	// --- ZonePresetExecutor (incrément 2b) ---------------------------------

	/// Exécution complète : reset + boucle ops. Vérifie le résumé.
	void Test_Executor_RunsAndSummarizes()
	{
		vol::caves::CaveCatalog caveCat;
		std::string err;
		REQUIRE(caveCat.ParseJson(R"({
			"caves":[{"id":"c1","gltf":"x.gltf","aabbMin":[-1,0,-1],"aabbMax":[1,2,1],
				"entrancePoint":[0,0,0]}]
		})", err));
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;
		REQUIRE(dgCat.ParseJson(R"({"dungeons":[{"id":"dt","minDifficulty":1,"maxDifficulty":1}]})", err));

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		// Pré-remplissage : reset doit nettoyer.
		vol::MeshInsertInstance pre;
		pre.gltfRelativePath = "pre"; pre.insertCategory = "cave";
		meshDoc.Add(pre);

		zp::ZonePreset preset;
		preset.id = "mixed";
		// 1) splat_paint = connu mais pas câblé → skipped (incrément 2e
		//    a câblé hydraulic_erosion ; il ne reste que sculpt_brush /
		//    splat_paint comme types non câblés).
		zp::ZonePresetOperation op1;
		op1.type    = "splat_paint";
		op1.rawJson = R"({"type":"splat_paint","layerIndex":2})";
		preset.operations.push_back(op1);
		// 2) place_cave OK
		zp::ZonePresetOperation op2;
		op2.type    = "place_cave";
		op2.rawJson = R"({"type":"place_cave","catalogId":"c1","worldPosition":[0,10,0]})";
		preset.operations.push_back(op2);
		// 3) place_cave catalogId ghost → failed
		zp::ZonePresetOperation op3;
		op3.type    = "place_cave";
		op3.rawJson = R"({"type":"place_cave","catalogId":"ghost","worldPosition":[0,0,0]})";
		preset.operations.push_back(op3);
		// 4) place_dungeon OK
		zp::ZonePresetOperation op4;
		op4.type    = "place_dungeon";
		op4.rawJson = R"({"type":"place_dungeon","catalogId":"dt","worldPosition":[5,0,5]})";
		preset.operations.push_back(op4);

		zp::ZonePresetExecutor executor;
		const zp::DispatchContext ctx{ terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat };
		int callbackCount = 0;
		const auto summary = executor.Execute(preset, zp::CustomizationParams{},
			stack, ctx,
			[&](const zp::ExecutionProgress&) { ++callbackCount; return true; });

		REQUIRE(summary.totalSteps == 4u);
		REQUIRE(summary.commandsPushed == 2u);     // cave + dungeon
		REQUIRE(summary.unsupportedSkipped == 1u);  // splat_paint
		REQUIRE(summary.failed == 1u);              // cave ghost
		REQUIRE(!summary.wasCancelled);
		REQUIRE(callbackCount == 4);
		// Le pré-remplissage a été nettoyé par le reset, puis le cave OK
		// a poussé 1 instance.
		REQUIRE(meshDoc.Size() == 1u);
		REQUIRE(portalDoc.Size() == 1u);
	}

	/// Callback retournant false → cancel après l'op courante.
	void Test_Executor_CancelViaCallback()
	{
		vol::caves::CaveCatalog caveCat;
		std::string err;
		REQUIRE(caveCat.ParseJson(R"({
			"caves":[{"id":"c1","gltf":"x.gltf","aabbMin":[-1,0,-1],"aabbMax":[1,2,1]}]
		})", err));
		vol::overhangs::OverhangCatalog ohCat;
		vol::arches::ArchCatalog arCat;
		vol::dungeons::DungeonCatalog dgCat;

		ew::TerrainDocument terrain;
		ew::WaterDocument water;
		vol::MeshInsertDocument meshDoc;
		vol::dungeons::DungeonPortalDocument portalDoc;
		ew::CommandStack stack;

		zp::ZonePreset preset;
		preset.id = "cancel_test";
		for (int i = 0; i < 3; ++i)
		{
			zp::ZonePresetOperation op;
			op.type    = "place_cave";
			op.rawJson = R"({"type":"place_cave","catalogId":"c1","worldPosition":[0,0,0]})";
			preset.operations.push_back(op);
		}

		zp::ZonePresetExecutor executor;
		const zp::DispatchContext ctx{ terrain, water, meshDoc, portalDoc, caveCat, ohCat, arCat, dgCat };
		const auto summary = executor.Execute(preset, zp::CustomizationParams{},
			stack, ctx,
			[](const zp::ExecutionProgress& p) {
				// Stoppe après l'étape 1.
				return p.currentStep < 2u;
			});
		REQUIRE(summary.wasCancelled);
		REQUIRE(summary.commandsPushed == 1u); // seule la 1ère op est passée
	}
}

int main()
{
	Test_ParsesValidPreset();
	Test_RejectsMissingId();
	Test_RejectsMissingOperations();
	Test_OperationRawJsonPreserved();
	Test_DecorationCountedWhenPresent();
	Test_ValidateAcceptsCleanPreset();
	Test_ValidateRejectsUnknownToolId();
	Test_ValidateRejectsUnknownAffectedBy();
	Test_ValidateRejectsEmptyOperations();
	Test_ValidateRejectsNonEmptyDecoration();
	Test_KnownTypeHelpers();
	Test_LocalizedStringFallback();
	Test_RegistryLoadsValidPresetsSkipsBad();
	Test_RegistryMissingDirIsNotAnError();
	Test_OpParams_ParsesScalars();
	Test_OpParams_FlattensCoordLists();
	Test_OpParams_ScaleNumber();
	Test_OpParams_TolerantToMalformed();
	Test_Customization_IsNeutral();
	Test_Customization_ReliefScalesHeights();
	Test_Customization_UnaffectedOpUnchanged();
	Test_Customization_WaterAndDryness();
	Test_Customization_NeutralLeavesParamsIdentical();
	Test_WorldMapEditDocumentReset_ClearsAllDocs();
	Test_Dispatcher_PlaceCave_BuildsCommand();
	Test_Dispatcher_PlaceCave_UnknownCatalog();
	Test_Dispatcher_PlaceDungeon_BuildsCommand();
	Test_Dispatcher_PlaceArch_DerivesGeometry();
	Test_Dispatcher_MountainMacro_RasterizesAndBuildsCommand();
	Test_Dispatcher_MountainMacro_RejectsBadPolyline();
	Test_Dispatcher_ValleyMacro_BuildsValleyCommand();
	Test_Dispatcher_LakePolygon_BuildsCommand();
	Test_Dispatcher_LakePolygon_RejectsDegenerate();
	Test_Dispatcher_RiverManual_BuildsCommand();
	Test_Dispatcher_UnsupportedType_GracefulSkip();
	Test_HeightGridAssembly_BuildsFlat();
	Test_HeightGridAssembly_PreservesNonZeroHeights();
	Test_Dispatcher_HydraulicErosion_RequiresConfig();
	Test_Dispatcher_ThermalWindErosion_RejectsAllDisabled();
	Test_Dispatcher_RiverNetwork_RejectsEmptySources();
	Test_Dispatcher_Coastline_RequiresConfig();
	Test_Dispatcher_HydraulicErosion_OkWithConfig();
	Test_Dispatcher_ThermalWindErosion_OkWithConfig();
	Test_Dispatcher_Coastline_OkWithConfig();
	Test_Dispatcher_RiverNetwork_OkWithConfig();
	Test_Executor_MixedPresetEndToEnd();
	Test_Executor_RunsAndSummarizes();
	Test_Executor_CancelViaCallback();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ZonePresetsTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ZonePresetsTests] all tests passed\n");
	return 0;
}
