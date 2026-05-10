/**
 * CMANGOS.36 (Phase 5.36 step 3+4) — Round-trip tests for OUTDOOR_PVP_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/OutdoorPvpPayloads.h"
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
// OUTDOOR_PVP_ZONE_LIST
// -----------------------------------------------------------------------------

static void TestZoneListRequestRoundTrip()
{
	auto buf = BuildOutdoorPvpZoneListRequestPayload();
	Assert(buf.empty(), "OutdoorPvp ZoneList request payload empty");
	auto parsed = ParseOutdoorPvpZoneListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "OutdoorPvp ZoneList request accepts empty payload");
}

static void TestZoneListResponseEmpty()
{
	auto buf = BuildOutdoorPvpZoneListResponsePayload(0u, {});
	auto parsed = ParseOutdoorPvpZoneListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->zones.empty(),
		"OutdoorPvp ZoneList response empty parses");
}

static void TestZoneListResponseSingleZoneNoObjectives()
{
	OutdoorPvpZoneSummary z;
	z.zoneId = 1u;
	z.name = "Hellfire Peninsula";
	z.allianceScore = 3u;
	z.hordeScore = 2u;
	auto buf = BuildOutdoorPvpZoneListResponsePayload(0u, {z});
	auto parsed = ParseOutdoorPvpZoneListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ZoneList single zone parses");
	if (parsed && parsed->zones.size() == 1u)
	{
		Assert(parsed->zones[0].zoneId == 1u, "Zone 1 id");
		Assert(parsed->zones[0].name == "Hellfire Peninsula", "Zone 1 name");
		Assert(parsed->zones[0].allianceScore == 3u, "Zone 1 alliance score");
		Assert(parsed->zones[0].hordeScore == 2u, "Zone 1 horde score");
		Assert(parsed->zones[0].objectives.empty(), "Zone 1 no objectives");
	}
	else
	{
		Assert(false, "ZoneList should have 1 zone");
	}
}

static void TestZoneListResponseFullWithObjectives()
{
	std::vector<OutdoorPvpZoneSummary> zones;

	OutdoorPvpZoneSummary z1;
	z1.zoneId = 1u;
	z1.name = "Hellfire Peninsula";
	z1.allianceScore = 1u;
	z1.hordeScore = 0u;
	z1.objectives.push_back({10u, 0u, 50u, 1u});      // Alliance owner, 50% horde capturing
	z1.objectives.push_back({11u, 0xFFu, 0u, 0xFFu}); // neutral, no capture
	z1.objectives.push_back({12u, 1u, 100u, 0u});     // Horde owner, 100% alliance capturing
	zones.push_back(z1);

	OutdoorPvpZoneSummary z2;
	z2.zoneId = 2u;
	z2.name = "Eastern Plaguelands";
	z2.allianceScore = 0u;
	z2.hordeScore = 0u;
	z2.objectives.push_back({20u, 0xFFu, 0u, 0xFFu});
	z2.objectives.push_back({21u, 0xFFu, 0u, 0xFFu});
	z2.objectives.push_back({22u, 0xFFu, 0u, 0xFFu});
	z2.objectives.push_back({23u, 0xFFu, 0u, 0xFFu});
	zones.push_back(z2);

	auto buf = BuildOutdoorPvpZoneListResponsePayload(0u, zones);
	auto parsed = ParseOutdoorPvpZoneListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "ZoneList full parses");
	if (parsed && parsed->zones.size() == 2u)
	{
		Assert(parsed->zones[0].zoneId == 1u, "Zone[0] id 1");
		Assert(parsed->zones[0].name == "Hellfire Peninsula", "Zone[0] name");
		Assert(parsed->zones[0].objectives.size() == 3u, "Zone[0] 3 objectives");
		Assert(parsed->zones[0].objectives[0].objectiveId == 10u, "Obj 10 id");
		Assert(parsed->zones[0].objectives[0].owner == 0u, "Obj 10 owner Alliance");
		Assert(parsed->zones[0].objectives[0].capturePct == 50u, "Obj 10 pct 50");
		Assert(parsed->zones[0].objectives[0].capturingBy == 1u, "Obj 10 Horde capturing");
		Assert(parsed->zones[0].objectives[1].owner == 0xFFu, "Obj 11 neutral");
		Assert(parsed->zones[0].objectives[1].capturingBy == 0xFFu, "Obj 11 no capture");
		Assert(parsed->zones[0].objectives[2].owner == 1u, "Obj 12 owner Horde");
		Assert(parsed->zones[0].objectives[2].capturePct == 100u, "Obj 12 pct 100");
		Assert(parsed->zones[1].zoneId == 2u, "Zone[1] id 2");
		Assert(parsed->zones[1].name == "Eastern Plaguelands", "Zone[1] name");
		Assert(parsed->zones[1].objectives.size() == 4u, "Zone[1] 4 objectives");
	}
	else
	{
		Assert(false, "ZoneList should have 2 zones");
	}
}

static void TestZoneListResponseError()
{
	auto buf = BuildOutdoorPvpZoneListResponsePayload(
		static_cast<uint8_t>(OutdoorPvpErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "ZoneList error has only 1 byte");
	auto parsed = ParseOutdoorPvpZoneListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 5u, "ZoneList Unauthorized");
}

static void TestZoneListResponseRejectsShort()
{
	auto parsed = ParseOutdoorPvpZoneListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "ZoneList rejects nullptr");
}

static void TestZoneListResponsePacket()
{
	OutdoorPvpZoneSummary z;
	z.zoneId = 42u;
	z.name = "Test Zone";
	z.allianceScore = 7u;
	z.hordeScore = 8u;
	z.objectives.push_back({100u, 0xFFu, 25u, 0u});
	auto pkt = BuildOutdoorPvpZoneListResponsePacket(0u, {z}, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "ZoneList packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"ZoneList PacketView parse OK");
	Assert(view.Opcode() == kOpcodeOutdoorPvpZoneListResponse, "Opcode is ZoneListResponse");
	Assert(view.RequestId() == 999u, "ZoneList RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "ZoneList SessionId preserved");
	auto parsed = ParseOutdoorPvpZoneListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->zones.size() == 1u
		&& parsed->zones[0].zoneId == 42u && parsed->zones[0].name == "Test Zone",
		"ZoneList payload decodes");
}

// -----------------------------------------------------------------------------
// OUTDOOR_PVP_SUBSCRIBE
// -----------------------------------------------------------------------------

static void TestSubscribeRequestRoundTrip()
{
	auto buf = BuildOutdoorPvpSubscribeRequestPayload(1u);
	Assert(buf.size() == 4u, "Subscribe request payload size 4");
	auto parsed = ParseOutdoorPvpSubscribeRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->zoneId == 1u, "Subscribe request parses zoneId 1");
}

static void TestSubscribeRequestBoundary()
{
	auto bufMax = BuildOutdoorPvpSubscribeRequestPayload(0xFFFFFFFFu);
	auto parsedMax = ParseOutdoorPvpSubscribeRequestPayload(bufMax.data(), bufMax.size());
	Assert(parsedMax.has_value() && parsedMax->zoneId == 0xFFFFFFFFu,
		"Subscribe boundary zoneId max");
}

static void TestSubscribeRequestRejectsShort()
{
	auto parsed = ParseOutdoorPvpSubscribeRequestPayload(nullptr, 4u);
	Assert(!parsed.has_value(), "Subscribe request rejects nullptr");
	uint8_t shortBuf[3]{};
	auto parsed2 = ParseOutdoorPvpSubscribeRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Subscribe request rejects 3 bytes");
}

static void TestSubscribeResponseOk()
{
	auto buf = BuildOutdoorPvpSubscribeResponsePayload(0u);
	Assert(buf.size() == 1u, "Subscribe response Ok size 1");
	auto parsed = ParseOutdoorPvpSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Subscribe Ok parses");
}

static void TestSubscribeResponseUnknownZone()
{
	auto buf = BuildOutdoorPvpSubscribeResponsePayload(
		static_cast<uint8_t>(OutdoorPvpErrorCode::UnknownZone));
	auto parsed = ParseOutdoorPvpSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Subscribe UnknownZone");
}

static void TestSubscribeResponseUnauthorized()
{
	auto buf = BuildOutdoorPvpSubscribeResponsePayload(
		static_cast<uint8_t>(OutdoorPvpErrorCode::Unauthorized));
	auto parsed = ParseOutdoorPvpSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 5u, "Subscribe Unauthorized");
}

static void TestSubscribeResponsePacket()
{
	auto pkt = BuildOutdoorPvpSubscribeResponsePacket(0u, 12345u, 0xBEEFull);
	Assert(!pkt.empty(), "Subscribe packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Subscribe PacketView parse OK");
	Assert(view.Opcode() == kOpcodeOutdoorPvpSubscribeResponse, "Opcode is SubscribeResponse");
	Assert(view.RequestId() == 12345u, "Subscribe RequestId preserved");
}

// -----------------------------------------------------------------------------
// OUTDOOR_PVP_UNSUBSCRIBE
// -----------------------------------------------------------------------------

static void TestUnsubscribeRequestRoundTrip()
{
	auto buf = BuildOutdoorPvpUnsubscribeRequestPayload(2u);
	Assert(buf.size() == 4u, "Unsubscribe request payload size 4");
	auto parsed = ParseOutdoorPvpUnsubscribeRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->zoneId == 2u, "Unsubscribe request parses zoneId 2");
}

static void TestUnsubscribeRequestRejectsShort()
{
	uint8_t shortBuf[3]{};
	auto parsed = ParseOutdoorPvpUnsubscribeRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Unsubscribe request rejects 3 bytes");
}

static void TestUnsubscribeResponseOk()
{
	auto buf = BuildOutdoorPvpUnsubscribeResponsePayload(0u);
	Assert(buf.size() == 1u, "Unsubscribe response Ok size 1");
	auto parsed = ParseOutdoorPvpUnsubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Unsubscribe Ok parses");
}

static void TestUnsubscribeResponseNotSubscribed()
{
	auto buf = BuildOutdoorPvpUnsubscribeResponsePayload(
		static_cast<uint8_t>(OutdoorPvpErrorCode::NotSubscribed));
	auto parsed = ParseOutdoorPvpUnsubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Unsubscribe NotSubscribed");
}

static void TestUnsubscribeResponsePacket()
{
	auto pkt = BuildOutdoorPvpUnsubscribeResponsePacket(0u, 99u, 0xFEEDull);
	Assert(!pkt.empty(), "Unsubscribe packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Unsubscribe PacketView parse OK");
	Assert(view.Opcode() == kOpcodeOutdoorPvpUnsubscribeResponse, "Opcode is UnsubscribeResponse");
	Assert(view.RequestId() == 99u, "Unsubscribe RequestId preserved");
}

// -----------------------------------------------------------------------------
// OUTDOOR_PVP_CAPTURE_START
// -----------------------------------------------------------------------------

static void TestCaptureStartRequestRoundTrip()
{
	auto buf = BuildOutdoorPvpCaptureStartRequestPayload(1u, 10u, 0u);
	Assert(buf.size() == 9u, "CaptureStart request payload size 9");
	auto parsed = ParseOutdoorPvpCaptureStartRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "CaptureStart request parses");
	if (parsed)
	{
		Assert(parsed->zoneId == 1u, "CaptureStart zoneId 1");
		Assert(parsed->objectiveId == 10u, "CaptureStart objectiveId 10");
		Assert(parsed->faction == 0u, "CaptureStart faction Alliance");
	}
}

static void TestCaptureStartRequestHorde()
{
	auto buf = BuildOutdoorPvpCaptureStartRequestPayload(2u, 23u, 1u);
	auto parsed = ParseOutdoorPvpCaptureStartRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->faction == 1u && parsed->objectiveId == 23u,
		"CaptureStart Horde objective 23");
}

static void TestCaptureStartRequestBoundary()
{
	auto buf = BuildOutdoorPvpCaptureStartRequestPayload(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFu);
	auto parsed = ParseOutdoorPvpCaptureStartRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->zoneId == 0xFFFFFFFFu
		&& parsed->objectiveId == 0xFFFFFFFFu && parsed->faction == 0xFFu,
		"CaptureStart boundary max values");
}

static void TestCaptureStartRequestRejectsShort()
{
	auto parsed = ParseOutdoorPvpCaptureStartRequestPayload(nullptr, 9u);
	Assert(!parsed.has_value(), "CaptureStart request rejects nullptr");
	uint8_t shortBuf[8]{};
	auto parsed2 = ParseOutdoorPvpCaptureStartRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "CaptureStart request rejects 8 bytes");
}

static void TestCaptureStartResponseOk()
{
	auto buf = BuildOutdoorPvpCaptureStartResponsePayload(0u);
	Assert(buf.size() == 1u, "CaptureStart response Ok size 1");
	auto parsed = ParseOutdoorPvpCaptureStartResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "CaptureStart Ok parses");
}

static void TestCaptureStartResponseUnknownObjective()
{
	auto buf = BuildOutdoorPvpCaptureStartResponsePayload(
		static_cast<uint8_t>(OutdoorPvpErrorCode::UnknownObjective));
	auto parsed = ParseOutdoorPvpCaptureStartResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "CaptureStart UnknownObjective");
}

static void TestCaptureStartResponseInvalidFaction()
{
	auto buf = BuildOutdoorPvpCaptureStartResponsePayload(
		static_cast<uint8_t>(OutdoorPvpErrorCode::InvalidFaction));
	auto parsed = ParseOutdoorPvpCaptureStartResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 4u, "CaptureStart InvalidFaction");
}

static void TestCaptureStartResponsePacket()
{
	auto pkt = BuildOutdoorPvpCaptureStartResponsePacket(0u, 7777u, 0xC0DEull);
	Assert(!pkt.empty(), "CaptureStart packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"CaptureStart PacketView parse OK");
	Assert(view.Opcode() == kOpcodeOutdoorPvpCaptureStartResponse, "Opcode is CaptureStartResponse");
	Assert(view.RequestId() == 7777u, "CaptureStart RequestId preserved");
}

// -----------------------------------------------------------------------------
// OUTDOOR_PVP_CAPTURE_PROGRESS_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestCaptureProgressRoundTrip()
{
	auto buf = BuildOutdoorPvpCaptureProgressNotificationPayload(1u, 10u, 50u, 0u);
	Assert(buf.size() == 13u, "CaptureProgress payload size 13");
	auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "CaptureProgress parses");
	if (parsed)
	{
		Assert(parsed->zoneId == 1u, "CaptureProgress zoneId");
		Assert(parsed->objectiveId == 10u, "CaptureProgress objectiveId");
		Assert(parsed->capturePct == 50u, "CaptureProgress pct 50");
		Assert(parsed->capturingBy == 0u, "CaptureProgress Alliance capturing");
	}
}

static void TestCaptureProgressZeroPct()
{
	auto buf = BuildOutdoorPvpCaptureProgressNotificationPayload(2u, 21u, 0u, 0xFFu);
	auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->capturePct == 0u && parsed->capturingBy == 0xFFu,
		"CaptureProgress 0% no capturing");
}

static void TestCaptureProgressFullPct()
{
	auto buf = BuildOutdoorPvpCaptureProgressNotificationPayload(2u, 22u, 100u, 1u);
	auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->capturePct == 100u && parsed->capturingBy == 1u,
		"CaptureProgress 100% Horde");
}

static void TestCaptureProgressRejectsShort()
{
	auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(nullptr, 13u);
	Assert(!parsed.has_value(), "CaptureProgress rejects nullptr");
	uint8_t shortBuf[12]{};
	auto parsed2 = ParseOutdoorPvpCaptureProgressNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "CaptureProgress rejects 12 bytes");
}

static void TestCaptureProgressPacket()
{
	auto pkt = BuildOutdoorPvpCaptureProgressNotificationPacket(1u, 11u, 75u, 1u, 0xFADEull);
	Assert(!pkt.empty(), "CaptureProgress packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"CaptureProgress PacketView parse OK");
	Assert(view.Opcode() == kOpcodeOutdoorPvpCaptureProgressNotification, "Opcode is CaptureProgress");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xFADEull, "CaptureProgress SessionId preserved");
	auto parsed = ParseOutdoorPvpCaptureProgressNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->capturePct == 75u && parsed->objectiveId == 11u,
		"CaptureProgress payload decodes");
}

// -----------------------------------------------------------------------------
// OUTDOOR_PVP_CAPTURE_COMPLETED_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestCaptureCompletedAlliance()
{
	auto buf = BuildOutdoorPvpCaptureCompletedNotificationPayload(1u, 10u, 0u, 4u, 2u);
	Assert(buf.size() == 17u, "CaptureCompleted payload size 17");
	auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "CaptureCompleted Alliance parses");
	if (parsed)
	{
		Assert(parsed->zoneId == 1u, "CaptureCompleted zoneId");
		Assert(parsed->objectiveId == 10u, "CaptureCompleted objectiveId");
		Assert(parsed->newOwner == 0u, "CaptureCompleted Alliance owner");
		Assert(parsed->allianceScore == 4u, "CaptureCompleted alliance score 4");
		Assert(parsed->hordeScore == 2u, "CaptureCompleted horde score 2");
	}
}

static void TestCaptureCompletedHorde()
{
	auto buf = BuildOutdoorPvpCaptureCompletedNotificationPayload(2u, 20u, 1u, 0u, 1u);
	auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->newOwner == 1u && parsed->hordeScore == 1u,
		"CaptureCompleted Horde");
}

static void TestCaptureCompletedNeutralReset()
{
	auto buf = BuildOutdoorPvpCaptureCompletedNotificationPayload(1u, 11u, 0xFFu, 0u, 0u);
	auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->newOwner == 0xFFu, "CaptureCompleted reset to neutral");
}

static void TestCaptureCompletedRejectsShort()
{
	auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(nullptr, 17u);
	Assert(!parsed.has_value(), "CaptureCompleted rejects nullptr");
	uint8_t shortBuf[16]{};
	auto parsed2 = ParseOutdoorPvpCaptureCompletedNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "CaptureCompleted rejects 16 bytes");
}

static void TestCaptureCompletedPacket()
{
	auto pkt = BuildOutdoorPvpCaptureCompletedNotificationPacket(1u, 12u, 0u, 5u, 3u, 0xABCDull);
	Assert(!pkt.empty(), "CaptureCompleted packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"CaptureCompleted PacketView parse OK");
	Assert(view.Opcode() == kOpcodeOutdoorPvpCaptureCompletedNotification, "Opcode is CaptureCompleted");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	auto parsed = ParseOutdoorPvpCaptureCompletedNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->zoneId == 1u && parsed->objectiveId == 12u
		&& parsed->newOwner == 0u && parsed->allianceScore == 5u && parsed->hordeScore == 3u,
		"CaptureCompleted payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestZoneListRequestRoundTrip();
	TestZoneListResponseEmpty();
	TestZoneListResponseSingleZoneNoObjectives();
	TestZoneListResponseFullWithObjectives();
	TestZoneListResponseError();
	TestZoneListResponseRejectsShort();
	TestZoneListResponsePacket();

	TestSubscribeRequestRoundTrip();
	TestSubscribeRequestBoundary();
	TestSubscribeRequestRejectsShort();
	TestSubscribeResponseOk();
	TestSubscribeResponseUnknownZone();
	TestSubscribeResponseUnauthorized();
	TestSubscribeResponsePacket();

	TestUnsubscribeRequestRoundTrip();
	TestUnsubscribeRequestRejectsShort();
	TestUnsubscribeResponseOk();
	TestUnsubscribeResponseNotSubscribed();
	TestUnsubscribeResponsePacket();

	TestCaptureStartRequestRoundTrip();
	TestCaptureStartRequestHorde();
	TestCaptureStartRequestBoundary();
	TestCaptureStartRequestRejectsShort();
	TestCaptureStartResponseOk();
	TestCaptureStartResponseUnknownObjective();
	TestCaptureStartResponseInvalidFaction();
	TestCaptureStartResponsePacket();

	TestCaptureProgressRoundTrip();
	TestCaptureProgressZeroPct();
	TestCaptureProgressFullPct();
	TestCaptureProgressRejectsShort();
	TestCaptureProgressPacket();

	TestCaptureCompletedAlliance();
	TestCaptureCompletedHorde();
	TestCaptureCompletedNeutralReset();
	TestCaptureCompletedRejectsShort();
	TestCaptureCompletedPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all outdoorpvp payload tests passed\n"
	                                : "[FAIL] some outdoorpvp tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
