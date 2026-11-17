// ServerApp — chat : dialogue PNJ, envoi du chat, modération (sourdine, bannissement, signalements) et commandes slash.
//
// Fonctions membres de ServerApp déplacées telles quelles depuis ServerApp.cpp
// (aucun changement de comportement). La documentation de chaque méthode est
// dans ServerApp.h ; les constantes et utilitaires communs sont dans
// ServerAppInternal.h.

#include "src/shardd/app/ServerAppInternal.h"

namespace engine::server
{
	using namespace engine::server::sa_detail;

	void ServerApp::HandleTalkRequest(const Endpoint& endpoint, uint32_t clientId, std::string_view targetId)
	{
		ConnectedClient* client = FindClient(endpoint);
		if (client == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] TalkRequest ignored from unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (client->clientId != clientId)
		{
			LOG_WARN(Net, "[ServerApp] TalkRequest ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				client->clientId,
				clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (targetId.empty())
		{
			LOG_WARN(Net, "[ServerApp] TalkRequest ignored: empty target (client_id={})", client->clientId);
			return;
		}

		uint32_t vendorId = 0;
		if (TryParseVendorTarget(targetId, vendorId))
		{
			if (m_vendorCatalog.FindVendor(vendorId) != nullptr)
			{
				LOG_INFO(Net, "[ServerApp] TalkRequest routes to vendor shop (client_id={}, vendor_id={})", client->clientId, vendorId);
				(void)SendShopOpen(*client, vendorId);
				return;
			}

			LOG_WARN(Net, "[ServerApp] TalkRequest vendor not in catalog (client_id={}, vendor_id={})", client->clientId, vendorId);
			SendChatSystemNotice(*client, "Vendor not available.");
			return;
		}

		if (targetId == "auction" || targetId == "ah")
		{
			AuctionBrowseRequestMessage q{};
			q.clientId = client->clientId;
			q.minPrice = 0;
			q.maxPrice = 0;
			q.itemIdFilter = 0;
			q.sortMode = 0;
			q.maxRows = kMaxAuctionBrowseRowsWire;
			LOG_INFO(Net, "[ServerApp] TalkRequest routes to auction house (client_id={})", client->clientId);
			(void)SendAuctionBrowseResult(*client, q);
			return;
		}

		LOG_INFO(Net, "[ServerApp] TalkRequest accepted (client_id={}, target={})", client->clientId, targetId);
		// EXT-2 — rejoue les resets temporels AVANT de construire la giver-list :
		// une quête répétable/quotidienne dont la borne est franchie redevient
		// Offered (re-proposée par le PNJ) sans relog. On resync juste après (le
		// Locked issu du reset est repromu Offered) et on propage les deltas au
		// client pour rafraîchir son journal.
		{
			std::vector<QuestProgressDelta> talkResetDeltas;
			(void)m_questRuntime.ApplyRepeatResets(client->questStates, NowUnixEpochMsUtc(), talkResetDeltas);
			if (m_questRuntime.SyncQuestStates(client->questStates, talkResetDeltas))
			{
				for (const QuestProgressDelta& delta : talkResetDeltas)
				{
					(void)SendQuestDelta(*client, delta);
				}
			}
		}
		SendQuestGiverList(*client, targetId);
		ApplyQuestEvent(*client, QuestStepType::Talk, targetId, 1, "talk");
	}

	void ServerApp::HandleChatSend(const Endpoint& endpoint, const ChatSendRequestMessage& request)
	{
		ConnectedClient* sender = FindClient(endpoint);
		if (sender == nullptr)
		{
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: unknown endpoint {}",
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (sender->clientId != request.clientId)
		{
			LOG_WARN(Net,
				"[ServerApp] ChatSend ignored: client_id mismatch (expected={}, got={}, endpoint={})",
				sender->clientId,
				request.clientId,
				UdpTransport::EndpointToString(endpoint));
			return;
		}

		if (request.text.empty())
		{
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: empty text (client_id={})", sender->clientId);
			return;
		}

		if (!m_chatRateLimiter.Allow(sender->clientId, std::chrono::steady_clock::now()))
		{
			LOG_WARN(Net, "[ServerApp] ChatSend dropped: rate limited (client_id={})", sender->clientId);
			return;
		}

		if (sender->chatMutedUntilServerTick != 0 && m_currentTick < sender->chatMutedUntilServerTick)
		{
			LOG_WARN(Net,
				"[ServerApp] ChatSend blocked: sender muted (client_id={}, until_tick={}, current_tick={})",
				sender->clientId,
				sender->chatMutedUntilServerTick,
				m_currentTick);
			SendChatSystemNotice(*sender, "You are muted.");
			return;
		}

		ParsedChatSlashCommand slashCommand{};
		if (TryParseChatSlashCommand(request.text, slashCommand))
		{
			(void)HandleChatSlashCommand(*sender, slashCommand);
			return;
		}

		engine::net::ChatChannel logicalChannel{};
		if (!engine::net::TryDecodeChannelWire(request.channel, logicalChannel))
		{
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: invalid channel wire ({})", request.channel);
			return;
		}

		ChatRelayMessage relay{};
		relay.channel = request.channel;
		relay.senderEntityId = sender->entityId;
		relay.timestampUnixMs = NowUnixEpochMsUtc();
		// ETAPE5.1 — nom du personnage (comme le master), repli "P<id>" en mode no-DB
		// sans nom d'admission. Coupé à 48 octets sans casser un caractère UTF-8.
		relay.senderDisplay = !sender->characterName.empty()
			? sender->characterName
			: "P" + std::to_string(sender->clientId);
		if (relay.senderDisplay.size() > 48)
		{
			size_t cut = 48;
			while (cut > 0 && (static_cast<unsigned char>(relay.senderDisplay[cut]) & 0xC0u) == 0x80u)
			{
				--cut;
			}
			relay.senderDisplay.resize(cut);
		}

		// ETAPE5.1 — même nettoyage que le master (troncature UTF-8, caractères
		// invisibles, liens non autorisés) : le chat de proximité passe désormais
		// par le shard. Un message rejeté est abandonné en silence (comme au master).
		engine::server::chat::ChatSanitizerConfig sanitizerCfg{};
		sanitizerCfg.maxMessageBytes = 255;
		const engine::server::chat::ChatSanitizeResult sanitized =
			engine::server::chat::Sanitize(request.text, sanitizerCfg);
		if (!sanitized.accepted)
		{
			LOG_INFO(Net, "[ServerApp] ChatSend rejected by sanitizer (client_id={}, reason={})",
				sender->clientId, sanitized.rejectReason);
			return;
		}
		relay.text = sanitized.text;

		std::vector<ConnectedClient*> recipients;
		const auto addUnique = [&recipients](ConnectedClient* clientPtr)
		{
			if (clientPtr == nullptr)
			{
				return;
			}

			if (std::find(recipients.begin(), recipients.end(), clientPtr) == recipients.end())
			{
				recipients.push_back(clientPtr);
			}
		};

		const auto addAllInZone = [this, &addUnique](uint32_t zoneId)
		{
			for (ConnectedClient& peer : m_clients)
			{
				if (peer.zoneId == zoneId)
				{
					addUnique(&peer);
				}
			}
		};
		// Monde « Royaumes » : toute la carte (toutes les régions de 10 km).
		const auto addAllOnMap = [this, &addUnique](uint32_t zoneId)
		{
			for (ConnectedClient& peer : m_clients)
			{
				if (engine::world::cells::SameMap(peer.zoneId, zoneId))
				{
					addUnique(&peer);
				}
			}
		};

		switch (logicalChannel)
		{
		case engine::net::ChatChannel::Say:
		{
			const float radius = engine::net::kChatSayRadiusMeters;
			const float radiusSq = radius * radius;
			for (ConnectedClient& peer : m_clients)
			{
				// Monde « Royaumes » : la portée (dire/crier) traverse les frontières de région.
				if (!engine::world::cells::SameMap(peer.zoneId, sender->zoneId))
				{
					continue;
				}

				if (DistanceSquaredXZ(
						sender->positionMetersX,
						sender->positionMetersZ,
						peer.positionMetersX,
						peer.positionMetersZ) <= radiusSq)
				{
					addUnique(&peer);
				}
			}

			break;
		}
		case engine::net::ChatChannel::Yell:
		{
			const float radius = engine::net::kChatYellRadiusMeters;
			const float radiusSq = radius * radius;
			for (ConnectedClient& peer : m_clients)
			{
				// Monde « Royaumes » : la portée (dire/crier) traverse les frontières de région.
				if (!engine::world::cells::SameMap(peer.zoneId, sender->zoneId))
				{
					continue;
				}

				if (DistanceSquaredXZ(
						sender->positionMetersX,
						sender->positionMetersZ,
						peer.positionMetersX,
						peer.positionMetersZ) <= radiusSq)
				{
					addUnique(&peer);
				}
			}

			break;
		}
		case engine::net::ChatChannel::Whisper:
		{
			ConnectedClient* target = FindClientByEntityId(request.whisperTargetEntityId);
			if (target == nullptr)
			{
				LOG_WARN(Net,
					"[ServerApp] ChatSend whisper FAILED: target not connected (client_id={}, target_entity_id={})",
					sender->clientId,
					request.whisperTargetEntityId);
				return;
			}

			addUnique(sender);
			addUnique(target);
			break;
		}
		case engine::net::ChatChannel::Party:
		case engine::net::ChatChannel::Guild:
			LOG_INFO(Net,
				"[ServerApp] ChatSend party/guild routing stub -> map broadcast (client_id={}, channel_wire={})",
				sender->clientId,
				request.channel);
			// Une région de 10 km ne doit pas couper un groupe : diffusion à la carte.
			addAllOnMap(sender->zoneId);
			break;
		case engine::net::ChatChannel::Zone:
			addAllInZone(sender->zoneId);
			break;
		case engine::net::ChatChannel::Global:
			for (ConnectedClient& peer : m_clients)
			{
				addUnique(&peer);
			}

			break;
		default:
			LOG_WARN(Net, "[ServerApp] ChatSend ignored: unhandled channel enum (client_id={})", sender->clientId);
			return;
		}

		if (recipients.empty())
		{
			LOG_WARN(Net,
				"[ServerApp] ChatSend produced zero recipients (client_id={}, channel_wire={})",
				sender->clientId,
				request.channel);
			return;
		}

		size_t sentOk = 0;
		for (ConnectedClient* receiver : recipients)
		{
			if (receiver->clientId != sender->clientId && IsChatSenderIgnoredBy(*receiver, relay.senderDisplay))
			{
				LOG_DEBUG(Net,
					"[ServerApp] ChatRelay skipped for ignored sender (receiver_client_id={}, sender_display={})",
					receiver->clientId,
					relay.senderDisplay);
				continue;
			}

			if (SendChatRelay(*receiver, relay))
			{
				++sentOk;
			}
		}

		LOG_INFO(Net,
			"[ServerApp] ChatSend routed (client_id={}, channel_wire={}, recipients={}, sent_ok={})",
			sender->clientId,
			request.channel,
			recipients.size(),
			sentOk);
	}

	namespace
	{
		/// Trim ASCII spaces for chat command argument splitting.
		std::string_view TrimChatArg(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			{
				text.remove_prefix(1);
			}

			while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
			{
				text.remove_suffix(1);
			}

			return text;
		}

		/// Split `first rest` from one argument line.
		std::pair<std::string, std::string> SplitFirstChatArg(std::string_view line)
		{
			line = TrimChatArg(line);
			if (line.empty())
			{
				return {{}, {}};
			}

			const size_t spacePos = line.find(' ');
			if (spacePos == std::string_view::npos)
			{
				return {std::string(line), {}};
			}

			return {
				std::string(TrimChatArg(line.substr(0, spacePos))),
				std::string(TrimChatArg(line.substr(spacePos + 1)))};
		}
	}

	void ServerApp::InitModerationAuditSubsystem()
	{
		// FIX.4 — un journal est une écriture : il ne doit PAS être résolu sous la racine de
		// contenu (game/data), montée en lecture seule en Docker (« Read-only file system »
		// au démarrage du shard, journal de modération désactivé). Un chemin relatif est
		// désormais pris depuis le répertoire de travail, comme les autres journaux
		// (`logs/` = /app/logs, volume ./data/logs/shard). Le dossier est créé si besoin.
		const std::string configuredLog = m_config.GetString("server.moderation_audit_log", "logs/moderation_audit.log");
		std::filesystem::path absolutePath(configuredLog);
		if (absolutePath.is_relative())
		{
			std::error_code ec;
			const std::filesystem::path cwd = std::filesystem::current_path(ec);
			if (!ec)
				absolutePath = cwd / absolutePath;
		}
		{
			std::error_code ec;
			if (absolutePath.has_parent_path())
				std::filesystem::create_directories(absolutePath.parent_path(), ec);
		}
		const size_t rotationMb = static_cast<size_t>((std::max)(static_cast<int64_t>(1), m_config.GetInt("server.moderation_audit_rotation_mb", 10)));
		const int retentionDays = static_cast<int>(m_config.GetInt("server.moderation_audit_retention_days", 7));
		if (!m_moderationAuditLog.Init(absolutePath.string(), rotationMb, retentionDays))
		{
			LOG_WARN(Net, "[ServerApp] Moderation audit log Init FAILED (path={})", absolutePath.string());
			m_moderationAuditLogReady = false;
			return;
		}

		m_moderationAuditLogReady = true;
		LOG_INFO(Net, "[ServerApp] Moderation audit subsystem Init OK (path={})", absolutePath.string());
	}

	void ServerApp::LoadChatBanFile()
	{
		m_bannedCharacterKeys.clear();
		const std::string relativePath = m_config.GetString("server.chat_ban_list_path", "persistence/server/chat_bans.ini");
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_INFO(Net, "[ServerApp] Chat ban list not present — starting empty (path={})", relativePath);
			return;
		}

		engine::core::Config banConfig;
		if (!banConfig.LoadFromFile(fullPath.string()))
		{
			LOG_WARN(Net, "[ServerApp] Chat ban list load FAILED (path={})", relativePath);
			return;
		}

		const uint32_t banCount = static_cast<uint32_t>(banConfig.GetInt("ban.count", 0));
		for (uint32_t banIndex = 0; banIndex < banCount && banIndex < 4096u; ++banIndex)
		{
			// Phase 3.7.5 — clé élargie à uint64. Config::GetInt retourne int64_t signé ;
			// on bit-cast pour préserver la valeur uint64 quand le bit 63 serait positionné.
			const int64_t rawKey = banConfig.GetInt("ban." + std::to_string(banIndex) + ".key", 0);
			const uint64_t key = (rawKey == 0) ? 0u : static_cast<uint64_t>(rawKey);
			if (key != 0u)
			{
				m_bannedCharacterKeys.insert(key);
			}
		}

		LOG_INFO(Net, "[ServerApp] Chat ban list loaded (path={}, keys={})", relativePath, m_bannedCharacterKeys.size());
	}

	void ServerApp::SaveChatBanFile()
	{
		std::vector<uint64_t> keys(m_bannedCharacterKeys.begin(), m_bannedCharacterKeys.end());
		std::sort(keys.begin(), keys.end());
		std::ostringstream output;
		output << "ban.count=" << keys.size() << "\n";
		for (size_t banIndex = 0; banIndex < keys.size(); ++banIndex)
		{
			output << "ban." << banIndex << ".key=" << keys[banIndex] << "\n";
		}

		const std::string relativePath = m_config.GetString("server.chat_ban_list_path", "persistence/server/chat_bans.ini");
		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relativePath, output.str()))
		{
			LOG_ERROR(Net, "[ServerApp] Chat ban list save FAILED (path={})", relativePath);
			return;
		}

		LOG_INFO(Net, "[ServerApp] Chat ban list saved (path={}, keys={})", relativePath, keys.size());
	}

