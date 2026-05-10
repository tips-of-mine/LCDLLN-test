/**
 * CMANGOS.21 (Phase 5.21 step 3+4) — Round-trip tests for ARENA_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/ArenaPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::network;

// -----------------------------------------------------------------------------
// ARENA_TEAM_LIST
// -----------------------------------------------------------------------------

static void TestTeamListRequestRoundTrip()
{
	auto buf = BuildArenaTeamListRequestPayload();
	Assert(buf.empty(), "TeamList request payload empty");
	auto parsed = ParseArenaTeamListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "TeamList request accepts empty payload");
}

static void TestTeamListResponseEmpty()
{
	auto buf = BuildArenaTeamListResponsePayload(0u, {});
	auto parsed = ParseArenaTeamListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->teams.empty(),
		"TeamList response empty parses");
}

static void TestTeamListResponseFull()
{
	std::vector<ArenaTeamSummary> teams;
	teams.push_back({1u, 2u, "LCDLLN A", 1500u, 0u, 0u});
	teams.push_back({2u, 3u, "LCDLLN B", 1700u, 5u, 3u});
	teams.push_back({3u, 5u, "LCDLLN C", 1500u, 0u, 0u});
	auto buf = BuildArenaTeamListResponsePayload(0u, teams);
	auto parsed = ParseArenaTeamListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "TeamList response full parses");
	if (parsed && parsed->teams.size() == 3u)
	{
		Assert(parsed->teams[0].teamId == 1u, "Team 1 id");
		Assert(parsed->teams[0].size == 2u, "Team 1 size 2");
		Assert(parsed->teams[0].name == "LCDLLN A", "Team 1 name");
		Assert(parsed->teams[0].rating == 1500u, "Team 1 rating");
		Assert(parsed->teams[1].size == 3u, "Team 2 size 3");
		Assert(parsed->teams[1].rating == 1700u, "Team 2 rating");
		Assert(parsed->teams[1].weeklyGames == 5u, "Team 2 weeklyGames");
		Assert(parsed->teams[1].weeklyWins == 3u, "Team 2 weeklyWins");
		Assert(parsed->teams[2].size == 5u, "Team 3 size 5");
	}
	else
	{
		Assert(false, "TeamList should have 3 teams");
	}
}

static void TestTeamListResponseError()
{
	auto buf = BuildArenaTeamListResponsePayload(
		static_cast<uint8_t>(ArenaErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "TeamList error has only 1 byte");
	auto parsed = ParseArenaTeamListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 7u, "TeamList Unauthorized");
}

static void TestTeamListResponseRejectsShort()
{
	auto parsed = ParseArenaTeamListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "TeamList rejects nullptr");
}

static void TestTeamListResponsePacket()
{
	std::vector<ArenaTeamSummary> teams;
	teams.push_back({42u, 3u, "Test", 2000u, 10u, 7u});
	auto pkt = BuildArenaTeamListResponsePacket(0u, teams, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "TeamList packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"TeamList PacketView parse OK");
	Assert(view.Opcode() == kOpcodeArenaTeamListResponse, "Opcode is ArenaTeamListResponse");
	Assert(view.RequestId() == 999u, "TeamList RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "TeamList SessionId preserved");
	auto parsed = ParseArenaTeamListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->teams.size() == 1u
		&& parsed->teams[0].teamId == 42u && parsed->teams[0].name == "Test",
		"TeamList payload decodes");
}

// -----------------------------------------------------------------------------
// ARENA_QUEUE
// -----------------------------------------------------------------------------

static void TestQueueRequestRoundTrip()
{
	auto buf = BuildArenaQueueRequestPayload(7u, 3u);
	Assert(buf.size() == 5u, "Queue request payload size 5");
	auto parsed = ParseArenaQueueRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Queue request parses");
	if (parsed)
	{
		Assert(parsed->teamId == 7u, "Queue teamId 7");
		Assert(parsed->size == 3u, "Queue size 3");
	}
}

static void TestQueueRequestBoundary()
{
	// size=2 valide.
	auto buf2 = BuildArenaQueueRequestPayload(0xFFFFFFFFu, 2u);
	auto parsed2 = ParseArenaQueueRequestPayload(buf2.data(), buf2.size());
	Assert(parsed2.has_value() && parsed2->teamId == 0xFFFFFFFFu && parsed2->size == 2u,
		"Queue boundary: teamId max + size 2");
	// size=5 valide.
	auto buf5 = BuildArenaQueueRequestPayload(1u, 5u);
	auto parsed5 = ParseArenaQueueRequestPayload(buf5.data(), buf5.size());
	Assert(parsed5.has_value() && parsed5->size == 5u, "Queue boundary: size 5");
	// size=0 (invalide cote handler mais wire l'accepte).
	auto buf0 = BuildArenaQueueRequestPayload(1u, 0u);
	auto parsed0 = ParseArenaQueueRequestPayload(buf0.data(), buf0.size());
	Assert(parsed0.has_value() && parsed0->size == 0u, "Queue wire accepts size 0");
}

static void TestQueueRequestRejectsShort()
{
	auto parsed = ParseArenaQueueRequestPayload(nullptr, 5u);
	Assert(!parsed.has_value(), "Queue request rejects nullptr");
	uint8_t shortBuf[4]{};
	auto parsed2 = ParseArenaQueueRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Queue request rejects 4 bytes");
}

static void TestQueueResponseOk()
{
	auto buf = BuildArenaQueueResponsePayload(0u, 30u);
	Assert(buf.size() == 5u, "Queue response Ok size 5");
	auto parsed = ParseArenaQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->estimatedWaitSec == 30u,
		"Queue response Ok parses");
}

static void TestQueueResponseError()
{
	auto buf = BuildArenaQueueResponsePayload(
		static_cast<uint8_t>(ArenaErrorCode::AlreadyQueued), 0u);
	Assert(buf.size() == 1u, "Queue response error has only 1 byte");
	auto parsed = ParseArenaQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Queue response AlreadyQueued");
}

static void TestQueueResponsePacket()
{
	auto pkt = BuildArenaQueueResponsePacket(0u, 60u, 12345u, 0xBEEFull);
	Assert(!pkt.empty(), "Queue packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Queue PacketView parse OK");
	Assert(view.Opcode() == kOpcodeArenaQueueResponse, "Opcode is ArenaQueueResponse");
	Assert(view.RequestId() == 12345u, "Queue RequestId preserved");
}

// -----------------------------------------------------------------------------
// ARENA_LEAVE_QUEUE
// -----------------------------------------------------------------------------

static void TestLeaveQueueRequestRoundTrip()
{
	auto buf = BuildArenaLeaveQueueRequestPayload();
	Assert(buf.empty(), "LeaveQueue request payload empty");
	auto parsed = ParseArenaLeaveQueueRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "LeaveQueue accepts empty payload");
}

static void TestLeaveQueueResponseOk()
{
	auto buf = BuildArenaLeaveQueueResponsePayload(0u);
	Assert(buf.size() == 1u, "LeaveQueue response Ok size 1");
	auto parsed = ParseArenaLeaveQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "LeaveQueue Ok parses");
}

static void TestLeaveQueueResponseNotInQueue()
{
	auto buf = BuildArenaLeaveQueueResponsePayload(
		static_cast<uint8_t>(ArenaErrorCode::NotInQueue));
	auto parsed = ParseArenaLeaveQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 4u, "LeaveQueue NotInQueue");
}

static void TestLeaveQueueResponsePacket()
{
	auto pkt = BuildArenaLeaveQueueResponsePacket(0u, 99u, 0xFEEDull);
	Assert(!pkt.empty(), "LeaveQueue packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"LeaveQueue PacketView parse OK");
	Assert(view.Opcode() == kOpcodeArenaLeaveQueueResponse, "Opcode is ArenaLeaveQueueResponse");
	Assert(view.RequestId() == 99u, "LeaveQueue RequestId preserved");
}

// -----------------------------------------------------------------------------
// ARENA_MATCH_PROPOSAL_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestMatchProposalRoundTrip()
{
	auto buf = BuildArenaMatchProposalNotificationPayload(123u, "AI Team Alpha", 1500u);
	auto parsed = ParseArenaMatchProposalNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "MatchProposal parses");
	if (parsed)
	{
		Assert(parsed->proposalId == 123u, "Proposal id 123");
		Assert(parsed->opponentTeamName == "AI Team Alpha", "Opponent name");
		Assert(parsed->opponentRating == 1500u, "Opponent rating");
	}
}

static void TestMatchProposalEmptyName()
{
	auto buf = BuildArenaMatchProposalNotificationPayload(7u, "", 0u);
	auto parsed = ParseArenaMatchProposalNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->opponentTeamName.empty()
		&& parsed->proposalId == 7u, "MatchProposal empty name");
}

static void TestMatchProposalLargeRating()
{
	auto buf = BuildArenaMatchProposalNotificationPayload(0xDEADBEEFu, "X", 3000u);
	auto parsed = ParseArenaMatchProposalNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->proposalId == 0xDEADBEEFu
		&& parsed->opponentRating == 3000u, "MatchProposal large rating");
}

static void TestMatchProposalRejectsShort()
{
	auto parsed = ParseArenaMatchProposalNotificationPayload(nullptr, 10u);
	Assert(!parsed.has_value(), "MatchProposal rejects nullptr");
	uint8_t shortBuf[9]{};
	auto parsed2 = ParseArenaMatchProposalNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MatchProposal rejects 9 bytes");
}

static void TestMatchProposalPacket()
{
	auto pkt = BuildArenaMatchProposalNotificationPacket(42u, "Beta", 1800u, 0xC0DEull);
	Assert(!pkt.empty(), "MatchProposal packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"MatchProposal PacketView parse OK");
	Assert(view.Opcode() == kOpcodeArenaMatchProposalNotification, "Opcode is MatchProposal");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xC0DEull, "MatchProposal SessionId preserved");
	auto parsed = ParseArenaMatchProposalNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->proposalId == 42u
		&& parsed->opponentTeamName == "Beta" && parsed->opponentRating == 1800u,
		"MatchProposal payload decodes");
}

// -----------------------------------------------------------------------------
// ARENA_MATCH_ACCEPT
// -----------------------------------------------------------------------------

static void TestMatchAcceptRequestRoundTrip()
{
	auto buf = BuildArenaMatchAcceptRequestPayload(99u, true);
	Assert(buf.size() == 5u, "MatchAccept request size 5");
	auto parsed = ParseArenaMatchAcceptRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "MatchAccept request parses");
	if (parsed)
	{
		Assert(parsed->proposalId == 99u, "MatchAccept proposalId");
		Assert(parsed->accept == true, "MatchAccept accept true");
	}
}

static void TestMatchAcceptRequestReject()
{
	auto buf = BuildArenaMatchAcceptRequestPayload(0xDEADu, false);
	auto parsed = ParseArenaMatchAcceptRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->accept == false, "MatchAccept reject");
}

static void TestMatchAcceptRequestRejectsShort()
{
	auto parsed = ParseArenaMatchAcceptRequestPayload(nullptr, 5u);
	Assert(!parsed.has_value(), "MatchAccept rejects nullptr");
	uint8_t shortBuf[4]{};
	auto parsed2 = ParseArenaMatchAcceptRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MatchAccept rejects 4 bytes");
}

static void TestMatchAcceptResponseOk()
{
	auto buf = BuildArenaMatchAcceptResponsePayload(0u);
	Assert(buf.size() == 1u, "MatchAccept response Ok size 1");
	auto parsed = ParseArenaMatchAcceptResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "MatchAccept response Ok");
}

static void TestMatchAcceptResponseExpired()
{
	auto buf = BuildArenaMatchAcceptResponsePayload(
		static_cast<uint8_t>(ArenaErrorCode::ProposalExpired));
	auto parsed = ParseArenaMatchAcceptResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 5u, "MatchAccept ProposalExpired");
}

static void TestMatchAcceptResponsePacket()
{
	auto pkt = BuildArenaMatchAcceptResponsePacket(0u, 7u, 0xABCDull);
	Assert(!pkt.empty(), "MatchAccept response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"MatchAccept PacketView parse OK");
	Assert(view.Opcode() == kOpcodeArenaMatchAcceptResponse, "Opcode is MatchAcceptResponse");
}

// -----------------------------------------------------------------------------
// ARENA_MATCH_RESULT_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestMatchResultWin()
{
	auto buf = BuildArenaMatchResultNotificationPayload(true, 1500u, 1512u, "AI Team Alpha");
	auto parsed = ParseArenaMatchResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "MatchResult win parses");
	if (parsed)
	{
		Assert(parsed->win == true, "MatchResult win true");
		Assert(parsed->oldRating == 1500u, "MatchResult oldRating");
		Assert(parsed->newRating == 1512u, "MatchResult newRating");
		Assert(parsed->opponentName == "AI Team Alpha", "MatchResult opponent name");
	}
}

static void TestMatchResultLoss()
{
	auto buf = BuildArenaMatchResultNotificationPayload(false, 1500u, 1488u, "AI Team Beta");
	auto parsed = ParseArenaMatchResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->win == false
		&& parsed->newRating == 1488u && parsed->oldRating == 1500u,
		"MatchResult loss");
}

static void TestMatchResultRejectsShort()
{
	auto parsed = ParseArenaMatchResultNotificationPayload(nullptr, 11u);
	Assert(!parsed.has_value(), "MatchResult rejects nullptr");
	uint8_t shortBuf[10]{};
	auto parsed2 = ParseArenaMatchResultNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MatchResult rejects 10 bytes");
}

static void TestMatchResultPacket()
{
	auto pkt = BuildArenaMatchResultNotificationPacket(true, 1500u, 1532u, "Gamma", 0xFADEull);
	Assert(!pkt.empty(), "MatchResult packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"MatchResult PacketView parse OK");
	Assert(view.Opcode() == kOpcodeArenaMatchResultNotification, "Opcode is MatchResultNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xFADEull, "MatchResult SessionId preserved");
	auto parsed = ParseArenaMatchResultNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->win == true && parsed->newRating == 1532u
		&& parsed->opponentName == "Gamma", "MatchResult payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestTeamListRequestRoundTrip();
	TestTeamListResponseEmpty();
	TestTeamListResponseFull();
	TestTeamListResponseError();
	TestTeamListResponseRejectsShort();
	TestTeamListResponsePacket();

	TestQueueRequestRoundTrip();
	TestQueueRequestBoundary();
	TestQueueRequestRejectsShort();
	TestQueueResponseOk();
	TestQueueResponseError();
	TestQueueResponsePacket();

	TestLeaveQueueRequestRoundTrip();
	TestLeaveQueueResponseOk();
	TestLeaveQueueResponseNotInQueue();
	TestLeaveQueueResponsePacket();

	TestMatchProposalRoundTrip();
	TestMatchProposalEmptyName();
	TestMatchProposalLargeRating();
	TestMatchProposalRejectsShort();
	TestMatchProposalPacket();

	TestMatchAcceptRequestRoundTrip();
	TestMatchAcceptRequestReject();
	TestMatchAcceptRequestRejectsShort();
	TestMatchAcceptResponseOk();
	TestMatchAcceptResponseExpired();
	TestMatchAcceptResponsePacket();

	TestMatchResultWin();
	TestMatchResultLoss();
	TestMatchResultRejectsShort();
	TestMatchResultPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all arena payload tests passed\n"
	                                : "[FAIL] some arena tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
