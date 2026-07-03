#pragma once

#include "src/shared/core/Config.h"
#include "src/shared/network/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// Supported quest step event types loaded from the JSON definitions.
	enum class QuestStepType : uint8_t
	{
		Kill = 1,
		Collect = 2,
		Talk = 3,
		Enter = 4
	};

	/// Runtime status stored for one quest on one player. L'ordre est
	/// significatif (persisté et transmis wire) : ne pas réordonner sans migration.
	enum class QuestStatus : uint8_t
	{
		Locked        = 0,   ///< pré-requis non remplis (interne, non affiché)
		Offered       = 1,   ///< proposée au PNJ giver, pas encore acceptée
		Active        = 2,   ///< acceptée, en cours
		ReadyToTurnIn = 3,   ///< étapes remplies, à rendre (pas encore récompensée)
		Completed     = 4,   ///< rendue + récompensée (terminal)
	};

	/// One typed step loaded from the quest definitions JSON.
	struct QuestStepDefinition
	{
		QuestStepType type = QuestStepType::Kill;
		std::string targetId;
		uint32_t requiredCount = 1;
	};

	/// One reward bundle granted when a quest is completed.
	struct QuestReward
	{
		uint32_t experience = 0;
		uint32_t gold = 0;
		std::vector<ItemStack> items;
	};

	/// EXT-2 — Mode de répétition d'une quête (par quête, choisi dans le JSON).
	/// L'ordre est significatif (mappé depuis le JSON, potentiellement persisté
	/// dans l'éditeur) : ne pas réordonner sans migration.
	enum class QuestRepeatMode : uint8_t
	{
		None       = 0,   ///< quête one-shot classique (défaut, rétro-compatible)
		Repeatable = 1,   ///< re-réalisable immédiatement, sans délai
		Daily      = 2,   ///< re-réalisable au changement de jour UTC
		Weekly     = 3,   ///< re-réalisable au changement de semaine UTC (lundi 00:00)
		Cooldown   = 4,   ///< re-réalisable après `cooldownHours` heures écoulées
	};

	/// One quest definition loaded from the data-driven JSON file.
	struct QuestDefinition
	{
		std::string questId;
		std::string giverId;    ///< PNJ qui propose la quête (même espace que targetId du Talk)
		std::string turnInId;   ///< PNJ où rendre la quête (souvent = giverId)
		std::vector<std::string> prerequisiteQuestIds;
		/// EXT-1 : quêtes mutuellement exclusives. S'engager dans cette quête
		/// (Active/ReadyToTurnIn/Completed) verrouille chacune de ces quêtes, et
		/// réciproquement (exclusion symétrique évaluée au runtime).
		std::vector<std::string> excludedQuestIds;
		std::vector<QuestStepDefinition> steps;
		QuestReward rewards;
		/// EXT-2 — mode de répétition (défaut None = one-shot, rétro-compatible).
		QuestRepeatMode repeatMode = QuestRepeatMode::None;
		/// EXT-2 — délai de réactivation en heures ; pertinent uniquement si
		/// repeatMode == Cooldown (doit alors être > 0 ; ignoré sinon).
		uint32_t cooldownHours = 0;
		/// EXT-2 — si vrai, la quête se termine (Completed) automatiquement dès la
		/// dernière étape complétée, sans retour au PNJ (récompense versée par le
		/// caller ServerApp, pas par QuestRuntime).
		bool autoComplete = false;
		/// EXT-3 — si vrai, le crédit d'étape est propagé aux membres du groupe de
		/// l'acteur à portée (fan-out réalisé côté ServerApp). Défaut false =
		/// quête solo, rétro-compatible.
		bool partyShared = false;
	};

	/// Per-player stored quest state required by the server runtime.
	struct QuestState
	{
		std::string questId;
		QuestStatus status = QuestStatus::Locked;
		std::vector<uint32_t> stepProgressCounts;
		/// EXT-2 — ms UTC de la dernière complétion (0 = jamais complétée).
		/// Sert de borne au reset des quêtes répétables/quotidiennes/cooldown.
		uint64_t completedAtEpochMs = 0;
	};

	/// One quest state change emitted after sync or progress evaluation.
	struct QuestProgressDelta
	{
		std::string questId;
		QuestStatus status = QuestStatus::Locked;
		std::vector<uint32_t> stepProgressCounts;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<ItemStack> rewardItems;
	};

	/// EXT-2 — Index du jour UTC (minuit UTC comme borne). Pur/testable.
	/// \param ms horodatage en ms depuis l'epoch Unix (UTC).
	uint64_t UtcDayIndex(uint64_t ms);

	/// EXT-2 — Index de la semaine UTC alignée sur lundi 00:00 UTC. Pur/testable.
	/// Le jour epoch 0 (1970-01-01) est un jeudi ; `+3` décale l'origine pour que
	/// chaque semaine démarre le lundi.
	/// \param ms horodatage en ms depuis l'epoch Unix (UTC).
	uint64_t UtcWeekIndex(uint64_t ms);

	/// EXT-2 — Vrai si une quête `Completed` doit redevenir disponible selon son
	/// mode de répétition. Fonction pure (aucune horloge interne : `nowMs` injecté),
	/// donc testable unitairement.
	/// \param mode mode de répétition de la définition.
	/// \param cooldownHours délai en heures (pertinent uniquement en mode Cooldown).
	/// \param completedAtMs ms UTC de la dernière complétion (0 = jamais).
	/// \param nowMs ms UTC courantes (injectées par le caller).
	bool ShouldRepeatReset(QuestRepeatMode mode, uint32_t cooldownHours,
		uint64_t completedAtMs, uint64_t nowMs);

	/// Return a readable name for one quest step type.
	const char* GetQuestStepTypeName(QuestStepType type);

	/// Return a readable name for one quest status.
	const char* GetQuestStatusName(QuestStatus status);

	/// Server-side quest runtime: JSON loading, prerequisite sync and progress evaluation.
	class QuestRuntime final
	{
	public:
		/// Capture the config used to resolve the quest JSON content path.
		explicit QuestRuntime(const engine::core::Config& config);

		/// Emit shutdown logs when the quest runtime is destroyed.
		~QuestRuntime();

		/// Load the JSON quest definitions and validate the schema.
		bool Init();

		/// Release every loaded quest definition and emit shutdown logs.
		void Shutdown();

		/// Ensure one player's quest state table matches the loaded quest definitions.
		bool SyncQuestStates(std::vector<QuestState>& states, std::vector<QuestProgressDelta>& outDeltas) const;

		/// Apply one authoritative quest event and emit deltas for every changed quest.
		/// EXT-3 — \p onlyPartyShared : si true, ignore toute définition dont
		/// `!partyShared` (crédit réservé aux quêtes partagées en groupe, pour le
		/// fan-out aux coéquipiers). Défaut false = comportement historique
		/// (toutes les quêtes Active matchantes avancent).
		bool ApplyEvent(
			std::vector<QuestState>& states,
			QuestStepType eventType,
			std::string_view targetId,
			uint32_t amount,
			std::vector<QuestProgressDelta>& outDeltas,
			bool onlyPartyShared = false) const;

		/// Find one quest definition by id, or return `nullptr` when it is absent.
		const QuestDefinition* FindQuestDefinition(std::string_view questId) const;

		/// Vrai si \p state peut être accepté au PNJ \p giverTargetId : quête Offered
		/// et \p giverTargetId == def.giverId. Pur, sans effet de bord.
		bool CanAccept(const QuestState& state, const QuestDefinition& def, std::string_view giverTargetId) const;

		/// Vrai si \p state peut être rendu au PNJ \p npcTargetId : quête ReadyToTurnIn
		/// et \p npcTargetId == def.turnInId. Pur, sans effet de bord.
		bool CanTurnIn(const QuestState& state, const QuestDefinition& def, std::string_view npcTargetId) const;

		/// Retourne le bundle de récompense à verser au turn-in (jamais nul).
		const QuestReward* TakeRewardOnTurnIn(const QuestDefinition& def) const;

		/// EXT-2 — Rejoue les resets temporels des quêtes répétables/quotidiennes/
		/// cooldown. Pour chaque \p state `Completed` dont la définition a
		/// `repeatMode != None` et `ShouldRepeatReset(...) == true` : statut remis à
		/// `Locked`, `stepProgressCounts` remis à zéro (taille = def.steps.size()),
		/// un `QuestProgressDelta` (Locked + compteurs zéro) ajouté à \p outDeltas.
		/// À appeler AVANT `SyncQuestStates` (qui repromeut alors le Locked à
		/// Offered dans la même passe). \param nowMs ms UTC courantes (injectées).
		/// \return true si au moins un état a changé.
		bool ApplyRepeatResets(std::vector<QuestState>& states, uint64_t nowMs,
			std::vector<QuestProgressDelta>& outDeltas) const;

		/// EXT-1 — Vrai si \p def est actuellement bloquée par exclusion mutuelle
		/// pour ce joueur : soit (a) une quête listée dans `def.excludedQuestIds`
		/// est engagée, soit (b) une autre définition dont `excludedQuestIds`
		/// contient `def.questId` est engagée (symétrie : l'exclusion déclarée d'un
		/// seul côté bloque les deux). Pur, sans effet de bord.
		bool IsBlockedByExclusion(const std::vector<QuestState>& states, const QuestDefinition& def) const;

	private:
		/// Load every quest definition from the configured JSON content file.
		bool LoadDefinitions();

		/// EXT-1 — Vrai si le joueur a un état pour \p questId dont le statut est
		/// « engagé » (∈ {Active, ReadyToTurnIn, Completed}). Offered et Locked ne
		/// comptent pas : voir une quête proposée n'engage pas encore.
		bool IsQuestEngaged(const std::vector<QuestState>& states, const std::string& questId) const;

		engine::core::Config m_config;
		std::string m_questDefinitionsRelativePath;
		std::vector<QuestDefinition> m_definitions;
		bool m_initialized = false;
	};
}
