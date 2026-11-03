// ADM.7 — Quêtes et événements dynamiques (shard, maître de jeu).
//
// /gmquest add|complete|ready|remove|reset|status|list, /questsearch, /event.
//
// Quêtes : l'état du joueur (ConnectedClient::questStates) est modifié puis
// resynchronisé par QuestRuntime::SyncQuestStates (prérequis, exclusions : les
// quêtes débloquées ou verrouillées en conséquence) ; chaque changement part au
// client en QuestDelta et l'état est sauvegardé. « complete » verse la
// récompense comme un rendu au PNJ.
// Événements : start lance l'événement (première phase) ; stop l'arrête SANS
// récompense (mobs de phase retirés, recharge normale) ; complete le termine
// AVEC récompense des participants.

#include "src/shared/server_bootstrap/GmCommandExecutor.h"

#include "src/shardd/gameplay/gm/GmPlaceDirectory.h"

#include <algorithm>
#include <chrono>

namespace engine::server
{
	namespace
	{
		const char* QuestStatusFr(QuestStatus s)
		{
			switch (s)
			{
			case QuestStatus::Locked: return "verrouillée";
			case QuestStatus::Offered: return "proposée";
			case QuestStatus::Active: return "en cours";
			case QuestStatus::ReadyToTurnIn: return "à rendre";
			case QuestStatus::Completed: return "terminée";
			}
			return "?";
		}

		const char* EventStatusFr(DynamicEventStatus s)
		{
			switch (s)
			{
			case DynamicEventStatus::Idle: return "en attente";
			case DynamicEventStatus::Active: return "en cours";
			case DynamicEventStatus::Cooldown: return "en recharge";
			}
			return "?";
		}
	}

	void GmCommandExecutor::PushQuestChanges(ConnectedClient& target, QuestState& changed)
	{
		std::vector<QuestProgressDelta> deltas;
		QuestProgressDelta self;
		self.questId = changed.questId;
		self.status = changed.status;
		self.stepProgressCounts = changed.stepProgressCounts;
		deltas.push_back(self);
		std::vector<QuestProgressDelta> synced;
		(void)app.m_questRuntime.SyncQuestStates(target.questStates, synced);
		deltas.insert(deltas.end(), synced.begin(), synced.end());
		for (const QuestProgressDelta& d : deltas)
			(void)app.SendQuestDelta(target, d);
		app.SaveConnectedClient(target, "gm_quest");
	}

