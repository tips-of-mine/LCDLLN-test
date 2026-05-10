/**
 * CMANGOS.32 (Phase 5.32 step 3+4) — Round-trip tests for GMTICKET_*_REQUEST /
 * GMTICKET_*_RESPONSE / GMTICKET_RESOLVED_NOTIFICATION payloads. Pure encoding
 * tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 *
 * Test policy : symetrique aux QuestPayloadsTests.cpp — pas de framework de
 * test externe, juste un Assert local + main() qui appelle chaque suite.
 */

#include "src/shared/network/GmTicketPayloads.h"
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
// GMTICKET_OPEN
// -----------------------------------------------------------------------------

static void TestOpenRequestRoundTrip()
{
	const std::string body = "Bonjour GM, mon perso est bloque sous la map au respawn de Goblin Cave.";
	auto buf = BuildGmTicketOpenRequestPayload(body);
	Assert(!buf.empty(), "Open request payload not empty");
	auto parsed = ParseGmTicketOpenRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Open request parses OK");
	if (parsed)
		Assert(parsed->body == body, "Open request body round-trips");
}

static void TestOpenRequestEmptyBody()
{
	auto buf = BuildGmTicketOpenRequestPayload("");
	Assert(buf.size() == 2u, "Empty body == 2 bytes (uint16 length=0)");
	auto parsed = ParseGmTicketOpenRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->body.empty(), "Empty body round-trips");
}

static void TestOpenRequestRejectsShort()
{
	auto parsed = ParseGmTicketOpenRequestPayload(nullptr, 2u);
	Assert(!parsed.has_value(), "Open request rejects nullptr");
	uint8_t one[1]{0};
	auto parsed2 = ParseGmTicketOpenRequestPayload(one, 1u);
	Assert(!parsed2.has_value(), "Open request rejects 1 byte");
}

static void TestOpenRequestTruncatesAtMax()
{
	std::string huge(kMaxGmTicketBodyBytes + 100u, 'X');
	auto buf = BuildGmTicketOpenRequestPayload(huge);
	auto parsed = ParseGmTicketOpenRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Build accepts oversize body (truncates)");
	if (parsed)
		Assert(parsed->body.size() == kMaxGmTicketBodyBytes, "Body truncated at kMaxGmTicketBodyBytes");
}

static void TestOpenResponseRoundTrip()
{
	auto buf = BuildGmTicketOpenResponsePayload(0u, 12345ull);
	Assert(buf.size() == 9u, "Open response is 9 bytes (1 + 8)");
	auto parsed = ParseGmTicketOpenResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Open response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Open response error 0");
		Assert(parsed->ticketId == 12345ull, "Open response ticketId");
	}
}

static void TestOpenResponseError()
{
	auto buf = BuildGmTicketOpenResponsePayload(
		static_cast<uint8_t>(GmTicketErrorCode::BodyTooLong), 0u);
	auto parsed = ParseGmTicketOpenResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Open BodyTooLong decodes");
}

static void TestOpenResponsePacket()
{
	auto pkt = BuildGmTicketOpenResponsePacket(0u, 999ull, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Open response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGmTicketOpenResponse, "Packet opcode is GmTicketOpenResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseGmTicketOpenResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->ticketId == 999ull, "Open response payload decodes");
}

// -----------------------------------------------------------------------------
// GMTICKET_LIST_MINE
// -----------------------------------------------------------------------------

static void TestListMineRequestRoundTrip()
{
	auto buf = BuildGmTicketListMineRequestPayload();
	Assert(buf.empty(), "ListMine request payload empty");
	auto parsed = ParseGmTicketListMineRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "ListMine request accepts empty payload");
}

static void TestListMineResponseEmpty()
{
	auto buf = BuildGmTicketListMineResponsePayload(0u, {});
	auto parsed = ParseGmTicketListMineResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ListMine response empty parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "ListMine response empty error 0");
		Assert(parsed->tickets.empty(), "ListMine response empty has 0 tickets");
	}
}

static void TestListMineResponseUnauthorized()
{
	auto buf = BuildGmTicketListMineResponsePayload(
		static_cast<uint8_t>(GmTicketErrorCode::Unauthorized), {});
	auto parsed = ParseGmTicketListMineResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "ListMine response Unauthorized");
}

static void TestListMineResponseMultiple()
{
	std::vector<GmTicketEntry> entries;
	entries.push_back({1ull, 1000ull, 0ull, 0u}); // Open
	entries.push_back({2ull, 2000ull, 0ull, 1u}); // Assigned
	entries.push_back({100ull, 3000ull, 4000ull, 2u}); // Resolved
	auto buf = BuildGmTicketListMineResponsePayload(0u, entries);
	auto parsed = ParseGmTicketListMineResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ListMine response multiple parses");
	if (parsed && parsed->tickets.size() == 3u)
	{
		Assert(parsed->tickets[0].id == 1ull && parsed->tickets[0].state == 0u, "entry[0]");
		Assert(parsed->tickets[1].id == 2ull && parsed->tickets[1].state == 1u, "entry[1]");
		Assert(parsed->tickets[2].id == 100ull && parsed->tickets[2].resolvedTsMs == 4000ull, "entry[2]");
	}
	else
	{
		Assert(false, "ListMine response should have 3 entries");
	}
}

