// Tests de QuestEditIo::Load — chargement authoring des quêtes (SP4, Tâche 1).
//
// Écrit une fixture (quest_definitions.json + quest_texts.fr.json) dans un
// répertoire temporaire unique (isolé du vrai contenu du dépôt), à l'instar
// de QuestRuntimeTests.cpp. QuestEditIo::Load prend un contentRoot brut
// (chemin filesystem, pas de Config) : pas besoin de FileSystem::WriteAllTextContent,
// un std::ofstream direct suffit.

#include "src/world_editor/quests/QuestEditIo.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

using engine::editor::world::quests::EditedQuest;
using engine::editor::world::quests::QuestEditIo;

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
			/ ("lcdlln_quest_edit_io_test_" + std::to_string(rng()));
		std::error_code ec;
		std::filesystem::create_directories(base / "quests", ec);
		if (ec)
		{
			std::cerr << "[FATAL] cannot create temp dir " << base.string() << ": " << ec.message() << "\n";
			std::abort();
		}
		return base;
	}

	/// Écrit \p text dans `<contentRoot>/quests/<fileName>`. Abort si l'écriture
	/// échoue (fixture invalide sinon).
	void WriteQuestsFile(const std::filesystem::path& contentRoot, const std::string& fileName, const std::string& text)
	{
		const std::filesystem::path filePath = contentRoot / "quests" / fileName;
		std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			std::cerr << "[FATAL] cannot write fixture file " << filePath.string() << "\n";
			std::abort();
		}
		file << text;
	}
}

int main()
{
	const std::filesystem::path contentRoot = MakeTempContentDir();

	const std::string definitionsJson = R"JSON({
      "quests": [
        { "id": "kill_10_boars", "giver": "npc:elder_marn", "turnIn": "npc:elder_marn",
          "prereqs": [], "steps": [ { "type": "kill", "target": "mob:100", "requiredCount": 10 } ],
          "rewards": { "xp": 50, "gold": 20, "items": [] } },
        { "id": "talk_to_scout", "giver": "npc:scout", "turnIn": "npc:scout",
          "prereqs": [], "steps": [ { "type": "talk", "target": "npc:scout", "requiredCount": 1 } ],
          "rewards": { "xp": 5, "gold": 0, "items": [] } }
      ]
    })JSON";
	WriteQuestsFile(contentRoot, "quest_definitions.json", definitionsJson);

	const std::string textsJson = R"JSON({
      "kill_10_boars": {
        "title": "Chasse aux sangliers",
        "description": "Tuez 10 sangliers pour l'aîné Marn.",
        "steps": [ "Sangliers tués : {current}/{required}" ]
      }
    })JSON";
	WriteQuestsFile(contentRoot, "quest_texts.fr.json", textsJson);

	QuestEditIo io;
	std::vector<EditedQuest> out;
	std::string error;
	const bool loaded = io.Load(contentRoot.string(), out, error);

	Check(loaded, "Load réussit sur une fixture valide");
	Check(error.empty(), "Load ne renseigne pas outError en cas de succès");
	Check(out.size() == 2, "2 quêtes chargées");

	const EditedQuest* boars = nullptr;
	for (const EditedQuest& quest : out)
	{
		if (quest.id == "kill_10_boars")
		{
			boars = &quest;
		}
	}

	Check(boars != nullptr, "kill_10_boars trouvée");
	if (boars != nullptr)
	{
		Check(boars->giver == "npc:elder_marn", "giver parsé");
		Check(boars->turnIn == "npc:elder_marn", "turnIn parsé");
		Check(boars->steps.size() == 1, "1 étape");
		if (!boars->steps.empty())
		{
			Check(boars->steps[0].type == "kill", "step.type parsé");
			Check(boars->steps[0].target == "mob:100", "step.target parsé");
			Check(boars->steps[0].requiredCount == 10, "step.requiredCount parsé");
		}
		Check(boars->rewardXp == 50, "rewardXp parsé");
		Check(boars->rewardGold == 20, "rewardGold parsé");
		Check(!boars->title.empty(), "title fusionné depuis quest_texts.fr.json");
		Check(!boars->stepLabels.empty() && !boars->stepLabels[0].empty(), "stepLabels[0] fusionné et non vide");
	}

	std::error_code cleanupError;
	std::filesystem::remove_all(contentRoot, cleanupError);

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