	void ServerApp::SendChatSystemNotice(ConnectedClient& receiver, std::string_view text)
	{
		ChatRelayMessage notice{};
		notice.channel = static_cast<uint8_t>(engine::net::ChatChannel::Say);
		notice.senderEntityId = 0;
		notice.timestampUnixMs = NowUnixEpochMsUtc();
		notice.senderDisplay = "System";
		notice.text.assign(text.begin(), text.end());
		if (!SendChatRelay(receiver, notice))
		{
			LOG_WARN(Net, "[ServerApp] System chat notice send FAILED (client_id={})", receiver.clientId);
			return;
		}

		LOG_INFO(Net, "[ServerApp] System chat notice sent (client_id={}, len={})", receiver.clientId, notice.text.size());
	}

	void ServerApp::BroadcastModerationAnnouncement(std::string_view text)
	{
		ChatRelayMessage notice{};
		notice.channel = static_cast<uint8_t>(engine::net::ChatChannel::Global);
		notice.senderEntityId = 0;
		notice.timestampUnixMs = NowUnixEpochMsUtc();
		notice.senderDisplay = "Moderation";
		notice.text.assign(text.begin(), text.end());
		size_t sentOk = 0;
		for (ConnectedClient& client : m_clients)
		{
			if (SendChatRelay(client, notice))
			{
				++sentOk;
			}
		}

		LOG_INFO(Net, "[ServerApp] Moderation announce broadcast (recipients_ok={}, text_len={})", sentOk, notice.text.size());
	}

