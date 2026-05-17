/// Tests unitaires pour M100.47 incrément 1 — `HelpContentStore` :
/// parsing JSON tooltips, loader content-path, lookup par id, robustesse
/// aux fichiers corrompus / dossier absent / doublons.

#include "src/world_editor/help/HelpContentStore.h"

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

	namespace help = engine::editor::world::help;

	std::filesystem::path MakeTempDir(const char* tag)
	{
		const auto base = std::filesystem::temp_directory_path()
			/ ("lcdlln_m10047_" + std::string(tag));
		std::error_code ec;
		std::filesystem::remove_all(base, ec);
		std::filesystem::create_directories(base / "editor" / "tooltips", ec);
		return base;
	}

	void WriteFile(const std::filesystem::path& path, const std::string& contents)
	{
		std::ofstream f(path);
		f << contents;
	}

	const char* kSampleJson = R"({
		"toolId": "hydraulic_erosion",
		"tooltips": {
			"numDroplets": {
				"label": "Nombre de gouttes",
				"description_simple": "Plus = érosion plus marquée.",
				"description_advanced": "Particles, typ. 50k-500k.",
				"defaultValue": "100000",
				"range": "10000 - 500000",
				"docSectionId": "tool/hydraulic_erosion#numDroplets"
			},
			"erosionRate": {
				"label": "Taux d'érosion",
				"description_simple": "Vitesse de grattage.",
				"description_advanced": "Fraction de sédiment retirée par cellule.",
				"defaultValue": "0.3",
				"range": "0.05 - 0.95",
				"docSectionId": "tool/hydraulic_erosion#erosionRate"
			}
		}
	})";

	// ---------------------------------------------------------------------
	// Parsing direct
	// ---------------------------------------------------------------------

	/// Parse un JSON tooltips bien formé : toolId + 2 entrées, tous les
	/// champs string remplis.
	void Test_ParseTooltipFileJson_Ok()
	{
		help::TooltipFileContents file;
		std::string err;
		REQUIRE(help::ParseTooltipFileJson(kSampleJson, file, err));
		REQUIRE(err.empty());
		REQUIRE(file.toolId == "hydraulic_erosion");
		REQUIRE(file.tooltips.size() == 2u);

		const auto& def = file.tooltips["numDroplets"];
		REQUIRE(def.label == "Nombre de gouttes");
		REQUIRE(def.descriptionSimple == "Plus = érosion plus marquée.");
		REQUIRE(def.descriptionAdvanced == "Particles, typ. 50k-500k.");
		REQUIRE(def.defaultValue == "100000");
		REQUIRE(def.range == "10000 - 500000");
		REQUIRE(def.docSectionId == "tool/hydraulic_erosion#numDroplets");
		// `id` n'est pas reconstruit par le parser (c'est le store qui le
		// fait à partir de toolId + paramName).
		REQUIRE(def.id.empty());
	}

	/// toolId manquant → Failed, err renseigné.
	void Test_ParseTooltipFileJson_RejectsMissingToolId()
	{
		const char* json = R"({"tooltips": {}})";
		help::TooltipFileContents file;
		std::string err;
		REQUIRE(!help::ParseTooltipFileJson(json, file, err));
		REQUIRE(!err.empty());
	}

	/// Bloc tooltips absent → Ok, fichier vide de définitions.
	void Test_ParseTooltipFileJson_AcceptsEmptyTooltips()
	{
		const char* json = R"({"toolId": "x"})";
		help::TooltipFileContents file;
		std::string err;
		REQUIRE(help::ParseTooltipFileJson(json, file, err));
		REQUIRE(file.toolId == "x");
		REQUIRE(file.tooltips.empty());
	}

	/// Clés inconnues ignorées (tolérance pour évolution du format).
	void Test_ParseTooltipFileJson_IgnoresUnknownKeys()
	{
		const char* json = R"({
			"toolId": "x",
			"futureField": "ignore_me",
			"tooltips": {
				"k": {
					"label": "L",
					"unknownField": "ignore",
					"description_simple": "S"
				}
			}
		})";
		help::TooltipFileContents file;
		std::string err;
		REQUIRE(help::ParseTooltipFileJson(json, file, err));
		REQUIRE(file.tooltips["k"].label == "L");
		REQUIRE(file.tooltips["k"].descriptionSimple == "S");
	}

	// ---------------------------------------------------------------------
	// Loader (content-path + filesystem)
	// ---------------------------------------------------------------------

	/// LoadFromContentPath charge 2 fichiers et indexe les ids comme
	/// "toolId.paramName".
	void Test_HelpContentStore_LoadFromContentPath()
	{
		const auto dir = MakeTempDir("load_two");
		WriteFile(dir / "editor" / "tooltips" / "tool1.json", kSampleJson);
		WriteFile(dir / "editor" / "tooltips" / "tool2.json", R"({
			"toolId": "mountain_macro",
			"tooltips": {
				"widthMeters": {
					"label": "Largeur",
					"description_simple": "S",
					"description_advanced": "A",
					"defaultValue": "250",
					"range": "50 - 3000",
					"docSectionId": "tool/mountain_macro#widthMeters"
				}
			}
		})");

		auto& store = help::HelpContentStore::Instance();
		store.ResetForTesting();
		const size_t n = store.LoadFromContentPath(dir.string());
		REQUIRE(n == 3u); // 2 hydraulic + 1 mountain
		REQUIRE(store.Size() == 3u);

		const auto* drop = store.FindTooltip("hydraulic_erosion.numDroplets");
		REQUIRE(drop != nullptr);
		REQUIRE(drop->label == "Nombre de gouttes");
		// Id complet (préfixé) après load.
		REQUIRE(drop->id == "hydraulic_erosion.numDroplets");

		const auto* width = store.FindTooltip("mountain_macro.widthMeters");
		REQUIRE(width != nullptr);
		REQUIRE(width->defaultValue == "250");

		REQUIRE(store.FindTooltip("nope.nope") == nullptr);
	}

	/// Dossier absent → 0 tooltips, pas d'erreur.
	void Test_HelpContentStore_MissingDirIsNotAnError()
	{
		const auto dir = std::filesystem::temp_directory_path()
			/ "lcdlln_m10047_nodir_xyz";
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);

		auto& store = help::HelpContentStore::Instance();
		store.ResetForTesting();
		REQUIRE(store.LoadFromContentPath(dir.string()) == 0u);
		REQUIRE(store.Size() == 0u);
	}

	/// Fichier JSON corrompu → ignoré + autres chargés.
	void Test_HelpContentStore_LoadSkipsBadJson()
	{
		const auto dir = MakeTempDir("mixed");
		WriteFile(dir / "editor" / "tooltips" / "good.json", kSampleJson);
		WriteFile(dir / "editor" / "tooltips" / "bad.json", "{not json");
		// `corrupt.json` valide JSON mais sans toolId → rejet.
		WriteFile(dir / "editor" / "tooltips" / "no_toolid.json", R"({"tooltips":{}})");

		auto& store = help::HelpContentStore::Instance();
		store.ResetForTesting();
		const size_t n = store.LoadFromContentPath(dir.string());
		REQUIRE(n == 2u); // seulement les 2 tooltips de good.json
	}

	/// Doublon d'id (deux fichiers déclarant le même tool/param) → premier
	/// gagne, warning loggué.
	void Test_HelpContentStore_DuplicateIdKeepsFirst()
	{
		const auto dir = MakeTempDir("dup");
		WriteFile(dir / "editor" / "tooltips" / "a.json", R"({
			"toolId": "tool",
			"tooltips": {
				"key": { "label": "FROM_A", "description_simple": "" }
			}
		})");
		WriteFile(dir / "editor" / "tooltips" / "b.json", R"({
			"toolId": "tool",
			"tooltips": {
				"key": { "label": "FROM_B", "description_simple": "" }
			}
		})");

		auto& store = help::HelpContentStore::Instance();
		store.ResetForTesting();
		const size_t n = store.LoadFromContentPath(dir.string());
		REQUIRE(n == 1u);
		const auto* def = store.FindTooltip("tool.key");
		REQUIRE(def != nullptr);
		// L'ordre d'itération du filesystem n'est pas garanti, donc on
		// vérifie seulement que c'est l'une des deux versions.
		REQUIRE(def->label == "FROM_A" || def->label == "FROM_B");
	}

	/// Reload re-charge depuis le dernier contentRoot.
	void Test_HelpContentStore_Reload()
	{
		const auto dir = MakeTempDir("reload");
		WriteFile(dir / "editor" / "tooltips" / "x.json", kSampleJson);

		auto& store = help::HelpContentStore::Instance();
		store.ResetForTesting();
		REQUIRE(store.LoadFromContentPath(dir.string()) == 2u);

		// Ajoute un nouveau fichier et reload.
		WriteFile(dir / "editor" / "tooltips" / "y.json", R"({
			"toolId": "stamp",
			"tooltips": {
				"radius": { "label": "R", "description_simple": "S" }
			}
		})");
		REQUIRE(store.Reload() == 3u);
		REQUIRE(store.FindTooltip("stamp.radius") != nullptr);
	}
}

int main()
{
	Test_ParseTooltipFileJson_Ok();
	Test_ParseTooltipFileJson_RejectsMissingToolId();
	Test_ParseTooltipFileJson_AcceptsEmptyTooltips();
	Test_ParseTooltipFileJson_IgnoresUnknownKeys();
	Test_HelpContentStore_LoadFromContentPath();
	Test_HelpContentStore_MissingDirIsNotAnError();
	Test_HelpContentStore_LoadSkipsBadJson();
	Test_HelpContentStore_DuplicateIdKeepsFirst();
	Test_HelpContentStore_Reload();

	if (g_failed > 0)
	{
		std::fprintf(stderr, "[HelpContentTests] %d failure(s)\n", g_failed);
		return 1;
	}
	std::fprintf(stdout, "[HelpContentTests] all tests passed\n");
	return 0;
}