static void TestListMineResponsePacket()
{
	std::vector<GmTicketEntry> entries;
	entries.push_back({42ull, 1234ull, 0ull, 0u});
	auto pkt = BuildGmTicketListMineResponsePacket(0u, entries, 7u, 0xDEADull);
	Assert(!pkt.empty(), "ListMine response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "ListMine PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGmTicketListMineResponse, "Packet opcode is GmTicketListMineResponse");
	Assert(view.RequestId() == 7u, "RequestId preserved");
	auto parsed = ParseGmTicketListMineResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->tickets.size() == 1u, "ListMine response payload decodes");
}

// -----------------------------------------------------------------------------
// GMTICKET_CANCEL
// -----------------------------------------------------------------------------

static void TestCancelRequestRoundTrip()
{
	auto buf = BuildGmTicketCancelRequestPayload(42ull);
	Assert(buf.size() == 8u, "Cancel request is 8 bytes");
	auto parsed = ParseGmTicketCancelRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->ticketId == 42ull, "Cancel request round-trips");
}

static void TestCancelRequestRejectsShort()
{
	auto parsed = ParseGmTicketCancelRequestPayload(nullptr, 8u);
	Assert(!parsed.has_value(), "Cancel request rejects nullptr");
	uint8_t shortBuf[7]{};
	auto parsed2 = ParseGmTicketCancelRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Cancel request rejects 7 bytes");
}

static void TestCancelResponseRoundTrip()
{
	auto buf = BuildGmTicketCancelResponsePayload(0u, 42ull);
	auto parsed = ParseGmTicketCancelResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->ticketId == 42ull, "Cancel response round-trips");
}

static void TestCancelResponseAlreadyResolved()
{
	auto buf = BuildGmTicketCancelResponsePayload(
		static_cast<uint8_t>(GmTicketErrorCode::AlreadyResolved), 42ull);
	auto parsed = ParseGmTicketCancelResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 5u, "Cancel AlreadyResolved decodes");
}

// -----------------------------------------------------------------------------
// GMTICKET_RESOLVED_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestResolvedNotificationRoundTrip()
{
	auto buf = BuildGmTicketResolvedNotificationPayload(99ull, 12345678ull);
	Assert(buf.size() == 16u, "ResolvedNotification is 16 bytes");
	auto parsed = ParseGmTicketResolvedNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ResolvedNotification parses");
	if (parsed)
	{
		Assert(parsed->ticketId == 99ull, "ResolvedNotification ticketId");
		Assert(parsed->resolvedTsMs == 12345678ull, "ResolvedNotification resolvedTsMs");
	}
}

static void TestResolvedNotificationPacket()
{
	auto pkt = BuildGmTicketResolvedNotificationPacket(99ull, 555ull, 0xCAFEull);
	Assert(!pkt.empty(), "ResolvedNotification packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "ResolvedNotification PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGmTicketResolvedNotification, "Opcode is GmTicketResolvedNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseGmTicketResolvedNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->ticketId == 99ull, "ResolvedNotification payload decodes");
}

// -----------------------------------------------------------------------------
// Boundary
// -----------------------------------------------------------------------------

static void TestBoundaryTicketId()
{
	auto buf = BuildGmTicketCancelRequestPayload(0xFFFFFFFFFFFFFFFFull);
	auto parsed = ParseGmTicketCancelRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->ticketId == 0xFFFFFFFFFFFFFFFFull, "ticketId max round-trips");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestOpenRequestRoundTrip();
	TestOpenRequestEmptyBody();
	TestOpenRequestRejectsShort();
	TestOpenRequestTruncatesAtMax();
	TestOpenResponseRoundTrip();
	TestOpenResponseError();
	TestOpenResponsePacket();

	TestListMineRequestRoundTrip();
	TestListMineResponseEmpty();
	TestListMineResponseUnauthorized();
	TestListMineResponseMultiple();
	TestListMineResponsePacket();

	TestCancelRequestRoundTrip();
	TestCancelRequestRejectsShort();
	TestCancelResponseRoundTrip();
	TestCancelResponseAlreadyResolved();

	TestResolvedNotificationRoundTrip();
	TestResolvedNotificationPacket();

	TestBoundaryTicketId();

	std::cerr << (s_failCount == 0 ? "[OK] all gm ticket payload tests passed\n" : "[FAIL] some gm ticket tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
