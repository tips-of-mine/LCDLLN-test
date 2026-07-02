// Tests du QuestTextCatalog — textes lisibles (titre/description/étapes) des
// quêtes, résolus par locale depuis le contenu data-driven côté client.
//
// Écrit une fixture JSON dans un répertoire temporaire unique (isolé du vrai
// contenu du dépôt) et pointe `paths.content` dessus via Config::SetValue, à
// l'instar de QuestRuntimeTests.cpp (MakeTempContentDir). QuestTextCatalog
// lit toujours un fichier via FileSystem::ReadAllTextContent (pas de
// LoadFromText), donc le test passe par une vraie E/S disque (déterministe :
// un dossier temporaire par appel).

#include "src/client/quest/QuestTextCatalog.h"
#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <string>

using engine::client::QuestTextCatalog;

namespace
{
	int g_failures = 0;

	void Check(bool condition, const char* label)
	{
		if (!condition)
		{
			std::cerr << "[FAIL] " << label << "\n";
			++g_failures;
		}
	}

	/// Construit un répertoire temporaire unique sous temp_directory_path.
	/// Abort si la création échoue (test invalide sinon).
	std::filesystem::path MakeTempContentDir()
	{
		std::random_device rd;
		std::mt19937_64 rng(rd());
		const std::filesystem::path base = std::filesystem::temp_directory_path()
			/ ("lcdlln_quest_text_catalog_test_" + std::to_string(rng()));
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		if (ec)
		{
			std::cerr << "[FATAL] cannot create temp dir " << base.string() << ": " << ec.message() << "\n";
			std::abort();
		}
		return base;
	}

	/// Écrit `jsonBody` comme textes de quêtes (`quests/quest_texts.<locale>.json`)
	/// dans un content root temporaire isolé, construit une Config pointant
	/// dessus (`paths.content`) et retourne le chemin de la racine.
	std::filesystem::path WriteFixture(const std::string& jsonBody, std::string_view locale, engine::core::Config& outConfig)
	{
		const std::filesystem::path contentRoot = MakeTempContentDir();

		outConfig = engine::core::Config();
		outConfig.SetValue("paths.content", engine::core::Config::Value{ contentRoot.string() });

		const std::string relativePath = "quests/quest_texts." + std::string(locale) + ".json";
		const bool written = engine::platform::FileSystem::WriteAllTextContent(outConfig, relativePath, jsonBody);
		if (!written)
		{
			std::cerr << "[FATAL] cannot write quest text fixture under " << contentRoot.string() << "\n";
			std::abort();
		}

		return contentRoot;
	}
}

int main()
{
	// Chargement nominal + résolution titre/description/étape.
	{
		const std::string json = R"JSON({
      "q1": {
        "title": "T",
        "description": "D",
        "steps": [ "Tués {current}/{required}" ]
      }
    })JSON";

		engine::core::Config config;
		WriteFixture(json, "fr", config);

		QuestTextCatalog catalog;
		Check(catalog.Load(config, "fr"), "Load OK sur fixture fr");

		Check(catalog.Title("q1") == "T", "Title(q1)==T");
		Check(catalog.Description("q1") == "D", "Description(q1)==D");
		Check(catalog.StepLabel("q1", 0, 3, 10) == "Tués 3/10", "StepLabel(q1,0,3,10)==\"Tués 3/10\"");

		// Fallback : questId inconnu.
		Check(catalog.Title("absent") == "absent", "Title(absent)==absent (fallback questId)");
		Check(catalog.Description("absent") == "", "Description(absent)==\"\" (fallback)");
		Check(catalog.StepLabel("absent", 0, 1, 2) == "1/2", "StepLabel(absent,0,1,2)==\"1/2\" (fallback numérique)");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
