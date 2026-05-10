/**
 * CMANGOS.27 (Phase 4.27 step 3+4) -- Round-trip tests for TRADE_*_REQUEST /
 * TRADE_*_RESPONSE / TRADE_*_NOTIFICATION payloads. Pure encoding tests
 * (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 *
 * Test policy : symetrique aux GmTicketPayloadsTests.cpp -- pas de framework
 * de test externe, juste un Assert local + main() qui appelle chaque suite.
 */

#include "src/shared/network/TradePayloads.h"
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
// TRADE_BEGIN
// -----------------------------------------------------------------------------

static void TestBeginRequestRoundTrip()
{
	auto buf = BuildTradeBeginRequestPayload(424242ull);
	Assert(buf.size() == 8u, "Begin request is 8 bytes");
	auto parsed = ParseTradeBeginRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->targetAccountId == 424242ull, "Begin request round-trips");
}

static void TestBeginRequestRejectsShort()
{
	uint8_t shortBuf[7]{};
	auto parsed = ParseTradeBeginRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Begin request rejects 7 bytes");
	auto parsed2 = ParseTradeBeginRequestPayload(nullptr, 8u);
	Assert(!parsed2.has_value(), "Begin request rejects nullptr");
}

static void TestBeginResponseRoundTrip()
{
	auto buf = BuildTradeBeginResponsePayload(0u, 17ull, 99ull);
	Assert(buf.size() == 17u, "Begin response is 17 bytes (1 + 8 + 8)");
	auto parsed = ParseTradeBeginResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Begin response parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Begin response error 0");
		Assert(parsed->sessionId == 17ull, "Begin response sessionId");
		Assert(parsed->partnerAccountId == 99ull, "Begin response partner");
	}
}

static void TestBeginResponseError()
{
	auto buf = BuildTradeBeginResponsePayload(
		static_cast<uint8_t>(TradeErrorCode::PartnerOffline), 0ull, 0ull);
	auto parsed = ParseTradeBeginResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "PartnerOffline decodes");
}

static void TestBeginResponsePacket()
{
	auto pkt = BuildTradeBeginResponsePacket(0u, 17ull, 99ull, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Begin response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeTradeBeginResponse, "Opcode is TradeBeginResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseTradeBeginResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->sessionId == 17ull, "payload decodes");
}

static void TestBeginNotificationRoundTrip()
{
	auto buf = BuildTradeBeginNotificationPayload(17ull, 42ull);
	Assert(buf.size() == 16u, "BeginNotification is 16 bytes");
	auto parsed = ParseTradeBeginNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "BeginNotification parses");
	if (parsed)
	{
		Assert(parsed->sessionId == 17ull, "BeginNotification sessionId");
		Assert(parsed->partnerAccountId == 42ull, "BeginNotification partner");
	}
}

static void TestBeginNotificationPacket()
{
	auto pkt = BuildTradeBeginNotificationPacket(17ull, 42ull, 0xDEADull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "BeginNotification packet parses");
	Assert(view.Opcode() == kOpcodeTradeBeginNotification, "Opcode TradeBeginNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
}

// -----------------------------------------------------------------------------
// TRADE_SET_OFFER
// -----------------------------------------------------------------------------

static void TestSetOfferRequestRoundTrip()
{
	std::vector<uint64_t> guids = {1ull, 2ull, 3ull, 0xFFFFFFFFFFFFFFFFull};
	auto buf = BuildTradeSetOfferRequestPayload(17ull, 100000ull, guids);
	auto parsed = ParseTradeSetOfferRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "SetOffer request parses");
	if (parsed)
	{
		Assert(parsed->sessionId == 17ull, "SetOffer sessionId");
		Assert(parsed->copperGold == 100000ull, "SetOffer copperGold");
		Assert(parsed->itemGuids.size() == 4u, "SetOffer 4 items");
		Assert(parsed->itemGuids[3] == 0xFFFFFFFFFFFFFFFFull, "SetOffer max guid");
	}
}

