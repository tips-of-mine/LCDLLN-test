// RetiredGameplayHandlerTests — ETAPE1.1 : vérifie la liste des opcodes de gameplay
// retirés du master (relais pur). Pas de framework : code retour 0 si tout passe.

#include "src/masterd/handlers/retired/RetiredGameplayHandler.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <iostream>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << '\n';
		}
	}
}

int main()
{
	using namespace engine::network;
	using engine::server::IsRetiredGameplayOpcode;
	using engine::server::RetiredGameplayFeature;

	// Fonctions retirées : une requête de chaque famille.
	Check(IsRetiredGameplayOpcode(kOpcodeTradeBeginRequest), "trade retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeTradeCancelRequest), "trade cancel retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeLfgQueueRequest), "lfg retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeSkillsListRequest), "skills retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeArenaQueueRequest), "arena retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeBgQueueRequest), "battleground retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeOutdoorPvpCaptureStartRequest), "outdoor pvp retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeWeatherSubscribeRequest), "weather retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeGameEventListRequest), "game events retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeGuildListRequest), "guild V1 retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeAuctionPostRequest), "auction V1 retiré");
	Check(IsRetiredGameplayOpcode(kOpcodeLootRollChoiceRequest), "loot retiré");
	Check(IsRetiredGameplayOpcode(kOpInteractiveStateChange), "interactive retiré");
	Check(RetiredGameplayFeature(kOpcodeAuctionBidRequest) == "auction", "nom de fonction auction");

	// Fonctions conservées par le master (compte, persistance, relais) : jamais retirées.
	Check(!IsRetiredGameplayOpcode(kOpcodeAuthRequest), "auth conservé");
	Check(!IsRetiredGameplayOpcode(kOpcodeCharacterEnterWorldRequest), "enter world conservé");
	Check(!IsRetiredGameplayOpcode(kOpcodeChatSendRequest), "chat conservé");
	Check(!IsRetiredGameplayOpcode(kOpcodeMailSendRequest), "mail conservé");
	Check(!IsRetiredGameplayOpcode(kOpcodeReputationListRequest), "réputation conservée");
	Check(!IsRetiredGameplayOpcode(kOpcodeCinematicAckRequest), "cinématiques conservées");
	Check(!IsRetiredGameplayOpcode(kOpcodeLunarStateRequest), "lune conservée");
	Check(!IsRetiredGameplayOpcode(kOpcodeWorldClockStateRequest), "horloge conservée");
	Check(!IsRetiredGameplayOpcode(kOpcodeAdminCommandRequest), "admin conservé");
	Check(RetiredGameplayFeature(kOpcodeAuthRequest).empty(), "pas de nom pour un opcode conservé");

	if (s_failCount == 0)
		std::cout << "[OK] retired_gameplay_handler_tests\n";
	return s_failCount == 0 ? 0 : 1;
}
