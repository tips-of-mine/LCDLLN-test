// RetiredGameplayOpcodes — liste des requêtes client des fonctions de gameplay
// retirées du master (ETAPE1.1). Fichier sans dépendance réseau : compilé aussi
// par retired_gameplay_handler_tests.

#include "src/masterd/handlers/retired/RetiredGameplayHandler.h"

#include "src/shared/network/ProtocolV1Constants.h"

namespace engine::server
{
	namespace
	{
		struct RetiredOpcode
		{
			uint16_t opcode;
			std::string_view feature;
		};

		using namespace engine::network;

		/// Requêtes client des fonctions retirées (les réponses et pushes associés
		/// ne sont plus émis par le master). Ordre : celui de l'ancien aiguillage.
		constexpr RetiredOpcode kRetiredOpcodes[] = {
			{kOpcodeTradeBeginRequest, "trade"},
			{kOpcodeTradeSetOfferRequest, "trade"},
			{kOpcodeTradeLockRequest, "trade"},
			{kOpcodeTradeCommitRequest, "trade"},
			{kOpcodeTradeCancelRequest, "trade"},
			{kOpcodeLfgQueueRequest, "lfg"},
			{kOpcodeLfgLeaveRequest, "lfg"},
			{kOpcodeLfgStatusRequest, "lfg"},
			{kOpcodeLfgMatchAcceptRequest, "lfg"},
			{kOpcodeSkillsListRequest, "skills"},
			{kOpcodeSkillLearnRequest, "skills"},
			{kOpcodeSkillUseRequest, "skills"},
			{kOpcodeArenaTeamListRequest, "arena"},
			{kOpcodeArenaQueueRequest, "arena"},
			{kOpcodeArenaLeaveQueueRequest, "arena"},
			{kOpcodeArenaMatchAcceptRequest, "arena"},
			{kOpcodeBgListRequest, "battleground"},
			{kOpcodeBgQueueRequest, "battleground"},
			{kOpcodeBgLeaveQueueRequest, "battleground"},
			{kOpcodeBgLeaveMatchRequest, "battleground"},
			{kOpcodeOutdoorPvpZoneListRequest, "outdoor_pvp"},
			{kOpcodeOutdoorPvpSubscribeRequest, "outdoor_pvp"},
			{kOpcodeOutdoorPvpUnsubscribeRequest, "outdoor_pvp"},
			{kOpcodeOutdoorPvpCaptureStartRequest, "outdoor_pvp"},
			{kOpcodeWeatherListRequest, "weather"},
			{kOpcodeWeatherSubscribeRequest, "weather"},
			{kOpcodeWeatherUnsubscribeRequest, "weather"},
			{kOpcodeGameEventListRequest, "game_events"},
			{kOpcodeGameEventSubscribeRequest, "game_events"},
			{kOpcodeGameEventUnsubscribeRequest, "game_events"},
			{kOpcodeGuildListRequest, "guild"},
			{kOpcodeGuildMembersRequest, "guild"},
			{kOpcodeGuildPermissionsRequest, "guild"},
			{kOpcodeGuildBankRequest, "guild"},
			{kOpcodeAuctionListRequest, "auction"},
			{kOpcodeAuctionPostRequest, "auction"},
			{kOpcodeAuctionBidRequest, "auction"},
			{kOpcodeAuctionCancelRequest, "auction"},
			{kOpcodeLootRollChoiceRequest, "loot"},
			{kOpcodeLootSimulateRollRequest, "loot"},
			{kOpInteractiveStateChange, "interactive"},
		};
	}

	std::string_view RetiredGameplayFeature(uint16_t opcode)
	{
		for (const RetiredOpcode& r : kRetiredOpcodes)
		{
			if (r.opcode == opcode)
				return r.feature;
		}
		return {};
	}

	bool IsRetiredGameplayOpcode(uint16_t opcode)
	{
		return !RetiredGameplayFeature(opcode).empty();
	}
}
