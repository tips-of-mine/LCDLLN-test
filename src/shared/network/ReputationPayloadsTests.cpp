/**
 * CMANGOS.24 (Phase 3.24 step 3+4) — Round-trip tests for REPUTATION_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/ReputationPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
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
// REPUTATION_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildReputationListRequestPayload();
	Assert(buf.empty(), "List request payload empty");
	auto parsed = ParseReputationListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildReputationListResponsePayload(0u, {});
	auto parsed = ParseReputationListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response empty parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "List response empty error 0");
		Assert(parsed->entries.empty(), "List response empty has 0 entries");
	}
}

static void TestListResponseUnauthorized()
{
	auto buf = BuildReputationListResponsePayload(
		static_cast<uint8_t>(ReputationErrorCode::Unauthorized), {});
	auto parsed = ParseReputationListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "List response Unauthorized");
}

static void TestListResponseMultiple()
{
	std::vector<ReputationEntry> entries;
	entries.push_back({1u, 0, -3});         // Neutral
	entries.push_back({2u, 6000, -1});      // Honored
	entries.push_back({100u, 21000, 0});    // Revered (border)
	entries.push_back({200u, -42000, -6});  // Hated (min)
	entries.push_back({300u, 41999, 1});    // Exalted (max)
	auto buf = BuildReputationListResponsePayload(0u, entries);
	auto parsed = ParseReputationListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response multiple parses");
	if (parsed && parsed->entries.size() == 5u)
	{
		Assert(parsed->entries[0].factionId == 1u && parsed->entries[0].value == 0
			&& parsed->entries[0].standing == -3, "entry[0] Neutral");
		Assert(parsed->entries[1].factionId == 2u && parsed->entries[1].value == 6000
			&& parsed->entries[1].standing == -1, "entry[1] Honored");
		Assert(parsed->entries[2].factionId == 100u && parsed->entries[2].value == 21000
			&& parsed->entries[2].standing == 0, "entry[2] Revered");
		Assert(parsed->entries[3].factionId == 200u && parsed->entries[3].value == -42000
			&& parsed->entries[3].standing == -6, "entry[3] Hated");
		Assert(parsed->entries[4].factionId == 300u && parsed->entries[4].value == 41999
			&& parsed->entries[4].standing == 1, "entry[4] Exalted");
	}
	else
	{
		Assert(false, "List response should have 5 entries");
	}
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseReputationListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List response rejects nullptr");
	uint8_t one[1]{0};
	auto parsed2 = ParseReputationListResponsePayload(one, 0u);
	Assert(!parsed2.has_value(), "List response rejects size 0");
}

static void TestListResponsePacket()
{
	std::vector<ReputationEntry> entries;
	entries.push_back({42u, 1500, -2}); // Friendly
	auto pkt = BuildReputationListResponsePacket(0u, entries, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "List response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeReputationListResponse, "Packet opcode is ReputationListResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseReputationListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->entries.size() == 1u
		&& parsed->entries[0].factionId == 42u, "List response payload decodes");
}

// -----------------------------------------------------------------------------
// REPUTATION_UPDATE_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestUpdateNotificationRoundTrip()
{
	auto buf = BuildReputationUpdateNotificationPayload(42u, 1500, -2, 250);
	Assert(buf.size() == 13u, "Update notification is 13 bytes");
	auto parsed = ParseReputationUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Update notification parses");
	if (parsed)
	{
		Assert(parsed->factionId == 42u, "Update factionId");
		Assert(parsed->newValue == 1500, "Update newValue");
		Assert(parsed->newStanding == -2, "Update newStanding Friendly");
		Assert(parsed->delta == 250, "Update delta");
	}
}

static void TestUpdateNotificationNegativeDelta()
{
	auto buf = BuildReputationUpdateNotificationPayload(99u, -5000, -4, -1500);
	auto parsed = ParseReputationUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Update with negative delta parses");
	if (parsed)
	{
		Assert(parsed->newValue == -5000, "Negative newValue");
		Assert(parsed->newStanding == -4, "Unfriendly standing");
		Assert(parsed->delta == -1500, "Negative delta");
	}
}

static void TestUpdateNotificationBoundary()
{
	// Values at protocol boundaries.
	auto buf = BuildReputationUpdateNotificationPayload(0xFFFFFFFFu, -42000, -6, 41999);
	auto parsed = ParseReputationUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Update boundary parses");
	if (parsed)
	{
		Assert(parsed->factionId == 0xFFFFFFFFu, "factionId max");
		Assert(parsed->newValue == -42000, "newValue min");
		Assert(parsed->newStanding == -6, "Hated standing");
		Assert(parsed->delta == 41999, "delta high");
	}
}

static void TestUpdateNotificationRejectsShort()
{
	auto parsed = ParseReputationUpdateNotificationPayload(nullptr, 13u);
	Assert(!parsed.has_value(), "Update rejects nullptr");
	uint8_t shortBuf[12]{};
	auto parsed2 = ParseReputationUpdateNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Update rejects 12 bytes");
}

static void TestUpdateNotificationPacket()
{
	auto pkt = BuildReputationUpdateNotificationPacket(42u, 1500, -2, 250, 0xBEEFull);
	Assert(!pkt.empty(), "Update notification packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Update PacketView parse OK");
	Assert(view.Opcode() == kOpcodeReputationUpdateNotification, "Opcode is ReputationUpdateNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xBEEFull, "SessionId preserved");
	auto parsed = ParseReputationUpdateNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->factionId == 42u && parsed->delta == 250,
		"Update payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseUnauthorized();
	TestListResponseMultiple();
	TestListResponseRejectsShort();
	TestListResponsePacket();

	TestUpdateNotificationRoundTrip();
	TestUpdateNotificationNegativeDelta();
	TestUpdateNotificationBoundary();
	TestUpdateNotificationRejectsShort();
	TestUpdateNotificationPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all reputation payload tests passed\n"
	                                : "[FAIL] some reputation tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