	void ServerApp::AuditLogChatReport(std::string_view reporterDisplay, std::string_view targetDisplay, std::string_view reason)
	{
		if (m_moderationAuditLogReady)
		{
			m_moderationAuditLog.LogChatReport(reporterDisplay, targetDisplay, reason);
		}
		else
		{
			LOG_INFO(Net,
				"[Moderation/Audit] CHAT_REPORT reporter={} target={} reason={}",
				reporterDisplay,
				targetDisplay,
				reason);
		}
	}

	void ServerApp::AuditLogModeration(std::string_view action, std::string_view actorDisplay, std::string_view targetDisplay, std::string_view detail)
	{
		if (m_moderationAuditLogReady)
		{
			m_moderationAuditLog.LogModerationAction(action, actorDisplay, targetDisplay, detail);
		}
		else
		{
			LOG_INFO(Net,
				"[Moderation/Audit] action={} actor={} target={} detail={}",
				action,
				actorDisplay,
				targetDisplay,
				detail);
		}
	}

	bool ServerApp::IsChatSenderIgnoredBy(const ConnectedClient& receiver, std::string_view senderDisplay) const
	{
		for (const std::string& ignoredName : receiver.chatIgnoredDisplayNames)
		{
			if (ChatNameEqualsAsciiI(ignoredName, senderDisplay))
			{
				return true;
			}
		}

		return false;
	}