static void TestSetOfferRequestEmpty()
{
	auto buf = BuildTradeSetOfferRequestPayload(17ull, 0ull, {});
	auto parsed = ParseTradeSetOfferRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->itemGuids.empty(), "SetOffer empty list round-trips");
}

static void TestSetOfferRequestTruncate()
{
	std::vector<uint64_t> too_many(kMaxTradeItemsPerOffer + 5u, 7ull);
	auto buf = BuildTradeSetOfferRequestPayload(17ull, 0ull, too_many);
	auto parsed = ParseTradeSetOfferRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "SetOffer truncates oversize list");
	if (parsed)
		Assert(parsed->itemGuids.size() == kMaxTradeItemsPerOffer, "SetOffer truncated to kMaxTradeItemsPerOffer");
}

static void TestSetOfferRequestRejectsShort()
{
	uint8_t shortBuf[10]{};
	auto parsed = ParseTradeSetOfferRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "SetOffer rejects 10 bytes");
}

static void TestSetOfferResponseRoundTrip()
{
	auto buf = BuildTradeSetOfferResponsePayload(0u);
	Assert(buf.size() == 1u, "SetOffer response is 1 byte");
	auto parsed = ParseTradeSetOfferResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "SetOffer response 0");
	auto buf2 = BuildTradeSetOfferResponsePayload(
		static_cast<uint8_t>(TradeErrorCode::WrongState));
	auto parsed2 = ParseTradeSetOfferResponsePayload(buf2.data(), buf2.size());
	Assert(parsed2.has_value() && parsed2->error == 6u, "SetOffer WrongState");
}

// -----------------------------------------------------------------------------
// TRADE_LOCK
// -----------------------------------------------------------------------------

static void TestLockRequestRoundTrip()
{
	auto buf = BuildTradeLockRequestPayload(99ull);
	auto parsed = ParseTradeLockRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->sessionId == 99ull, "Lock round-trips");
}

static void TestLockResponseRoundTrip()
{
	auto buf = BuildTradeLockResponsePayload(0u, 3u);
	Assert(buf.size() == 2u, "Lock response is 2 bytes");
	auto parsed = ParseTradeLockResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Lock response parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Lock response error 0");
		Assert(parsed->newState == 3u, "Lock response newState BothLocked");
	}
}

static void TestLockResponsePacket()
{
	auto pkt = BuildTradeLockResponsePacket(0u, 1u, 7u, 0xBEEFull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Lock packet parses");
	Assert(view.Opcode() == kOpcodeTradeLockResponse, "Opcode TradeLockResponse");
	auto parsed = ParseTradeLockResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->newState == 1u, "Lock payload decodes");
}

// -----------------------------------------------------------------------------
// TRADE_STATE_UPDATE_NOTIFICATION
// -----------------------------------------------------------------------------

static void TestStateUpdateRoundTrip()
{
	std::vector<uint64_t> guids = {10ull, 20ull, 30ull};
	auto buf = BuildTradeStateUpdateNotificationPayload(17ull, 3u, 5000ull, guids);
	auto parsed = ParseTradeStateUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "StateUpdate parses");
	if (parsed)
	{
		Assert(parsed->sessionId == 17ull, "StateUpdate sessionId");
		Assert(parsed->state == 3u, "StateUpdate state BothLocked");
		Assert(parsed->partnerCopperGold == 5000ull, "StateUpdate gold");
		Assert(parsed->partnerItemGuids.size() == 3u, "StateUpdate 3 items");
		Assert(parsed->partnerItemGuids[2] == 30ull, "StateUpdate item[2]");
	}
}

static void TestStateUpdateEmpty()
{
	auto buf = BuildTradeStateUpdateNotificationPayload(17ull, 0u, 0ull, {});
	auto parsed = ParseTradeStateUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->partnerItemGuids.empty(), "StateUpdate empty parses");
}

