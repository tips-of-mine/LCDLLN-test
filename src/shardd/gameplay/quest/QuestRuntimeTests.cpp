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

	// EXT-2 — ShouldRepeatReset : helpers purs (now/completedAt fixes, pas d'horloge).
	{
		using engine::server::ShouldRepeatReset;
		using engine::server::UtcDayIndex;
		using engine::server::UtcWeekIndex;

		constexpr uint64_t kDayMs  = 86400000ULL;
		constexpr uint64_t kHourMs = 3600000ULL;

		// None → jamais de reset.
		Check(!ShouldRepeatReset(QuestRepeatMode::None, 0, 1000, 999999999ULL),
		      "EXT-2 ShouldRepeatReset None → false");

		// Repeatable → toujours vrai (même completedAt==now).
		Check(ShouldRepeatReset(QuestRepeatMode::Repeatable, 0, 42, 42),
		      "EXT-2 ShouldRepeatReset Repeatable → true");

		// Daily : même jour → false, jour suivant → true. On place completedAt juste
		// avant une borne minuit UTC (fin du jour N) et now juste après (début N+1).
		const uint64_t dayBoundary = 20000ULL * kDayMs; // minuit UTC du jour 20000
		Check(UtcDayIndex(dayBoundary - 1) == 19999ULL, "EXT-2 UtcDayIndex avant borne = 19999");
		Check(UtcDayIndex(dayBoundary) == 20000ULL, "EXT-2 UtcDayIndex à la borne = 20000");
		Check(!ShouldRepeatReset(QuestRepeatMode::Daily, 0, dayBoundary + 10, dayBoundary + 20),
		      "EXT-2 Daily même jour → false");
		Check(ShouldRepeatReset(QuestRepeatMode::Daily, 0, dayBoundary - 10, dayBoundary + 10),
		      "EXT-2 Daily jour suivant → true");

		// Weekly : borne lundi. UtcWeekIndex = (day + 3) / 7. Un jour d tel que
		// (d+3)%7==0 est un lundi : d=4 → (4+3)/7=1. Le jour 4 = lundi 1970-01-05
		// (l'epoch 0 est un jeudi). completedAt le dimanche (jour 3), now le lundi
		// (jour 4) : la borne de semaine est franchie.
		Check(UtcWeekIndex(3ULL * kDayMs) != UtcWeekIndex(4ULL * kDayMs),
		      "EXT-2 UtcWeekIndex change entre dimanche(j3) et lundi(j4)");
		Check(UtcWeekIndex(4ULL * kDayMs) == UtcWeekIndex(10ULL * kDayMs),
		      "EXT-2 UtcWeekIndex identique lundi(j4)..dimanche(j10)");
		Check(ShouldRepeatReset(QuestRepeatMode::Weekly, 0, 3ULL * kDayMs, 4ULL * kDayMs),
		      "EXT-2 Weekly passe la borne lundi → true");
		Check(!ShouldRepeatReset(QuestRepeatMode::Weekly, 0, 4ULL * kDayMs, 5ULL * kDayMs),
		      "EXT-2 Weekly même semaine → false");

		// Cooldown : écoulé >= heures → true, sinon false, completedAt==0 → false.
		const uint64_t base = 1000ULL * kDayMs;
		Check(ShouldRepeatReset(QuestRepeatMode::Cooldown, 6, base, base + 6 * kHourMs),
		      "EXT-2 Cooldown exactement 6h écoulées → true");
		Check(ShouldRepeatReset(QuestRepeatMode::Cooldown, 6, base, base + 7 * kHourMs),
		      "EXT-2 Cooldown 7h écoulées → true");
		Check(!ShouldRepeatReset(QuestRepeatMode::Cooldown, 6, base, base + 5 * kHourMs),
		      "EXT-2 Cooldown 5h < 6h → false");
		Check(!ShouldRepeatReset(QuestRepeatMode::Cooldown, 6, 0, base + 100 * kHourMs),
		      "EXT-2 Cooldown completedAt==0 → false (jamais complétée)");
	}

	// EXT-2 — ApplyRepeatResets : Completed daily ancien → Locked + delta + steps zéro ;
	// Completed None → inchangé ; Completed daily aujourd'hui → inchangé.
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "qdaily", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "repeat": "daily",
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 3 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } },
        { "id": "qonce", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [],
          "steps": [ { "type": "kill", "target": "mob:2", "requiredCount": 2 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-2 Init charge qdaily/qonce pour ApplyRepeatResets");

		constexpr uint64_t kDayMs = 86400000ULL;
		const uint64_t nowMs = 30000ULL * kDayMs + 12 * 3600000ULL; // milieu du jour 30000

		// Cas 1 : qdaily Completed complétée hier → doit reset.
		{
			std::vector<QuestState> states;
			QuestState d{}; d.questId = "qdaily"; d.status = QuestStatus::Completed;
			d.stepProgressCounts = { 3u }; d.completedAtEpochMs = 29999ULL * kDayMs + 100; // jour 29999
			states.push_back(d);
			QuestState o{}; o.questId = "qonce"; o.status = QuestStatus::Completed;
			o.stepProgressCounts = { 2u }; o.completedAtEpochMs = 29999ULL * kDayMs + 100;
			states.push_back(o);

			std::vector<QuestProgressDelta> deltas;
			const bool changed = runtime.ApplyRepeatResets(states, nowMs, deltas);
			Check(changed, "EXT-2 ApplyRepeatResets signale un changement");
			Check(states[0].status == QuestStatus::Locked, "EXT-2 qdaily repasse Locked");
			Check(states[0].stepProgressCounts.size() == 1 && states[0].stepProgressCounts[0] == 0u,
			      "EXT-2 qdaily steps remis à zéro");
			// qonce (mode None) reste Completed.
			Check(states[1].status == QuestStatus::Completed, "EXT-2 qonce (None) reste Completed");
			Check(states[1].stepProgressCounts[0] == 2u, "EXT-2 qonce steps inchangés");
			bool sawDailyDelta = false, sawOnceDelta = false;
			for (const auto& dl : deltas)
			{
				if (dl.questId == "qdaily" && dl.status == QuestStatus::Locked) sawDailyDelta = true;
				if (dl.questId == "qonce") sawOnceDelta = true;
			}
			Check(sawDailyDelta, "EXT-2 delta Locked émis pour qdaily");
			Check(!sawOnceDelta, "EXT-2 aucun delta pour qonce (inchangée)");
		}

		// Cas 2 : qdaily complétée aujourd'hui → pas de reset.
		{
			std::vector<QuestState> states;
			QuestState d{}; d.questId = "qdaily"; d.status = QuestStatus::Completed;
			d.stepProgressCounts = { 3u }; d.completedAtEpochMs = 30000ULL * kDayMs + 1000; // même jour que now
			states.push_back(d);

			std::vector<QuestProgressDelta> deltas;
			const bool changed = runtime.ApplyRepeatResets(states, nowMs, deltas);
			Check(!changed, "EXT-2 ApplyRepeatResets : rien à reset le même jour");
			Check(states[0].status == QuestStatus::Completed, "EXT-2 qdaily complétée aujourd'hui reste Completed");
			Check(deltas.empty(), "EXT-2 aucun delta quand rien ne change");
		}
	}

	// EXT-3 — parse du flag partyShared (défaut false + true lu).
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "shared1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "partyShared": true,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } },
        { "id": "solo1", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [],
          "steps": [ { "type": "kill", "target": "mob:2", "requiredCount": 1 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-3 Init charge shared1 (partyShared) + solo1");
		const auto* shared = runtime.FindQuestDefinition("shared1");
		const auto* solo = runtime.FindQuestDefinition("solo1");
		Check(shared != nullptr && solo != nullptr, "EXT-3 shared1/solo1 trouvées");
		if (shared != nullptr && solo != nullptr)
		{
			Check(shared->partyShared, "EXT-3 partyShared=true parsé");
			Check(!solo->partyShared, "EXT-3 partyShared absent → false (rétro-compat)");
		}
	}

	// EXT-3 — filtre ApplyEvent(onlyPartyShared) : deux quêtes Active avec une
	// étape kill matchante, une partyShared:true une :false. onlyPartyShared=true
	// n'avance QUE la partagée ; le défaut (false) avance les deux (non-régression).
	{
		const std::string json = R"JSON({
      "quests": [
        { "id": "qshared", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "partyShared": true,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 3 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } },
        { "id": "qsolo", "giver": "npc:marn", "turnIn": "npc:marn",
          "prereqs": [], "partyShared": false,
          "steps": [ { "type": "kill", "target": "mob:1", "requiredCount": 3 } ],
          "rewards": { "xp": 10, "gold": 5, "items": [] } }
      ]
    })JSON";

		QuestRuntime runtime = MakeRuntimeWithFixture(json);
		Check(runtime.Init(), "EXT-3 Init charge qshared/qsolo pour le filtre");

		// Cas A : onlyPartyShared=true → seule qshared avance.
		{
			std::vector<QuestState> states;
			std::vector<QuestProgressDelta> deltas;
			Check(runtime.SyncQuestStates(states, deltas), "EXT-3 sync (filtre) OK");
			Check(states.size() == 2, "EXT-3 deux états créés (filtre)");
			if (states.size() == 2)
			{
				states[0].status = QuestStatus::Active; // qshared
				states[1].status = QuestStatus::Active; // qsolo

				deltas.clear();
				const bool advanced = runtime.ApplyEvent(
					states, QuestStepType::Kill, "mob:1", 1, deltas, /*onlyPartyShared=*/true);
				Check(advanced, "EXT-3 onlyPartyShared=true progresse au moins une quête");
				Check(states[0].stepProgressCounts[0] == 1u,
				      "EXT-3 onlyPartyShared=true avance qshared (partagée)");
				Check(states[1].stepProgressCounts[0] == 0u,
				      "EXT-3 onlyPartyShared=true n'avance PAS qsolo (non partagée)");
				bool sawSoloDelta = false;
				for (const auto& d : deltas)
					if (d.questId == "qsolo") sawSoloDelta = true;
				Check(!sawSoloDelta, "EXT-3 aucun delta pour qsolo sous onlyPartyShared=true");
			}
		}

		// Cas B (non-régression) : défaut (onlyPartyShared=false) → les deux avancent.
		{
			std::vector<QuestState> states;
			std::vector<QuestProgressDelta> deltas;
			Check(runtime.SyncQuestStates(states, deltas), "EXT-3 sync (défaut) OK");
			if (states.size() == 2)
			{
				states[0].status = QuestStatus::Active;
				states[1].status = QuestStatus::Active;

				deltas.clear();
				runtime.ApplyEvent(states, QuestStepType::Kill, "mob:1", 1, deltas);
				Check(states[0].stepProgressCounts[0] == 1u,
				      "EXT-3 défaut avance qshared");
				Check(states[1].stepProgressCounts[0] == 1u,
				      "EXT-3 défaut avance aussi qsolo (non-régression)");
			}
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