	ConnectedClient* ServerApp::FindConnectedClientByChatDisplayName(std::string_view displayToken)
	{
		const std::string_view token = TrimChatArg(displayToken);
		if (token.empty())
		{
			return nullptr;
		}

		for (ConnectedClient& client : m_clients)
		{
			const std::string selfLabel = "P" + std::to_string(client.clientId);
			if (ChatNameEqualsAsciiI(token, selfLabel))
			{
				return &client;
			}
		}

		return nullptr;
	}

	void ServerApp::DisconnectConnectedClient(uint32_t clientId, std::string_view persistenceReason)
	{
		const auto clientIt = std::find_if(
			m_clients.begin(),
			m_clients.end(),
			[clientId](const ConnectedClient& client) { return client.clientId == clientId; });
		if (clientIt == m_clients.end())
		{
			LOG_WARN(Net, "[ServerApp] Disconnect skipped: client_id not found ({})", clientId);
			return;
		}

		SaveConnectedClient(*clientIt, persistenceReason);
		RecordCharacterSnapshot(*clientIt, persistenceReason); // ETAPE2.6
		// ETAPE2.2 — la sauvegarde de déconnexion est écrite tout de suite (attente de
		// la file, 2 s au plus) : si le joueur rejoint un autre shard, le master
		// déplace le verrou de propriété et une écriture tardive serait refusée.
		if (m_characterSaveQueue != nullptr
			&& !m_characterSaveQueue->Flush(clientIt->persistenceCharacterKey, std::chrono::milliseconds(2000)))
		{
			LOG_WARN(Net, "[ServerApp] Disconnect save still pending after 2 s (client_id={}, character_key={})",
				clientIt->clientId, clientIt->persistenceCharacterKey);
		}
		else
		{
			// ETAPE5.5 — personnage parti (sauvegarde finale écrite, plus rien en cours) :
			// ses empreintes ne servent plus (mémoire bornée).
			m_savedBlocks.Forget(clientIt->persistenceCharacterKey);
		}
		if (clientIt->hasCell)
		{
			const auto zoneIt = m_zoneGrids.find(engine::world::cells::MapIdOfZone(clientIt->zoneId));
			if (zoneIt != m_zoneGrids.end())
			{
				(void)zoneIt->second.RemoveEntity(clientIt->entityId);
			}
		}

		// TA.3c — reset de l'état anti-cheat pour ce character_key. Sans ça, si le même
		// personnage se reconnecte plus tard, son inputSequence redémarre à 1 alors que le
		// state AntiCheat conserve l'ancien tsMs (très supérieur) → dt négatif clampé à 1 ms
		// → faux positif SpeedHack sur le premier input post-reconnexion.
		m_antiCheat.Reset(clientIt->persistenceCharacterKey);

		OnClientLogout(*clientIt);
		// ETAPE3.1 — la session chiffrée meurt avec le client (après OnClientLogout, qui
		// peut encore lui envoyer des paquets). Un nouveau Hello rouvrira une session.
		DropUdpSession(clientIt->endpoint);
		LOG_INFO(Net, "[ServerApp] Client disconnected (client_id={}, reason={})", clientId, persistenceReason);
		m_clients.erase(clientIt);
		// TG.2 — l'erase décale les indices après le slot supprimé ; reconstruire les 3 maps.
		RebuildClientIndexes();
	}