static void TestStateUpdatePacket()
{
	auto pkt = BuildTradeStateUpdateNotificationPacket(17ull, 1u, 999ull, {1ull, 2ull}, 0xDEADull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "StateUpdate packet parses");
	Assert(view.Opcode() == kOpcodeTradeStateUpdateNotification, "Opcode TradeStateUpdateNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	auto parsed = ParseTradeStateUpdateNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->partnerItemGuids.size() == 2u, "StateUpdate payload decodes");
}

// -----------------------------------------------------------------------------
// TRADE_COMMIT
// -----------------------------------------------------------------------------

static void TestCommitRequestRoundTrip()
{
	auto buf = BuildTradeCommitRequestPayload(17ull);
	Assert(buf.size() == 8u, "Commit request 8 bytes");
	auto parsed = ParseTradeCommitRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->sessionId == 17ull, "Commit request round-trips");
}

static void TestCommitResponseRoundTrip()
{
	auto buf = BuildTradeCommitResponsePayload(0u);
	auto parsed = ParseTradeCommitResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Commit response OK");
	auto buf2 = BuildTradeCommitResponsePayload(
		static_cast<uint8_t>(TradeErrorCode::WrongState));
	auto parsed2 = ParseTradeCommitResponsePayload(buf2.data(), buf2.size());
	Assert(parsed2.has_value() && parsed2->error == 6u, "Commit WrongState decodes");
}

// -----------------------------------------------------------------------------
// TRADE_CANCEL
// -----------------------------------------------------------------------------

static void TestCancelRequestRoundTrip()
{
	auto buf = BuildTradeCancelRequestPayload(17ull);
	auto parsed = ParseTradeCancelRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->sessionId == 17ull, "Cancel request round-trips");
}

static void TestCancelNotificationRoundTrip()
{
	const std::string reason = "partner cancelled the trade";
	auto buf = BuildTradeCancelNotificationPayload(17ull, reason);
	auto parsed = ParseTradeCancelNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "CancelNotification parses");
	if (parsed)
	{
		Assert(parsed->sessionId == 17ull, "CancelNotification sessionId");
		Assert(parsed->reason == reason, "CancelNotification reason round-trips");
	}
}

static void TestCancelNotificationEmptyReason()
{
	auto buf = BuildTradeCancelNotificationPayload(17ull, "");
	auto parsed = ParseTradeCancelNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->reason.empty(), "CancelNotification empty reason OK");
}

static void TestCancelNotificationPacket()
{
	auto pkt = BuildTradeCancelNotificationPacket(17ull, "timeout", 0xCAFEull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "CancelNotification packet parses");
	Assert(view.Opcode() == kOpcodeTradeCancelNotification, "Opcode TradeCancelNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	auto parsed = ParseTradeCancelNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->reason == "timeout", "CancelNotification payload decodes");
}

// -----------------------------------------------------------------------------
// Boundary
// -----------------------------------------------------------------------------

static void TestBoundaryMaxSession()
{
	auto buf = BuildTradeLockRequestPayload(0xFFFFFFFFFFFFFFFFull);
	auto parsed = ParseTradeLockRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->sessionId == 0xFFFFFFFFFFFFFFFFull, "Lock max sessionId round-trips");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestBeginRequestRoundTrip();
	TestBeginRequestRejectsShort();
	TestBeginResponseRoundTrip();
	TestBeginResponseError();
	TestBeginResponsePacket();
	TestBeginNotificationRoundTrip();
	TestBeginNotificationPacket();

	TestSetOfferRequestRoundTrip();
	TestSetOfferRequestEmpty();
	TestSetOfferRequestTruncate();
	TestSetOfferRequestRejectsShort();
	TestSetOfferResponseRoundTrip();

	TestLockRequestRoundTrip();
	TestLockResponseRoundTrip();
	TestLockResponsePacket();

	TestStateUpdateRoundTrip();
	TestStateUpdateEmpty();
	TestStateUpdatePacket();

	TestCommitRequestRoundTrip();
	TestCommitResponseRoundTrip();

	TestCancelRequestRoundTrip();
	TestCancelNotificationRoundTrip();
	TestCancelNotificationEmptyReason();
	TestCancelNotificationPacket();

	TestBoundaryMaxSession();

	std::cerr << (s_failCount == 0 ? "[OK] all trade payload tests passed\n" : "[FAIL] some trade tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