	void GmCommandExecutor::CmdGmQuest()
	{
		const std::string sub = args.empty() ? std::string() : engine::admin::ToLowerAscii(args[0]);
		static constexpr std::string_view kSyntax = "/gmquest add|complete|ready|remove|reset|status <quête> [joueur] | /gmquest list [joueur]";
		if (sub == "list")
		{
			ConnectedClient* target = TargetOrSelf(1);
			if (target == nullptr)
				return;
			size_t shown = 0;
			for (const QuestState& q : target->questStates)
			{
				if (q.status == QuestStatus::Locked)
					continue;
				if (shown == 0u)
					Say("Quêtes de " + NameOf(*target) + " :");
				if (++shown > 30u)
				{
					Say("  … (liste tronquée à 30)");
					break;
				}
				Say("  " + q.questId + " — " + QuestStatusFr(q.status));
			}
			if (shown == 0u)
				Say(NameOf(*target) + " n'a aucune quête proposée, en cours ou terminée.");
			return;
		}
		if (args.size() < 2u)
			return Usage(kSyntax);
		const QuestDefinition* def = app.m_questRuntime.FindQuestDefinition(args[1]);
		if (def == nullptr)
			return Fail(GmCommandStatus::NotFound, "Quête inconnue : " + args[1] + " (voir /questsearch).");
		ConnectedClient* target = TargetOrSelf(2);
		if (target == nullptr)
			return;
		auto it = std::find_if(target->questStates.begin(), target->questStates.end(),
			[&](const QuestState& q) { return q.questId == def->questId; });
		if (sub == "status")
		{
			if (it == target->questStates.end())
				return Say(def->questId + " : jamais vue par " + NameOf(*target) + ".");
			std::string steps;
			for (size_t i = 0; i < def->steps.size(); ++i)
			{
				const uint32_t done = i < it->stepProgressCounts.size() ? it->stepProgressCounts[i] : 0u;
				steps += (i == 0 ? " — étapes " : ", ") + std::to_string(done) + "/" + std::to_string(def->steps[i].requiredCount);
			}
			return Say(def->questId + " : " + QuestStatusFr(it->status) + steps + ".");
		}
		if (it == target->questStates.end())
		{
			QuestState fresh;
			fresh.questId = def->questId;
			fresh.status = QuestStatus::Locked;
			fresh.stepProgressCounts.assign(def->steps.size(), 0u);
			target->questStates.push_back(fresh);
			it = target->questStates.end() - 1;
		}
		QuestState& q = *it;
		if (q.stepProgressCounts.size() != def->steps.size())
			q.stepProgressCounts.resize(def->steps.size(), 0u);
		if (sub == "add" || sub == "start")
		{
			q.status = QuestStatus::Active;
			std::fill(q.stepProgressCounts.begin(), q.stepProgressCounts.end(), 0u);
			PushQuestChanges(*target, q);
			Say(def->questId + " ajoutée à " + NameOf(*target) + " (en cours).");
		}
		else if (sub == "ready")
		{
			q.status = QuestStatus::ReadyToTurnIn;
			for (size_t i = 0; i < def->steps.size(); ++i)
				q.stepProgressCounts[i] = def->steps[i].requiredCount;
			PushQuestChanges(*target, q);
			Say(def->questId + " : étapes remplies, à rendre au PNJ.");
		}
		else if (sub == "complete" || sub == "finish")
		{
			if (q.status == QuestStatus::Completed)
				return Fail(GmCommandStatus::Usage, def->questId + " est déjà terminée pour " + NameOf(*target) + ".");
			for (size_t i = 0; i < def->steps.size(); ++i)
				q.stepProgressCounts[i] = def->steps[i].requiredCount;
			q.status = QuestStatus::Completed;
			q.completedAtEpochMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count());
			app.GrantQuestReward(*target, *def);
			PushQuestChanges(*target, q);
			Say(def->questId + " terminée pour " + NameOf(*target) + " (récompense versée : " + std::to_string(def->rewards.experience)
				+ " XP, " + std::to_string(def->rewards.gold) + " or, " + std::to_string(def->rewards.items.size()) + " objet(s)).");
		}
		else if (sub == "remove" || sub == "reset")
		{
			// L'état est oublié : la synchronisation la repropose si ses prérequis sont remplis.
			const std::string questId = q.questId;
			target->questStates.erase(it);
			std::vector<QuestProgressDelta> synced;
			(void)app.m_questRuntime.SyncQuestStates(target->questStates, synced);
			QuestProgressDelta gone;
			gone.questId = questId;
			gone.status = QuestStatus::Locked;
			(void)app.SendQuestDelta(*target, gone);
			for (const QuestProgressDelta& d : synced)
				(void)app.SendQuestDelta(*target, d);
			app.SaveConnectedClient(*target, "gm_quest");
			Say(questId + " retirée à " + NameOf(*target) + (sub == "reset" ? " (elle pourra être reprise)." : "."));
		}
		else
		{
			Usage(kSyntax);
		}
	}

	void GmCommandExecutor::CmdQuestSearch()
	{
		const std::string query = engine::admin::JoinArgs(args, 0);
		const std::string folded = gm::FoldPlaceText(query);
		std::vector<const QuestDefinition*> found;
		for (const QuestDefinition& d : app.m_questRuntime.Definitions())
		{
			if (folded.empty() || gm::FoldPlaceText(d.questId).find(folded) != std::string::npos
				|| gm::FoldPlaceText(d.giverId).find(folded) != std::string::npos)
				found.push_back(&d);
		}
		if (found.empty())
			return Fail(GmCommandStatus::NotFound, "Aucune quête ne correspond à « " + query + " » ("
				+ std::to_string(app.m_questRuntime.Definitions().size()) + " quêtes chargées).");
		Say(std::to_string(found.size()) + " quête(s)" + (query.empty() ? std::string() : " pour « " + query + " »")
			+ (found.size() > 25u ? " (25 premières)" : "") + " :");
		for (size_t i = 0; i < found.size() && i < 25u; ++i)
		{
			const QuestDefinition& d = *found[i];
			Say("  " + d.questId + " — PNJ " + (d.giverId.empty() ? std::string("?") : d.giverId) + ", "
				+ std::to_string(d.steps.size()) + " étape(s), " + std::to_string(d.rewards.experience) + " XP");
		}
	}

	void GmCommandExecutor::CmdEvent()
	{
		const std::string sub = args.empty() ? std::string("list") : engine::admin::ToLowerAscii(args[0]);
		if (sub == "list")
		{
			if (app.m_dynamicEvents.empty())
				return Say("Aucun événement dynamique chargé sur ce shard.");
			Say(std::to_string(app.m_dynamicEvents.size()) + " événement(s) dynamique(s) :");
			for (const DynamicEventState& e : app.m_dynamicEvents)
			{
				Say("  " + e.definition.eventId + " — zone " + std::to_string(e.definition.zoneId) + ", " + EventStatusFr(e.status)
					+ (e.status == DynamicEventStatus::Active
						? ", phase " + std::to_string(e.currentPhaseIndex + 1u) + "/" + std::to_string(e.definition.phases.size())
							+ ", " + std::to_string(e.participantClientIds.size()) + " participant(s)"
						: std::string()));
			}
			return;
		}
		if (args.size() < 2u)
			return Usage("/event list | /event start|stop|complete <événement>");
		auto it = std::find_if(app.m_dynamicEvents.begin(), app.m_dynamicEvents.end(),
			[&](const DynamicEventState& e) { return engine::admin::EqualsIgnoreCase(e.definition.eventId, args[1]); });
		if (it == app.m_dynamicEvents.end())
			return Fail(GmCommandStatus::NotFound, "Événement inconnu : " + args[1] + " (voir /event list).");
		DynamicEventState& e = *it;
		if (sub == "start")
		{
			if (e.status == DynamicEventStatus::Active)
				return Fail(GmCommandStatus::Usage, e.definition.eventId + " est déjà en cours.");
			if (!app.StartDynamicEvent(e))
				return Fail(GmCommandStatus::Failed, "Démarrage impossible (phase sans mob valide ?).");
			return Say(e.definition.eventId + " démarré (zone " + std::to_string(e.definition.zoneId) + ").");
		}
		if (sub == "stop")
		{
			if (e.status != DynamicEventStatus::Active)
				return Fail(GmCommandStatus::Usage, e.definition.eventId + " n'est pas en cours.");
			const std::vector<EntityId> mobs = e.phaseMobEntityIds;
			for (const EntityId id : mobs)
				(void)app.DespawnDynamicEventMob(e, id, "gm_stop");
			e.status = DynamicEventStatus::Cooldown;
			e.phaseMobEntityIds.clear();
			e.participantClientIds.clear();
			e.currentPhaseIndex = 0;
			e.currentPhaseProgress = 0;
			app.BroadcastDynamicEventState(e, "Événement interrompu par un maître de jeu.", 0, 0, 0, {});
			app.ScheduleDynamicEventTrigger(e, true);
			return Say(e.definition.eventId + " arrêté sans récompense (" + std::to_string(mobs.size()) + " mob(s) retiré(s)).");
		}
		if (sub == "complete" || sub == "finish")
		{
			if (e.status != DynamicEventStatus::Active)
				return Fail(GmCommandStatus::Usage, e.definition.eventId + " n'est pas en cours.");
			const size_t participants = e.participantClientIds.size();
			const std::vector<EntityId> mobs = e.phaseMobEntityIds;
			for (const EntityId id : mobs)
				(void)app.DespawnDynamicEventMob(e, id, "gm_complete");
			app.CompleteDynamicEvent(e);
			return Say(e.definition.eventId + " terminé : " + std::to_string(participants) + " participant(s) récompensé(s).");
		}
		Usage("/event list | /event start|stop|complete <événement>");
	}
}
