// Tests de QuestEditIo::Load, QuestEditIo::Validate et QuestEditIo::Save —
// chargement, validation et écriture authoring des quêtes (SP4, Tâches 1, 2, 3).
//
// Écrit une fixture (quest_definitions.json + quest_texts.fr.json) dans un
// répertoire temporaire unique (isolé du vrai contenu du dépôt), à l'instar
// de QuestRuntimeTests.cpp. QuestEditIo::Load prend un contentRoot brut
// (chemin filesystem, pas de Config) : pas besoin de FileSystem::WriteAllTextContent,
// un std::ofstream direct suffit.
//
// QuestEditIo::Validate est pure (pas d'I/O) : les cas ci-dessous construisent
// des std::vector<EditedQuest> directement en mémoire, sans fixture disque.
//
// QuestEditIo::Save écrit sur disque (3 fichiers) : les cas ci-dessous
// vérifient le round-trip Save -> Load (égalité structurelle) et le contenu
// brut des fichiers écrits (quest_givers.json régénéré, quest_definitions.json
// en JSON pur tableaux et non au format `count`-indexé de Config).
//
// EXT-2 : cas (n)-(r) couvrent la re-réalisation (repeat/cooldownHours/
// autoComplete) : round-trip cooldown, défaut None/0/false sans champ, rejet
// d'un mode inconnu, rejet du mode cooldown avec cooldownHours=0 (parse ET
// Validate).

#include "src/world_editor/quests/QuestEditIo.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

