// Tests du QuestRuntime — cycle de vie piloté par le joueur (SP1).
//
// Écrit une fixture JSON dans un répertoire temporaire unique (isolé du vrai
// contenu du dépôt) et pointe `paths.content` dessus via Config::SetValue,
// à l'instar de WorldEditorSessionTests.cpp (MakeTempContentDir). QuestRuntime
// n'expose pas de LoadFromText comme CreatureArchetypeLibrary : Init() lit
// toujours un fichier via FileSystem::ReadAllTextContent, donc le test passe
// par une vraie E/S disque (déterministe : un dossier temporaire par appel).

#include "src/shardd/gameplay/quest/QuestRuntime.h"
#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"

#include <filesystem>
#include <iostream>
#include <random>
#include <string>

using engine::server::QuestRuntime;
using engine::server::QuestStatus;
using engine::server::QuestState;
using engine::server::QuestStepType;
using engine::server::QuestProgressDelta;

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
			/ ("lcdlln_quest_runtime_test_" + std::to_string(rng()));
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		if (ec)
		{
			std::cerr << "[FATAL] cannot create temp dir " << base.string() << ": " << ec.message() << "\n";
			std::abort();
		}
		return base;
	}

	/// Écrit `jsonBody` comme définitions de quêtes dans un content root
	/// temporaire isolé, construit une Config pointant dessus (`paths.content`)
	/// et retourne un QuestRuntime prêt à appeler Init(). Le chemin relatif
	/// reste le défaut (`quests/quest_definitions.json`) : seul le content
	/// root change, donc on ne touche pas server.quest_definitions_path.
	QuestRuntime MakeRuntimeWithFixture(const std::string& jsonBody)
	{
		const std::filesystem::path contentRoot = MakeTempContentDir();

		engine::core::Config config;
		config.SetValue("paths.content", engine::core::Config::Value{ contentRoot.string() });

		const bool written = engine::platform::FileSystem::WriteAllTextContent(
			config, "quests/quest_definitions.json", jsonBody);
		if (!written)
		{
			std::cerr << "[FATAL] cannot write quest fixture under " << contentRoot.string() << "\n";
			std::abort();
		}

		return QuestRuntime(config);
	}
}

int main()
{
	// Une définition minimale avec giver/turnIn.
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "q1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 2 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "Init charge une définition avec giver/turnIn");

		const auto* def = runtime.FindQuestDefinition("q1");
		Check(def != nullptr, "q1 trouvée");
		if (def != nullptr)
		{
			Check(def->giverId == "npc:marn", "giver parsé");
			Check(def->turnInId == "npc:marn", "turnIn parsé");
		}
	}

	// Rejet : quête sans giver.
	{
		const std::string bad = R"JSON({ "quests": [
          { "id": "nogiver", "turnIn": "npc:x", "prereqs": [],
            "steps": [ { "type": "talk", "target": "npc:x", "requiredCount": 1 } ],
            "rewards": { "xp": 1, "gold": 0, "items": [] } } ] })JSON";

		QuestRuntime badRuntime = MakeRuntimeWithFixture(bad);
		Check(!badRuntime.Init(), "Init rejette une quête sans giver");
	}

	// Rejet : quête sans turnIn.
	{
		const std::string bad = R"JSON({ "quests": [
          { "id": "noturnin", "giver": "npc:x", "prereqs": [],
            "steps": [ { "type": "talk", "target": "npc:x", "requiredCount": 1 } ],
            "rewards": { "xp": 1, "gold": 0, "items": [] } } ] })JSON";

		QuestRuntime badRuntime = MakeRuntimeWithFixture(bad);
		Check(!badRuntime.Init(), "Init rejette une quête sans turnIn");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
