// ServerApp — quêtes et inventaire : événements de quête, acceptation / rendu, récompenses, monnaie, état des événements dynamiques.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::AddItemToInventory(ConnectedClient& client, const ItemStack& item)
	{
		// Anniversaires SP3 (2026-07-18) — un gâteau qui ENTRE en inventaire
		// reçoit son expiration : fin du jour UTC courant (« mangé » à minuit,
		// cf. TickCakeBuffs). Posée une seule fois (pas re-décalée si le même
		// gâteau re-transite, ex. rollback de vente).
		if (engine::anniversary::IsCakeItemId(item.itemId)
			&& client.cakeExpiresAtMsUtcByItemId.find(item.itemId) == client.cakeExpiresAtMsUtcByItemId.end())
		{
			constexpr uint64_t kDayMs = 86400000ull;
			const uint64_t nowUtc = NowUnixEpochMsUtc();
			client.cakeExpiresAtMsUtcByItemId[item.itemId] = ((nowUtc / kDayMs) + 1ull) * kDayMs;
		}

		for (ItemStack& inventoryItem : client.inventory)
		{
			if (inventoryItem.itemId == item.itemId)
			{
				inventoryItem.quantity += item.quantity;
				LOG_INFO(Net, "[ServerApp] Inventory stack updated (client_id={}, item_id={}, quantity={})",
					client.clientId,
					item.itemId,
					inventoryItem.quantity);
				return;
			}
		}

		client.inventory.push_back(item);
		LOG_INFO(Net, "[ServerApp] Inventory item added (client_id={}, item_id={}, quantity={})",
			client.clientId,
			item.itemId,
			item.quantity);
	}

	void ServerApp::ApplyQuestEvent(
		ConnectedClient& client,
		QuestStepType eventType,
		std::string_view targetId,
		uint32_t amount,
		std::string_view reason)
	{
		std::vector<QuestProgressDelta> deltas;
		if (m_questRuntime.ApplyEvent(client.questStates, eventType, targetId, amount, deltas))
		{
			// L'acteur crédite TOUTES ses quêtes matchantes (onlyPartyShared=false),
			// puis on verse récompenses + envoie deltas + persiste via le helper.
			const size_t actorDeltas = deltas.size();
			FinalizeQuestDeltas(client, deltas, reason);
			LOG_INFO(Net,
				"[ServerApp] Quest event applied (client_id={}, type={}, target={}, deltas={}, reason={})",
				client.clientId,
				GetQuestStepTypeName(eventType),
				targetId,
				actorDeltas,
				reason);
		}

		// EXT-3 — fan-out groupe : propager le crédit aux coéquipiers à portée,
		// mais UNIQUEMENT pour les quêtes partyShared (filtre onlyPartyShared=true).
		// L'acteur a déjà été crédité (toutes quêtes) ci-dessus ; on l'exclut ici
		// pour éviter tout double-crédit.
		Party* party = m_partySystem.FindPartyByMember(client.clientId);
		if (party != nullptr)
		{
			const double radiusM = m_config.GetDouble("server.quest.party_share_radius_m", 30.0);
			const float radiusSq = static_cast<float>(radiusM * radiusM);
			for (const PartyMember& member : party->members)
			{
				if (member.clientId == client.clientId)
				{
					continue; // pas l'acteur (déjà crédité)
				}
				// FindConnectedClient résout un clientId via m_clientIndexByClientId.
				ConnectedClient* mate = FindConnectedClient(member.clientId);
				if (mate == nullptr)
				{
					continue; // coéquipier hors-ligne / non connecté
				}
				// Garde zone AVANT rayon : les positions sont en coordonnées par-zone,
				// exiger la même zone évite un faux positif de distance inter-zones.
				if (!engine::world::cells::SameMap(mate->zoneId, client.zoneId))
				{
					continue;
				}
				if (DistanceSquaredXZ(client.positionMetersX, client.positionMetersZ,
					mate->positionMetersX, mate->positionMetersZ) > radiusSq)
				{
					continue; // hors de portée de partage
				}

				std::vector<QuestProgressDelta> mateDeltas;
				if (m_questRuntime.ApplyEvent(mate->questStates, eventType, targetId, amount,
					mateDeltas, /*onlyPartyShared=*/true))
				{
					// Chaque coéquipier a sa propre instance de quête : récompense,
					// envoi et persistance lui sont propres (pas de partage de butin).
					FinalizeQuestDeltas(*mate, mateDeltas, reason);
					LOG_INFO(Net,
						"[ServerApp] Quest event shared to party mate (actor_id={}, mate_id={}, type={}, target={}, deltas={})",
						client.clientId,
						mate->clientId,
						GetQuestStepTypeName(eventType),
						targetId,
						mateDeltas.size());
				}
			}
		}
	}

	void ServerApp::FinalizeQuestDeltas(
		ConnectedClient& client,
		std::vector<QuestProgressDelta>& deltas,
		std::string_view reason)
	{
		// EXT-2 — auto-complétion : ApplyEvent n'émet un delta Completed QUE pour
		// une quête `autoComplete` (le seul chemin de QuestRuntime vers Completed ;
		// SyncQuestStates ne produit que Locked/Offered, et une quête non-auto passe
		// par ReadyToTurnIn puis Completed via HandleTurnInQuest, hors de ce chemin).
		// On verse donc la récompense ici pour ces deltas — sans risque de double
		// versement avec le turn-in explicite.
		for (const QuestProgressDelta& delta : deltas)
		{
			if (delta.status != QuestStatus::Completed)
			{
				continue;
			}
			const QuestDefinition* def = m_questRuntime.FindQuestDefinition(delta.questId);
			if (def == nullptr)
			{
				continue;
			}
			// EXT-2 — horodate la complétion auto sur l'état persisté (borne du
			// reset répétable/quotidien/cooldown) AVANT de verser la récompense.
			const size_t stateIdx = FindClientQuestStateIndex(client, delta.questId);
			if (stateIdx != kInvalidQuestIndex)
			{
				client.questStates[stateIdx].completedAtEpochMs = NowUnixEpochMsUtc();
			}
			GrantQuestReward(client, *def);
		}

		// SP1 quêtes — les récompenses de turn-in explicite sont versées dans
		// HandleTurnInQuest ; ce chemin propage les deltas de progression et verse
		// la récompense des seules quêtes auto-complétées (ci-dessus).
		for (const QuestProgressDelta& delta : deltas)
		{
			(void)SendQuestDelta(client, delta);
		}

		SaveConnectedClient(client, reason);
	}

	size_t ServerApp::FindClientQuestStateIndex(const ConnectedClient& client, std::string_view questId)
	{
		for (size_t i = 0; i < client.questStates.size(); ++i)
		{
			if (client.questStates[i].questId == questId)
			{
				return i;
			}
		}
		return kInvalidQuestIndex;
	}

	bool ServerApp::IsClientNearNpc(const ConnectedClient& client, std::string_view npcTargetId) const
	{
		// V1 : aucune vérification de distance serveur. Le Talk (HandleTalkRequest)
		// route aujourd'hui uniquement par targetId, sans contrôle de portée côté
		// serveur ; la proximité est garantie côté client (le Talk n'est émis que
		// lorsque le joueur est à portée du PNJ). TODO SP-ultérieur : durcir avec
		// une vraie distance joueur/PNJ une fois les positions PNJ exposées ici.
		(void)client;
		(void)npcTargetId;
		return true;
	}

	void ServerApp::SendQuestGiverList(const ConnectedClient& receiver, std::string_view npcTargetId)
	{
		QuestGiverListMessage msg{};
		msg.clientId = receiver.clientId;
		msg.npcTargetId = std::string(npcTargetId);
		for (const QuestState& state : receiver.questStates)
		{
			const QuestDefinition* def = m_questRuntime.FindQuestDefinition(state.questId);
			if (def == nullptr)
			{
				continue;
			}
			if (state.status == QuestStatus::Offered && def->giverId == npcTargetId)
			{
				msg.entries.push_back(QuestGiverEntry{ state.questId, 0u });
			}
			else if (state.status == QuestStatus::ReadyToTurnIn && def->turnInId == npcTargetId)
			{
				msg.entries.push_back(QuestGiverEntry{ state.questId, 1u });
			}
		}

		if (msg.entries.empty())
		{
			return;
		}

		const std::vector<std::byte> packet = EncodeQuestGiverList(msg);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] QuestGiverList send failed (client_id={}, npc={})",
				receiver.clientId,
				npcTargetId);
			return;
		}

		LOG_INFO(Net, "[ServerApp] QuestGiverList sent (client_id={}, npc={}, entries={})",
			receiver.clientId,
			npcTargetId,
			msg.entries.size());
	}

	void ServerApp::HandleAcceptQuest(const Endpoint& endpoint, const QuestAcceptRequestMessage& msg)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != msg.clientId)
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				msg.clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		const QuestDefinition* def = m_questRuntime.FindQuestDefinition(msg.questId);
		if (def == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest: quête inconnue (client_id={}, quest_id={})", client->clientId, msg.questId);
			return;
		}

		const size_t idx = FindClientQuestStateIndex(*client, msg.questId);
		if (idx == kInvalidQuestIndex)
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest: état de quête absent (client_id={}, quest_id={})", client->clientId, msg.questId);
			return;
		}
		QuestState& state = client->questStates[idx];

		if (!m_questRuntime.CanAccept(state, *def, msg.giverTargetId))
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest refusé (client_id={}, quest_id={}, giver={})",
				client->clientId, msg.questId, msg.giverTargetId);
			return;
		}

		// EXT-1 — re-vérification atomique de l'exclusion mutuelle : ferme la
		// fenêtre de course où deux quêtes exclusives sont Offered simultanément et
		// où le client accepterait la seconde avant que la sync n'ait rétrogradé
		// l'une des deux à Locked.
		if (m_questRuntime.IsBlockedByExclusion(client->questStates, *def))
		{
			LOG_WARN(Net, "[ServerApp] AcceptQuest refusé (exclusion) (client_id={}, quest_id={})",
				client->clientId, msg.questId);
			return;
		}

		if (!IsClientNearNpc(*client, msg.giverTargetId))
		{
			return;
		}

		state.status = QuestStatus::Active;
		state.stepProgressCounts.assign(def->steps.size(), 0u);

		QuestProgressDelta delta{};
		delta.questId = state.questId;
		delta.status = state.status;
		delta.stepProgressCounts = state.stepProgressCounts;
		(void)SendQuestDelta(*client, delta);
		SaveConnectedClient(*client, "quest_accept");
		LOG_INFO(Net, "[ServerApp] Quest accepted (client_id={}, quest_id={})", client->clientId, msg.questId);
	}

	void ServerApp::HandleTurnInQuest(const Endpoint& endpoint, const QuestTurnInRequestMessage& msg)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] TurnInQuest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != msg.clientId)
		{
			LOG_WARN(Net, "[ServerApp] TurnInQuest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				msg.clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		const QuestDefinition* def = m_questRuntime.FindQuestDefinition(msg.questId);
		if (def == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] TurnInQuest: quête inconnue (client_id={}, quest_id={})", client->clientId, msg.questId);
			return;
		}

		const size_t idx = FindClientQuestStateIndex(*client, msg.questId);
		if (idx == kInvalidQuestIndex)
		{
			LOG_WARN(Net, "[ServerApp] TurnInQuest: état de quête absent (client_id={}, quest_id={})", client->clientId, msg.questId);
			return;
		}
		QuestState& state = client->questStates[idx];

		if (!m_questRuntime.CanTurnIn(state, *def, msg.npcTargetId))
		{
			LOG_WARN(Net, "[ServerApp] TurnInQuest refusé (client_id={}, quest_id={}, npc={})",
				client->clientId, msg.questId, msg.npcTargetId);
			return;
		}

		if (!IsClientNearNpc(*client, msg.npcTargetId))
		{
			return;
		}

		// Verser les récompenses (bloc factorisé dans GrantQuestReward — partagé
		// avec l'auto-complétion). GrantQuestReward réplique déjà les deltas
		// inventaire/wallet ; ne reste ici que la transition de statut + le
		// QuestDelta (qui porte le résumé de récompense au client).
		const QuestReward* reward = m_questRuntime.TakeRewardOnTurnIn(*def);
		GrantQuestReward(*client, *def);

		state.status = QuestStatus::Completed;
		// EXT-2 — horodate la complétion (borne du reset répétable/quotidien/cooldown).
		state.completedAtEpochMs = NowUnixEpochMsUtc();

		QuestProgressDelta delta{};
		delta.questId = state.questId;
		delta.status = state.status;
		delta.stepProgressCounts = state.stepProgressCounts;
		if (reward != nullptr)
		{
			delta.rewardExperience = reward->experience;
			delta.rewardGold = reward->gold;
			delta.rewardItems = reward->items;
		}
		(void)SendQuestDelta(*client, delta);
		SaveConnectedClient(*client, "quest_turnin");
		LOG_INFO(Net, "[ServerApp] Quest turned in (client_id={}, quest_id={}, xp={}, gold={})",
			client->clientId, msg.questId, reward ? reward->experience : 0u, reward ? reward->gold : 0u);
	}

	void ServerApp::GrantQuestReward(ConnectedClient& client, const QuestDefinition& def)
	{
		// Bloc de versement déplacé verbatim depuis HandleTurnInQuest : XP (avec
		// montées de niveau), or (wallet) et objets (inventaire), puis réplication
		// des deltas inventaire/wallet vers le client.
		const QuestReward* reward = m_questRuntime.TakeRewardOnTurnIn(def);
		std::vector<ItemStack> rewardedItems;
		if (reward != nullptr)
		{
			ApplyLevelUpsAfterXp(client, reward->experience);
			if (reward->gold != 0u)
			{
				std::string walletErr;
				if (!m_playerWallet.AddCurrency(client, kCurrencyGold, reward->gold, walletErr))
				{
					LOG_WARN(Net, "[ServerApp] Quest gold grant blocked (client_id={}, err={})", client.clientId, walletErr);
				}
			}
			for (const ItemStack& item : reward->items)
			{
				AddItemToInventory(client, item);
				rewardedItems.push_back(item);
			}
		}

		if (!rewardedItems.empty())
		{
			(void)SendInventoryDelta(client, rewardedItems);
		}
		(void)SendWalletUpdate(client);
	}

	void ServerApp::SendQuestStateBootstrap(const ConnectedClient& receiver)
	{
		for (const QuestState& state : receiver.questStates)
		{
			QuestProgressDelta delta{};
			delta.questId = state.questId;
			delta.status = state.status;
			delta.stepProgressCounts = state.stepProgressCounts;
			(void)SendQuestDelta(receiver, delta);
		}

		LOG_INFO(Net, "[ServerApp] Quest bootstrap sent (client_id={}, quests={})",
			receiver.clientId,
			receiver.questStates.size());
	}

	void ServerApp::SendDynamicEventBootstrap(const ConnectedClient& receiver)
	{
		size_t eventCount = 0;
		for (const DynamicEventState& eventState : m_dynamicEvents)
		{
			if (eventState.definition.zoneId != receiver.zoneId || eventState.status == DynamicEventStatus::Idle)
			{
				continue;
			}

			++eventCount;
			(void)SendDynamicEventState(receiver, eventState, eventState.definition.startNotificationText, 0, 0, {});
		}

		LOG_INFO(Net, "[ServerApp] Dynamic event bootstrap sent (client_id={}, zone_id={}, events={})",
			receiver.clientId,
			receiver.zoneId,
			eventCount);
	}

	bool ServerApp::SendInventoryDelta(const ConnectedClient& receiver, std::span<const ItemStack> changedItems)
	{
		// IMPORTANT — le client applique InventoryDelta comme un SNAPSHOT COMPLET
		// (UIModelBinding::ApplyInventoryDelta fait `m_model.inventory = delta`, un
		// remplacement total). On envoie donc TOUJOURS l'inventaire complet du joueur,
		// jamais la seule liste des objets qui viennent de changer : sinon l'affichage
		// est écrasé par le dernier delta partiel. Symptôme (bug corrigé ici) : le butin
		// auto-loot de N sangliers n'affichait que le dernier kill (1 épée + 3 potions)
		// au lieu du cumul (ex. 12 + 36), alors que l'inventaire serveur était correct.
		// `changedItems` ne sert plus qu'au log (nombre d'objets modifiés).
		InventoryDeltaMessage message{};
		message.clientId = receiver.clientId;
		const std::vector<std::byte> packet = EncodeInventoryDelta(message, receiver.inventory);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] InventoryDelta send failed (client_id={}, changed={}, total={})",
				receiver.clientId,
				changedItems.size(),
				receiver.inventory.size());
			return false;
		}

		LOG_INFO(Net, "[ServerApp] InventoryDelta sent (client_id={}, changed={}, total={})",
			receiver.clientId,
			changedItems.size(),
			receiver.inventory.size());
		return true;
	}

	bool ServerApp::SendWalletUpdate(const ConnectedClient& receiver)
	{
		WalletUpdateMessage message{};
		message.clientId = receiver.clientId;
		message.gold = receiver.gold;
		message.honor = receiver.honor;
		message.badges = receiver.badges;
		message.premiumCurrency = receiver.premiumCurrency;
		const std::vector<std::byte> packet = EncodeWalletUpdate(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] WalletUpdate send failed (client_id={})", receiver.clientId);
			return false;
		}
		LOG_INFO(Net,
			"[ServerApp] WalletUpdate sent (client_id={}, gold={}, honor={}, badges={}, premium={})",
			receiver.clientId,
			message.gold,
			message.honor,
			message.badges,
			message.premiumCurrency);
		return true;
	}

	ConnectedClient* ServerApp::FindConnectedClient(uint32_t clientId)
	{
		// TG.2 — O(1) via index map (auparavant O(N) sur m_clients).
		const auto it = m_clientIndexByClientId.find(clientId);
		if (it == m_clientIndexByClientId.end() || it->second >= m_clients.size())
			return nullptr;
		return &m_clients[it->second];
	}

	bool ServerApp::TryAddCurrency(uint32_t clientId, uint8_t currencyId, uint64_t amount, std::string& outError)
	{
		ConnectedClient* client = FindConnectedClient(clientId);
		if (client == nullptr)
		{
			outError = "client_not_found";
			LOG_WARN(Net, "[ServerApp] TryAddCurrency FAILED: unknown client_id={}", clientId);
			return false;
		}
		if (!m_playerWallet.AddCurrency(*client, currencyId, amount, outError))
		{
			return false;
		}
		(void)SendWalletUpdate(*client);
		SaveConnectedClient(*client, "wallet_add");
		return true;
	}

	bool ServerApp::TrySubtractCurrency(uint32_t clientId, uint8_t currencyId, uint64_t amount, std::string& outError)
	{
		ConnectedClient* client = FindConnectedClient(clientId);
		if (client == nullptr)
		{
			outError = "client_not_found";
			LOG_WARN(Net, "[ServerApp] TrySubtractCurrency FAILED: unknown client_id={}", clientId);
			return false;
		}
		if (!m_playerWallet.SubtractCurrency(*client, currencyId, amount, outError))
		{
			return false;
		}
		(void)SendWalletUpdate(*client);
		SaveConnectedClient(*client, "wallet_subtract");
		return true;
	}

	bool ServerApp::TryTransferCurrency(
		uint32_t fromClientId,
		uint32_t toClientId,
		uint8_t currencyId,
		uint64_t amount,
		std::string& outError)
	{
		ConnectedClient* from = FindConnectedClient(fromClientId);
		ConnectedClient* to = FindConnectedClient(toClientId);
		if (from == nullptr || to == nullptr)
		{
			outError = "client_not_found";
			LOG_WARN(Net,
				"[ServerApp] TryTransferCurrency FAILED: from={} to={} (missing client)",
				fromClientId,
				toClientId);
			return false;
		}
		if (!m_playerWallet.Transfer(*from, *to, currencyId, amount, outError))
		{
			return false;
		}
		(void)SendWalletUpdate(*from);
		(void)SendWalletUpdate(*to);
		// ETAPE2.4 — débit et crédit dans la même transaction.
		SaveCharacterPair(*from, *to, "wallet_transfer");
		return true;
	}

	bool ServerApp::SendDynamicEventState(
		const ConnectedClient& receiver,
		const DynamicEventState& eventState,
		std::string_view notificationText,
		uint32_t rewardExperience,
		uint32_t rewardGold,
		std::span<const ItemStack> rewardItems)
	{
		EventStateMessage message{};
		message.zoneId = eventState.definition.zoneId;
		message.status = static_cast<uint8_t>(eventState.status);
		message.phaseIndex = static_cast<uint16_t>(eventState.status == DynamicEventStatus::Active ? (eventState.currentPhaseIndex + 1u) : 0u);
		message.phaseCount = static_cast<uint16_t>(eventState.definition.phases.size());
		message.progressCurrent = eventState.currentPhaseProgress;
		if (eventState.status == DynamicEventStatus::Active && eventState.currentPhaseIndex < eventState.definition.phases.size())
		{
			message.progressRequired = eventState.definition.phases[eventState.currentPhaseIndex].progressRequired;
		}
		message.eventId = eventState.definition.eventId;
		message.notificationText = std::string(notificationText);
		message.rewardExperience = rewardExperience;
		message.rewardGold = rewardGold;
		message.rewardItems.assign(rewardItems.begin(), rewardItems.end());

		const std::vector<std::byte> packet = EncodeEventState(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] EventState send failed (client_id={}, event_id={})",
				receiver.clientId,
				eventState.definition.eventId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] EventState sent (client_id={}, event_id={}, status={}, phase={}/{}, progress={}/{})",
			receiver.clientId,
			eventState.definition.eventId,
			GetDynamicEventStatusName(eventState.status),
			message.phaseIndex,
			message.phaseCount,
			message.progressCurrent,
			message.progressRequired);
		return true;
	}

	void ServerApp::BroadcastDynamicEventState(
		const DynamicEventState& eventState,
		std::string_view notificationText,
		uint32_t rewardedClientId,
		uint32_t rewardExperience,
		uint32_t rewardGold,
		std::span<const ItemStack> rewardItems)
	{
		size_t recipientCount = 0;
		for (const ConnectedClient& client : m_clients)
		{
			if (!client.hasReplicatedState || client.zoneId != eventState.definition.zoneId)
			{
				continue;
			}

			const bool rewardedReceiver = rewardedClientId != 0 && client.clientId == rewardedClientId;
			++recipientCount;
			(void)SendDynamicEventState(
				client,
				eventState,
				notificationText,
				rewardedReceiver ? rewardExperience : 0u,
				rewardedReceiver ? rewardGold : 0u,
				rewardedReceiver ? rewardItems : std::span<const ItemStack>{});
		}

		LOG_INFO(Net, "[ServerApp] EventState broadcast (event_id={}, zone_id={}, recipients={})",
			eventState.definition.eventId,
			eventState.definition.zoneId,
			recipientCount);
	}

	bool ServerApp::SendQuestDelta(const ConnectedClient& receiver, const QuestProgressDelta& delta)
	{
		const QuestDefinition* definition = m_questRuntime.FindQuestDefinition(delta.questId);
		if (definition == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] QuestDelta skipped: missing definition (client_id={}, quest_id={})",
				receiver.clientId,
				delta.questId);
			return false;
		}

		QuestDeltaMessage message{};
		message.clientId = receiver.clientId;
		message.status = static_cast<uint8_t>(delta.status);
		message.questId = delta.questId;
		message.rewardExperience = delta.rewardExperience;
		message.rewardGold = delta.rewardGold;
		message.rewardItems = delta.rewardItems;
		message.steps.reserve(definition->steps.size());
		for (size_t stepIndex = 0; stepIndex < definition->steps.size(); ++stepIndex)
		{
			const QuestStepDefinition& definitionStep = definition->steps[stepIndex];
			QuestDeltaStep messageStep{};
			messageStep.stepType = static_cast<uint8_t>(definitionStep.type);
			messageStep.targetId = definitionStep.targetId;
			messageStep.requiredCount = definitionStep.requiredCount;
			if (stepIndex < delta.stepProgressCounts.size())
			{
				messageStep.currentCount = delta.stepProgressCounts[stepIndex];
			}
			message.steps.push_back(std::move(messageStep));
		}

		const std::vector<std::byte> packet = EncodeQuestDelta(message);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] QuestDelta send failed (client_id={}, quest_id={})",
				receiver.clientId,
				delta.questId);
			return false;
		}

		LOG_INFO(Net, "[ServerApp] QuestDelta sent (client_id={}, quest_id={}, status={}, steps={})",
			receiver.clientId,
			delta.questId,
			GetQuestStatusName(delta.status),
			message.steps.size());
		return true;
	}
}