	bool ServerApp::HandleChatSlashCommand(ConnectedClient& sender, const ParsedChatSlashCommand& command)
	{
		const std::string actorLabel = "P" + std::to_string(sender.clientId);

		switch (command.kind)
		{
		case ChatSlashCommandKind::Who:
		{
			const bool globalWho = ChatNameEqualsAsciiI(TrimChatArg(command.argsRemainder), "global");
			std::ostringstream list;
			list << (globalWho ? "ONLINE (global): " : "ONLINE (zone): ");
			bool first = true;
			for (const ConnectedClient& peer : m_clients)
			{
				if (!globalWho && peer.zoneId != sender.zoneId)
				{
					continue;
				}

				if (!first)
				{
					list << ' ';
				}

				first = false;
				list << 'P' << peer.clientId;
			}

			if (first)
			{
				list << "(none)";
			}

			SendChatSystemNotice(sender, list.str());
			LOG_INFO(Net, "[ServerApp] /who handled (client_id={}, global={})", sender.clientId, globalWho ? "true" : "false");
			return true;
		}
		case ChatSlashCommandKind::Ignore:
		{
			const auto [targetName, extra] = SplitFirstChatArg(command.argsRemainder);
			(void)extra;
			if (targetName.empty())
			{
				SendChatSystemNotice(sender, "Usage: /ignore <player>");
				LOG_WARN(Net, "[ServerApp] /ignore rejected: missing target (client_id={})", sender.clientId);
				return true;
			}

			const std::string selfLabel = "P" + std::to_string(sender.clientId);
			if (ChatNameEqualsAsciiI(targetName, selfLabel))
			{
				SendChatSystemNotice(sender, "Cannot ignore yourself.");
				LOG_WARN(Net, "[ServerApp] /ignore rejected: self-target (client_id={})", sender.clientId);
				return true;
			}

			if (sender.chatIgnoredDisplayNames.size() >= 32)
			{
				SendChatSystemNotice(sender, "Ignore list full (max 32).");
				LOG_WARN(Net, "[ServerApp] /ignore rejected: list full (client_id={})", sender.clientId);
				return true;
			}

			for (const std::string& existing : sender.chatIgnoredDisplayNames)
			{
				if (ChatNameEqualsAsciiI(existing, targetName))
				{
					SendChatSystemNotice(sender, "Already ignoring that player.");
					LOG_INFO(Net, "[ServerApp] /ignore duplicate ignored (client_id={})", sender.clientId);
					return true;
				}
			}

			sender.chatIgnoredDisplayNames.push_back(targetName);
			SaveConnectedClient(sender, "chat_ignore");
			SendChatSystemNotice(sender, "Ignored " + targetName + ".");
			LOG_INFO(Net, "[ServerApp] /ignore applied (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}
		case ChatSlashCommandKind::Unignore:
		{
			const auto [targetName, extra] = SplitFirstChatArg(command.argsRemainder);
			(void)extra;
			if (targetName.empty())
			{
				SendChatSystemNotice(sender, "Usage: /unignore <player>");
				LOG_WARN(Net, "[ServerApp] /unignore rejected: missing target (client_id={})", sender.clientId);
				return true;
			}

			const auto it = std::find_if(
				sender.chatIgnoredDisplayNames.begin(),
				sender.chatIgnoredDisplayNames.end(),
				[&](const std::string& entry) { return ChatNameEqualsAsciiI(entry, targetName); });
			if (it == sender.chatIgnoredDisplayNames.end())
			{
				SendChatSystemNotice(sender, "Not ignoring that player.");
				LOG_WARN(Net, "[ServerApp] /unignore rejected: not found (client_id={})", sender.clientId);
				return true;
			}

			sender.chatIgnoredDisplayNames.erase(it);
			SaveConnectedClient(sender, "chat_unignore");
			SendChatSystemNotice(sender, "Unignored " + targetName + ".");
			LOG_INFO(Net, "[ServerApp] /unignore applied (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}
		case ChatSlashCommandKind::Report:
		{
			const auto [targetName, reason] = SplitFirstChatArg(command.argsRemainder);
			if (targetName.empty() || reason.empty())
			{
				SendChatSystemNotice(sender, "Usage: /report <player> <reason>");
				LOG_WARN(Net, "[ServerApp] /report rejected: bad args (client_id={})", sender.clientId);
				return true;
			}

			AuditLogChatReport(actorLabel, targetName, reason);
			SendChatSystemNotice(sender, "Report recorded. Thank you.");
			LOG_INFO(Net, "[ServerApp] /report recorded (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}
		case ChatSlashCommandKind::Kick:
		case ChatSlashCommandKind::Ban:
		case ChatSlashCommandKind::Mute:
		case ChatSlashCommandKind::Announce:
		{
			if (!sender.chatModeratorRole)
			{
				SendChatSystemNotice(sender, "Permission denied.");
				LOG_WARN(Net,
					"[ServerApp] Admin chat command denied (client_id={}, cmd={})",
					sender.clientId,
					ChatSlashCommandLabel(command.kind));
				return true;
			}

			if (command.kind == ChatSlashCommandKind::Announce)
			{
				const std::string_view announcement = TrimChatArg(command.argsRemainder);
				if (announcement.empty())
				{
					SendChatSystemNotice(sender, "Usage: /announce <message>");
					LOG_WARN(Net, "[ServerApp] /announce rejected: empty (client_id={})", sender.clientId);
					return true;
				}

				BroadcastModerationAnnouncement(announcement);
				AuditLogModeration("ANNOUNCE", actorLabel, "*", announcement);
				SendChatSystemNotice(sender, "Announcement broadcast.");
				LOG_INFO(Net, "[ServerApp] /announce executed (client_id={})", sender.clientId);
				return true;
			}

			const auto [targetName, detail] = SplitFirstChatArg(command.argsRemainder);
			if (targetName.empty())
			{
				SendChatSystemNotice(sender, "Usage: /kick|/ban|/mute <player> [detail]");
				LOG_WARN(Net, "[ServerApp] Admin chat command rejected: missing target (client_id={})", sender.clientId);
				return true;
			}

			ConnectedClient* target = FindConnectedClientByChatDisplayName(targetName);
			if (target == nullptr)
			{
				SendChatSystemNotice(sender, "Target not online.");
				LOG_WARN(Net, "[ServerApp] Admin chat command rejected: target offline (client_id={})", sender.clientId);
				return true;
			}

			if (target->clientId == sender.clientId)
			{
				SendChatSystemNotice(sender, "Cannot target yourself.");
				return true;
			}

			const std::string targetLabel = "P" + std::to_string(target->clientId);

			if (command.kind == ChatSlashCommandKind::Mute)
			{
				uint32_t durationSeconds = 60;
				if (!detail.empty())
				{
					char* endPtr = nullptr;
					const unsigned long parsed = std::strtoul(detail.c_str(), &endPtr, 10);
					if (endPtr != detail.c_str() && parsed > 0ul && parsed < 604800ul)
					{
						durationSeconds = static_cast<uint32_t>(parsed);
					}
					else
					{
						SendChatSystemNotice(sender, "Mute duration invalid; using 60s.");
						LOG_WARN(Net, "[ServerApp] /mute duration fallback 60s (client_id={})", sender.clientId);
					}
				}

				const uint32_t deltaTicks = durationSeconds * static_cast<uint32_t>(m_tickHz > 0 ? m_tickHz : 20u);
				target->chatMutedUntilServerTick = m_currentTick + deltaTicks;
				AuditLogModeration("MUTE", actorLabel, targetLabel, std::to_string(durationSeconds) + "s");
				SendChatSystemNotice(*target, "You have been muted.");
				SendChatSystemNotice(sender, "Mute applied.");
				LOG_INFO(Net,
					"[ServerApp] /mute applied (actor_client_id={}, target_client_id={}, seconds={})",
					sender.clientId,
					target->clientId,
					durationSeconds);
				return true;
			}

			if (command.kind == ChatSlashCommandKind::Kick)
			{
				AuditLogModeration("KICK", actorLabel, targetLabel, detail);
				SendChatSystemNotice(*target, "You were kicked from the server.");
				DisconnectConnectedClient(target->clientId, "moderation_kick");
				SendChatSystemNotice(sender, "Kick executed.");
				LOG_INFO(Net, "[ServerApp] /kick executed (actor_client_id={}, target={})", sender.clientId, targetLabel);
				return true;
			}

			if (command.kind == ChatSlashCommandKind::Ban)
			{
				const uint64_t characterKey = target->persistenceCharacterKey;
				if (characterKey == 0)
				{
					SendChatSystemNotice(sender, "Ban failed: target has no persistence key.");
					LOG_WARN(Net, "[ServerApp] /ban rejected: no persistence key (target_client_id={})", target->clientId);
					return true;
				}

				m_bannedCharacterKeys.insert(characterKey);
				SaveChatBanFile();
				AuditLogModeration("BAN", actorLabel, targetLabel, detail);
				SendChatSystemNotice(*target, "You are banned from this server.");
				DisconnectConnectedClient(target->clientId, "moderation_ban");
				SendChatSystemNotice(sender, "Ban applied.");
				LOG_INFO(Net,
					"[ServerApp] /ban executed (actor_client_id={}, character_key={})",
					sender.clientId,
					characterKey);
				return true;
			}
		}
		case ChatSlashCommandKind::Friend:
			return HandleFriendCommand(sender, command.argsRemainder);

		// M32.2 — Party commands
		case ChatSlashCommandKind::Invite:
			return HandleInviteCommand(sender, command.argsRemainder);

		case ChatSlashCommandKind::Leave:
			return HandleLeaveCommand(sender);

		case ChatSlashCommandKind::Loot:
			return HandleLootCommand(sender, command.argsRemainder);

		case ChatSlashCommandKind::PartyKick:
			return HandlePartyKickCommand(sender, command.argsRemainder);

	// M35.3 — Trade command
	case ChatSlashCommandKind::Trade:
		return HandleTradeCommand(sender, command.argsRemainder);

	// Level-up runtime — Commande admin de test : /setlevel <joueur> <niveau>.
	// Fixe le niveau d'un joueur en ligne, recalcule ses stats (soin complet),
	// re-pousse la fiche et persiste. Commande de triche puissante → gardée par
	// le rôle le plus strict disponible sur ConnectedClient (chatModeratorRole ;
	// aucun flag admin/GM distinct n'existe au niveau shard) + audit serveur.
	case ChatSlashCommandKind::SetLevel:
	{
		if (!sender.chatModeratorRole)
		{
			SendChatSystemNotice(sender, "Permission refusée.");
			LOG_WARN(Net,
				"[ServerApp] /setlevel refusé : rôle insuffisant (client_id={})",
				sender.clientId);
			return true;
		}

		// argsRemainder = « <joueur> <niveau> ».
		const auto [targetName, levelArg] = SplitFirstChatArg(command.argsRemainder);
		if (targetName.empty() || levelArg.empty())
		{
			SendChatSystemNotice(sender, "Usage: /setlevel <joueur> <niveau>");
			LOG_WARN(Net, "[ServerApp] /setlevel rejeté : arguments manquants (client_id={})", sender.clientId);
			return true;
		}

		// Parse du niveau (entier strictement positif).
		char* endPtr = nullptr;
		const long parsedLevel = std::strtol(levelArg.c_str(), &endPtr, 10);
		if (endPtr == levelArg.c_str() || *endPtr != '\0' || parsedLevel < 1)
		{
			SendChatSystemNotice(sender, "Usage: /setlevel <joueur> <niveau>");
			LOG_WARN(Net, "[ServerApp] /setlevel rejeté : niveau invalide (client_id={}, arg={})", sender.clientId, levelArg);
			return true;
		}

		// Clamp dans [1, levelMax] (fallback 100 si tables absentes).
		const uint32_t levelMax = m_statsTables ? m_statsTables->levelMax : 100u;
		uint32_t newLevel = static_cast<uint32_t>(parsedLevel);
		if (newLevel > levelMax)
		{
			newLevel = levelMax;
		}

		ConnectedClient* target = FindConnectedClientByChatDisplayName(targetName);
		if (target == nullptr)
		{
			SendChatSystemNotice(sender, "Cible hors ligne.");
			LOG_WARN(Net, "[ServerApp] /setlevel rejeté : cible hors ligne (client_id={}, target={})", sender.clientId, targetName);
			return true;
		}

		const std::string targetLabel = "P" + std::to_string(target->clientId);

		// Application : niveau + reset XP dans le niveau.
		target->level = newLevel;
		target->experiencePoints = 0;

		if (m_statsTables)
		{
			// Recalcul des stats au nouveau niveau + soin complet (mirroir level-up).
			const engine::server::gameplay::Sex sex =
				(target->gender == "female") ? engine::server::gameplay::Sex::Female
				                             : engine::server::gameplay::Sex::Male;
			const auto d = engine::server::gameplay::ComputeStats(
				*m_statsTables, target->factionId, target->classId, sex, target->level);
			if (d)
			{
				target->stats.maxHealth = d->hp;
				target->stats.currentHealth = d->hp;   // soin complet.
			}
			(void)SendPlayerStats(*target);
			SaveConnectedClient(*target, "admin_setlevel");
		}
		else
		{
			// Tables absentes : on a quand même fixé le niveau, mais sans recalcul de stats.
			SaveConnectedClient(*target, "admin_setlevel");
			SendChatSystemNotice(sender, "Niveau fixé (stats non recalculées : tables absentes).");
		}

		// Audit serveur (même fonction/format que les commandes de modération).
		AuditLogModeration("SETLEVEL", actorLabel, targetLabel, "level=" + std::to_string(newLevel));

		SendChatSystemNotice(sender, "Niveau de " + targetLabel + " fixé à " + std::to_string(newLevel) + ".");
		SendChatSystemNotice(*target, "Votre niveau a été fixé à " + std::to_string(newLevel) + " par un administrateur.");
		LOG_INFO(Net,
			"[ServerApp] /setlevel appliqué (actor_client_id={}, target_client_id={}, level={}, maxHp={})",
			sender.clientId, target->clientId, newLevel, target->stats.maxHealth);
		return true;
	}

	case ChatSlashCommandKind::None:
	default:
		LOG_WARN(Net, "[ServerApp] Chat slash command ignored: kind=None (client_id={})", sender.clientId);
		return true;
	}
}
}
