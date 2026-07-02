// Tests du QuestGiverTable — association PNJ -> quêtes (donneur/receveur),
// résolue depuis le contenu data-driven côté client.
//
// Écrit une fixture JSON dans un répertoire temporaire unique (isolé du vrai
// contenu du dépôt) et pointe `paths.content` dessus via Config::SetValue, à
// l'instar de QuestTextCatalogTests.cpp (MakeTempContentDir). QuestGiverTable
// lit toujours un fichier via FileSystem::ReadAllTextContent, donc le test
// passe par une vraie E/S disque (déterministe : un dossier temporaire par
// appel).

#include "src/client/quest/QuestGiverTable.h"
#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <string>

using engine::client::QuestGiverTable;

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
			/ ("lcdlln_quest_giver_table_test_" + std::to_string(rng()));
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		if (ec)
		{
			std::cerr << "[FATAL] cannot create temp dir " << base.string() << ": " << ec.message() << "\n";
			std::abort();
		}
		return base;
	}

	/// Écrit `jsonBody` comme table PNJ->quêtes (`quests/quest_givers.json`)
	/// dans un content root temporaire isolé, construit une Config pointant
	/// dessus (`paths.content`) et retourne le chemin de la racine.
	std::filesystem::path WriteFixture(const std::string& jsonBody, engine::core::Config& outConfig)
	{
		const std::filesystem::path contentRoot = MakeTempContentDir();

		outConfig = engine::core::Config();
		outConfig.SetValue("paths.content", engine::core::Config::Value{ contentRoot.string() });

		const bool written = engine::platform::FileSystem::WriteAllTextContent(outConfig, "quests/quest_givers.json", jsonBody);
		if (!written)
		{
			std::cerr << "[FATAL] cannot write quest giver fixture under " << contentRoot.string() << "\n";
			std::abort();
		}

		return contentRoot;
	}
}

int main()
{
	// Chargement nominal + résolution PNJ connu/inconnu.
	{
		const std::string json = R"JSON({
      "npc:elder_marn": [
        { "questId": "kill_10_boars", "role": 0 },
        { "questId": "kill_10_boars", "role": 1 }
      ]
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestGiverTable table;
		Check(table.Load(config), "Load OK sur fixture nominale");

		const std::vector<engine::client::QuestGiverLink>* links = table.ForNpc("npc:elder_marn");
		Check(links != nullptr, "ForNpc(npc:elder_marn) != nullptr");
		if (links != nullptr)
		{
			Check(links->size() == 2, "ForNpc(npc:elder_marn)->size()==2");
			if (links->size() == 2)
			{
				Check(links->at(0).questId == "kill_10_boars", "links[0].questId==kill_10_boars");
				Check(links->at(0).role == 0, "links[0].role==0 (giver)");
				Check(links->at(1).questId == "kill_10_boars", "links[1].questId==kill_10_boars");
				Check(links->at(1).role == 1, "links[1].role==1 (turnIn)");
			}
		}

		Check(table.ForNpc("npc:inconnu") == nullptr, "ForNpc(npc:inconnu)==nullptr");
	}

	// Rejet : role hors {0,1}.
	{
		const std::string json = R"JSON({
      "npc:elder_marn": [
        { "questId": "kill_10_boars", "role": 2 }
      ]
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestGiverTable table;
		Check(!table.Load(config), "Load rejette role==2");
	}

	// Rejet : questId vide.
	{
		const std::string json = R"JSON({
      "npc:elder_marn": [
        { "questId": "", "role": 0 }
      ]
    })JSON";

		engine::core::Config config;
		WriteFixture(json, config);

		QuestGiverTable table;
		Check(!table.Load(config), "Load rejette questId vide");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
