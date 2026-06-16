/**
 * CMANGOS.09 (Phase 5.09 step 3+4 AuctionHouse) — Round-trip tests pour les
 * payloads AUCTION_*. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/AuctionPayloads.h"
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
// AUCTION_LIST
// -----------------------------------------------------------------------------

static void TestListRequestNoFilter()
{
	auto buf = BuildAuctionListRequestPayload(0u);
	auto parsed = ParseAuctionListRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->itemTemplateIdFilter == 0u,
		"List request round-trip filter=0");
}

static void TestListRequestWithFilter()
{
	auto buf = BuildAuctionListRequestPayload(42u);
	auto parsed = ParseAuctionListRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->itemTemplateIdFilter == 42u,
		"List request round-trip filter=42");
}

static void TestListRequestRejectsShort()
{
	auto parsed = ParseAuctionListRequestPayload(nullptr, 4u);
	Assert(!parsed.has_value(), "List request rejects nullptr");
	uint8_t shortBuf[3]{};
	auto parsed2 = ParseAuctionListRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "List request rejects 3 bytes");
}

static void TestListResponseEmpty()
{
	auto buf = BuildAuctionListResponsePayload(0u, {});
	auto parsed = ParseAuctionListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->listings.empty(),
		"List response empty parses");
}

static void TestListResponseSingle()
{
	AuctionListingSummary s;
	s.auctionId              = 7ull;
	s.itemTemplateId         = 1u;
	s.itemName               = "Minerai de fer";
	s.count                  = 20u;
	s.currentBidCopper       = 500ull;
	s.buyoutCopper           = 1000ull;
	s.ownerName              = "Garond";
	s.secondsUntilExpiration = 3600ull;
	auto buf = BuildAuctionListResponsePayload(0u, {s});
	auto parsed = ParseAuctionListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response single parses");
	if (parsed && parsed->listings.size() == 1u)
	{
		Assert(parsed->listings[0].auctionId == 7ull,           "List[0] auctionId");
		Assert(parsed->listings[0].itemTemplateId == 1u,        "List[0] itemTemplateId");
		Assert(parsed->listings[0].itemName == "Minerai de fer", "List[0] itemName");
		Assert(parsed->listings[0].count == 20u,                "List[0] count");
		Assert(parsed->listings[0].currentBidCopper == 500ull,  "List[0] currentBid");
		Assert(parsed->listings[0].buyoutCopper == 1000ull,     "List[0] buyout");
		Assert(parsed->listings[0].ownerName == "Garond",       "List[0] ownerName");
		Assert(parsed->listings[0].secondsUntilExpiration == 3600ull, "List[0] secondsUntil");
	}
	else
	{
		Assert(false, "List should have 1 listing");
	}
}

static void TestListResponseMultiple()
{
	std::vector<AuctionListingSummary> listings;
	for (uint64_t i = 0; i < 8u; ++i)
	{
		AuctionListingSummary s;
		s.auctionId              = i + 1u;
		s.itemTemplateId         = static_cast<uint32_t>(i + 1u);
		s.itemName               = "Item";
		s.count                  = static_cast<uint32_t>(i + 1u);
		s.currentBidCopper       = (i + 1u) * 100ull;
		s.buyoutCopper           = (i + 1u) * 200ull;
		s.ownerName              = "Owner";
		s.secondsUntilExpiration = (i + 1u) * 3600ull;
		listings.push_back(s);
	}
	auto buf = BuildAuctionListResponsePayload(0u, listings);
	auto parsed = ParseAuctionListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->listings.size() == 8u, "List 8 listings parses");
	if (parsed && parsed->listings.size() == 8u)
	{
		Assert(parsed->listings[0].auctionId == 1ull, "L[0] id 1");
		Assert(parsed->listings[7].auctionId == 8ull, "L[7] id 8");
		Assert(parsed->listings[7].secondsUntilExpiration == 8ull * 3600ull, "L[7] expiration");
	}
}

static void TestListResponseUnauthorized()
{
	auto buf = BuildAuctionListResponsePayload(
		static_cast<uint8_t>(AuctionErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "List error has only 1 byte");
	auto parsed = ParseAuctionListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "List Unauthorized");
}

static void TestListResponsePriceMaxAndZero()
{
	AuctionListingSummary s1;
	s1.auctionId = 1ull;
	s1.itemTemplateId = 1u;
	s1.itemName = "Cheap";
	s1.count = 1u;
	s1.currentBidCopper = 0ull;
	s1.buyoutCopper = 0ull;
	s1.ownerName = "X";
	s1.secondsUntilExpiration = 0ull;
	AuctionListingSummary s2;
	s2.auctionId = 2ull;
	s2.itemTemplateId = 2u;
	s2.itemName = "Max";
	s2.count = 0xFFFFFFFFu;
	s2.currentBidCopper = 0xFFFFFFFFFFFFFFFFull;
	s2.buyoutCopper = 0xFFFFFFFFFFFFFFFFull;
	s2.ownerName = "Y";
	s2.secondsUntilExpiration = 0xFFFFFFFFFFFFFFFFull;
	auto buf = BuildAuctionListResponsePayload(0u, {s1, s2});
	auto parsed = ParseAuctionListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->listings.size() == 2u
		&& parsed->listings[0].currentBidCopper == 0ull
		&& parsed->listings[1].currentBidCopper == 0xFFFFFFFFFFFFFFFFull
		&& parsed->listings[1].buyoutCopper == 0xFFFFFFFFFFFFFFFFull,
		"List accepts 0 and uint64 max copper");
}

static void TestListResponsePacket()
{
	AuctionListingSummary s;
	s.auctionId = 42ull;
	s.itemTemplateId = 5u;
	s.itemName = "Potion de mana";
	s.count = 5u;
	s.currentBidCopper = 250ull;
	s.buyoutCopper = 500ull;
	s.ownerName = "Boss";
	s.secondsUntilExpiration = 7200ull;
	auto pkt = BuildAuctionListResponsePacket(0u, {s}, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "List packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"List PacketView parse OK");
	Assert(view.Opcode() == kOpcodeAuctionListResponse, "Opcode is ListResponse");
	Assert(view.RequestId() == 999u, "List RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "List SessionId preserved");
	auto parsed = ParseAuctionListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->listings.size() == 1u
		&& parsed->listings[0].auctionId == 42ull
		&& parsed->listings[0].ownerName == "Boss",
		"List payload decodes");
}

// -----------------------------------------------------------------------------
// AUCTION_POST
// -----------------------------------------------------------------------------

static void TestPostRequestRoundTrip()
{
	auto buf = BuildAuctionPostRequestPayload(7u, 10u, 100ull, 500ull, 24u);
	auto parsed = ParseAuctionPostRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Post request parses");
	if (parsed)
	{
		Assert(parsed->itemTemplateId == 7u, "Post itemTemplateId");
		Assert(parsed->count == 10u, "Post count");
		Assert(parsed->startBidCopper == 100ull, "Post startBid");
		Assert(parsed->buyoutCopper == 500ull, "Post buyout");
		Assert(parsed->durationHours == 24u, "Post durationHours");
	}
}

static void TestPostRequestNoBuyout()
{
	auto buf = BuildAuctionPostRequestPayload(1u, 1u, 50ull, 0ull, 12u);
	auto parsed = ParseAuctionPostRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->buyoutCopper == 0ull
		&& parsed->durationHours == 12u,
		"Post accepts buyout=0 + duration=12h");
}

static void TestPostRequestDuration48h()
{
	auto buf = BuildAuctionPostRequestPayload(99u, 100u, 1000ull, 5000ull, 48u);
	auto parsed = ParseAuctionPostRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->durationHours == 48u,
		"Post accepts duration=48h");
}

static void TestPostRequestRejectsShort()
{
	auto parsed = ParseAuctionPostRequestPayload(nullptr, 25u);
	Assert(!parsed.has_value(), "Post request rejects nullptr");
	uint8_t shortBuf[24]{};
	auto parsed2 = ParseAuctionPostRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Post request rejects 24 bytes");
}

static void TestPostResponseOk()
{
	auto buf = BuildAuctionPostResponsePayload(0u, 12345ull);
	auto parsed = ParseAuctionPostResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->auctionId == 12345ull,
		"Post response Ok with auctionId");
}

static void TestPostResponseInvalidParams()
{
	auto buf = BuildAuctionPostResponsePayload(
		static_cast<uint8_t>(AuctionErrorCode::InvalidParams), 0ull);
	Assert(buf.size() == 1u, "Post error has only 1 byte");
	auto parsed = ParseAuctionPostResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Post InvalidParams");
}

static void TestPostResponsePacket()
{
	auto pkt = BuildAuctionPostResponsePacket(0u, 777ull, 1234u, 0xBEEFull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Post PacketView parse OK");
	Assert(view.Opcode() == kOpcodeAuctionPostResponse, "Opcode is PostResponse");
	Assert(view.RequestId() == 1234u, "Post RequestId preserved");
	auto parsed = ParseAuctionPostResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->auctionId == 777ull, "Post payload decodes");
}

// -----------------------------------------------------------------------------
// AUCTION_BID
// -----------------------------------------------------------------------------

static void TestBidRequestRoundTrip()
{
	auto buf = BuildAuctionBidRequestPayload(7ull, 1500ull);
	auto parsed = ParseAuctionBidRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->auctionId == 7ull
		&& parsed->bidAmountCopper == 1500ull,
		"Bid request round-trip");
}

static void TestBidRequestRejectsShort()
{
	auto parsed = ParseAuctionBidRequestPayload(nullptr, 16u);
	Assert(!parsed.has_value(), "Bid request rejects nullptr");
	uint8_t shortBuf[15]{};
	auto parsed2 = ParseAuctionBidRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Bid request rejects 15 bytes");
}

static void TestBidResponseNormal()
{
	auto buf = BuildAuctionBidResponsePayload(0u, 0u);
	auto parsed = ParseAuctionBidResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->isBuyout == 0u,
		"Bid response Ok normal");
}

static void TestBidResponseBuyout()
{
	auto buf = BuildAuctionBidResponsePayload(0u, 1u);
	auto parsed = ParseAuctionBidResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->isBuyout == 1u,
		"Bid response Ok buyout");
}

static void TestBidResponseTooLow()
{
	auto buf = BuildAuctionBidResponsePayload(
		static_cast<uint8_t>(AuctionErrorCode::BidTooLow), 0u);
	Assert(buf.size() == 1u, "Bid error has only 1 byte");
	auto parsed = ParseAuctionBidResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "Bid BidTooLow");
}

static void TestBidResponseOwnAuction()
{
	auto buf = BuildAuctionBidResponsePayload(
		static_cast<uint8_t>(AuctionErrorCode::OwnAuction), 0u);
	auto parsed = ParseAuctionBidResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 5u, "Bid OwnAuction");
}

static void TestBidResponsePacket()
{
	auto pkt = BuildAuctionBidResponsePacket(0u, 1u, 555u, 0xFEEDull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Bid PacketView parse OK");
	Assert(view.Opcode() == kOpcodeAuctionBidResponse, "Opcode is BidResponse");
	auto parsed = ParseAuctionBidResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->isBuyout == 1u, "Bid payload decodes buyout=1");
}

// -----------------------------------------------------------------------------
// AUCTION_CANCEL
// -----------------------------------------------------------------------------

static void TestCancelRequestRoundTrip()
{
	auto buf = BuildAuctionCancelRequestPayload(99ull);
	auto parsed = ParseAuctionCancelRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->auctionId == 99ull,
		"Cancel request round-trip");
}

static void TestCancelRequestRejectsShort()
{
	auto parsed = ParseAuctionCancelRequestPayload(nullptr, 8u);
	Assert(!parsed.has_value(), "Cancel request rejects nullptr");
	uint8_t shortBuf[7]{};
	auto parsed2 = ParseAuctionCancelRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Cancel request rejects 7 bytes");
}

static void TestCancelResponseOk()
{
	auto buf = BuildAuctionCancelResponsePayload(0u);
	auto parsed = ParseAuctionCancelResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Cancel response Ok");
}

static void TestCancelResponseNotOwner()
{
	auto buf = BuildAuctionCancelResponsePayload(
		static_cast<uint8_t>(AuctionErrorCode::NotOwner));
	auto parsed = ParseAuctionCancelResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "Cancel NotOwner");
}

static void TestCancelResponsePacket()
{
	auto pkt = BuildAuctionCancelResponsePacket(0u, 42u, 0x12345ull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Cancel PacketView parse OK");
	Assert(view.Opcode() == kOpcodeAuctionCancelResponse, "Opcode is CancelResponse");
}

// -----------------------------------------------------------------------------
// AUCTION_EXPIRED_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestExpiredNotifWonRoundTrip()
{
	auto buf = BuildAuctionExpiredNotificationPayload(7ull, 1u, 1500ull, "Garond");
	auto parsed = ParseAuctionExpiredNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ExpiredNotif won parses");
	if (parsed)
	{
		Assert(parsed->auctionId == 7ull, "ExpiredNotif auctionId");
		Assert(parsed->won == 1u, "ExpiredNotif won=1");
		Assert(parsed->finalBidCopper == 1500ull, "ExpiredNotif finalBid");
		Assert(parsed->winnerName == "Garond", "ExpiredNotif winnerName");
	}
}

static void TestExpiredNotifLostRoundTrip()
{
	auto buf = BuildAuctionExpiredNotificationPayload(99ull, 0u, 0ull, "");
	auto parsed = ParseAuctionExpiredNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->won == 0u
		&& parsed->finalBidCopper == 0ull
		&& parsed->winnerName.empty(),
		"ExpiredNotif lost (won=0, no winner) round-trip");
}

static void TestExpiredNotifRejectsShort()
{
	auto parsed = ParseAuctionExpiredNotificationPayload(nullptr, 19u);
	Assert(!parsed.has_value(), "ExpiredNotif rejects nullptr");
	uint8_t shortBuf[18]{};
	auto parsed2 = ParseAuctionExpiredNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "ExpiredNotif rejects 18 bytes");
}

static void TestExpiredNotifPacket()
{
	auto pkt = BuildAuctionExpiredNotificationPacket(123ull, 1u, 9999ull, "Sylvane", 0xFADEull);
	Assert(!pkt.empty(), "ExpiredNotif packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"ExpiredNotif PacketView parse OK");
	Assert(view.Opcode() == kOpcodeAuctionExpiredNotification, "Opcode is ExpiredNotif");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xFADEull, "ExpiredNotif SessionId preserved");
	auto parsed = ParseAuctionExpiredNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->auctionId == 123ull
		&& parsed->winnerName == "Sylvane",
		"ExpiredNotif payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestNoFilter();
	TestListRequestWithFilter();
	TestListRequestRejectsShort();
	TestListResponseEmpty();
	TestListResponseSingle();
	TestListResponseMultiple();
	TestListResponseUnauthorized();
	TestListResponsePriceMaxAndZero();
	TestListResponsePacket();

	TestPostRequestRoundTrip();
	TestPostRequestNoBuyout();
	TestPostRequestDuration48h();
	TestPostRequestRejectsShort();
	TestPostResponseOk();
	TestPostResponseInvalidParams();
	TestPostResponsePacket();

	TestBidRequestRoundTrip();
	TestBidRequestRejectsShort();
	TestBidResponseNormal();
	TestBidResponseBuyout();
	TestBidResponseTooLow();
	TestBidResponseOwnAuction();
	TestBidResponsePacket();

	TestCancelRequestRoundTrip();
	TestCancelRequestRejectsShort();
	TestCancelResponseOk();
	TestCancelResponseNotOwner();
	TestCancelResponsePacket();

	TestExpiredNotifWonRoundTrip();
	TestExpiredNotifLostRoundTrip();
	TestExpiredNotifRejectsShort();
	TestExpiredNotifPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all auction payload tests passed\n"
	                                : "[FAIL] some auction tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
