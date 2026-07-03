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
using engine::server::QuestRepeatMode;

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

	// Transitions pilotées par le joueur : Offered → Active (accept) →
	// ReadyToTurnIn (dernière étape complétée, sans récompense auto) → turn-in.
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "q1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 2 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "Init charge q1 pour le test de transitions");

		std::vector<QuestState> states;
		std::vector<QuestProgressDelta> deltas;
		Check(runtime.SyncQuestStates(states, deltas), "sync OK");
		// q1 sans prérequis → Offered (PAS Active).
		Check(states.size() == 1 && states[0].status == QuestStatus::Offered,
		      "prérequis remplis → Offered");

		const auto* def = runtime.FindQuestDefinition("q1");
		Check(def != nullptr, "def q1");
		Check(runtime.CanAccept(states[0], *def, "npc:marn"), "accept au bon giver");
		Check(!runtime.CanAccept(states[0], *def, "npc:autre"), "refus mauvais giver");

		// Simuler l'accept : Offered → Active.
		states[0].status = QuestStatus::Active;

		// Progresser : 1 kill (pas assez) reste Active.
		deltas.clear();
		runtime.ApplyEvent(states, QuestStepType::Kill, "mob:1", 1, deltas);
		Check(states[0].status == QuestStatus::Active, "1/2 kill → reste Active");

		// 2e kill → ReadyToTurnIn, SANS reward dans le delta.
		deltas.clear();
		runtime.ApplyEvent(states, QuestStepType::Kill, "mob:1", 1, deltas);
		Check(states[0].status == QuestStatus::ReadyToTurnIn, "2/2 kill → ReadyToTurnIn");
		bool anyReward = false;
		for (const auto& d : deltas)
			if (d.rewardExperience != 0 || d.rewardGold != 0 || !d.rewardItems.empty()) anyReward = true;
		Check(!anyReward, "aucune récompense au passage ReadyToTurnIn");

		Check(runtime.CanTurnIn(states[0], *def, "npc:marn"), "turn-in au bon PNJ");
		Check(!runtime.CanTurnIn(states[0], *def, "npc:autre"), "refus turn-in mauvais PNJ");

		const auto* reward = runtime.TakeRewardOnTurnIn(*def);
		Check(reward != nullptr && reward->experience == 10, "reward exposé au turn-in");
	}

	// EXT-1 — Exclusion mutuelle : A exclut B. Fixture partagée pour les cas
	// (a)..(d). A et B sont sans prérequis (donc naturellement Offered), A
	// déclarant "excludes": ["qB"] ; B ne déclare rien (test de symétrie).
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "qA", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "excludes": [ "qB" ],
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } },
        { "id": "qB", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [],
          "steps": [ { "type": "kill", "target": "mob:2", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "Init charge qA (excludes qB) + qB");

		const auto* defA = runtime.FindQuestDefinition("qA");
		const auto* defB = runtime.FindQuestDefinition("qB");
		Check(defA != nullptr && defB != nullptr, "qA et qB trouvées");
		if (defA != nullptr && defB != nullptr)
		{
			Check(defA->excludedQuestIds.size() == 1 && defA->excludedQuestIds[0] == "qB",
			      "excludes parsé sur qA");
			Check(defB->excludedQuestIds.empty(), "qB sans excludes (rétro-compat champ absent)");

			std::vector<QuestState> states;
			std::vector<QuestProgressDelta> deltas;
			Check(runtime.SyncQuestStates(states, deltas), "sync initiale OK");
			// (d) Aucune quête engagée → les deux sont seulement Offered → aucun blocage.
			const size_t iA = 0, iB = 1; // ordre = ordre des définitions
			Check(states.size() == 2, "deux états créés");
			bool bothOffered = states.size() == 2
				&& states[iA].status == QuestStatus::Offered
				&& states[iB].status == QuestStatus::Offered;
			Check(bothOffered, "(d) A et B Offered quand rien n'est engagé");
			Check(!runtime.IsBlockedByExclusion(states, *defB),
			      "(d) B non bloquée quand A seulement Offered");
			Check(!runtime.IsBlockedByExclusion(states, *defA),
			      "(d) A non bloquée quand B seulement Offered");

			// Engager A (Offered → Active), comme le ferait un accept.
			states[iA].status = QuestStatus::Active;

			// (b) IsBlockedByExclusion(B) vrai quand A engagée (Active).
			Check(runtime.IsBlockedByExclusion(states, *defB),
			      "(b) B bloquée quand A Active (sens direct : A exclut B)");
			// (c) Symétrie : B ne déclare pas A, mais reste bloquée.
			Check(runtime.IsBlockedByExclusion(states, *defB),
			      "(c) symétrie : B bloquée même sans déclarer A");
			// A n'est pas bloquée par elle-même.
			Check(!runtime.IsBlockedByExclusion(states, *defA),
			      "A non bloquée par sa propre exclusion");

			// (a) offer-sync : re-sync ne doit PAS repasser B à Offered (reste Locked).
			deltas.clear();
			Check(runtime.SyncQuestStates(states, deltas), "re-sync avec A engagée OK");
			Check(states[iB].status == QuestStatus::Locked,
			      "(a) offer-sync garde B Locked quand A engagée");

			// (b) idem quand A est Completed (statut terminal, toujours « engagée »).
			states[iA].status = QuestStatus::Completed;
			Check(runtime.IsBlockedByExclusion(states, *defB),
			      "(b) B bloquée quand A Completed");
		}
	}

	// EXT-1 (e) — Rétro-compat : une quête sans "excludes" se comporte à
	// l'identique (Offered malgré une autre quête engagée).
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "qX", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [],
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } },
        { "id": "qY", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [],
          "steps": [ { "type": "kill", "target": "mob:2", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "(e) Init charge qX/qY sans excludes");

		const auto* defY = runtime.FindQuestDefinition("qY");
		Check(defY != nullptr, "(e) qY trouvée");

		std::vector<QuestState> states;
		std::vector<QuestProgressDelta> deltas;
		Check(runtime.SyncQuestStates(states, deltas), "(e) sync OK");
		Check(states.size() == 2, "(e) deux états créés");
		if (states.size() == 2)
		{
			states[0].status = QuestStatus::Active; // qX engagée
			deltas.clear();
			Check(runtime.SyncQuestStates(states, deltas), "(e) re-sync OK");
			// Sans excludes, qY reste proposée malgré qX engagée.
			Check(states[1].status == QuestStatus::Offered,
			      "(e) qY reste Offered (aucun excludes déclaré)");
			if (defY != nullptr)
			{
				Check(!runtime.IsBlockedByExclusion(states, *defY),
				      "(e) qY jamais bloquée sans excludes");
			}
		}
	}

	// EXT-2 — parse repeat/cooldownHours/autoComplete.
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "daily1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "repeat": "daily", "autoComplete": true,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-2 Init charge daily1 (repeat=daily, autoComplete)");
		const auto* def = runtime.FindQuestDefinition("daily1");
		Check(def != nullptr, "EXT-2 daily1 trouvée");
		if (def != nullptr)
		{
			Check(def->repeatMode == QuestRepeatMode::Daily, "EXT-2 repeat=daily → Daily");
			Check(def->autoComplete, "EXT-2 autoComplete=true parsé");
		}
	}

	// EXT-2 — rétro-compat : sans champs repeat/autoComplete → None/false.
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "legacy1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-2 Init charge legacy1 (aucun champ EXT-2)");
		const auto* def = runtime.FindQuestDefinition("legacy1");
		Check(def != nullptr, "EXT-2 legacy1 trouvée");
		if (def != nullptr)
		{
			Check(def->repeatMode == QuestRepeatMode::None, "EXT-2 repeat absent → None (rétro-compat)");
			Check(!def->autoComplete, "EXT-2 autoComplete absent → false (rétro-compat)");
			Check(def->cooldownHours == 0, "EXT-2 cooldownHours absent → 0");
		}
	}

	// EXT-2 — cooldown valide (cooldownHours > 0).
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "cd1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "repeat": "cooldown", "cooldownHours": 6,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-2 Init charge cd1 (cooldown 6h)");
		const auto* def = runtime.FindQuestDefinition("cd1");
		Check(def != nullptr, "EXT-2 cd1 trouvée");
		if (def != nullptr)
		{
			Check(def->repeatMode == QuestRepeatMode::Cooldown, "EXT-2 repeat=cooldown → Cooldown");
			Check(def->cooldownHours == 6, "EXT-2 cooldownHours=6 parsé");
		}
	}

	// EXT-2 — rejet : cooldown sans cooldownHours (défaut 0).
	{
		const std::string bad = R"JSON({ "quests": [
          { "id": "cdbad", "giver": "npc:marn", "turnIn": "npc:marn",
            "prereqs": [], "repeat": "cooldown",
            "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
            "rewards": { "xp": 1, "gold": 0, "items": [] } } ] })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(bad);
		Check(!runtime.Init(), "EXT-2 Init rejette cooldown sans cooldownHours");
	}

	// EXT-2 — rejet : cooldown avec cooldownHours == 0.
	{
		const std::string bad = R"JSON({ "quests": [
          { "id": "cdzero", "giver": "npc:marn", "turnIn": "npc:marn",
            "prereqs": [], "repeat": "cooldown", "cooldownHours": 0,
            "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
            "rewards": { "xp": 1, "gold": 0, "items": [] } } ] })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(bad);
		Check(!runtime.Init(), "EXT-2 Init rejette cooldown avec cooldownHours 0");
	}

	// EXT-2 — rejet : mode repeat invalide.
	{
		const std::string bad = R"JSON({ "quests": [
          { "id": "bogus", "giver": "npc:marn", "turnIn": "npc:marn",
            "prereqs": [], "repeat": "bogus",
            "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
            "rewards": { "xp": 1, "gold": 0, "items": [] } } ] })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(bad);
		Check(!runtime.Init(), "EXT-2 Init rejette repeat invalide");
	}

	// EXT-2 — autoComplete : la dernière étape complétée passe Completed
	// (pas ReadyToTurnIn). Un jumeau autoComplete=false reste ReadyToTurnIn.
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "qauto", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "autoComplete": true,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } },
        { "id": "qmanual", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "autoComplete": false,
          "steps": [ { "type": "kill", "target": "mob:2", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-2 Init charge qauto/qmanual");

		std::vector<QuestState> states;
		std::vector<QuestProgressDelta> deltas;
		Check(runtime.SyncQuestStates(states, deltas), "EXT-2 sync OK");
		Check(states.size() == 2, "EXT-2 deux états créés");
		if (states.size() == 2)
		{
			// Accepter les deux (Offered → Active).
			states[0].status = QuestStatus::Active;
			states[1].status = QuestStatus::Active;

			// Compléter l'étape de qauto → Completed (auto).
			deltas.clear();
			runtime.ApplyEvent(states, QuestStepType::Kill, "mob:1", 1, deltas);
			Check(states[0].status == QuestStatus::Completed,
			      "EXT-2 autoComplete → Completed (pas ReadyToTurnIn)");
			bool sawAutoCompletedDelta = false;
			for (const auto& d : deltas)
				if (d.questId == "qauto" && d.status == QuestStatus::Completed) sawAutoCompletedDelta = true;
			Check(sawAutoCompletedDelta, "EXT-2 delta qauto porte le statut Completed");

			// Compléter l'étape de qmanual → ReadyToTurnIn (inchangé).
			deltas.clear();
			runtime.ApplyEvent(states, QuestStepType::Kill, "mob:2", 1, deltas);
			Check(states[1].status == QuestStatus::ReadyToTurnIn,
			      "EXT-2 autoComplete=false → ReadyToTurnIn (inchangé)");
		}
	}

	if (g_failures != 0)
	{
		std::cerr << g_failures << " assertion(s) échouée(s)\n";
		return 1;
	}
	std::cout << "OK\n";
	return 0;
}
