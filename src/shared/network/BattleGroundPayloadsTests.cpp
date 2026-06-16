/**
 * CMANGOS.10 (Phase 5 step 3+4) — Round-trip tests for BG_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/BattleGroundPayloads.h"
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
// BG_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildBgListRequestPayload();
	Assert(buf.empty(), "BgList request payload empty");
	auto parsed = ParseBgListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "BgList request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildBgListResponsePayload(0u, {});
	auto parsed = ParseBgListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->battlegrounds.empty(),
		"BgList response empty parses");
}

static void TestListResponseFull()
{
	std::vector<BgInfo> bgs;
	bgs.push_back({1u, "Gorge de Feyhin", 10u, "gorge_feyhin"});
	bgs.push_back({2u, "Bassin des Ombres",  15u, "bassin_ombres"});
	bgs.push_back({3u, "Vallee Gelee", 40u, "vallee_gelee"});
	auto buf = BuildBgListResponsePayload(0u, bgs);
	auto parsed = ParseBgListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "BgList response full parses");
	if (parsed && parsed->battlegrounds.size() == 3u)
	{
		Assert(parsed->battlegrounds[0].bgType == 1u, "BG 1 bgType");
		Assert(parsed->battlegrounds[0].name == "Gorge de Feyhin", "BG 1 name");
		Assert(parsed->battlegrounds[0].teamSize == 10u, "BG 1 teamSize");
		Assert(parsed->battlegrounds[0].mapName == "gorge_feyhin", "BG 1 map");
		Assert(parsed->battlegrounds[1].bgType == 2u, "BG 2 bgType");
		Assert(parsed->battlegrounds[1].teamSize == 15u, "BG 2 teamSize");
		Assert(parsed->battlegrounds[2].teamSize == 40u, "BG 3 teamSize");
		Assert(parsed->battlegrounds[2].mapName == "vallee_gelee", "BG 3 map");
	}
	else
	{
		Assert(false, "BgList should have 3 battlegrounds");
	}
}

static void TestListResponseError()
{
	auto buf = BuildBgListResponsePayload(
		static_cast<uint8_t>(BgErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "BgList error has only 1 byte");
	auto parsed = ParseBgListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 5u, "BgList Unauthorized");
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseBgListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "BgList rejects nullptr");
}

static void TestListResponsePacket()
{
	std::vector<BgInfo> bgs;
	bgs.push_back({42u, "Test BG", 20u, "testmap"});
	auto pkt = BuildBgListResponsePacket(0u, bgs, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "BgList packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"BgList PacketView parse OK");
	Assert(view.Opcode() == kOpcodeBgListResponse, "Opcode is BgListResponse");
	Assert(view.RequestId() == 999u, "BgList RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "BgList SessionId preserved");
	auto parsed = ParseBgListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->battlegrounds.size() == 1u
		&& parsed->battlegrounds[0].bgType == 42u && parsed->battlegrounds[0].name == "Test BG",
		"BgList payload decodes");
}

// -----------------------------------------------------------------------------
// BG_QUEUE
// -----------------------------------------------------------------------------

static void TestQueueRequestRoundTrip()
{
	auto buf = BuildBgQueueRequestPayload(2u, 1u);
	Assert(buf.size() == 3u, "BgQueue request payload size 3");
	auto parsed = ParseBgQueueRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "BgQueue request parses");
	if (parsed)
	{
		Assert(parsed->bgType == 2u, "BgQueue bgType 2");
		Assert(parsed->faction == 1u, "BgQueue faction Horde");
	}
}

static void TestQueueRequestBoundary()
{
	// Alliance bgType=1.
	auto bufA = BuildBgQueueRequestPayload(1u, 0u);
	auto parsedA = ParseBgQueueRequestPayload(bufA.data(), bufA.size());
	Assert(parsedA.has_value() && parsedA->bgType == 1u && parsedA->faction == 0u,
		"BgQueue boundary: Gorge de Feyhin Alliance");
	// Max bgType.
	auto bufMax = BuildBgQueueRequestPayload(0xFFFFu, 1u);
	auto parsedMax = ParseBgQueueRequestPayload(bufMax.data(), bufMax.size());
	Assert(parsedMax.has_value() && parsedMax->bgType == 0xFFFFu,
		"BgQueue boundary: bgType max");
}

static void TestQueueRequestRejectsShort()
{
	auto parsed = ParseBgQueueRequestPayload(nullptr, 3u);
	Assert(!parsed.has_value(), "BgQueue request rejects nullptr");
	uint8_t shortBuf[2]{};
	auto parsed2 = ParseBgQueueRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "BgQueue request rejects 2 bytes");
}

static void TestQueueResponseOk()
{
	auto buf = BuildBgQueueResponsePayload(0u, 30u, 5u);
	Assert(buf.size() == 9u, "BgQueue response Ok size 9");
	auto parsed = ParseBgQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u
		&& parsed->estimatedWaitSec == 30u && parsed->queuePosition == 5u,
		"BgQueue response Ok parses");
}

static void TestQueueResponseError()
{
	auto buf = BuildBgQueueResponsePayload(
		static_cast<uint8_t>(BgErrorCode::AlreadyQueued), 0u, 0u);
	Assert(buf.size() == 1u, "BgQueue response error has only 1 byte");
	auto parsed = ParseBgQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "BgQueue response AlreadyQueued");
}

static void TestQueueResponseUnknownBg()
{
	auto buf = BuildBgQueueResponsePayload(
		static_cast<uint8_t>(BgErrorCode::UnknownBg), 0u, 0u);
	auto parsed = ParseBgQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "BgQueue response UnknownBg");
}

static void TestQueueResponseInvalidFaction()
{
	auto buf = BuildBgQueueResponsePayload(
		static_cast<uint8_t>(BgErrorCode::InvalidFaction), 0u, 0u);
	auto parsed = ParseBgQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "BgQueue response InvalidFaction");
}

static void TestQueueResponsePacket()
{
	auto pkt = BuildBgQueueResponsePacket(0u, 60u, 1u, 12345u, 0xBEEFull);
	Assert(!pkt.empty(), "BgQueue packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"BgQueue PacketView parse OK");
	Assert(view.Opcode() == kOpcodeBgQueueResponse, "Opcode is BgQueueResponse");
	Assert(view.RequestId() == 12345u, "BgQueue RequestId preserved");
}

// -----------------------------------------------------------------------------
// BG_LEAVE_QUEUE
// -----------------------------------------------------------------------------

static void TestLeaveQueueRequestRoundTrip()
{
	auto buf = BuildBgLeaveQueueRequestPayload();
	Assert(buf.empty(), "BgLeaveQueue request payload empty");
	auto parsed = ParseBgLeaveQueueRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "BgLeaveQueue accepts empty payload");
}

static void TestLeaveQueueResponseOk()
{
	auto buf = BuildBgLeaveQueueResponsePayload(0u);
	Assert(buf.size() == 1u, "BgLeaveQueue response Ok size 1");
	auto parsed = ParseBgLeaveQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "BgLeaveQueue Ok parses");
}

static void TestLeaveQueueResponseNotInQueue()
{
	auto buf = BuildBgLeaveQueueResponsePayload(
		static_cast<uint8_t>(BgErrorCode::NotInQueue));
	auto parsed = ParseBgLeaveQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 4u, "BgLeaveQueue NotInQueue");
}

static void TestLeaveQueueResponsePacket()
{
	auto pkt = BuildBgLeaveQueueResponsePacket(0u, 99u, 0xFEEDull);
	Assert(!pkt.empty(), "BgLeaveQueue packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"BgLeaveQueue PacketView parse OK");
	Assert(view.Opcode() == kOpcodeBgLeaveQueueResponse, "Opcode is BgLeaveQueueResponse");
	Assert(view.RequestId() == 99u, "BgLeaveQueue RequestId preserved");
}

// -----------------------------------------------------------------------------
// BG_MATCH_START_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestMatchStartRoundTrip()
{
	auto buf = BuildBgMatchStartNotificationPayload(123ull, 1u, "gorge_feyhin", 10u, 10u);
	auto parsed = ParseBgMatchStartNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "BgMatchStart parses");
	if (parsed)
	{
		Assert(parsed->matchId == 123ull, "MatchStart matchId");
		Assert(parsed->bgType == 1u, "MatchStart bgType");
		Assert(parsed->mapName == "gorge_feyhin", "MatchStart map");
		Assert(parsed->allianceCount == 10u, "MatchStart alliance count");
		Assert(parsed->hordeCount == 10u, "MatchStart horde count");
	}
}

static void TestMatchStartEmptyMap()
{
	auto buf = BuildBgMatchStartNotificationPayload(7ull, 0u, "", 0u, 0u);
	auto parsed = ParseBgMatchStartNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->mapName.empty()
		&& parsed->matchId == 7ull && parsed->allianceCount == 0u && parsed->hordeCount == 0u,
		"MatchStart empty map + counts 0");
}

static void TestMatchStartLargeCounts()
{
	auto buf = BuildBgMatchStartNotificationPayload(0xDEADBEEFCAFEull, 3u, "vallee_gelee", 40u, 40u);
	auto parsed = ParseBgMatchStartNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->matchId == 0xDEADBEEFCAFEull
		&& parsed->allianceCount == 40u && parsed->hordeCount == 40u, "MatchStart large counts");
}

static void TestMatchStartRejectsShort()
{
	auto parsed = ParseBgMatchStartNotificationPayload(nullptr, 14u);
	Assert(!parsed.has_value(), "MatchStart rejects nullptr");
	uint8_t shortBuf[13]{};
	auto parsed2 = ParseBgMatchStartNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MatchStart rejects 13 bytes");
}

static void TestMatchStartPacket()
{
	auto pkt = BuildBgMatchStartNotificationPacket(42ull, 2u, "bassin_ombres", 15u, 15u, 0xC0DEull);
	Assert(!pkt.empty(), "MatchStart packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"MatchStart PacketView parse OK");
	Assert(view.Opcode() == kOpcodeBgMatchStartNotification, "Opcode is MatchStart");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xC0DEull, "MatchStart SessionId preserved");
	auto parsed = ParseBgMatchStartNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->matchId == 42ull
		&& parsed->bgType == 2u && parsed->mapName == "bassin_ombres",
		"MatchStart payload decodes");
}

// -----------------------------------------------------------------------------
// BG_SCORE_UPDATE_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestScoreUpdateRoundTrip()
{
	auto buf = BuildBgScoreUpdateNotificationPayload(123ull, 500u, 250u, 60u);
	Assert(buf.size() == 20u, "ScoreUpdate payload size 20");
	auto parsed = ParseBgScoreUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ScoreUpdate parses");
	if (parsed)
	{
		Assert(parsed->matchId == 123ull, "ScoreUpdate matchId");
		Assert(parsed->allianceScore == 500u, "ScoreUpdate alliance");
		Assert(parsed->hordeScore == 250u, "ScoreUpdate horde");
		Assert(parsed->elapsedSec == 60u, "ScoreUpdate elapsed");
	}
}

static void TestScoreUpdateZero()
{
	auto buf = BuildBgScoreUpdateNotificationPayload(1ull, 0u, 0u, 0u);
	auto parsed = ParseBgScoreUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->allianceScore == 0u && parsed->hordeScore == 0u
		&& parsed->elapsedSec == 0u, "ScoreUpdate zero");
}

static void TestScoreUpdateMax()
{
	auto buf = BuildBgScoreUpdateNotificationPayload(0xFFFFFFFFFFFFFFFFull,
		0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
	auto parsed = ParseBgScoreUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->matchId == 0xFFFFFFFFFFFFFFFFull
		&& parsed->allianceScore == 0xFFFFFFFFu, "ScoreUpdate max");
}

static void TestScoreUpdateRejectsShort()
{
	auto parsed = ParseBgScoreUpdateNotificationPayload(nullptr, 20u);
	Assert(!parsed.has_value(), "ScoreUpdate rejects nullptr");
	uint8_t shortBuf[19]{};
	auto parsed2 = ParseBgScoreUpdateNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "ScoreUpdate rejects 19 bytes");
}

static void TestScoreUpdatePacket()
{
	auto pkt = BuildBgScoreUpdateNotificationPacket(7ull, 100u, 200u, 30u, 0xFADEull);
	Assert(!pkt.empty(), "ScoreUpdate packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"ScoreUpdate PacketView parse OK");
	Assert(view.Opcode() == kOpcodeBgScoreUpdateNotification, "Opcode is ScoreUpdate");
	Assert(view.RequestId() == 0u, "Push requestId=0");
}

// -----------------------------------------------------------------------------
// BG_MATCH_END_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestMatchEndAlliance()
{
	auto buf = BuildBgMatchEndNotificationPayload(123ull, 0u, 1500u, 0u, 225u);
	Assert(buf.size() == 21u, "MatchEnd payload size 21");
	auto parsed = ParseBgMatchEndNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "MatchEnd alliance parses");
	if (parsed)
	{
		Assert(parsed->matchId == 123ull, "MatchEnd matchId");
		Assert(parsed->winnerFaction == 0u, "MatchEnd Alliance wins");
		Assert(parsed->allianceScore == 1500u, "MatchEnd alliance score");
		Assert(parsed->hordeScore == 0u, "MatchEnd horde score");
		Assert(parsed->durationSec == 225u, "MatchEnd duration");
	}
}

static void TestMatchEndHorde()
{
	auto buf = BuildBgMatchEndNotificationPayload(456ull, 1u, 200u, 1500u, 600u);
	auto parsed = ParseBgMatchEndNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->winnerFaction == 1u
		&& parsed->hordeScore == 1500u, "MatchEnd Horde wins");
}

static void TestMatchEndDraw()
{
	auto buf = BuildBgMatchEndNotificationPayload(789ull, 2u, 750u, 750u, 1200u);
	auto parsed = ParseBgMatchEndNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->winnerFaction == 2u, "MatchEnd Draw");
}

static void TestMatchEndRejectsShort()
{
	auto parsed = ParseBgMatchEndNotificationPayload(nullptr, 21u);
	Assert(!parsed.has_value(), "MatchEnd rejects nullptr");
	uint8_t shortBuf[20]{};
	auto parsed2 = ParseBgMatchEndNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MatchEnd rejects 20 bytes");
}

static void TestMatchEndPacket()
{
	auto pkt = BuildBgMatchEndNotificationPacket(99ull, 0u, 800u, 600u, 540u, 0xABCDull);
	Assert(!pkt.empty(), "MatchEnd packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"MatchEnd PacketView parse OK");
	Assert(view.Opcode() == kOpcodeBgMatchEndNotification, "Opcode is MatchEnd");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	auto parsed = ParseBgMatchEndNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->matchId == 99ull && parsed->winnerFaction == 0u,
		"MatchEnd payload decodes");
}

// -----------------------------------------------------------------------------
// BG_LEAVE_MATCH (fire-and-forget)
// -----------------------------------------------------------------------------

static void TestLeaveMatchRequestRoundTrip()
{
	auto buf = BuildBgLeaveMatchRequestPayload();
	Assert(buf.empty(), "BgLeaveMatch request payload empty");
	auto parsed = ParseBgLeaveMatchRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "BgLeaveMatch accepts empty payload");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseFull();
	TestListResponseError();
	TestListResponseRejectsShort();
	TestListResponsePacket();

	TestQueueRequestRoundTrip();
	TestQueueRequestBoundary();
	TestQueueRequestRejectsShort();
	TestQueueResponseOk();
	TestQueueResponseError();
	TestQueueResponseUnknownBg();
	TestQueueResponseInvalidFaction();
	TestQueueResponsePacket();

	TestLeaveQueueRequestRoundTrip();
	TestLeaveQueueResponseOk();
	TestLeaveQueueResponseNotInQueue();
	TestLeaveQueueResponsePacket();

	TestMatchStartRoundTrip();
	TestMatchStartEmptyMap();
	TestMatchStartLargeCounts();
	TestMatchStartRejectsShort();
	TestMatchStartPacket();

	TestScoreUpdateRoundTrip();
	TestScoreUpdateZero();
	TestScoreUpdateMax();
	TestScoreUpdateRejectsShort();
	TestScoreUpdatePacket();

	TestMatchEndAlliance();
	TestMatchEndHorde();
	TestMatchEndDraw();
	TestMatchEndRejectsShort();
	TestMatchEndPacket();

	TestLeaveMatchRequestRoundTrip();

	std::cerr << (s_failCount == 0 ? "[OK] all battleground payload tests passed\n"
	                                : "[FAIL] some battleground tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
