// Tests du QuestPoiTable — positions minimap associées à une cible de quête
// (`targetId`), résolues depuis le contenu data-driven côté client.
//
// Écrit une fixture JSON dans un répertoire temporaire unique (isolé du vrai
// contenu du dépôt) et pointe `paths.content` dessus via Config::SetValue, à
// l'instar de QuestGiverTableTests.cpp/QuestTextCatalogTests.cpp
// (MakeTempContentDir). QuestPoiTable lit toujours un fichier via
// FileSystem::ReadAllTextContent, donc le test passe par une vraie E/S disque
// (déterministe : un dossier temporaire par appel).

#include "src/client/quest/QuestPoiTable.h"
#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <string>

using engine::client::QuestPoiTable;

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
			/ ("lcdlln_quest_poi_table_test_" + std::to_string(rng()));
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		if (ec)
		{
			std::cerr << "[FATAL] cannot create temp dir " << base.string() << ": " << ec.message() << "\n";
			std::abort();
		}
		return base;
	}

	/// Écrit `jsonBody` comme table de POI (`quests/quest_poi.json`) dans un
	/// content root temporaire isolé, construit une Config pointant dessus
	/// (`paths.content`) et retourne le chemin de la racine.
	std::filesystem::path WriteFixture(const std::string& jsonBody, engine::core::Config& outConfig)
	{
		const std::filesystem::path contentRoot = MakeTempContentDir();

		outConfig = engine::core::Config();
		outConfig.SetValue("paths.content", engine::core::Config::Value{ contentRoot.string() });

		const bool written = engine::platform::FileSystem::WriteAllTextContent(outConfig, "quests/quest_poi.json", jsonBody);
		if (!written)
		{
			std::cerr << "[FATAL] cannot write quest poi fixture under " << contentRoot.string() << "\n";
			std::abort();
		}

		return contentRoot;
	}
}

int main()
{
	// Chargement nominal + résolution cible connue/inconnue.
	{
		const std::string json = R"JSON({
      "mob:100": [[12,-28],[-10,-34]],
      "npc:elder_marn": [[4,0]]
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestPoiTable table;
		Check(table.Load(config), "Load OK sur fixture nominale");

		const std::vector<engine::client::QuestPoiPosition>* mobPositions = table.Positions("mob:100");
		Check(mobPositions != nullptr, "Positions(mob:100) != nullptr");
		if (mobPositions != nullptr)
		{
			Check(mobPositions->size() == 2, "Positions(mob:100)->size()==2");
			if (mobPositions->size() == 2)
			{
				Check(mobPositions->at(0).x == 12.0f && mobPositions->at(0).z == -28.0f, "Positions(mob:100)[0]==(12,-28)");
				Check(mobPositions->at(1).x == -10.0f && mobPositions->at(1).z == -34.0f, "Positions(mob:100)[1]==(-10,-34)");
			}
		}

		const std::vector<engine::client::QuestPoiPosition>* npcPositions = table.Positions("npc:elder_marn");
		Check(npcPositions != nullptr, "Positions(npc:elder_marn) != nullptr");
		if (npcPositions != nullptr)
		{
			Check(npcPositions->size() == 1, "Positions(npc:elder_marn)->size()==1");
			if (!npcPositions->empty())
			{
				Check(npcPositions->at(0).x == 4.0f && npcPositions->at(0).z == 0.0f, "Positions(npc:elder_marn)[0]==(4,0)");
			}
		}

		Check(table.Positions("inconnu") == nullptr, "Positions(inconnu)==nullptr");
	}

	// Rejet : valeur non-tableau pour une cible.
	{
		const std::string json = R"JSON({
      "mob:100": "pas-un-tableau"
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestPoiTable table;
		Check(!table.Load(config), "Load rejette valeur non-tableau");
	}

	// Rejet : paire [x,z] invalide (mauvaise arité).
	{
		const std::string json = R"JSON({
      "mob:100": [[12,-28,99]]
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestPoiTable table;
		Check(!table.Load(config), "Load rejette paire de mauvaise arité");
	}

	// Rejet : paire [x,z] non numérique.
	{
		const std::string json = R"JSON({
      "mob:100": [["a","b"]]
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestPoiTable table;
		Check(!table.Load(config), "Load rejette paire non numérique");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
