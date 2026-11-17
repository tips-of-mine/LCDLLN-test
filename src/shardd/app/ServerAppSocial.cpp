// ServerApp — social : connexion / déconnexion, amis, groupe (invitation, butin, expulsion), partage d'XP.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

// -------------------------------------------------------------------------
// M32.1 — Friend system helpers
// -------------------------------------------------------------------------

	void ServerApp::OnClientLogin(ConnectedClient& client)
	{
		// Présence unifiée (Niveau 1) : autorité UNIQUE. On y inscrit le joueur (clé
		// account_id + index character_id) ; FriendSystem et le snapshot heartbeat la
		// consultent. Remplace l'ancien FriendSystem.SetPresence (map dédiée supprimée).
		// Repli : sur build sans gate d'admission (WIN32 no-DB), accountId n'est pas
		// résolu (0) — on clé alors sur le character_id pour préserver la présence amis
		// (le snapshot master n'est de toute façon pas consommé sur ces builds).
		const uint64_t presenceKey = client.accountId != 0 ? client.accountId
			: static_cast<uint64_t>(client.persistenceCharacterKey);
		m_shardPresence.SetOnline(presenceKey, client.persistenceCharacterKey,
			client.characterName, client.level, client.zoneId, PresenceStatus::Online);

		if (!m_friendSystem.IsInitialized())
			return;

		// FIX.4 — avec une base : liste lue hors du tick, puis seuls les amis connectés
		// sont prévenus (ApplyFriendDbResults). Sans base : comportement historique.
		if (m_friendSystem.IsDbAvailable())
		{
			RequestFriendListSync(static_cast<uint64_t>(client.persistenceCharacterKey), true);
		}
		else
		{
			SendFriendListSync(client, {});
			BroadcastFriendStatusUpdate(client, PresenceStatus::Online);
		}

		LOG_INFO(Net, "[ServerApp] OnClientLogin: presence set online (client_id={}, account_id={})",
			client.clientId, client.accountId);
	}

	void ServerApp::OnClientLogout(const ConnectedClient& client)
	{
		// M32.2 — Remove the leaving client from their party (promotes leader or disbands).
		if (m_partySystem.IsInitialized())
		{
			const uint32_t partyId = m_partySystem.LeaveParty(client.clientId);
			if (partyId != 0)
			{
				// Broadcast updated party state to remaining members (if party still alive).
				const Party* party = m_partySystem.FindPartyById(partyId);
				if (party != nullptr)
					BroadcastPartyUpdate(*party);
				LOG_INFO(Net, "[ServerApp] OnClientLogout: party updated on disconnect (client_id={}, party_id={})",
				    client.clientId, partyId);
			}
		}

		if (m_friendSystem.IsInitialized())
		{
			if (m_friendSystem.IsDbAvailable())
				NotifyFriendsOfPresence(client, PresenceStatus::Offline); // FIX.4
			else
				BroadcastFriendStatusUpdate(client, PresenceStatus::Offline);
		}

		// Présence unifiée : retire le joueur (même clé de repli qu'au login).
		const uint64_t presenceKey = client.accountId != 0 ? client.accountId
			: static_cast<uint64_t>(client.persistenceCharacterKey);
		m_shardPresence.SetOffline(presenceKey);

		LOG_INFO(Net, "[ServerApp] OnClientLogout: presence cleared (client_id={}, account_id={})",
			client.clientId, client.accountId);
	}

	void ServerApp::SendFriendListSync(const ConnectedClient& receiver, const std::vector<FriendRecord>& records)
	{
		// In no-DB mode the list is empty; we still send the packet so the client is aware
		// the sync occurred.

		FriendListSyncMessage msg{};
		msg.friends.reserve(records.size());
		for (const auto& rec : records)
		{
			FriendListEntry entry;
			entry.name              = rec.friendName;
			entry.presenceStatus    = m_friendSystem.GetPresence(rec.friendId);
			entry.isPendingInbound  = (rec.status == 0);
			msg.friends.push_back(std::move(entry));
		}

		const std::vector<std::byte> packet = EncodeFriendListSync(msg);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendFriendListSync send failed (client_id={})", receiver.clientId);
			return;
		}

		LOG_DEBUG(Net, "[ServerApp] FriendListSync sent (client_id={}, friends={})",
			receiver.clientId, msg.friends.size());
	}

	void ServerApp::BroadcastFriendStatusUpdate(const ConnectedClient& subject, PresenceStatus status)
	{
		// Collect online friend ids from the in-memory presence map.
		// In no-DB mode GetOnlineFriendIds returns empty; we iterate all clients and check if
		// they have the subject in their (in-memory-only) presence map instead.
		// This is best-effort: bilateral DB relationships are not available in no-DB mode.
		const std::string subjectLabel = "P" + std::to_string(subject.clientId);

		FriendStatusUpdateMessage msg{};
		msg.friendName     = subjectLabel;
		msg.presenceStatus = status;

		const std::vector<std::byte> packet = EncodeFriendStatusUpdate(msg);

		for (const ConnectedClient& peer : m_clients)
		{
			if (peer.clientId == subject.clientId)
				continue;

			if (!SendDatagram(peer.endpoint, packet))
			{
				LOG_WARN(Net, "[ServerApp] BroadcastFriendStatusUpdate send failed (peer_client_id={})", peer.clientId);
			}
		}
	}

	bool ServerApp::HandleFriendCommand(ConnectedClient& sender, std::string_view argsRemainder)
	{
		std::string targetName;
		const FriendSubCommand sub = ParseFriendSubCommand(argsRemainder, targetName);

		if (sub == FriendSubCommand::Unknown || targetName.empty())
		{
			SendChatSystemNotice(sender, "Usage: /friend add|accept|decline|remove <name>");
			LOG_WARN(Net, "[ServerApp] /friend unknown sub-command (client_id={}, args='{}')",
				sender.clientId, argsRemainder);
			return true;
		}

		HandleFriendAction(sender, sub, targetName, true);
		return true;
	}

	void ServerApp::HandleFriendAction(ConnectedClient& sender, FriendSubCommand sub, const std::string& targetName, bool notify)
	{
		const uint64_t playerId = static_cast<uint64_t>(sender.persistenceCharacterKey);
		const std::string playerLabel = sender.characterName.empty()
			? "P" + std::to_string(sender.clientId) : sender.characterName;

		if (!m_friendSystem.IsDbAvailable())
		{
			// Sans base (build Windows, shard sans pool) : comportement historique, les
			// opérations échouent immédiatement en « no-DB mode ».
			std::string notice;
			switch (sub)
			{
			case FriendSubCommand::Add:
				notice = m_friendSystem.SendFriendRequest(playerId, playerLabel, targetName, nullptr) != 0
					? "Friend request sent to '" + targetName + "'." : "Friend request failed (no-DB mode or target not found).";
				break;
			case FriendSubCommand::Accept:
				notice = m_friendSystem.AcceptFriendRequest(playerId, targetName, nullptr) != 0
					? "Friend request from '" + targetName + "' accepted." : "Accept failed (no-DB mode or request not found).";
				break;
			case FriendSubCommand::Decline:
				notice = m_friendSystem.DeclineFriendRequest(playerId, targetName, nullptr) != 0
					? "Friend request from '" + targetName + "' declined." : "Decline failed (no-DB mode or request not found).";
				break;
			case FriendSubCommand::Remove:
				notice = m_friendSystem.RemoveFriend(playerId, targetName, nullptr)
					? "'" + targetName + "' removed from friends." : "Remove failed (no-DB mode or friend not found).";
				break;
			default:
				return;
			}
			if (notify)
				SendChatSystemNotice(sender, notice);
			return;
		}

		// Avec une base : tout le SQL part hors du tick, à l'ordre des écritures du joueur.
		EnqueueFriendDbJob(playerId, [this, playerId, playerLabel, sub, targetName, notify](MYSQL* mysql, FriendDbResult& r) {
			uint64_t other = 0;
			bool ok = false;
			switch (sub)
			{
			case FriendSubCommand::Add:
				other = m_friendSystem.SendFriendRequest(playerId, playerLabel, targetName, mysql);
				ok = other != 0;
				r.notice = ok ? "Friend request sent to '" + targetName + "'."
					: "Friend request failed (unknown name, already requested, limit reached or too many requests).";
				if (ok)
					r.otherNotice = "'" + playerLabel + "' wants to be your friend (/friend accept " + playerLabel + ").";
				break;
			case FriendSubCommand::Accept:
				other = m_friendSystem.AcceptFriendRequest(playerId, targetName, mysql);
				ok = other != 0;
				r.notice = ok ? "Friend request from '" + targetName + "' accepted." : "Accept failed (request not found).";
				if (ok)
					r.otherNotice = "'" + playerLabel + "' accepted your friend request.";
				break;
			case FriendSubCommand::Decline:
				other = m_friendSystem.DeclineFriendRequest(playerId, targetName, mysql);
				ok = other != 0;
				r.notice = ok ? "Friend request from '" + targetName + "' declined." : "Decline failed (request not found).";
				break;
			case FriendSubCommand::Remove:
				other = m_friendSystem.ResolveCharacterId(targetName, mysql);
				ok = other != 0 && m_friendSystem.RemoveFriend(playerId, targetName, mysql);
				r.notice = ok ? "'" + targetName + "' removed from friends." : "Remove failed (friend not found).";
				break;
			default:
				break;
			}
			if (!notify)
				r.notice.clear();
			// Les deux listes concernées sont relues (si les joueurs sont connectés).
			r.refreshKeys.push_back(playerId);
			if (ok && other != 0)
			{
				r.otherKey = other;
				r.refreshKeys.push_back(other);
			}
		});
	}

	void ServerApp::EnqueueFriendDbJob(uint64_t characterKey, std::function<void(MYSQL*, FriendDbResult&)> work)
	{
#if defined(SHARD_POSITION_DB)
		if (m_characterDbPool == nullptr)
			return;
		auto job = [this, characterKey, work = std::move(work)]() {
			FriendDbResult result;
			result.characterKey = characterKey;
			{
				auto guard = m_characterDbPool->Acquire();
				MYSQL* mysql = guard.get();
				if (mysql == nullptr)
				{
					LOG_WARN(Net, "[ServerApp] Amis : aucune connexion en base disponible (character_key={})", characterKey);
					result.notice = "Friends are temporarily unavailable.";
				}
				else
				{
					work(mysql, result);
				}
			}
			std::lock_guard lock(m_friendDbMutex);
			m_friendDbResults.push_back(std::move(result));
		};
		if (m_characterSaveQueue == nullptr
			|| m_characterSaveQueue->EnqueueOrdered(characterKey, job) == CharacterSaveQueue::EnqueueResult::Rejected)
			job(); // file pleine ou arrêtée : exécution sur place (rare)
#else
		(void)characterKey;
		(void)work;
#endif
	}

	void ServerApp::RequestFriendListSync(uint64_t characterKey, bool announceOnline)
	{
		if (!m_friendSystem.IsDbAvailable())
		{
			if (ConnectedClient* client = FindConnectedClientByCharacterKey(characterKey))
				SendFriendListSync(*client, {});
			return;
		}
		EnqueueFriendDbJob(characterKey, [this, characterKey, announceOnline](MYSQL* mysql, FriendDbResult& r) {
			r.records = m_friendSystem.GetFriendList(characterKey, mysql);
			r.acceptedIds = m_friendSystem.GetAcceptedFriendIds(characterKey, mysql);
			r.listLoaded = true;
			r.announceOnline = announceOnline;
		});
	}

	void ServerApp::ApplyFriendDbResults()
	{
		std::vector<FriendDbResult> results;
		{
			std::lock_guard lock(m_friendDbMutex);
			if (m_friendDbResults.empty())
				return;
			results.swap(m_friendDbResults);
		}
		for (FriendDbResult& r : results)
		{
			ConnectedClient* client = FindConnectedClientByCharacterKey(r.characterKey);
			if (client != nullptr && !r.notice.empty())
				SendChatSystemNotice(*client, r.notice);
			if (r.otherKey != 0 && !r.otherNotice.empty())
			{
				if (ConnectedClient* other = FindConnectedClientByCharacterKey(r.otherKey))
					SendChatSystemNotice(*other, r.otherNotice);
			}
			if (r.listLoaded && client != nullptr)
			{
				client->friendCharacterKeys = r.acceptedIds;
				SendFriendListSync(*client, r.records);
				if (r.announceOnline)
					NotifyFriendsOfPresence(*client, PresenceStatus::Online);
			}
			for (uint64_t key : r.refreshKeys)
			{
				// Joueur parti entre-temps : sa liste sera relue à sa prochaine connexion.
				if (FindConnectedClientByCharacterKey(key) != nullptr)
					RequestFriendListSync(key, false);
			}
		}
	}

	void ServerApp::NotifyFriendsOfPresence(const ConnectedClient& subject, PresenceStatus status)
	{
		FriendStatusUpdateMessage msg{};
		msg.friendName = subject.characterName.empty() ? "P" + std::to_string(subject.clientId) : subject.characterName;
		msg.presenceStatus = status;
		const std::vector<std::byte> packet = EncodeFriendStatusUpdate(msg);
		for (uint64_t friendKey : subject.friendCharacterKeys)
		{
			ConnectedClient* peer = FindConnectedClientByCharacterKey(friendKey);
			if (peer == nullptr || peer->clientId == subject.clientId)
				continue;
			if (!SendDatagram(peer->endpoint, packet))
				LOG_WARN(Net, "[ServerApp] NotifyFriendsOfPresence send failed (peer_client_id={})", peer->clientId);
		}
	}

	// =========================================================================
	// M32.2 — Party system helpers
	// =========================================================================

	bool ServerApp::HandleInviteCommand(ConnectedClient& sender, std::string_view argsRemainder)
	{
		if (!m_partySystem.IsInitialized())
		{
			SendChatSystemNotice(sender, "Party system unavailable.");
			return true;
		}

		std::string targetName;
		if (!ParsePartyTargetName(argsRemainder, targetName))
		{
			SendChatSystemNotice(sender, "Usage: /invite <player_name>");
			return true;
		}

		// Resolve target by display-name token (format "P<clientId>").
		ConnectedClient* target = FindConnectedClientByChatDisplayName(targetName);
		if (target == nullptr)
		{
			SendChatSystemNotice(sender, "Player '" + targetName + "' not found.");
			LOG_WARN(Net, "[ServerApp] /invite target not found (client_id={}, target='{}')",
			    sender.clientId, targetName);
			return true;
		}

		if (target->clientId == sender.clientId)
		{
			SendChatSystemNotice(sender, "You cannot invite yourself.");
			return true;
		}

		const std::string senderLabel = "P" + std::to_string(sender.clientId);
		if (!m_partySystem.SendInvite(sender.clientId, target->clientId, targetName, m_currentTick))
		{
			SendChatSystemNotice(sender, "Cannot invite '" + targetName + "' (already in party or invite pending).");
			return true;
		}

		// Notify the target.
		PartyInviteNotifyMessage notify{};
		notify.inviterName = senderLabel;
		const std::vector<std::byte> notifyPacket = EncodePartyInviteNotify(notify);
		if (!SendDatagram(target->endpoint, notifyPacket))
		{
			LOG_WARN(Net, "[ServerApp] PartyInviteNotify send failed (target_client_id={})", target->clientId);
		}

		SendChatSystemNotice(sender, "Party invite sent to '" + targetName + "'.");
		SendChatSystemNotice(*target, "'" + senderLabel + "' has invited you to a party. Type /accept to join.");
		LOG_INFO(Net, "[ServerApp] /invite sent (inviter_client_id={}, invitee_client_id={})",
		    sender.clientId, target->clientId);
		return true;
	}

	bool ServerApp::HandleLeaveCommand(ConnectedClient& sender)
	{
		if (!m_partySystem.IsInitialized())
		{
			SendChatSystemNotice(sender, "Party system unavailable.");
			return true;
		}

		const uint32_t partyId = m_partySystem.LeaveParty(sender.clientId);
		if (partyId == 0)
		{
			SendChatSystemNotice(sender, "You are not in a party.");
			return true;
		}

		SendChatSystemNotice(sender, "You have left the party.");

		// Broadcast updated state to remaining members.
		const Party* party = m_partySystem.FindPartyById(partyId);
		if (party != nullptr)
		{
			BroadcastPartyUpdate(*party);
			// Notify remaining members.
			const std::string senderLabel = "P" + std::to_string(sender.clientId);
			for (const PartyMember& m : party->members)
			{
				ConnectedClient* member = FindClientByEntityId(m.entityId);
				if (member != nullptr)
					SendChatSystemNotice(*member, "'" + senderLabel + "' has left the party.");
			}
		}

		LOG_INFO(Net, "[ServerApp] /leave handled (client_id={}, party_id={})", sender.clientId, partyId);
		return true;
	}

	bool ServerApp::HandleLootCommand(ConnectedClient& sender, std::string_view argsRemainder)
	{
		if (!m_partySystem.IsInitialized())
		{
			SendChatSystemNotice(sender, "Party system unavailable.");
			return true;
		}

		const Party* party = m_partySystem.FindPartyByMember(sender.clientId);
		if (party == nullptr)
		{
			SendChatSystemNotice(sender, "You are not in a party.");
			return true;
		}

		if (!m_partySystem.IsPartyLeader(sender.clientId))
		{
			SendChatSystemNotice(sender, "Only the party leader can change the loot mode.");
			return true;
		}

		std::string token;
		if (!ParseLootCommandToken(argsRemainder, token))
		{
			SendChatSystemNotice(sender, "Usage: /loot <ffa|roundrobin|master|needgreed>");
			return true;
		}

		LootMode mode = LootMode::FreeForAll;
		if (!ParseLootModeToken(token, mode))
		{
			SendChatSystemNotice(sender, "Unknown loot mode '" + token + "'. Use: ffa, roundrobin, master, needgreed.");
			return true;
		}

		if (!m_partySystem.SetLootMode(party->partyId, sender.clientId, mode))
		{
			SendChatSystemNotice(sender, "Failed to change loot mode.");
			return true;
		}

		// Refresh party pointer after modification.
		const Party* updatedParty = m_partySystem.FindPartyById(party->partyId);
		if (updatedParty != nullptr)
		{
			BroadcastPartyUpdate(*updatedParty);
			const std::string notice = std::string("Loot mode changed to: ") + LootModeLabel(mode);
			for (const PartyMember& m : updatedParty->members)
			{
				ConnectedClient* member = FindClientByEntityId(m.entityId);
				if (member != nullptr)
					SendChatSystemNotice(*member, notice);
			}
		}

		LOG_INFO(Net, "[ServerApp] /loot handled (client_id={}, mode={})", sender.clientId, LootModeLabel(mode));
		return true;
	}

	bool ServerApp::HandlePartyKickCommand(ConnectedClient& sender, std::string_view argsRemainder)
	{
		if (!m_partySystem.IsInitialized())
		{
			SendChatSystemNotice(sender, "Party system unavailable.");
			return true;
		}

		const Party* party = m_partySystem.FindPartyByMember(sender.clientId);
		if (party == nullptr)
		{
			SendChatSystemNotice(sender, "You are not in a party.");
			return true;
		}

		if (!m_partySystem.IsPartyLeader(sender.clientId))
		{
			SendChatSystemNotice(sender, "Only the party leader can kick members.");
			return true;
		}

		std::string targetName;
		if (!ParsePartyTargetName(argsRemainder, targetName))
		{
			SendChatSystemNotice(sender, "Usage: /pkick <player_name>");
			return true;
		}

		ConnectedClient* target = FindConnectedClientByChatDisplayName(targetName);
		if (target == nullptr)
		{
			SendChatSystemNotice(sender, "Player '" + targetName + "' not found.");
			return true;
		}

		const uint32_t savedPartyId = party->partyId;
		if (!m_partySystem.KickMember(savedPartyId, sender.clientId, target->clientId))
		{
			SendChatSystemNotice(sender, "Could not kick '" + targetName + "' (not in party or not leader).");
			return true;
		}

		const std::string senderLabel = "P" + std::to_string(sender.clientId);
		SendChatSystemNotice(*target, "You have been kicked from the party by '" + senderLabel + "'.");
		SendChatSystemNotice(sender, "'" + targetName + "' has been kicked from the party.");

		// Broadcast updated party state to remaining members.
		const Party* updatedParty = m_partySystem.FindPartyById(savedPartyId);
		if (updatedParty != nullptr)
			BroadcastPartyUpdate(*updatedParty);

		LOG_INFO(Net, "[ServerApp] /pkick handled (leader_client_id={}, kicked_client_id={})",
		    sender.clientId, target->clientId);
		return true;
	}

	void ServerApp::BroadcastPartyUpdate(const Party& party)
	{
		const std::vector<std::byte> packet = [&]
		{
			PartyUpdateMessage msg{};
			msg.partyId  = party.partyId;
			msg.leaderId = party.leaderId;
			msg.lootMode = static_cast<WireLootMode>(party.lootMode);
			msg.members.reserve(party.members.size());
			for (const PartyMember& m : party.members)
			{
				PartyMemberEntry entry{};
				entry.clientId      = m.clientId;
				entry.currentHealth = m.currentHealth;
				entry.maxHealth     = m.maxHealth;
				entry.currentMana   = m.currentMana;
				entry.maxMana       = m.maxMana;
				entry.displayName   = m.displayName;
				msg.members.push_back(std::move(entry));
			}
			return EncodePartyUpdate(msg);
		}();

		for (const PartyMember& m : party.members)
		{
			const ConnectedClient* member = FindClientByEntityId(m.entityId);
			if (member == nullptr)
				continue;
			if (!SendDatagram(member->endpoint, packet))
			{
				LOG_WARN(Net, "[ServerApp] BroadcastPartyUpdate send failed (client_id={})", m.clientId);
			}
		}

		LOG_DEBUG(Net, "[ServerApp] PartyUpdate broadcast (party_id={}, members={})",
		    party.partyId, party.members.size());
	}

	bool ServerApp::SendPartyUpdate(const ConnectedClient& receiver, const Party& party)
	{
		PartyUpdateMessage msg{};
		msg.partyId  = party.partyId;
		msg.leaderId = party.leaderId;
		msg.lootMode = static_cast<WireLootMode>(party.lootMode);
		msg.members.reserve(party.members.size());
		for (const PartyMember& m : party.members)
		{
			PartyMemberEntry entry{};
			entry.clientId      = m.clientId;
			entry.currentHealth = m.currentHealth;
			entry.maxHealth     = m.maxHealth;
			entry.currentMana   = m.currentMana;
			entry.maxMana       = m.maxMana;
			entry.displayName   = m.displayName;
			msg.members.push_back(std::move(entry));
		}
		const std::vector<std::byte> packet = EncodePartyUpdate(msg);
		if (!SendDatagram(receiver.endpoint, packet))
		{
			LOG_WARN(Net, "[ServerApp] SendPartyUpdate send failed (client_id={})", receiver.clientId);
			return false;
		}
		return true;
	}

	void ServerApp::ApplyLevelUpsAfterXp(ConnectedClient& client, uint32_t gainedXp)
	{
		if (!m_statsTables)
		{
			client.experiencePoints += gainedXp;   // fallback : comportement legacy (XP brute).
			return;
		}
		const auto r = engine::server::gameplay::ApplyXpGain(
			client.level, client.experiencePoints, gainedXp,
			m_statsTables->xpBase, m_statsTables->xpFactor, m_statsTables->levelMax);
		client.level = r.newLevel;
		client.experiencePoints = r.newXpIntoLevel;
		// PR-C — pousse la barre d'XP à CHAQUE gain (même sans level-up : sinon la
		// progression intra-niveau n'atteindrait jamais le client).
		(void)SendPlayerXpUpdate(client);
		if (r.levelsGained == 0u)
			return;
		// Recalcul des stats au nouveau niveau + soin complet.
		const engine::server::gameplay::Sex sex =
			(client.gender == "female") ? engine::server::gameplay::Sex::Female
			                            : engine::server::gameplay::Sex::Male;
		const auto d = engine::server::gameplay::ComputeStats(
			*m_statsTables, client.factionId, client.classId, sex, client.level);
		if (d)
		{
			client.stats.maxHealth = d->hp;
			client.stats.currentHealth = d->hp;   // soin complet au level-up.
			// Combat SP2 — les stats offensives suivent aussi le niveau.
			ApplyDerivedCombatStats(client, *d);
			// Combat SP3 — la ressource max suit le niveau (refill complet).
			client.maxResource = d->resource;
			client.currentResource = client.maxResource;
		}
		(void)SendPlayerStats(client);
		SaveConnectedClient(client, "level_up");
		LOG_INFO(Net, "[ServerApp] LEVEL UP (client_id={}, +{} niveaux -> level={}, maxHp={})",
			client.clientId, r.levelsGained, client.level, client.stats.maxHealth);
	}

	void ServerApp::DistributePartyXp(ConnectedClient& attacker,
	                                   float            killPosX,
	                                   float            killPosZ,
	                                   uint32_t         baseXp)
	{
		const Party* party = m_partySystem.FindPartyByMember(attacker.clientId);
		if (party == nullptr || party->members.size() <= 1)
		{
			// Solo player: grant full XP.
			ApplyLevelUpsAfterXp(attacker, baseXp);
			LOG_DEBUG(Net, "[ServerApp] XP granted solo (client_id={}, xp={}, total_xp={})",
			    attacker.clientId, baseXp, attacker.experiencePoints);
			return;
		}

		// Collect members within range.
		const std::vector<uint32_t> inRange = m_partySystem.GetMembersInRange(
		    party->partyId,
		    attacker.clientId,
		    killPosX,
		    killPosZ,
		    kPartyShareRangeMeters,
		    [this, &attacker](uint32_t clientId, float& outX, float& outZ) -> bool
		    {
			    for (const ConnectedClient& c : m_clients)
			    {
				    // Seconde carte : un membre d'une autre carte n'est jamais « à portée ».
				    if (c.clientId == clientId && engine::world::cells::SameMap(c.zoneId, attacker.zoneId))
				    {
					    outX = c.positionMetersX;
					    outZ = c.positionMetersZ;
					    return true;
				    }
			    }
			    return false;
		    });

		if (inRange.empty())
		{
			ApplyLevelUpsAfterXp(attacker, baseXp);
			return;
		}

		// Split XP evenly; always at least 1 XP per member.
		const uint32_t share = std::max(1u, baseXp / static_cast<uint32_t>(inRange.size()));

		for (const uint32_t memberId : inRange)
		{
			for (ConnectedClient& c : m_clients)
			{
				if (c.clientId == memberId)
				{
					ApplyLevelUpsAfterXp(c, share);
					LOG_DEBUG(Net, "[ServerApp] Party XP share granted (client_id={}, share={}, total_xp={})",
					    c.clientId, share, c.experiencePoints);
					break;
				}
			}
		}
	}

	EntityId ServerApp::ResolvePartyLooterEntityId(const ConnectedClient& killerClient)
	{
		const Party* party = m_partySystem.FindPartyByMember(killerClient.clientId);
		if (party == nullptr || party->members.size() <= 1)
			return 0; // Solo or not in party — caller uses killer's entityId.

		switch (party->lootMode)
		{
		case LootMode::FreeForAll:
			return 0; // Public loot bag — anyone can pick up.

		case LootMode::RoundRobin:
		{
			// GetNextLooterEntityId advances the round-robin index.
			Party* mutableParty = m_partySystem.FindPartyById(party->partyId);
			if (mutableParty == nullptr)
				return killerClient.entityId;
			const EntityId looter = m_partySystem.GetNextLooterEntityId(mutableParty->partyId);
			return (looter != 0) ? looter : killerClient.entityId;
		}
		case LootMode::MasterLooter:
		{
			Party* mutableParty = m_partySystem.FindPartyById(party->partyId);
			if (mutableParty == nullptr)
				return killerClient.entityId;
			const EntityId looter = m_partySystem.GetNextLooterEntityId(mutableParty->partyId);
			return (looter != 0) ? looter : killerClient.entityId;
		}
		case LootMode::NeedGreed:
		{
			// Auto-roll for all members in range; highest roll wins.
			const std::vector<uint32_t> inRange = m_partySystem.GetMembersInRange(
			    party->partyId,
			    killerClient.clientId,
			    killerClient.positionMetersX,
			    killerClient.positionMetersZ,
			    kPartyShareRangeMeters,
			    [this, &killerClient](uint32_t clientId, float& outX, float& outZ) -> bool
			    {
				    for (const ConnectedClient& c : m_clients)
				    {
					    // Seconde carte : un membre d'une autre carte n'est jamais « à portée ».
					    if (c.clientId == clientId && engine::world::cells::SameMap(c.zoneId, killerClient.zoneId))
					    {
						    outX = c.positionMetersX;
						    outZ = c.positionMetersZ;
						    return true;
					    }
				    }
				    return false;
			    });

			if (inRange.empty())
				return killerClient.entityId;

			// Deterministic roll using clientId + current tick as seed.
			uint32_t bestRoll   = 0;
			uint32_t winnerClientId = killerClient.clientId;
			for (const uint32_t memberId : inRange)
			{
				// Simple LCG hash: reproducible per tick, varies per member.
				const uint32_t roll = ((memberId * 1664525u) + m_currentTick * 1013904223u) % 100u + 1u;
				if (roll > bestRoll)
				{
					bestRoll = roll;
					winnerClientId = memberId;
				}
			}

			for (const ConnectedClient& c : m_clients)
			{
				if (c.clientId == winnerClientId)
				{
					LOG_INFO(Net, "[ServerApp] NeedGreed roll winner (client_id={}, roll={}, party_id={})",
					    winnerClientId, bestRoll, party->partyId);
					return c.entityId;
				}
			}
			return killerClient.entityId;
		}
		default:
			return 0;
		}
	}
}