using engine::editor::world::quests::EditedQuest;
using engine::editor::world::quests::EditedRewardItem;
using engine::editor::world::quests::EditedStep;
using engine::editor::world::quests::QuestEditIo;
using engine::editor::world::quests::QuestRepeatMode;

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

	/// Construit une quête minimale mais valide (un `id` et un `giver`/`turnIn`
	/// donnés, une seule étape "kill" bien formée). Point de départ pour les
	/// cas de test de `QuestEditIo::Validate`, modifiée localement par chaque cas.
	EditedQuest MakeValidQuest(const std::string& id)
	{
		EditedQuest quest;
		quest.id = id;
		quest.giver = "npc:giver_" + id;
		quest.turnIn = "npc:giver_" + id;

		EditedStep step;
		step.type = "kill";
		step.target = "mob:100";
		step.requiredCount = 1;
		quest.steps.push_back(step);

		return quest;
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

	/// Relit intégralement `<contentRoot>/quests/<fileName>` en mémoire (pour
	/// inspection brute du contenu écrit par `QuestEditIo::Save` dans les tests).
	/// Abort si le fichier n'a pas pu être ouvert (fixture/écriture invalide sinon).
	std::string ReadQuestsFile(const std::filesystem::path& contentRoot, const std::string& fileName)
	{
		const std::filesystem::path filePath = contentRoot / "quests" / fileName;
		std::ifstream file(filePath, std::ios::binary);
		if (!file)
		{
			std::cerr << "[FATAL] cannot read written file " << filePath.string() << "\n";
			std::abort();
		}
		std::ostringstream ss;
		ss << file.rdbuf();
		return ss.str();
	}

	/// Construit l'ensemble de quêtes en mémoire utilisé par le test round-trip
	/// Save -> Load : `kill_10_boars` avec `giver` == `turnIn` == "npc:elder_marn"
	/// (pour exercer la régénération quest_givers.json avec les 2 rôles sur le
	/// même PNJ), plus une seconde quête avec prereq et textes complets.
	std::vector<EditedQuest> MakeRoundTripQuests()
	{
		EditedQuest boars;
		boars.id = "kill_10_boars";
		boars.giver = "npc:elder_marn";
		boars.turnIn = "npc:elder_marn";
		EditedStep boarStep;
		boarStep.type = "kill";
		boarStep.target = "mob:100";
		boarStep.requiredCount = 10;
		boars.steps.push_back(boarStep);
		boars.rewardXp = 50;
		boars.rewardGold = 20;
		boars.rewardItems.push_back(EditedRewardItem{ 7, 2 });
		boars.title = "Chasse aux sangliers";
		boars.description = "Tuez 10 sangliers pour l'aîné Marn.";
		boars.stepLabels.push_back("Sangliers tués : {current}/{required}");

		EditedQuest scout;
		scout.id = "talk_to_scout";
		scout.giver = "npc:scout";
		scout.turnIn = "npc:scout";
		scout.prereqs.push_back("kill_10_boars");
		// Exclusion mutuelle (structurelle) exercee par le round-trip : scout
		// exclut boars (id existant, != scout) -> parse -> serialize -> parse.
		scout.excludes.push_back("kill_10_boars");
		EditedStep talkStep;
		talkStep.type = "talk";
		talkStep.target = "npc:scout";
		talkStep.requiredCount = 1;
		scout.steps.push_back(talkStep);
		scout.rewardXp = 5;
		scout.title = "Éclaireur";
		scout.description = "Parlez à l'éclaireur.";
		scout.stepLabels.push_back("Parlé à l'éclaireur");

		return { boars, scout };
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

	// --- QuestEditIo::Validate ---------------------------------------------

	// (a) ensemble valide → true, 0 erreur.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_a"), MakeValidQuest("quest_b") };
		quests[1].prereqs.push_back("quest_a");

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(ok, "Validate: ensemble valide -> true");
		Check(errors.empty(), "Validate: ensemble valide -> 0 erreur");
	}

	// (b) deux quêtes avec le même id -> false.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("dup"), MakeValidQuest("dup") };

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: id dupliqué -> false");
		Check(!errors.empty(), "Validate: id dupliqué -> au moins 1 erreur");
	}

	// (c) prereqs=["inconnu"] -> false (dangling).
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_c") };
		quests[0].prereqs.push_back("inconnu");

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: prereq dangling -> false");
		Check(!errors.empty(), "Validate: prereq dangling -> au moins 1 erreur");
	}

	// (d) cycle A.prereqs=[B], B.prereqs=[A] -> false.
	{
		EditedQuest questA = MakeValidQuest("quest_cycle_a");
		EditedQuest questB = MakeValidQuest("quest_cycle_b");
		questA.prereqs.push_back("quest_cycle_b");
		questB.prereqs.push_back("quest_cycle_a");
		std::vector<EditedQuest> quests = { questA, questB };

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: cycle prereqs -> false");
		Check(!errors.empty(), "Validate: cycle prereqs -> au moins 1 erreur");
	}

	// (e) giver == "" -> false.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_e") };
		quests[0].giver.clear();

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: giver vide -> false");
		Check(!errors.empty(), "Validate: giver vide -> au moins 1 erreur");
	}

	// (f) quête sans étape -> false.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_f") };
		quests[0].steps.clear();

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: quête sans étape -> false");
		Check(!errors.empty(), "Validate: quête sans étape -> au moins 1 erreur");
	}

	// (g) étape avec target == "" -> false.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_g") };
		quests[0].steps[0].target.clear();

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: étape target vide -> false");
		Check(!errors.empty(), "Validate: étape target vide -> au moins 1 erreur");
	}

	// (k) excludes vers un id existant -> valide (pas de contrainte d'acyclicite :
	// une exclusion mutuelle declaree dans les deux sens reste valide).
	{
		EditedQuest questA = MakeValidQuest("quest_excl_a");
		EditedQuest questB = MakeValidQuest("quest_excl_b");
		questA.excludes.push_back("quest_excl_b");
		questB.excludes.push_back("quest_excl_a");
		std::vector<EditedQuest> quests = { questA, questB };

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(ok, "Validate: excludes mutuel (A<->B) -> true (pas de cycle-check)");
		Check(errors.empty(), "Validate: excludes mutuel -> 0 erreur");
	}

	// (l) auto-exclusion (id dans son propre excludes) -> false.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_self_excl") };
		quests[0].excludes.push_back("quest_self_excl");

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: auto-exclusion -> false");
		Check(!errors.empty(), "Validate: auto-exclusion -> au moins 1 erreur");
	}

	// (m) excludes vers un id inexistant (dangling) -> false.
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("quest_m") };
		quests[0].excludes.push_back("inconnu");

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "Validate: exclude dangling -> false");
		Check(!errors.empty(), "Validate: exclude dangling -> au moins 1 erreur");
	}

	// --- QuestEditIo::Save ---------------------------------------------------

	// (h) round-trip Save -> Load : égalité structurelle id/giver/turnIn/steps/
	// rewards/title/stepLabels.
	{
		const std::filesystem::path saveRoot = MakeTempContentDir();
		const std::vector<EditedQuest> original = MakeRoundTripQuests();

		std::string saveError;
		const bool saved = io.Save(saveRoot.string(), original, saveError);
		Check(saved, "Save réussit sur un ensemble valide");
		Check(saveError.empty(), "Save ne renseigne pas outError en cas de succès");

		std::vector<EditedQuest> reloaded;
		std::string loadError;
		const bool reloadedOk = io.Load(saveRoot.string(), reloaded, loadError);
		Check(reloadedOk, "Load réussit après Save (round-trip)");
		Check(reloaded.size() == original.size(), "round-trip: même nombre de quêtes");

		for (const EditedQuest& orig : original)
		{
			const EditedQuest* found = nullptr;
			for (const EditedQuest& candidate : reloaded)
			{
				if (candidate.id == orig.id)
				{
					found = &candidate;
				}
			}

			const std::string label = "round-trip '" + orig.id + "'";
			Check(found != nullptr, (label + ": retrouvée après reload").c_str());
			if (found == nullptr)
			{
				continue;
			}

			Check(found->giver == orig.giver, (label + ": giver identique").c_str());
			Check(found->turnIn == orig.turnIn, (label + ": turnIn identique").c_str());
			Check(found->prereqs == orig.prereqs, (label + ": prereqs identiques").c_str());
			Check(found->excludes == orig.excludes, (label + ": excludes identiques").c_str());
			Check(found->steps.size() == orig.steps.size(), (label + ": nombre d'étapes identique").c_str());
			for (size_t i = 0; i < orig.steps.size() && i < found->steps.size(); ++i)
			{
				Check(found->steps[i].type == orig.steps[i].type, (label + ": step.type identique").c_str());
				Check(found->steps[i].target == orig.steps[i].target, (label + ": step.target identique").c_str());
				Check(found->steps[i].requiredCount == orig.steps[i].requiredCount, (label + ": step.requiredCount identique").c_str());
			}
			Check(found->rewardXp == orig.rewardXp, (label + ": rewardXp identique").c_str());
			Check(found->rewardGold == orig.rewardGold, (label + ": rewardGold identique").c_str());
			Check(found->rewardItems.size() == orig.rewardItems.size(), (label + ": nombre d'items de récompense identique").c_str());
			for (size_t i = 0; i < orig.rewardItems.size() && i < found->rewardItems.size(); ++i)
			{
				Check(found->rewardItems[i].itemId == orig.rewardItems[i].itemId, (label + ": rewardItem.itemId identique").c_str());
				Check(found->rewardItems[i].quantity == orig.rewardItems[i].quantity, (label + ": rewardItem.quantity identique").c_str());
			}
			Check(found->title == orig.title, (label + ": title identique").c_str());
			Check(found->stepLabels == orig.stepLabels, (label + ": stepLabels identiques").c_str());
		}

		// (i) quest_givers.json régénéré : npc:elder_marn doit avoir à la fois
		// {kill_10_boars, role 0} (giver) et {kill_10_boars, role 1} (turnIn),
		// puisque giver == turnIn == "npc:elder_marn" pour cette quête.
		const std::string giversJson = ReadQuestsFile(saveRoot, "quest_givers.json");
		Check(giversJson.find("\"npc:elder_marn\"") != std::string::npos, "quest_givers.json: clé npc:elder_marn présente");
		Check(giversJson.find("\"questId\": \"kill_10_boars\", \"role\": 0") != std::string::npos,
			"quest_givers.json: entrée role 0 (giver) pour kill_10_boars");
		Check(giversJson.find("\"questId\": \"kill_10_boars\", \"role\": 1") != std::string::npos,
			"quest_givers.json: entrée role 1 (turnIn) pour kill_10_boars");

		// (j) quest_definitions.json écrit est du JSON pur (tableaux), pas le
		// format `count`-indexé de Config.
		const std::string definitionsJsonWritten = ReadQuestsFile(saveRoot, "quest_definitions.json");
		Check(definitionsJsonWritten.find("\"quests\"") != std::string::npos, "quest_definitions.json: clé 'quests' présente");
		Check(definitionsJsonWritten.find('[') != std::string::npos, "quest_definitions.json: contient un tableau JSON '['");
		Check(definitionsJsonWritten.find("\"count\"") == std::string::npos, "quest_definitions.json: PAS de format count-indexé");

		std::error_code saveCleanupError;
		std::filesystem::remove_all(saveRoot, saveCleanupError);
	}

	// --- EXT-2 : re-réalisation (repeat/cooldownHours/autoComplete) ----------

	// (n) round-trip Save -> Load d'une quête cooldown : repeat=cooldown,
	// cooldownHours=12, autoComplete=true doivent survivre parse -> serialize ->
	// parse à l'identique.
	{
		const std::filesystem::path repeatRoot = MakeTempContentDir();

		EditedQuest daily = MakeValidQuest("cooldown_quest");
		daily.repeatMode = QuestRepeatMode::Cooldown;
		daily.cooldownHours = 12;
		daily.autoComplete = true;

		std::string saveError;
		const bool saved = io.Save(repeatRoot.string(), { daily }, saveError);
		Check(saved, "EXT-2: Save d'une quête cooldown réussit");

		std::vector<EditedQuest> reloaded;
		std::string loadError;
		const bool reloadedOk = io.Load(repeatRoot.string(), reloaded, loadError);
		Check(reloadedOk, "EXT-2: Load après Save (cooldown) réussit");
		Check(reloaded.size() == 1, "EXT-2: 1 quête rechargée");
		if (reloaded.size() == 1)
		{
			Check(reloaded[0].repeatMode == QuestRepeatMode::Cooldown, "EXT-2: repeatMode == Cooldown après round-trip");
			Check(reloaded[0].cooldownHours == 12, "EXT-2: cooldownHours == 12 après round-trip");
			Check(reloaded[0].autoComplete == true, "EXT-2: autoComplete == true après round-trip");
		}

		std::error_code repeatCleanupError;
		std::filesystem::remove_all(repeatRoot, repeatCleanupError);
	}

	// (o) une quête SANS champ EXT-2 se recharge en None/0/false (rétro-compat :
	// la fixture principale plus haut n'a pas de champ repeat).
	{
		const std::filesystem::path plainRoot = MakeTempContentDir();
		const std::string plainJson = R"JSON({
      "quests": [
        { "id": "plain", "giver": "npc:g", "turnIn": "npc:g",
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ] }
      ]
    })JSON";
		WriteQuestsFile(plainRoot, "quest_definitions.json", plainJson);

		std::vector<EditedQuest> plainOut;
		std::string plainError;
		const bool plainOk = io.Load(plainRoot.string(), plainOut, plainError);
		Check(plainOk, "EXT-2: Load d'une quête sans champ repeat réussit");
		Check(plainOut.size() == 1, "EXT-2: 1 quête sans repeat chargée");
		if (plainOut.size() == 1)
		{
			Check(plainOut[0].repeatMode == QuestRepeatMode::None, "EXT-2: défaut repeatMode == None");
			Check(plainOut[0].cooldownHours == 0, "EXT-2: défaut cooldownHours == 0");
			Check(plainOut[0].autoComplete == false, "EXT-2: défaut autoComplete == false");
		}

		std::error_code plainCleanupError;
		std::filesystem::remove_all(plainRoot, plainCleanupError);
	}

	// (p) `repeat` inconnu -> Load rejette (parse bloquant).
	{
		const std::filesystem::path badRoot = MakeTempContentDir();
		const std::string badJson = R"JSON({
      "quests": [
        { "id": "bad_repeat", "giver": "npc:g", "turnIn": "npc:g", "repeat": "monthly",
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ] }
      ]
    })JSON";
		WriteQuestsFile(badRoot, "quest_definitions.json", badJson);

		std::vector<EditedQuest> badOut;
		std::string badError;
		const bool badOk = io.Load(badRoot.string(), badOut, badError);
		Check(!badOk, "EXT-2: repeat inconnu -> Load échoue");
		Check(!badError.empty(), "EXT-2: repeat inconnu -> outError renseigné");

		std::error_code badCleanupError;
		std::filesystem::remove_all(badRoot, badCleanupError);
	}

	// (q) mode cooldown avec cooldownHours=0 -> Load rejette (règle inter-champs).
	{
		const std::filesystem::path zeroRoot = MakeTempContentDir();
		const std::string zeroJson = R"JSON({
      "quests": [
        { "id": "cooldown_zero", "giver": "npc:g", "turnIn": "npc:g",
          "repeat": "cooldown", "cooldownHours": 0,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ] }
      ]
    })JSON";
		WriteQuestsFile(zeroRoot, "quest_definitions.json", zeroJson);

		std::vector<EditedQuest> zeroOut;
		std::string zeroError;
		const bool zeroOk = io.Load(zeroRoot.string(), zeroOut, zeroError);
		Check(!zeroOk, "EXT-2: cooldown + cooldownHours=0 -> Load échoue");
		Check(!zeroError.empty(), "EXT-2: cooldown + cooldownHours=0 -> outError renseigné");

		std::error_code zeroCleanupError;
		std::filesystem::remove_all(zeroRoot, zeroCleanupError);
	}

	// (r) Validate rejette aussi le mode cooldown avec cooldownHours=0 (règle
	// inter-champs, en mémoire, sans passer par le disque).
	{
		std::vector<EditedQuest> quests = { MakeValidQuest("cooldown_validate") };
		quests[0].repeatMode = QuestRepeatMode::Cooldown;
		quests[0].cooldownHours = 0;

		std::vector<std::string> errors;
		const bool ok = io.Validate(quests, errors);
		Check(!ok, "EXT-2: Validate cooldown + cooldownHours=0 -> false");
		Check(!errors.empty(), "EXT-2: Validate cooldown + cooldownHours=0 -> au moins 1 erreur");
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
