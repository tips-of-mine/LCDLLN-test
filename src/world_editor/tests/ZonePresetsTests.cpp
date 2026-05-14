/// Tests unitaires CPU pour M100.46 incrément 1 — Zone Presets Library
/// (socle data : format JSON + parsing + validation + registry).
///
/// Le moteur d'exécution (ZonePresetExecutor / OperationDispatcher /
/// CustomizationApplier) et l'UI sont des incréments suivants — leurs
/// tests (déterminisme, customisation, annulation) viendront avec.

#include "src/world_editor/zone_presets/ZonePreset.h"
#include "src/world_editor/zone_presets/ZonePresetIo.h"
#include "src/world_editor/zone_presets/ZonePresetRegistry.h"

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

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[ZonePresetsTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[ZonePresetsTests] all tests passed\n");
	return 0;
}
