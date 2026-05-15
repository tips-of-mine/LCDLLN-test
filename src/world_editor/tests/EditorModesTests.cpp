/// Tests unitaires CPU pour M100.45 Phase A — Simple/Advanced Mode +
/// Tool Parameter Presets (infrastructure transverse, Phase 12).
///
/// Couvre : EditorModeRegistry (set + subscribers), ToolPresetIo (parsing
/// JSON tolérant), ToolPresetRegistry (chargement répertoire + résilience
/// fichier corrompu), UserPrefsStore (round-trip disque + sauvegarde
/// atomique + premier lancement).
///
/// Les tests qui touchent au disque utilisent un sous-dossier temporaire
/// unique pour ne pas polluer `game/data/editor/`.

#include "src/world_editor/modes/EditorMode.h"
#include "src/world_editor/modes/EditorModeRegistry.h"
#include "src/world_editor/prefs/UserPrefs.h"
#include "src/world_editor/prefs/UserPrefsStore.h"
#include "src/world_editor/presets/ToolPreset.h"
#include "src/world_editor/presets/ToolPresetIo.h"
#include "src/world_editor/presets/ToolPresetRegistry.h"

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

	namespace modes   = engine::editor::world::modes;
	namespace prefs   = engine::editor::world::prefs;
	namespace presets = engine::editor::world::presets;

	/// Racine temporaire unique pour les tests disque. Layout attendu par
	/// les stores : `<root>/editor/...`.
	std::filesystem::path MakeTempContentRoot(const char* tag)
	{
		const auto base = std::filesystem::temp_directory_path()
			/ ("lcdlln_m10045_" + std::string(tag));
		std::error_code ec;
		std::filesystem::remove_all(base, ec);
		std::filesystem::create_directories(base / "editor" / "tool_presets", ec);
		return base;
	}

	void WriteFile(const std::filesystem::path& p, const std::string& content)
	{
		std::ofstream f(p, std::ios::binary | std::ios::trunc);
		f.write(content.data(), static_cast<std::streamsize>(content.size()));
	}

	// --- EditorModeRegistry -------------------------------------------------

	void Test_EditorMode_SetAndNotify()
	{
		auto& reg = modes::EditorModeRegistry::Instance();
		reg.ResetForTesting();
		REQUIRE(reg.GetCurrentMode() == modes::EditorMode::Simple);

		int callCount = 0;
		modes::EditorMode lastSeen = modes::EditorMode::Simple;
		const size_t handle = reg.Subscribe([&](modes::EditorMode m) {
			++callCount;
			lastSeen = m;
		});

		reg.SetCurrentMode(modes::EditorMode::Advanced);
		REQUIRE(reg.GetCurrentMode() == modes::EditorMode::Advanced);
		REQUIRE(callCount == 1);
		REQUIRE(lastSeen == modes::EditorMode::Advanced);

		// No-op si déjà dans le mode demandé : pas de notification.
		reg.SetCurrentMode(modes::EditorMode::Advanced);
		REQUIRE(callCount == 1);

		reg.Unsubscribe(handle);
		reg.SetCurrentMode(modes::EditorMode::Simple);
		REQUIRE(callCount == 1); // plus abonné
		reg.ResetForTesting();
	}

	void Test_EditorMode_SilentDoesNotPersistButNotifies()
	{
		auto& reg = modes::EditorModeRegistry::Instance();
		reg.ResetForTesting();
		int callCount = 0;
		reg.Subscribe([&](modes::EditorMode) { ++callCount; });
		reg.SetCurrentModeSilent(modes::EditorMode::Advanced);
		REQUIRE(reg.GetCurrentMode() == modes::EditorMode::Advanced);
		REQUIRE(callCount == 1);
		reg.ResetForTesting();
	}

	void Test_EditorMode_StringRoundTrip()
	{
		REQUIRE(modes::FromString(modes::ToString(modes::EditorMode::Simple))
			== modes::EditorMode::Simple);
		REQUIRE(modes::FromString(modes::ToString(modes::EditorMode::Advanced))
			== modes::EditorMode::Advanced);
		// Tolérance : valeur inconnue → Simple.
		REQUIRE(modes::FromString("garbage") == modes::EditorMode::Simple);
		REQUIRE(modes::FromString(nullptr) == modes::EditorMode::Simple);
	}

	// --- ToolPresetIo -------------------------------------------------------

	void Test_ToolPresetIo_ParsesValidJson()
	{
		const std::string json = R"({
			"toolId": "hydraulic_erosion",
			"defaultPreset": "realistic",
			"presets": [
				{
					"id": "subtle",
					"displayName": "Légère",
					"description": "Douce.",
					"parameters": { "numDroplets": 30000, "erosionRate": 0.15 }
				},
				{
					"id": "intense",
					"displayName": "Intense",
					"parameters": { "numDroplets": 500000, "erosionRate": 0.55 }
				}
			]
		})";
		presets::ToolPresetFile out;
		std::string err;
		REQUIRE(presets::ParseToolPresetJson(json, out, err));
		REQUIRE(out.toolId == "hydraulic_erosion");
		REQUIRE(out.defaultPreset == "realistic");
		REQUIRE(out.presets.size() == 2u);
		REQUIRE(out.presets[0].id == "subtle");
		REQUIRE(out.presets[0].displayName == "Légère");
		REQUIRE(out.presets[0].GetParam("numDroplets", -1.0) == 30000.0);
		REQUIRE(out.presets[0].GetParam("erosionRate", -1.0) == 0.15);
		// displayName absent → retombe sur l'id.
		REQUIRE(out.presets[1].displayName == "Intense");
		REQUIRE(out.presets[1].GetParam("numDroplets", -1.0) == 500000.0);
		// Clé absente → fallback inchangé.
		REQUIRE(out.presets[0].GetParam("inexistant", 42.0) == 42.0);
		REQUIRE(!out.presets[0].HasParam("inexistant"));
	}

	void Test_ToolPresetIo_RejectsMissingToolId()
	{
		presets::ToolPresetFile out;
		std::string err;
		REQUIRE(!presets::ParseToolPresetJson(R"({"presets":[]})", out, err));
		REQUIRE(!err.empty());
	}

	void Test_ToolPresetIo_TolerantToMissingPresetsArray()
	{
		presets::ToolPresetFile out;
		std::string err;
		REQUIRE(presets::ParseToolPresetJson(R"({"toolId":"sculpt"})", out, err));
		REQUIRE(out.toolId == "sculpt");
		REQUIRE(out.presets.empty());
	}

	void Test_ToolPresetIo_SkipsPresetWithoutId()
	{
		const std::string json = R"({
			"toolId": "stamp",
			"presets": [
				{ "displayName": "Sans id" },
				{ "id": "valid", "displayName": "Valide" }
			]
		})";
		presets::ToolPresetFile out;
		std::string err;
		REQUIRE(presets::ParseToolPresetJson(json, out, err));
		REQUIRE(out.presets.size() == 1u);
		REQUIRE(out.presets[0].id == "valid");
	}

	// --- ToolPresetRegistry -------------------------------------------------

	void Test_ToolPresetRegistry_LoadsDirectory()
	{
		const auto root = MakeTempContentRoot("registry");
		const auto presetDir = root / "editor" / "tool_presets";
		WriteFile(presetDir / "alpha.json",
			R"({"toolId":"alpha","defaultPreset":"a1","presets":[{"id":"a1","parameters":{"x":1}}]})");
		WriteFile(presetDir / "beta.json",
			R"({"toolId":"beta","presets":[{"id":"b1","parameters":{"y":2}},{"id":"b2","parameters":{"y":3}}]})");

		auto& reg = presets::ToolPresetRegistry::Instance();
		reg.ResetForTesting();
		const size_t loaded = reg.LoadFromContentPath(root.string());
		REQUIRE(loaded == 2u);
		REQUIRE(reg.ToolCount() == 2u);
		REQUIRE(reg.GetPresetsForTool("alpha").size() == 1u);
		REQUIRE(reg.GetPresetsForTool("beta").size() == 2u);
		REQUIRE(reg.GetDefaultPresetId("alpha") == "a1");
		REQUIRE(reg.FindPreset("beta", "b2") != nullptr);
		REQUIRE(reg.FindPreset("beta", "b2")->GetParam("y", -1.0) == 3.0);
		REQUIRE(reg.FindPreset("beta", "absent") == nullptr);
		REQUIRE(reg.GetPresetsForTool("inconnu").empty());

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		reg.ResetForTesting();
	}

	void Test_ToolPresetRegistry_TolerantToMalformedJson()
	{
		const auto root = MakeTempContentRoot("registry_bad");
		const auto presetDir = root / "editor" / "tool_presets";
		WriteFile(presetDir / "good.json",
			R"({"toolId":"good","presets":[{"id":"g","parameters":{"x":1}}]})");
		WriteFile(presetDir / "corrupt.json", R"({ this is not json at all )");
		WriteFile(presetDir / "no_toolid.json", R"({"presets":[]})");

		auto& reg = presets::ToolPresetRegistry::Instance();
		reg.ResetForTesting();
		const size_t loaded = reg.LoadFromContentPath(root.string());
		// Seul le fichier valide est compté ; les 2 autres sont ignorés
		// (log warning), pas de crash.
		REQUIRE(loaded == 1u);
		REQUIRE(reg.GetPresetsForTool("good").size() == 1u);

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		reg.ResetForTesting();
	}

	void Test_ToolPresetRegistry_MissingDirectoryIsNotAnError()
	{
		auto& reg = presets::ToolPresetRegistry::Instance();
		reg.ResetForTesting();
		const auto missing = std::filesystem::temp_directory_path()
			/ "lcdlln_m10045_does_not_exist_xyz";
		const size_t loaded = reg.LoadFromContentPath(missing.string());
		REQUIRE(loaded == 0u);
		REQUIRE(reg.ToolCount() == 0u);
		reg.ResetForTesting();
	}

	// --- UserPrefsStore -----------------------------------------------------

	void Test_UserPrefs_FirstLaunchCreatesDefaults()
	{
		const auto root = MakeTempContentRoot("prefs_first");
		auto& store = prefs::UserPrefsStore::Instance();
		store.ResetForTesting();

		// Pas de user_prefs.json → Init retourne false (créé) + défauts.
		const bool existed = store.Init(root.string());
		REQUIRE(!existed);
		REQUIRE(store.GetEditorMode() == modes::EditorMode::Simple);
		REQUIRE(std::filesystem::exists(root / "editor" / "user_prefs.json"));

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		store.ResetForTesting();
	}

	void Test_UserPrefs_RoundTripPersist()
	{
		const auto root = MakeTempContentRoot("prefs_rt");
		auto& store = prefs::UserPrefsStore::Instance();

		store.ResetForTesting();
		store.Init(root.string());
		store.SetEditorMode(modes::EditorMode::Advanced);
		store.SetLastPresetForTool("hydraulic_erosion", "intense");
		store.SetTutorialFlag("first_launch_toast_dismissed", true);

		// Recharge depuis le disque dans une instance « fraîche » (simulée
		// par ResetForTesting + Init sur le même chemin).
		store.ResetForTesting();
		const bool existed = store.Init(root.string());
		REQUIRE(existed);
		REQUIRE(store.GetEditorMode() == modes::EditorMode::Advanced);
		REQUIRE(store.GetLastPresetForTool("hydraulic_erosion") == "intense");
		REQUIRE(store.GetLastPresetForTool("inconnu").empty());
		REQUIRE(store.GetTutorialFlag("first_launch_toast_dismissed"));
		REQUIRE(!store.GetTutorialFlag("flag_absent"));

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		store.ResetForTesting();
	}

	void Test_UserPrefs_AtomicSaveLeavesNoTmp()
	{
		const auto root = MakeTempContentRoot("prefs_atomic");
		auto& store = prefs::UserPrefsStore::Instance();
		store.ResetForTesting();
		store.Init(root.string());
		store.SetEditorMode(modes::EditorMode::Advanced);
		// Après une sauvegarde réussie, le .tmp ne doit pas subsister.
		REQUIRE(!std::filesystem::exists(root / "editor" / "user_prefs.json.tmp"));
		REQUIRE(std::filesystem::exists(root / "editor" / "user_prefs.json"));

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		store.ResetForTesting();
	}

	void Test_UserPrefs_TolerantToMalformedFile()
	{
		const auto root = MakeTempContentRoot("prefs_bad");
		WriteFile(root / "editor" / "user_prefs.json", "{ not valid json");
		auto& store = prefs::UserPrefsStore::Instance();
		store.ResetForTesting();
		// Fichier présent mais corrompu : Init le « charge » sans crash,
		// les champs non parsés gardent leurs défauts.
		const bool existed = store.Init(root.string());
		REQUIRE(existed);
		REQUIRE(store.GetEditorMode() == modes::EditorMode::Simple);

		std::error_code ec;
		std::filesystem::remove_all(root, ec);
		store.ResetForTesting();
	}
}

int main()
{
	Test_EditorMode_SetAndNotify();
	Test_EditorMode_SilentDoesNotPersistButNotifies();
	Test_EditorMode_StringRoundTrip();
	Test_ToolPresetIo_ParsesValidJson();
	Test_ToolPresetIo_RejectsMissingToolId();
	Test_ToolPresetIo_TolerantToMissingPresetsArray();
	Test_ToolPresetIo_SkipsPresetWithoutId();
	Test_ToolPresetRegistry_LoadsDirectory();
	Test_ToolPresetRegistry_TolerantToMalformedJson();
	Test_ToolPresetRegistry_MissingDirectoryIsNotAnError();
	Test_UserPrefs_FirstLaunchCreatesDefaults();
	Test_UserPrefs_RoundTripPersist();
	Test_UserPrefs_AtomicSaveLeavesNoTmp();
	Test_UserPrefs_TolerantToMalformedFile();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[EditorModesTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[EditorModesTests] all tests passed\n");
	return 0;
}
