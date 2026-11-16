// Engine — traitement des messages réseau reçus du master et des envois de
// chat (anciens lambdas du constructeur Engine::Engine, enregistrés via
// AuthUiPresenter::SetMasterPushHandler et ChatUi::SetSendCallback).
//
// Code déplacé tel quel (aucun changement de comportement) : le constructeur
// passe de ~6 500 lignes à un enregistrement d'une ligne par rappel.
// La documentation de chaque fonction est dans Engine.h.

#include "src/client/app/Engine.h"
#include "src/client/app/EngineHelpers.h"

#include "src/shared/network/ChatPayloads.h"
#include "src/shared/network/LunarPayloads.h"
#include "src/shared/network/AdminCommandPayloads.h"
#include "src/shared/network/ShardPayloads.h"
#include "src/client/render/clouds/WeatherKindMap.h"
#include "src/client/render/CharacterWindowImGuiRenderer.h"
#include "src/client/render/skinned/PlaceholderPart.h"

#include <vulkan/vulkan.h>

// vk_mem_alloc.h removed: VMA is disabled (STAB.7) — all subsystems use raw Vulkan allocations.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include "imgui.h"
#endif

namespace engine
{
	using namespace engine::helpers; // utilitaires internes partagés (EngineHelpers.h)

	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	void Engine::HandleMasterPush(uint16_t opcode, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		// CMANGOS.18 (Phase 3.18 step 4) — Dispatch des reponses Mail. Les
		// reponses (opcodes 50/52/54/56/58) ne sont pas des push purs mais
		// arrivent via le meme mecanisme PacketReceived puisque le client
		// n'utilise pas de RequestResponseDispatcher pour Mail (requestId=0
		// fire-and-forget cote envoi). On parse + dispatche ici.
		switch (opcode)
		{
		// ETAPE5.1 — le master a déclaré mort le shard qui hébergeait notre personnage
		// (heartbeat expiré). Le verrou du personnage est libéré : on revient à l'écran
		// de connexion, d'où le joueur peut rejouer aussitôt. Traitement différé à la
		// frame suivante (on est ici à l'intérieur du poll de l'auth UI, qui ne doit
		// pas être réinitialisé pendant son propre parcours d'événements).
		case kOpcodeShardLostNotification:
		{
			auto parsed = ParseShardLostNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] SHARD_LOST parse failed (size={})", payloadSize);
				return;
			}
			LOG_WARN(Net, "[Engine] SHARD_LOST : shard {} perdu (raison={}) -> retour a l'ecran de connexion",
				parsed->shardId, static_cast<unsigned>(parsed->reason));
			m_shardLostPending = true;
			// ETAPE5.3 — arrêt ordonné du cluster : message dédié (maintenance).
			m_shardLostByShutdown = (parsed->reason == ShardLostReason::Shutdown);
			return;
		}
		case kOpcodeMailListInboxResponse:
		{
			auto parsed = ParseMailListInboxResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] MAIL_LIST_INBOX_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_mailUi.OnInboxResponse(*parsed);
			return;
		}
		case kOpcodeMailReadResponse:
		{
			auto parsed = ParseMailReadResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] MAIL_READ_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_mailUi.OnReadResponse(*parsed);
			return;
		}
		case kOpcodeMailSendResponse:
		{
			auto parsed = ParseMailSendResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] MAIL_SEND_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_mailUi.OnSendResponse(*parsed);
			return;
		}
		case kOpcodeMailTakeAttachmentsResponse:
		{
			auto parsed = ParseMailTakeAttachmentsResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] MAIL_TAKE_ATTACHMENTS_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_mailUi.OnTakeAttachmentsResponse(*parsed);
			return;
		}
		case kOpcodeMailDeleteResponse:
		{
			auto parsed = ParseMailDeleteResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] MAIL_DELETE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_mailUi.OnDeleteResponse(*parsed);
			return;
		}
		// CMANGOS.25 (Phase 3.25 step 3+4) — Dispatch des reponses IgnoreList
		// (69/71/73). La cache locale du presenter est mise a jour ici.
		case kOpcodeIgnoreAddResponse:
		{
			auto parsed = ParseIgnoreAddResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] IGNORE_ADD_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_ignoreListUi.OnIgnoreAddResponse(*parsed);
			return;
		}
		case kOpcodeIgnoreRemoveResponse:
		{
			auto parsed = ParseIgnoreRemoveResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] IGNORE_REMOVE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_ignoreListUi.OnIgnoreRemoveResponse(*parsed);
			return;
		}
		case kOpcodeIgnoreListResponse:
		{
			auto parsed = ParseIgnoreListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] IGNORE_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_ignoreListUi.OnIgnoreListResponse(*parsed);
			return;
		}
		// CMANGOS.32 (Phase 5.32 step 3+4) — Dispatch des reponses GmTickets
		// (77/79/81) + push GmTicketResolvedNotification (82).
		case kOpcodeGmTicketOpenResponse:
		{
			auto parsed = ParseGmTicketOpenResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GMTICKET_OPEN_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_gmTicketUi.OnOpenResponse(*parsed);
			return;
		}
		case kOpcodeGmTicketListMineResponse:
		{
			auto parsed = ParseGmTicketListMineResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GMTICKET_LIST_MINE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_gmTicketUi.OnListMineResponse(*parsed);
			return;
		}
		case kOpcodeGmTicketCancelResponse:
		{
			auto parsed = ParseGmTicketCancelResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GMTICKET_CANCEL_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_gmTicketUi.OnCancelResponse(*parsed);
			return;
		}
		case kOpcodeGmTicketResolvedNotification:
		{
			auto parsed = ParseGmTicketResolvedNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GMTICKET_RESOLVED_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_gmTicketUi.OnResolvedNotification(*parsed);
			return;
		}
		// CMANGOS.24 (Phase 3.24 step 3+4) — Dispatch des reponses Reputation
		// (96) + push UpdateNotification (97).
		case kOpcodeReputationListResponse:
		{
			auto parsed = ParseReputationListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] REPUTATION_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_reputationUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeReputationUpdateNotification:
		{
			auto parsed = ParseReputationUpdateNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] REPUTATION_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_reputationUi.OnUpdateNotification(*parsed);
			return;
		}
		// Carte à découvrir — page de cases du personnage (209) ; la suivante
		// est demandée tant que le master en annonce une.
		case kOpcodeMapDiscoveryResponse:
		{
			auto parsed = ParseMapDiscoveryResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] MAP_DISCOVERY_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			if (parsed->error != 0u)
				LOG_INFO(Net, "[MapDiscovery] carte du personnage indisponible (code={}) : decouverte locale seulement", parsed->error);
			if (const auto next = m_mapDiscovery.OnServerPage(*parsed))
				(void)m_authUi.SendGenericRequestAsync(kOpcodeMapDiscoveryRequest, BuildMapDiscoveryRequestPayload(*next));
			else if (parsed->error == 0u)
				LOG_INFO(Net, "[MapDiscovery] carte du personnage recue ({} cellule(s))", m_mapDiscovery.CellCount());
			return;
		}
		// SP2 anniversaires (2026-07-18) — Dispatch de la réponse Exploits (207).
		case kOpcodeExploitListResponse:
		{
			auto parsed = ParseExploitListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] EXPLOIT_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_exploitsUi.OnListResponse(*parsed);
			return;
		}
		// CMANGOS.39 (Phase 4.39 step 3+4) — Dispatch des reponses Skills
		// (114/116/118) + push UpgradeNotification (119).
		case kOpcodeSkillsListResponse:
		{
			auto parsed = ParseSkillsListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] SKILLS_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_skillBookUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeSkillLearnResponse:
		{
			auto parsed = ParseSkillLearnResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] SKILL_LEARN_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_skillBookUi.OnLearnResponse(*parsed);
			return;
		}
		case kOpcodeSkillUseResponse:
		{
			auto parsed = ParseSkillUseResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] SKILL_USE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_skillBookUi.OnUseResponse(*parsed);
			return;
		}
		case kOpcodeSkillUpgradeNotification:
		{
			auto parsed = ParseSkillUpgradeNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] SKILL_UPGRADE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_skillBookUi.OnUpgradeNotification(*parsed);
			return;
		}
		// CMANGOS.21 (Phase 5.21 step 3+4) — Dispatch des reponses Arena
		// (121/123/125/128) + push notifications (126/129).
		case kOpcodeArenaTeamListResponse:
		{
			auto parsed = ParseArenaTeamListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] ARENA_TEAM_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_arenaUi.OnTeamListResponse(*parsed);
			return;
		}
		case kOpcodeArenaQueueResponse:
		{
			auto parsed = ParseArenaQueueResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] ARENA_QUEUE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_arenaUi.OnQueueResponse(*parsed);
			return;
		}
		case kOpcodeArenaLeaveQueueResponse:
		{
			auto parsed = ParseArenaLeaveQueueResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] ARENA_LEAVE_QUEUE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_arenaUi.OnLeaveQueueResponse(*parsed);
			return;
		}
		case kOpcodeArenaMatchProposalNotification:
		{
			auto parsed = ParseArenaMatchProposalNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] ARENA_MATCH_PROPOSAL_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_arenaUi.OnMatchProposalNotification(*parsed);
			return;
		}
		case kOpcodeArenaMatchAcceptResponse:
		{
			auto parsed = ParseArenaMatchAcceptResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] ARENA_MATCH_ACCEPT_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_arenaUi.OnMatchAcceptResponse(*parsed);
			return;
		}
		case kOpcodeArenaMatchResultNotification:
		{
			auto parsed = ParseArenaMatchResultNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] ARENA_MATCH_RESULT_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_arenaUi.OnMatchResultNotification(*parsed);
			return;
		}
		// CMANGOS.10 (Phase 5 step 3+4) — Dispatch des reponses BattleGround
		// (131/133/135) + push notifications (136/137/138).
		case kOpcodeBgListResponse:
		{
			auto parsed = ParseBgListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] BG_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_battleGroundUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeBgQueueResponse:
		{
			auto parsed = ParseBgQueueResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] BG_QUEUE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_battleGroundUi.OnQueueResponse(*parsed);
			return;
		}
		case kOpcodeBgLeaveQueueResponse:
		{
			auto parsed = ParseBgLeaveQueueResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] BG_LEAVE_QUEUE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_battleGroundUi.OnLeaveQueueResponse(*parsed);
			return;
		}
		case kOpcodeBgMatchStartNotification:
		{
			auto parsed = ParseBgMatchStartNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] BG_MATCH_START_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_battleGroundUi.OnMatchStartNotification(*parsed);
			return;
		}
		case kOpcodeBgScoreUpdateNotification:
		{
			auto parsed = ParseBgScoreUpdateNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] BG_SCORE_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_battleGroundUi.OnScoreUpdateNotification(*parsed);
			return;
		}
		case kOpcodeBgMatchEndNotification:
		{
			auto parsed = ParseBgMatchEndNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] BG_MATCH_END_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_battleGroundUi.OnMatchEndNotification(*parsed);
			return;
		}
		// CMANGOS.36 (Phase 5.36 step 3+4) — Dispatch des reponses OutdoorPvp
		// (141/143/145/147) + push notifications (148/149).
		case kOpcodeOutdoorPvpZoneListResponse:
		{
			auto parsed = ParseOutdoorPvpZoneListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] OUTDOOR_PVP_ZONE_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_outdoorPvpUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeOutdoorPvpSubscribeResponse:
		{
			auto parsed = ParseOutdoorPvpSubscribeResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] OUTDOOR_PVP_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_outdoorPvpUi.OnSubscribeResponse(*parsed);
			return;
		}
		case kOpcodeOutdoorPvpUnsubscribeResponse:
		{
			auto parsed = ParseOutdoorPvpUnsubscribeResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] OUTDOOR_PVP_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_outdoorPvpUi.OnUnsubscribeResponse(*parsed);
			return;
		}
		case kOpcodeOutdoorPvpCaptureStartResponse:
		{
			auto parsed = ParseOutdoorPvpCaptureStartResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_START_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_outdoorPvpUi.OnCaptureStartResponse(*parsed);
			return;
		}
		case kOpcodeOutdoorPvpCaptureProgressNotification:
		{
			auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_outdoorPvpUi.OnCaptureProgressNotification(*parsed);
			return;
		}
		case kOpcodeOutdoorPvpCaptureCompletedNotification:
		{
			auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_outdoorPvpUi.OnCaptureCompletedNotification(*parsed);
			return;
		}
		// CMANGOS.42 (Phase 4.42 step 3+4) — Dispatch des reponses Weather
		// (151/153/155) + push notification (156).
		case kOpcodeWeatherListResponse:
		{
			auto parsed = ParseWeatherListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] WEATHER_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_weatherUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeWeatherSubscribeResponse:
		{
			auto parsed = ParseWeatherSubscribeResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] WEATHER_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_weatherUi.OnSubscribeResponse(*parsed);
			return;
		}
		case kOpcodeWeatherUnsubscribeResponse:
		{
			auto parsed = ParseWeatherUnsubscribeResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] WEATHER_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_weatherUi.OnUnsubscribeResponse(*parsed);
			return;
		}
		case kOpcodeWeatherUpdateNotification:
		{
			auto parsed = ParseWeatherUpdateNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] WEATHER_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_weatherUi.OnUpdateNotification(*parsed);
			// Signal autoritaire unique -> pilote le visuel (particules + nuages).
			// (Branchement absent jusqu'ici : la météo serveur n'affectait pas le rendu.)
			m_weatherSystem.SetWeather(
				engine::render::MapServerKindToWeatherState(parsed->kind));
			return;
		}
		// CMANGOS.31 (Phase 5.31 step 3+4) — Dispatch des reponses
		// GameEvents (158/160/162) + push notification (163).
		case kOpcodeGameEventListResponse:
		{
			auto parsed = ParseGameEventListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GAME_EVENT_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_gameEventUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeGameEventSubscribeResponse:
		{
			auto parsed = ParseGameEventSubscribeResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GAME_EVENT_SUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_gameEventUi.OnSubscribeResponse(*parsed);
			return;
		}
		case kOpcodeGameEventUnsubscribeResponse:
		{
			auto parsed = ParseGameEventUnsubscribeResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GAME_EVENT_UNSUBSCRIBE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_gameEventUi.OnUnsubscribeResponse(*parsed);
			return;
		}
		case kOpcodeGameEventStateChangeNotification:
		{
			auto parsed = ParseGameEventStateChangeNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GAME_EVENT_STATE_CHANGE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_gameEventUi.OnStateChangeNotification(*parsed);
			return;
		}
		// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Dispatch des reponses
		// Guild (165/167/169/171) + push notification (172 MotdUpdate).
		case kOpcodeGuildListResponse:
		{
			auto parsed = ParseGuildListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GUILD_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_guildUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeGuildMembersResponse:
		{
			auto parsed = ParseGuildMembersResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GUILD_MEMBERS_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_guildUi.OnMembersResponse(*parsed);
			return;
		}
		case kOpcodeGuildPermissionsResponse:
		{
			auto parsed = ParseGuildPermissionsResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GUILD_PERMISSIONS_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_guildUi.OnPermissionsResponse(*parsed);
			return;
		}
		case kOpcodeGuildBankResponse:
		{
			auto parsed = ParseGuildBankResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GUILD_BANK_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_guildUi.OnBankResponse(*parsed);
			return;
		}
		case kOpcodeGuildMotdUpdateNotification:
		{
			auto parsed = ParseGuildMotdUpdateNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] GUILD_MOTD_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_guildUi.OnMotdUpdateNotification(*parsed);
			return;
		}
		// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Dispatch des
		// reponses Auction (174/176/178/180) + push notification 181
		// (AuctionExpired).
		case kOpcodeAuctionListResponse:
		{
			auto parsed = ParseAuctionListResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] AUCTION_LIST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_auctionHouseUi.OnListResponse(*parsed);
			return;
		}
		case kOpcodeAuctionPostResponse:
		{
			auto parsed = ParseAuctionPostResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] AUCTION_POST_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_auctionHouseUi.OnPostResponse(*parsed);
			return;
		}
		case kOpcodeAuctionBidResponse:
		{
			auto parsed = ParseAuctionBidResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] AUCTION_BID_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_auctionHouseUi.OnBidResponse(*parsed);
			return;
		}
		case kOpcodeAuctionCancelResponse:
		{
			auto parsed = ParseAuctionCancelResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] AUCTION_CANCEL_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_auctionHouseUi.OnCancelResponse(*parsed);
			return;
		}
		case kOpcodeAuctionExpiredNotification:
		{
			auto parsed = ParseAuctionExpiredNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] AUCTION_EXPIRED_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_auctionHouseUi.OnExpiredNotification(*parsed);
			return;
		}
		// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Dispatch des push Loot
		// (182 RollNotification + 185 RollResultNotification) + responses
		// (184 ChoiceResponse + 187 SimulateRollResponse).
		case kOpcodeLootRollNotification:
		{
			auto parsed = ParseLootRollNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LOOT_ROLL_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_lootRollUi.OnRollNotification(*parsed);
			return;
		}
		case kOpcodeLootRollChoiceResponse:
		{
			auto parsed = ParseLootRollChoiceResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LOOT_ROLL_CHOICE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_lootRollUi.OnChoiceResponse(*parsed);
			return;
		}
		case kOpcodeLootRollResultNotification:
		{
			auto parsed = ParseLootRollResultNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LOOT_ROLL_RESULT_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_lootRollUi.OnRollResultNotification(*parsed);
			return;
		}
		case kOpcodeLootSimulateRollResponse:
		{
			auto parsed = ParseLootSimulateRollResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LOOT_SIMULATE_ROLL_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_lootRollUi.OnSimulateRollResponse(*parsed);
			return;
		}
		// Phase 5 step 3+4 Lunar — Dispatch des opcodes 193 (StateResponse)
		// et 194 (PhaseChangeNotification, push). Master autoritaire ; le
		// client recoit l'etat initial sur EnterWorld puis un push toutes
		// les ~21h sur changement de phase.
		case kOpcodeLunarStateResponse:
		{
			engine::network::lunar::LunarStateResponse parsed;
			if (!engine::network::lunar::ParseLunarStateResponsePayload(payload, payloadSize, parsed))
			{
				LOG_WARN(Net, "[Engine] LUNAR_STATE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			if (parsed.status == engine::network::lunar::LunarStatus::Ok)
			{
				m_dayNight.OnLunarPhaseChange(parsed.phase, parsed.illumination);
				LOG_INFO(Render, "[Engine] LunarState received: phase={} illumination={:.3f}",
					static_cast<unsigned>(parsed.phase), parsed.illumination);
			}
			return;
		}
		case kOpcodeLunarPhaseChangeNotification:
		{
			engine::network::lunar::LunarPhaseChangeNotification parsed;
			if (!engine::network::lunar::ParseLunarPhaseChangeNotificationPayload(payload, payloadSize, parsed))
			{
				LOG_WARN(Net, "[Engine] LUNAR_PHASE_CHANGE parse failed (size={})", payloadSize);
				return;
			}
			m_dayNight.OnLunarPhaseChange(parsed.newPhase, parsed.newIllumination);
			LOG_INFO(Render, "[Engine] LunarPhaseChange: phase={} illumination={:.3f}",
				static_cast<unsigned>(parsed.newPhase), parsed.newIllumination);
			return;
		}
		// WorldClock sync (Task 6.2) — Dispatch des opcodes 204 (StateResponse)
		// et 205 (ChangeNotification, push admin). Les deux portent les memes
		// champs ; on construit les WorldClockParams et on les branche sur le
		// cycle jour/nuit via SetServerClock (bascule en mode driven). Le master
		// est autoritaire ; le client recoit l'etat initial sur EnterWorld, un
		// push 205 a chaque changement admin (/settime, /pausetime...), et une
		// re-sync periodique 204 (controle de derive) declenchee par Update().
		case kOpcodeWorldClockStateResponse:
		case kOpcodeWorldClockChangeNotification:
		{
			engine::network::worldclock::WorldClockStateResponse parsed;
			const bool ok = (opcode == kOpcodeWorldClockStateResponse)
				? engine::network::worldclock::ParseWorldClockStateResponsePayload(payload, payloadSize, parsed)
				: engine::network::worldclock::ParseWorldClockChangeNotificationPayload(payload, payloadSize, parsed);
			if (!ok || parsed.status != engine::network::worldclock::WorldClockStatus::Ok)
			{
				LOG_WARN(Net, "[Engine] WORLDCLOCK parse/status failed (opcode={}, size={})",
					opcode, payloadSize);
				return;
			}
			engine::world::WorldClockParams p;
			p.epochRefUnixMs          = parsed.epochRefUnixMs;
			p.timeScaleRealMinPerDay  = parsed.timeScaleRealMinPerDay;
			p.offsetGameSec           = parsed.offsetGameSec;
			p.paused                  = (parsed.paused != 0);
			p.pausedAtGameSec         = parsed.pausedAtGameSec;
			p.lunarPeriodGameSec      = parsed.lunarPeriodGameSec;
			const uint64_t clientNow = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			m_dayNight.SetServerClock(p, parsed.serverTimeUnixMs, clientNow);
			LOG_INFO(Render, "[Engine] WorldClock sync: timeScale={:.1f} offset={:.0f} paused={}",
				p.timeScaleRealMinPerDay, p.offsetGameSec, static_cast<int>(p.paused));
			return;
		}
		// AdminCommand RBAC — reponse master apres validation du role +
		// log audit. Si status==Ok, on dispatch sur le nom de la commande
		// pour appliquer l'effet local (ex: /sky moon -> override visuel).
		// Sinon on log le refus.
		case kOpcodeAdminCommandResponse:
		{
			engine::network::admin::AdminCommandResponse parsed;
			if (!engine::network::admin::ParseAdminCommandResponsePayload(payload, payloadSize, parsed))
			{
				LOG_WARN(Net, "[Engine] ADMIN_COMMAND_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			// Helper inline pour pousser une ligne SRV dans le chat in-game
			// (cf. pattern pushSkyChatLine du send callback).
			auto pushAdminChatLine = [this](const char* sender, const char* fmt, auto... args) {
				char buf[256];
				std::snprintf(buf, sizeof(buf), fmt, args...);
				engine::net::ChatMessage chatMsg;
				chatMsg.timestampUnixMs = static_cast<uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch()).count());
				chatMsg.channel = engine::net::ChatChannel::Server;
				chatMsg.sender  = sender;
				chatMsg.text    = buf;
				m_chatUi.PushNetworkLine(chatMsg);
			};
			if (parsed.status == engine::network::admin::AdminCommandStatus::Ok)
			{
				if (parsed.command == "/sky moon")
				{
					// Parse echoed args : ["phase=7", "illumination=1.000"]
					uint8_t phase = 0;
					float illumination = 0.0f;
					for (const auto& kv : parsed.result)
					{
						if (kv.starts_with("phase="))
						{
							try { phase = static_cast<uint8_t>(std::stoi(kv.substr(6))); }
							catch (...) { phase = 0; }
						}
						else if (kv.starts_with("illumination="))
						{
							try { illumination = std::stof(kv.substr(13)); }
							catch (...) { illumination = 0.0f; }
						}
					}
					m_dayNight.OnLunarPhaseChange(phase, illumination);
					LOG_INFO(Render, "[Sky] /sky moon ACK applied : phase={} illumination={:.3f}",
						static_cast<unsigned>(phase), illumination);
					pushAdminChatLine("[Sky]",
						"OK : moon phase %u illumination=%.0f%% (master autorise)",
						static_cast<unsigned>(phase),
						static_cast<double>(illumination * 100.0f));
				}
				else if (parsed.command == "/sky time")
				{
					float hours = 12.0f;
					for (const auto& kv : parsed.result)
					{
						if (kv.starts_with("hours="))
						{
							try { hours = std::stof(kv.substr(6)); }
							catch (...) { hours = 12.0f; }
						}
					}
					m_dayNight.SetTime(hours);
					LOG_INFO(Render, "[Sky] /sky time ACK applied : {:.2f}h",
						static_cast<double>(hours));
				}
				else if (parsed.command == "/sky info")
				{
					const auto& s = m_dayNight.GetState();
					static const char* kMoonName[16] = {
						"NewMoon", "WaxingCrescentEarly", "WaxingCrescentLate", "FirstQuarter",
						"WaxingGibbousEarly", "WaxingGibbousLate", "FullMoonRising", "FullMoon",
						"FullMoonSetting", "WaningGibbousEarly", "WaningGibbousLate", "LastQuarter",
						"WaningCrescentEarly", "WaningCrescentLate", "EarthshineEarly", "EarthshineLate"
					};
					LOG_INFO(Render, "[Sky] timeOfDay={:.2f}h isDaytime={}",
						s.timeOfDay, s.isDaytime);
					LOG_INFO(Render, "[Sky] sunDir=({:.2f},{:.2f},{:.2f})",
						s.lightDir[0], s.lightDir[1], s.lightDir[2]);
					LOG_INFO(Render, "[Sky] moonPhase={} ({}) illumination={:.0f}%",
						static_cast<unsigned>(s.moonPhase),
						kMoonName[s.moonPhase < 16 ? s.moonPhase : 0],
						s.moonIllumination * 100.0f);

					// Affichage chat in-game : sans ce push, l'utilisateur ne voit
					// l'info que dans les logs. Cf. pattern identique pour /sky moon
					// (pushAdminChatLine plus haut) et /who/report/kick/ban/announce.
					pushAdminChatLine("[Sky]", "timeOfDay=%.2fh isDaytime=%d",
						static_cast<double>(s.timeOfDay),
						static_cast<int>(s.isDaytime));
					pushAdminChatLine("[Sky]", "sunDir=(%.2f,%.2f,%.2f)",
						static_cast<double>(s.lightDir[0]),
						static_cast<double>(s.lightDir[1]),
						static_cast<double>(s.lightDir[2]));
					pushAdminChatLine("[Sky]", "moonPhase=%u (%s) illumination=%.0f%%",
						static_cast<unsigned>(s.moonPhase),
						kMoonName[s.moonPhase < 16 ? s.moonPhase : 0],
						static_cast<double>(s.moonIllumination * 100.0f));
				}
				else if (parsed.command == "/loot")
				{
					LOG_INFO(Core, "[Admin] /loot ACK : {}", parsed.message);
				}
				else if (parsed.command == "/promote")
				{
					LOG_INFO(Core, "[Admin] /promote ACK : {}", parsed.message);
				}
				else if (parsed.command == "/who"
				      || parsed.command == "/report"
				      || parsed.command == "/kick"
				      || parsed.command == "/mute"
				      || parsed.command == "/ban"
				      || parsed.command == "/announce"
				      || parsed.command == "/shutdown")
				{
					// Wave 2 : reponses moderation. Affiche le resultat
					// dans le chat (canal Server) pour feedback joueur.
					std::string body;
					if (parsed.command == "/who")
					{
						std::string countStr;
						std::string loginsStr;
						for (const auto& kv : parsed.result)
						{
							if (kv.starts_with("count="))
								countStr = kv.substr(6);
							else if (kv.starts_with("logins="))
								loginsStr = kv.substr(7);
						}
						body = "[Who] " + countStr + " joueurs connectes";
						if (!loginsStr.empty())
							body += " : " + loginsStr;
					}
					else if (parsed.command == "/report")
					{
						body = "[Report] " + parsed.message;
					}
					else if (parsed.command == "/kick")
					{
						body = "[Kick] " + parsed.message;
					}
					else if (parsed.command == "/mute")
					{
						body = "[Mute] " + parsed.message;
					}
					else if (parsed.command == "/ban")
					{
						body = "[Ban] " + parsed.message;
					}
					else if (parsed.command == "/announce")
					{
						body = "[Announce] " + parsed.message;
					}
					else if (parsed.command == "/shutdown")
					{
						body = "[Shutdown] " + parsed.message; // ETAPE5.3
					}

					if (!body.empty() && m_chatUi.IsInitialized())
					{
						engine::net::ChatMessage line;
						line.timestampUnixMs = 0;
						line.channel = engine::net::ChatChannel::Server;
						line.sender = "system";
						line.text = std::move(body);
						m_chatUi.PushNetworkLine(line);
					}
					LOG_INFO(Net, "[AdminCommand] OK command={} message={}",
						parsed.command, parsed.message);
				}
				else
				{
					LOG_INFO(Net, "[AdminCommand] OK : command={} (no client effect V1)", parsed.command);
					pushAdminChatLine("[AdminCommand]", "OK : %s", parsed.command.c_str());
				}
			}
			else
			{
				// Tout refus (Denied, InvalidArgs, Unauthorized, ServerError,
				// UnknownCommand) est trace cote client. L'audit cote master
				// a deja ete emis avec result=DENIED/INVALID_ARGS/etc.
				LOG_WARN(Net, "[AdminCommand] REFUSED command={} status={} message={}",
					parsed.command,
					static_cast<unsigned>(parsed.status),
					parsed.message);
				const char* statusName = "ERROR";
				switch (parsed.status)
				{
					case engine::network::admin::AdminCommandStatus::Unauthorized:
						statusName = "UNAUTHORIZED"; break;
					case engine::network::admin::AdminCommandStatus::Denied:
						statusName = "DENIED (role insuffisant)"; break;
					case engine::network::admin::AdminCommandStatus::UnknownCommand:
						statusName = "UNKNOWN_COMMAND"; break;
					case engine::network::admin::AdminCommandStatus::InvalidArgs:
						statusName = "INVALID_ARGS"; break;
					default: break;
				}
				pushAdminChatLine("[AdminCommand]",
					"REFUSE %s : %s -- %s",
					statusName, parsed.command.c_str(), parsed.message.c_str());
			}
			return;
		}
		// CMANGOS.33 (Phase 5.33 step 3+4) — Dispatch des reponses LFG
		// (101/103/105) + push MatchProposalNotification (106).
		case kOpcodeLfgQueueResponse:
		{
			auto parsed = ParseLfgQueueResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LFG_QUEUE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_lfgUi.OnQueueResponse(*parsed);
			return;
		}
		case kOpcodeLfgLeaveResponse:
		{
			auto parsed = ParseLfgLeaveResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LFG_LEAVE_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_lfgUi.OnLeaveResponse(*parsed);
			return;
		}
		case kOpcodeLfgStatusResponse:
		{
			auto parsed = ParseLfgStatusResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LFG_STATUS_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_lfgUi.OnStatusResponse(*parsed);
			return;
		}
		case kOpcodeLfgMatchProposalNotification:
		{
			auto parsed = ParseLfgMatchProposalNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] LFG_MATCH_PROPOSAL_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_lfgUi.OnMatchProposal(*parsed);
			return;
		}
		// CMANGOS.30 (Phase 5.30 step 3+4) — Dispatch des opcodes Cinematics
		// (push 108 + responses 110/112). 109 (Ack) et 111 (SkipRequest)
		// sont sortants : pas de dispatch ici.
		case kOpcodeCinematicPlayNotification:
		{
			auto parsed = ParseCinematicPlayNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] CINEMATIC_PLAY_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_cinematicUi.OnPlayNotification(*parsed);
			return;
		}
		case kOpcodeCinematicAckResponse:
		{
			auto parsed = ParseCinematicAckResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] CINEMATIC_ACK_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_cinematicUi.OnAckResponse(*parsed);
			return;
		}
		case kOpcodeCinematicSkipResponse:
		{
			auto parsed = ParseCinematicSkipResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] CINEMATIC_SKIP_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_cinematicUi.OnSkipResponse(*parsed);
			return;
		}
		// CMANGOS.27 (Phase 4.27 step 3+4) — Dispatch des reponses Trade
		// (84/87/89/92) + push notifications (85/90/94).
		case kOpcodeTradeBeginResponse:
		{
			auto parsed = ParseTradeBeginResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_BEGIN_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeBeginResponse(*parsed);
			return;
		}
		case kOpcodeTradeBeginNotification:
		{
			auto parsed = ParseTradeBeginNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_BEGIN_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeBeginNotification(*parsed);
			return;
		}
		case kOpcodeTradeSetOfferResponse:
		{
			auto parsed = ParseTradeSetOfferResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_SET_OFFER_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeSetOfferResponse(*parsed);
			return;
		}
		case kOpcodeTradeLockResponse:
		{
			auto parsed = ParseTradeLockResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_LOCK_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeLockResponse(*parsed);
			return;
		}
		case kOpcodeTradeStateUpdateNotification:
		{
			auto parsed = ParseTradeStateUpdateNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_STATE_UPDATE_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeStateUpdate(*parsed);
			return;
		}
		case kOpcodeTradeCommitResponse:
		{
			auto parsed = ParseTradeCommitResponsePayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_COMMIT_RESPONSE parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeCommitResponse(*parsed);
			return;
		}
		case kOpcodeTradeCancelNotification:
		{
			auto parsed = ParseTradeCancelNotificationPayload(payload, payloadSize);
			if (!parsed)
			{
				LOG_WARN(Net, "[Engine] TRADE_CANCEL_NOTIFICATION parse failed (size={})", payloadSize);
				return;
			}
			m_tradeWindowUi.OnTradeCancelNotification(*parsed);
			return;
		}
		default:
			break;
		}
		if (opcode != kOpcodeChatRelay)
			return;
		auto parsed = ParseChatRelayPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[Engine] CHAT_RELAY parse failed (size={})", payloadSize);
			return;
		}
		engine::net::ChatChannel ch = engine::net::ChatChannel::Say;
		(void)engine::net::TryDecodeChannelWire(parsed->channel, ch);
		engine::net::ChatMessage msg{};
		msg.timestampUnixMs = parsed->timestampUnixMs;
		msg.channel = ch;
		msg.sender = std::move(parsed->sender);
		msg.text = std::move(parsed->text);
		m_chatUi.PushNetworkLine(msg);
	}
	// Ancien corps de lambda du constructeur — voir la documentation dans Engine.h.
	bool Engine::HandleChatSend(uint8_t channel, std::string_view targetToken, std::string_view text)
	{
		// Wave 3 RBAC migration : helper local pour envoyer un audit
		// AdminCommand au master pour les UI panel toggles. Fire-and-forget
		// (requestId=0, on n'attend pas l'ACK : le toggle visuel est
		// applique immediatement, le master log juste le geste).
		//
		// Le master valide l'auth + role (player suffit pour ces commandes)
		// et emet [AdminCommand] result=OK dans son log audit.
		auto sendAdminAudit = [this](std::string_view cmd) {
			engine::network::admin::AdminCommandRequest req;
			req.command = std::string(cmd);
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
		};

		// CMANGOS.18 (Phase 3.18 step 4) — Intercept /mail avant l'envoi
		// chat. ParseSlashPrefixes ne connait pas /mail, donc le texte
		// arrive sur le canal Say avec text == "/mail" (ou "/mail ...").
		// On consomme le slash command localement et retourne true (le
		// chat presenter pense que c'est envoye et clear l'input).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/mail" || text.starts_with("/mail ") || text.starts_with("/mail\t")))
		{
			m_mailVisible = !m_mailVisible;
			if (m_mailVisible)
			{
				m_mailUi.RequestInbox();
			}
			LOG_INFO(Core, "[Engine] /mail toggle (visible={})", m_mailVisible);
			sendAdminAudit("/mail");
			return true;
		}
		// CHAR-MODEL — Emotes par slash command : posent un role d'emote consomme
		// par la state machine (avatar -> etat Emote, anim en boucle annulee au
		// moindre deplacement/saut). Locales (pas d'aller-retour serveur). Ajouter
		// une emote = une ligne dans kEmotes (+ addRole correspondant).
		{
			struct EmoteCmd { const char* cmd; const char* role; const char* feedback; };
			static const EmoteCmd kEmotes[] = {
				{ "/dance", "Dance", "Vous dansez." },
				{ "/sit",   "Sit",   "Vous vous asseyez." },
				{ "/assis", "Sit",   "Vous vous asseyez." },
				{ "/talk",  "Talk",  "Vous discutez." },
				{ "/torch", "Torch", "Vous brandissez une torche." },
				{ "/kneel", "Kneel", "Vous vous agenouillez." },
				{ "/sittalk", "SitTalk", "Vous discutez, assis." },
				{ "/push", "Push", "Vous poussez." },
			};
			if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say))
			{
				for (const auto& em : kEmotes)
				{
					const std::string c = em.cmd;
					if (text == c || text.starts_with(c + " ") || text.starts_with(c + "\t"))
					{
						m_pendingEmoteRole = em.role;
						LOG_INFO(Core, "[Engine] emote {} (role {}) requested", em.cmd, em.role);
						engine::net::ChatMessage emoteMsg;
						emoteMsg.timestampUnixMs = static_cast<uint64_t>(
							std::chrono::duration_cast<std::chrono::milliseconds>(
								std::chrono::system_clock::now().time_since_epoch()).count());
						emoteMsg.channel = engine::net::ChatChannel::Server;
						emoteMsg.sender  = "[Emote]";
						emoteMsg.text    = em.feedback;
						m_chatUi.PushNetworkLine(emoteMsg);
						return true;
					}
				}
			}
		}
		// CMANGOS.27 (Phase 4.27 step 3+4) — Slash command /trade <accountId>
		// pour initier un echange avec le joueur cible. V1 : resolution par
		// account_id direct (la resolution par character_name viendra avec
		// PartySystem display ulterieurement). "/trade cancel" annule la
		// trade en cours.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/trade" || text.starts_with("/trade ") || text.starts_with("/trade\t")))
		{
			const auto spaceIdx = text.find_first_of(" \t");
			if (spaceIdx == std::string_view::npos)
			{
				LOG_INFO(Core, "[Engine] /trade : usage /trade <account_id> ou /trade cancel");
				return true;
			}
			std::string_view arg = text.substr(spaceIdx + 1);
			while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
				arg.remove_prefix(1u);
			if (arg == "cancel")
			{
				m_tradeWindowUi.Cancel();
				LOG_INFO(Core, "[Engine] /trade cancel");
				return true;
			}
			uint64_t targetAccountId = 0u;
			bool parsed = false;
			try
			{
				size_t pos = 0;
				const std::string argStr(arg);
				targetAccountId = std::stoull(argStr, &pos, 10);
				parsed = (pos > 0u && targetAccountId != 0u);
			}
			catch (...)
			{
				parsed = false;
			}
			if (!parsed)
			{
				LOG_WARN(Core, "[Engine] /trade : argument '{}' n'est pas un account_id valide",
					std::string(arg));
				return true;
			}
			m_tradeWindowUi.RequestBeginTrade(targetAccountId);
			LOG_INFO(Core, "[Engine] /trade {}", targetAccountId);
			return true;
		}
		// CMANGOS.24 (Phase 3.24 step 3+4) — Slash command /rep et /reputation
		// pour ouvrir/fermer le panneau Reputation et synchroniser la liste
		// depuis le master au moment de l'ouverture.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/rep" || text == "/reputation"
			    || text.starts_with("/rep ") || text.starts_with("/rep\t")
			    || text.starts_with("/reputation ") || text.starts_with("/reputation\t")))
		{
			m_reputationVisible = !m_reputationVisible;
			if (m_reputationVisible)
			{
				m_reputationUi.RequestReputationList();
			}
			LOG_INFO(Core, "[Engine] /rep toggle (visible={})", m_reputationVisible);
			sendAdminAudit("/rep");
			return true;
		}
		// CMANGOS.39 (Phase 4.39 step 3+4) — Slash command /skills pour
		// ouvrir/fermer le panneau Skill Book et synchroniser la liste
		// depuis le master au moment de l'ouverture. La touche B fait
		// la meme chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/skills" || text == "/skill"
			    || text.starts_with("/skills ") || text.starts_with("/skills\t")
			    || text.starts_with("/skill ") || text.starts_with("/skill\t")))
		{
			// Chantier 1 — route vers la fenêtre Personnage unifiée (onglet Compétences).
			if (m_characterWindowImGui)
				m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Competences);
			m_skillBookUi.RequestList();
			LOG_INFO(Core, "[Engine] /skills -> fenetre Personnage (onglet Competences)");
			sendAdminAudit("/skills");
			return true;
		}
		// Grimoire (Task 13) — Slash commands /grimoire et /sorts pour
		// ouvrir/fermer le panneau Grimoire. Équivalent à la touche V remappable.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/grimoire" || text == "/sorts"
			    || text.starts_with("/grimoire ") || text.starts_with("/grimoire\t")
			    || text.starts_with("/sorts ") || text.starts_with("/sorts\t")))
		{
			if (m_characterWindowImGui)
				m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Techniques);
			LOG_INFO(Core, "[Engine] /grimoire -> fenetre Personnage (onglet Techniques)");
			return true;
		}
		// SP-D — Slash commands /arbre et /competences pour l'arbre de compétences.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/arbre" || text == "/competences"
			    || text.starts_with("/arbre ") || text.starts_with("/arbre	")
			    || text.starts_with("/competences ") || text.starts_with("/competences	")))
		{
			if (m_characterWindowImGui)
				m_characterWindowImGui->OpenAtTab(engine::render::CharacterWindowImGuiRenderer::Tab::Arbre);
			LOG_INFO(Core, "[Engine] /arbre -> fenetre Personnage (onglet Arbre)");
			return true;
		}
		// Chantier 2 SP1 — commande DEBUG /modular <a|b|off> : pose/echange/retire
		// une PARTIE placeholder (boite) sur la TETE de l'avatar. Preuve visuelle
		// du pipeline modulaire (avant les vrais assets). Gate derriere
		// client.debug.modular_test (defaut false) : outil de dev, pas une feature.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/modular" || text.starts_with("/modular ")))
		{
			if (!m_cfg.GetBool("client.debug.modular_test", false))
			{
				LOG_INFO(Core, "[Engine] /modular ignore (client.debug.modular_test=false)");
				return true;
			}
			// Genere 2 variantes placeholder a la 1re utilisation (mesh + device valides).
			if (m_placeholderParts.empty() && m_currentSkinnedMesh != nullptr)
			{
				const engine::render::skinned::Skeleton& skel = m_currentSkinnedMesh->skeleton;
				int headBone = skel.FindBoneIndex("mixamorig:Head");
				if (headBone < 0) headBone = skel.FindBoneIndex("head");
				if (headBone < 0) headBone = skel.FindBoneIndex("Head");
				if (headBone < 0) headBone = static_cast<int>(skel.bones.size()) - 1;
				const float sizes[2] = { 0.16f, 0.24f };
				for (int i = 0; i < 2; ++i)
				{
					engine::render::skinned::SkinnedMeshCpuData cpu =
						engine::render::skinned::MakePlaceholderBoxPart(skel, headBone, sizes[i]);
					auto mesh = std::make_unique<engine::render::skinned::SkinnedMesh>();
					if (mesh->Upload(m_vkDeviceContext.GetDevice(),
						m_vkDeviceContext.GetPhysicalDevice(), cpu))
					{
						m_placeholderParts.push_back(std::move(mesh));
					}
				}
				LOG_INFO(Render, "[Modular] {} partie(s) placeholder generee(s) (os tete={})",
					m_placeholderParts.size(), headBone);
			}
			// `text` est un std::string_view -> substr renvoie un string_view (pas de
			// conversion implicite vers std::string). find() marche sur string_view.
			const std::string_view arg = (text.size() > 9u) ? text.substr(9) : std::string_view{};
			if (arg.find("off") != std::string_view::npos)
			{
				m_modularAvatar.ClearPart(engine::render::skinned::EquipVisualSlot::Head);
				LOG_INFO(Render, "[Modular] tete retiree");
			}
			else if ((arg.find('b') != std::string::npos || arg.find('B') != std::string::npos)
				&& m_placeholderParts.size() >= 2)
			{
				(void)m_modularAvatar.SetPart(
					engine::render::skinned::EquipVisualSlot::Head, m_placeholderParts[1].get());
				LOG_INFO(Render, "[Modular] tete = variante B");
			}
			else if (!m_placeholderParts.empty())
			{
				(void)m_modularAvatar.SetPart(
					engine::render::skinned::EquipVisualSlot::Head, m_placeholderParts[0].get());
				LOG_INFO(Render, "[Modular] tete = variante A");
			}
			return true;
		}
		// CMANGOS.33 (Phase 5.33 step 3+4) — Slash command /lfg pour
		// ouvrir/fermer la fenetre LFG et synchroniser le status depuis le
		// master au moment de l'ouverture.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/lfg" || text.starts_with("/lfg ") || text.starts_with("/lfg\t")))
		{
			m_lfgVisible = !m_lfgVisible;
			if (m_lfgVisible)
			{
				m_lfgUi.RequestStatus();
			}
			LOG_INFO(Core, "[Engine] /lfg toggle (visible={})", m_lfgVisible);
			sendAdminAudit("/lfg");
			return true;
		}
		// CMANGOS.21 (Phase 5.21 step 3+4) — Slash command /arena pour
		// ouvrir/fermer la fenetre Arena et synchroniser la liste des
		// teams depuis le master au moment de l'ouverture. La touche A
		// fait la meme chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/arena" || text.starts_with("/arena ") || text.starts_with("/arena\t")))
		{
			m_arenaVisible = !m_arenaVisible;
			if (m_arenaVisible)
			{
				m_arenaUi.RequestTeams();
			}
			LOG_INFO(Core, "[Engine] /arena toggle (visible={})", m_arenaVisible);
			sendAdminAudit("/arena");
			return true;
		}
		// CMANGOS.10 (Phase 5 step 3+4) — Slash command /bg pour ouvrir/
		// fermer la fenetre BattleGround et synchroniser la liste depuis
		// le master au moment de l'ouverture. La touche G fait la meme
		// chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/bg" || text.starts_with("/bg ") || text.starts_with("/bg\t")))
		{
			m_battleGroundVisible = !m_battleGroundVisible;
			if (m_battleGroundVisible)
			{
				m_battleGroundUi.RequestList();
			}
			LOG_INFO(Core, "[Engine] /bg toggle (visible={})", m_battleGroundVisible);
			sendAdminAudit("/bg");
			return true;
		}
		// CMANGOS.36 (Phase 5.36 step 3+4) — Slash command /pvp pour
		// ouvrir/fermer la fenetre OutdoorPvp et synchroniser la liste
		// des zones depuis le master au moment de l'ouverture. La touche
		// P fait la meme chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/pvp" || text.starts_with("/pvp ") || text.starts_with("/pvp\t")))
		{
			m_outdoorPvpVisible = !m_outdoorPvpVisible;
			if (m_outdoorPvpVisible)
			{
				m_outdoorPvpUi.RequestList();
			}
			LOG_INFO(Core, "[Engine] /pvp toggle (visible={})", m_outdoorPvpVisible);
			sendAdminAudit("/pvp");
			return true;
		}
		// CMANGOS.42 (Phase 4.42 step 3+4) — Slash command /weather pour
		// ouvrir/fermer le panneau Weather et synchroniser la liste des
		// zones meteo depuis le master au moment de l'ouverture. La
		// touche Y fait la meme chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/weather" || text.starts_with("/weather ") || text.starts_with("/weather\t")))
		{
			m_weatherVisible = !m_weatherVisible;
			if (m_weatherVisible)
			{
				m_weatherUi.RequestList();
			}
			LOG_INFO(Core, "[Engine] /weather toggle (visible={})", m_weatherVisible);
			sendAdminAudit("/weather");
			return true;
		}
		// CMANGOS.31 (Phase 5.31 step 3+4) — Slash command /events pour
		// ouvrir/fermer le panneau GameEvents et synchroniser la liste
		// depuis le master au moment de l'ouverture. Le menu "Panneaux"
		// (barre de menus ImGui in-game) fait la meme chose (la touche E,
		// qui servait a cela, a ete liberee).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/events" || text.starts_with("/events ") || text.starts_with("/events\t")))
		{
			m_gameEventVisible = !m_gameEventVisible;
			if (m_gameEventVisible)
			{
				m_gameEventUi.RequestList();
			}
			LOG_INFO(Core, "[Engine] /events toggle (visible={})", m_gameEventVisible);
			sendAdminAudit("/events");
			return true;
		}
		// CMANGOS.21 (Phase 5.21 step 3+4 Guilds) — Slash command /guild
		// pour ouvrir/fermer le panneau Guildes et synchroniser la liste
		// depuis le master au moment de l'ouverture. La touche U fait
		// la meme chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/guild" || text == "/guilds"
			    || text.starts_with("/guild ") || text.starts_with("/guild\t")
			    || text.starts_with("/guilds ") || text.starts_with("/guilds\t")))
		{
			m_guildVisible = !m_guildVisible;
			if (m_guildVisible)
			{
				m_guildUi.RequestList();
			}
			LOG_INFO(Core, "[Engine] /guild toggle (visible={})", m_guildVisible);
			sendAdminAudit("/guild");
			return true;
		}
		// CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Slash command /ah
		// pour ouvrir/fermer le panneau Hotel des Ventes et synchroniser
		// la liste des encheres depuis le master au moment de l'ouverture.
		// La touche H fait la meme chose (cf. boucle input dans BeginFrame).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/ah" || text == "/auction"
			    || text.starts_with("/ah ") || text.starts_with("/ah\t")
			    || text.starts_with("/auction ") || text.starts_with("/auction\t")))
		{
			m_auctionHouseVisible = !m_auctionHouseVisible;
			if (m_auctionHouseVisible)
			{
				m_auctionHouseUi.RequestList(0u);
			}
			LOG_INFO(Core, "[Engine] /ah toggle (visible={})", m_auctionHouseVisible);
			sendAdminAudit("/ah");
			return true;
		}
		// CMANGOS.17 (Phase 3.17 step 3+4 Loot) — Slash command /loot
		// pour ouvrir/fermer la fenetre Loot Roll. La touche L fait la
		// meme chose (cf. boucle input dans BeginFrame).
		//
		// Wave 1 RBAC : /loot est admin-gated (le bouton Simulate Roll
		// bypass la logique loot serveur). On envoie une AdminCommand
		// au master pour audit + validation role administrator. Le
		// toggle UI s'applique localement independamment de la reponse
		// master (le panneau est purement visuel ; le bouton Simulate
		// reste lui-meme gate cote serveur via LootHandler).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/loot"
			    || text.starts_with("/loot ") || text.starts_with("/loot\t")))
		{
			engine::network::admin::AdminCommandRequest req;
			req.command = "/loot";
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			m_lootRollVisible = !m_lootRollVisible;
			LOG_INFO(Core, "[Engine] /loot toggle (visible={}) + admin audit envoye",
				m_lootRollVisible);
			return true;
		}
		// Phase 5 step 3+4 Lunar + M38.1 Sky : slash commands debug pour
		// inspecter et override le cycle jour/nuit + phase lunaire.
		// Wave 1 RBAC : toutes les sous-commandes /sky passent par
		// AdminCommand pour audit et validation role serveur (minRole
		// player pour /sky info, administrator pour /sky time et
		// /sky moon — cf. game/data/config/slash_commands.json).
		//   /sky info        : audit + applique l'affichage local sur ACK Ok.
		//   /sky time <h>    : audit + applique SetTime(h) sur ACK Ok.
		//   /sky moon <i>    : audit + OnLunarPhaseChange(i, calc(i)) sur ACK Ok.
		//
		// Chat echo immediat : chaque commande pousse une ligne sur le canal
		// Server (sender="[Sky]") via m_chatUi.PushNetworkLine pour feedback
		// utilisateur. La suite (Ok/Denied/etc.) est rendue par le case
		// kOpcodeAdminCommandResponse plus bas.
		auto pushSkyChatLine = [this](const char* fmt, auto... args) {
			char buf[256];
			std::snprintf(buf, sizeof(buf), fmt, args...);
			engine::net::ChatMessage msg;
			msg.timestampUnixMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch()).count());
			msg.channel = engine::net::ChatChannel::Server;
			msg.sender  = "[Sky]";
			msg.text    = buf;
			m_chatUi.PushNetworkLine(msg);
		};

		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/sky info" || text == "/sky"))
		{
			engine::network::admin::AdminCommandRequest req;
			req.command = "/sky info";
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Render, "[Sky] /sky info envoye au master (audit + RBAC)");
			pushSkyChatLine("/sky info envoye au master (audit + RBAC)");
			return true;
		}
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/sky time "))
		{
			const auto rest = text.substr(10);
			// Validation cliente legere : on echoue tot si la valeur n'est
			// pas un float dans [0..24). La validation autoritative reste
			// cote master (DispatchSkyTime).
			float hours = -1.0f;
			try { hours = std::stof(std::string(rest)); } catch (...) { hours = -1.0f; }
			if (!(hours >= 0.0f) || hours >= 24.0f)
			{
				LOG_WARN(Render, "[Sky] /sky time : '{}' hors plage [0..24)",
					std::string(rest));
				pushSkyChatLine("/sky time '%s' hors plage [0..24)", std::string(rest).c_str());
				return true;
			}
			engine::network::admin::AdminCommandRequest req;
			req.command = "/sky time";
			req.args.push_back(std::string(rest));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Render, "[Sky] /sky time {} envoye au master pour validation RBAC",
				std::string(rest));
			pushSkyChatLine("/sky time %.2fh envoye au master (validation RBAC en cours...)",
				static_cast<double>(hours));
			return true;
		}
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/sky moon "))
		{
			const auto rest = text.substr(10);
			int phase = 0;
			try { phase = std::stoi(std::string(rest)); } catch (...) { phase = 0; }
			if (phase < 0 || phase > 15)
			{
				LOG_WARN(Render, "[Sky] phase {} hors plage [0..15]", phase);
				pushSkyChatLine("phase %d hors plage [0..15]", phase);
				return true;
			}
			// AdminCommand RBAC pilot : /sky moon est admin-only. On envoie au
			// master qui valide le role + log audit + retourne Ok/Denied.
			// Le client applique l'override visuel SEULEMENT apres ACK Ok
			// (voir dispatch kOpcodeAdminCommandResponse plus bas).
			// Chat echo immediat pour feedback : la suite (Ok/Denied) sera
			// rendue par le case kOpcodeAdminCommandResponse.
			engine::network::admin::AdminCommandRequest req;
			req.command = "/sky moon";
			req.args.push_back(std::to_string(phase));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Render, "[Sky] /sky moon {} sent to master for RBAC validation", phase);
			pushSkyChatLine("/sky moon %d envoye au master (validation RBAC en cours...)", phase);
			return true;
		}
		// WorldClock client sync : commandes admin horloge monde. Calquees
		// EXACTEMENT sur /sky time et /sky moon : on intercepte le prefixe
		// tape, on extrait l'argument brut (texte apres la commande, trimme
		// des espaces de bord) et on envoie un AdminCommandRequest au master
		// via kOpcodeAdminCommandRequest (opcode 195). AUCUNE validation
		// metier cliente : le master valide RBAC + arguments, applique et
		// broadcast (205) ; le client verra l'effet via la sync horloge.
		// Le nom de commande envoye est la forme CANONIQUE (sans placeholder)
		// indexee par le registre serveur. La reponse 196 est traitee
		// generiquement par le case kOpcodeAdminCommandResponse (branche
		// "else" -> ligne chat OK, ou branche refus -> ligne chat DENIED).
		//
		// Petit helper local de trim (espaces de bord) pour extraire
		// l'argument tape proprement, sans dependre d'un util externe.
		auto trimEdges = [](std::string_view sv) -> std::string {
			const auto first = sv.find_first_not_of(" \t");
			if (first == std::string_view::npos)
				return std::string();
			const auto last = sv.find_last_not_of(" \t");
			return std::string(sv.substr(first, last - first + 1));
		};

		//   /settime HH:MM : regle l'heure du monde (master valide le format).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/settime "))
		{
			const std::string arg = trimEdges(text.substr(9));
			engine::network::admin::AdminCommandRequest req;
			req.command = "/settime";
			req.args.push_back(arg);
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Render, "[WorldClock] /settime '{}' envoye au master (validation RBAC)", arg);
			pushSkyChatLine("/settime %s envoye au master (validation RBAC en cours...)", arg.c_str());
			return true;
		}
		//   /pausetime on|off : met en pause ou reprend l'horloge monde.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/pausetime "))
		{
			const std::string arg = trimEdges(text.substr(11));
			engine::network::admin::AdminCommandRequest req;
			req.command = "/pausetime";
			req.args.push_back(arg);
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Render, "[WorldClock] /pausetime '{}' envoye au master (validation RBAC)", arg);
			pushSkyChatLine("/pausetime %s envoye au master (validation RBAC en cours...)", arg.c_str());
			return true;
		}
		//   /settimescale N : regle l'echelle de temps (min reels / jour).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/settimescale "))
		{
			const std::string arg = trimEdges(text.substr(14));
			engine::network::admin::AdminCommandRequest req;
			req.command = "/settimescale";
			req.args.push_back(arg);
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Render, "[WorldClock] /settimescale '{}' envoye au master (validation RBAC)", arg);
			pushSkyChatLine("/settimescale %s envoye au master (validation RBAC en cours...)", arg.c_str());
			return true;
		}
		// Wave 1 RBAC : /promote <account_id> <role> est un outil admin
		// permettant de promouvoir/retrograder un compte sans acces direct
		// DB. Le serveur valide le role administrator + applique la mise
		// a jour via AccountRoleService::SetRole (qui ecrit en DB + emet
		// l'audit role_change).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/promote "))
		{
			const auto rest = text.substr(9);
			const auto spacePos = rest.find(' ');
			if (spacePos == std::string_view::npos)
			{
				LOG_WARN(Core,
					"[Admin] /promote usage : /promote <account_id> <role>");
				return true;
			}
			std::string accIdStr(rest.substr(0, spacePos));
			std::string roleStr(rest.substr(spacePos + 1));
			engine::network::admin::AdminCommandRequest req;
			req.command = "/promote";
			req.args.push_back(accIdStr);
			req.args.push_back(roleStr);
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Admin] /promote {} -> {} envoye au master (RBAC + audit)",
				accIdStr, roleStr);
			return true;
		}
		// Wave 2 RBAC migration : /who (player) -> AdminCommand.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/who" || text.starts_with("/who ") || text.starts_with("/who\t")))
		{
			engine::network::admin::AdminCommandRequest req;
			req.command = "/who";
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /who sent to master");
			return true;
		}
		// Wave 2 RBAC migration : /report <player> (player) -> AdminCommand.
		// Cree un ticket GM cote master ; la reponse affiche le ticket_id.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/report "))
		{
			std::string_view rest = text.substr(8);
			while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
				rest.remove_prefix(1u);
			if (rest.empty())
			{
				LOG_WARN(Core, "[Engine] /report sans argument joueur");
				return true;
			}
			engine::network::admin::AdminCommandRequest req;
			req.command = "/report";
			req.args.push_back(std::string(rest));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /report {} sent to master", std::string(rest));
			return true;
		}
		// Wave 2 RBAC migration : /kick <player> (moderator) -> AdminCommand.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/kick "))
		{
			std::string_view rest = text.substr(6);
			while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
				rest.remove_prefix(1u);
			if (rest.empty())
			{
				LOG_WARN(Core, "[Engine] /kick sans argument joueur");
				return true;
			}
			engine::network::admin::AdminCommandRequest req;
			req.command = "/kick";
			req.args.push_back(std::string(rest));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /kick {} sent to master", std::string(rest));
			return true;
		}
		// Wave 2 RBAC migration : /mute <player> <duration_min> (moderator).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/mute "))
		{
			std::string_view rest = text.substr(6);
			while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
				rest.remove_prefix(1u);
			if (rest.empty())
			{
				LOG_WARN(Core, "[Engine] /mute sans arguments");
				return true;
			}
			// Split : premier mot = player, reste = duration (ou plus).
			auto firstSpace = rest.find_first_of(" \t");
			if (firstSpace == std::string_view::npos)
			{
				LOG_WARN(Core, "[Engine] /mute necessite <player> <duration_minutes>");
				return true;
			}
			std::string player(rest.substr(0, firstSpace));
			std::string_view durView = rest.substr(firstSpace + 1u);
			while (!durView.empty() && (durView.front() == ' ' || durView.front() == '\t'))
				durView.remove_prefix(1u);
			if (durView.empty())
			{
				LOG_WARN(Core, "[Engine] /mute : duree manquante");
				return true;
			}
			engine::network::admin::AdminCommandRequest req;
			req.command = "/mute";
			req.args.push_back(std::move(player));
			req.args.push_back(std::string(durView));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /mute sent to master");
			return true;
		}
		// Wave 2 RBAC migration : /ban <player> <reason...> (game_master).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/ban "))
		{
			std::string_view rest = text.substr(5);
			while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
				rest.remove_prefix(1u);
			if (rest.empty())
			{
				LOG_WARN(Core, "[Engine] /ban sans argument joueur");
				return true;
			}
			auto firstSpace = rest.find_first_of(" \t");
			std::string player;
			std::string reason;
			if (firstSpace == std::string_view::npos)
			{
				player = std::string(rest);
			}
			else
			{
				player = std::string(rest.substr(0, firstSpace));
				std::string_view reasonView = rest.substr(firstSpace + 1u);
				while (!reasonView.empty() && (reasonView.front() == ' ' || reasonView.front() == '\t'))
					reasonView.remove_prefix(1u);
				reason = std::string(reasonView);
			}
			engine::network::admin::AdminCommandRequest req;
			req.command = "/ban";
			req.args.push_back(std::move(player));
			if (!reason.empty())
				req.args.push_back(std::move(reason));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /ban sent to master");
			return true;
		}
		// ETAPE5.3 — /shutdown <secondes> [motif] | /shutdown cancel (administrator) :
		// arrêt ordonné du cluster, décidé et exécuté par le master (RBAC + audit).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/shutdown" || text.starts_with("/shutdown ")))
		{
			std::string_view rest = text.size() > 9u ? std::string_view(text).substr(10) : std::string_view{};
			while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
				rest.remove_prefix(1u);
			engine::network::admin::AdminCommandRequest req;
			req.command = "/shutdown";
			if (!rest.empty())
				req.args.push_back(std::string(rest));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /shutdown sent to master");
			return true;
		}
		// Wave 2 RBAC migration : /announce <message> (game_master).
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& text.starts_with("/announce "))
		{
			std::string_view rest = text.substr(10);
			while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
				rest.remove_prefix(1u);
			if (rest.empty())
			{
				LOG_WARN(Core, "[Engine] /announce : message vide");
				return true;
			}
			engine::network::admin::AdminCommandRequest req;
			req.command = "/announce";
			req.args.push_back(std::string(rest));
			std::vector<uint8_t> payload;
			engine::network::admin::BuildAdminCommandRequestPayload(req, payload);
			(void)m_authUi.SendGenericRequestAsync(
				engine::network::kOpcodeAdminCommandRequest, payload);
			LOG_INFO(Core, "[Engine] /announce sent to master");
			return true;
		}
		// CMANGOS.32 (Phase 5.32 step 3+4) — Slash command /ticket et /gmticket
		// pour ouvrir/fermer le panneau Support GM et synchroniser la liste
		// depuis le master au moment de l'ouverture.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/ticket" || text == "/gmticket"
			    || text.starts_with("/ticket ") || text.starts_with("/ticket\t")
			    || text.starts_with("/gmticket ") || text.starts_with("/gmticket\t")))
		{
			m_gmTicketsVisible = !m_gmTicketsVisible;
			if (m_gmTicketsVisible)
			{
				m_gmTicketUi.RequestMyTickets();
			}
			LOG_INFO(Core, "[Engine] /ticket toggle (visible={})", m_gmTicketsVisible);
			sendAdminAudit("/ticket");
			return true;
		}
		// CMANGOS.25 (Phase 3.25 step 3+4) — Slash commands /ignore et /unignore.
		// Format V1 : "/ignore <account_id>" et "/unignore <account_id>" — la
		// resolution par character_name viendra avec PartySystem display.
		// "/ignore list" sans argument refresh la cache depuis le master.
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text == "/ignore" || text.starts_with("/ignore ") || text.starts_with("/ignore\t")))
		{
			const auto spaceIdx = text.find_first_of(" \t");
			if (spaceIdx == std::string_view::npos)
			{
				// "/ignore" tout seul : refresh la liste.
				m_ignoreListUi.RequestIgnoreList();
				LOG_INFO(Core, "[Engine] /ignore (refresh list)");
				return true;
			}
			std::string_view arg = text.substr(spaceIdx + 1);
			while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
				arg.remove_prefix(1u);
			if (arg == "list" || arg.empty())
			{
				m_ignoreListUi.RequestIgnoreList();
				LOG_INFO(Core, "[Engine] /ignore list");
				return true;
			}
			// Parse account_id en uint64 (base 10).
			uint64_t targetAccountId = 0u;
			bool parsed = false;
			try
			{
				size_t pos = 0;
				const std::string argStr(arg);
				targetAccountId = std::stoull(argStr, &pos, 10);
				parsed = (pos > 0u && targetAccountId != 0u);
			}
			catch (...)
			{
				parsed = false;
			}
			if (!parsed)
			{
				LOG_WARN(Core, "[Engine] /ignore : argument '{}' n'est pas un account_id valide",
					std::string(arg));
				return true;
			}
			m_ignoreListUi.IgnoreAccount(targetAccountId);
			LOG_INFO(Core, "[Engine] /ignore {}", targetAccountId);
			return true;
		}
		if (channel == static_cast<uint8_t>(engine::net::ChatChannel::Say)
			&& (text.starts_with("/unignore ") || text.starts_with("/unignore\t")))
		{
			const auto spaceIdx = text.find_first_of(" \t");
			std::string_view arg = text.substr(spaceIdx + 1);
			while (!arg.empty() && (arg.front() == ' ' || arg.front() == '\t'))
				arg.remove_prefix(1u);
			uint64_t targetAccountId = 0u;
			bool parsed = false;
			try
			{
				size_t pos = 0;
				const std::string argStr(arg);
				targetAccountId = std::stoull(argStr, &pos, 10);
				parsed = (pos > 0u && targetAccountId != 0u);
			}
			catch (...)
			{
				parsed = false;
			}
			if (!parsed)
			{
				LOG_WARN(Core, "[Engine] /unignore : argument '{}' n'est pas un account_id valide",
					std::string(arg));
				return true;
			}
			m_ignoreListUi.UnignoreAccount(targetAccountId);
			LOG_INFO(Core, "[Engine] /unignore {}", targetAccountId);
			return true;
		}
		// ETAPE5.1 — Dire et Crier sont du chat de PROXIMITÉ : ils partent au shard
		// en UDP (MessageKind::ChatSend), qui les redistribue aux joueurs à portée
		// (25 m / 200 m). Les autres canaux (chuchotement, guilde, amis, global…)
		// restent routés par le master. Hors du monde (écran de sélection, pas de
		// session UDP), il n'y a pas de proximité : la ligne part sur le canal
		// Global du master, comme le salon d'attente. Une commande (« /help »…)
		// tapée sans préfixe arrive ici sur Dire : elle reste adressée au master
		// (ChatCommandRouter), qui ne la diffuse jamais.
		const uint8_t sayWire = engine::net::ToWire(engine::net::ChatChannel::Say);
		const uint8_t yellWire = engine::net::ToWire(engine::net::ChatChannel::Yell);
		const bool isSlashCommand = !text.empty() && text.front() == '/';
		if ((channel == sayWire || channel == yellWire) && !isSlashCommand)
		{
			const uint32_t udpClientId = m_gameplayUdp.ServerClientId();
			if (m_gameplayNetInitialized && m_gameplayUdp.IsActive() && udpClientId != 0u)
			{
				return m_gameplayUdp.SendChatSend(udpClientId, channel, text);
			}
			return m_authUi.SendChatAsync(engine::net::ToWire(engine::net::ChatChannel::Global), targetToken, text);
		}
		return m_authUi.SendChatAsync(channel, targetToken, text);
	}
} // namespace engine
