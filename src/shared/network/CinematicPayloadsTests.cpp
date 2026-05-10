/**
 * CMANGOS.30 (Phase 5.30 step 3+4) — Round-trip tests for CINEMATIC_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/CinematicPayloads.h"
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
// CINEMATIC_PLAY_NOTIFICATION (push, 108)
// -----------------------------------------------------------------------------

static void TestPlayNotificationRoundTrip()
{
	auto buf = BuildCinematicPlayNotificationPayload(42u, static_cast<uint8_t>(CinematicReason::ZoneEnter));
	Assert(buf.size() == 5u, "Play notification payload size 5");
	auto parsed = ParseCinematicPlayNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Play notification parses");
	if (parsed)
	{
		Assert(parsed->sequenceId == 42u, "Play sequenceId 42");
		Assert(parsed->reason == 0u, "Play reason ZoneEnter");
	}
}

static void TestPlayNotificationBoundary()
{
	auto buf = BuildCinematicPlayNotificationPayload(0xFFFFFFFFu, static_cast<uint8_t>(CinematicReason::Other));
	auto parsed = ParseCinematicPlayNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->sequenceId == 0xFFFFFFFFu && parsed->reason == 3u,
		"Play boundary: sequenceId max + reason Other");
}

static void TestPlayNotificationRejectsShort()
{
	auto parsed = ParseCinematicPlayNotificationPayload(nullptr, 5u);
	Assert(!parsed.has_value(), "Play notification rejects nullptr");
	uint8_t shortBuf[4]{};
	auto parsed2 = ParseCinematicPlayNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Play notification rejects 4 bytes");
}

static void TestPlayNotificationPacket()
{
	auto pkt = BuildCinematicPlayNotificationPacket(7u, static_cast<uint8_t>(CinematicReason::Intro), 0xC0DEull);
	Assert(!pkt.empty(), "Play notification packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Play PacketView parse OK");
	Assert(view.Opcode() == kOpcodeCinematicPlayNotification, "Opcode is CinematicPlayNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xC0DEull, "SessionId preserved");
	auto parsed = ParseCinematicPlayNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->sequenceId == 7u && parsed->reason == 2u,
		"Play notification payload decodes");
}

// -----------------------------------------------------------------------------
// CINEMATIC_ACK
// -----------------------------------------------------------------------------

static void TestAckRequestRoundTrip()
{
	auto buf = BuildCinematicAckRequestPayload(123u,
		static_cast<uint8_t>(CinematicCompletionState::EndedNormally));
	Assert(buf.size() == 5u, "Ack request payload size 5");
	auto parsed = ParseCinematicAckRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Ack request parses");
	if (parsed)
	{
		Assert(parsed->sequenceId == 123u, "Ack sequenceId 123");
		Assert(parsed->completionState == 0u, "Ack completionState EndedNormally");
	}
}

static void TestAckRequestSkipped()
{
	auto buf = BuildCinematicAckRequestPayload(456u,
		static_cast<uint8_t>(CinematicCompletionState::SkippedByUser));
	auto parsed = ParseCinematicAckRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->completionState == 1u, "Ack SkippedByUser");
}

static void TestAckRequestRejectsShort()
{
	auto parsed = ParseCinematicAckRequestPayload(nullptr, 5u);
	Assert(!parsed.has_value(), "Ack request rejects nullptr");
	uint8_t shortBuf[4]{};
	auto parsed2 = ParseCinematicAckRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Ack request rejects 4 bytes");
}

static void TestAckResponseOk()
{
	auto buf = BuildCinematicAckResponsePayload(0u);
	Assert(buf.size() == 1u, "Ack response Ok size 1");
	auto parsed = ParseCinematicAckResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Ack response Ok parses");
}

static void TestAckResponseError()
{
	auto buf = BuildCinematicAckResponsePayload(
		static_cast<uint8_t>(CinematicErrorCode::Unauthorized));
	auto parsed = ParseCinematicAckResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "Ack response Unauthorized");
}

static void TestAckResponsePacket()
{
	auto pkt = BuildCinematicAckResponsePacket(0u, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Ack response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Ack PacketView parse OK");
	Assert(view.Opcode() == kOpcodeCinematicAckResponse, "Opcode is CinematicAckResponse");
	Assert(view.RequestId() == 12345u, "Ack RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "Ack SessionId preserved");
}

// -----------------------------------------------------------------------------
// CINEMATIC_SKIP
// -----------------------------------------------------------------------------

static void TestSkipRequestRoundTrip()
{
	auto buf = BuildCinematicSkipRequestPayload(789u);
	Assert(buf.size() == 4u, "Skip request payload size 4");
	auto parsed = ParseCinematicSkipRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->sequenceId == 789u, "Skip request parses");
}

static void TestSkipRequestRejectsShort()
{
	auto parsed = ParseCinematicSkipRequestPayload(nullptr, 4u);
	Assert(!parsed.has_value(), "Skip request rejects nullptr");
	uint8_t shortBuf[3]{};
	auto parsed2 = ParseCinematicSkipRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Skip request rejects 3 bytes");
}

static void TestSkipResponseAllowed()
{
	auto buf = BuildCinematicSkipResponsePayload(0u, true);
	Assert(buf.size() == 2u, "Skip response size 2");
	auto parsed = ParseCinematicSkipResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Skip response allowed parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Skip error 0");
		Assert(parsed->allowed == true, "Skip allowed true");
	}
}

static void TestSkipResponseDenied()
{
	auto buf = BuildCinematicSkipResponsePayload(
		static_cast<uint8_t>(CinematicErrorCode::SkipNotAllowed), false);
	auto parsed = ParseCinematicSkipResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u && parsed->allowed == false,
		"Skip response SkipNotAllowed");
}

static void TestSkipResponseRejectsShort()
{
	auto parsed = ParseCinematicSkipResponsePayload(nullptr, 2u);
	Assert(!parsed.has_value(), "Skip response rejects nullptr");
	uint8_t shortBuf[1]{};
	auto parsed2 = ParseCinematicSkipResponsePayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Skip response rejects 1 byte");
}

static void TestSkipResponsePacket()
{
	auto pkt = BuildCinematicSkipResponsePacket(0u, true, 99u, 0xBEEFull);
	Assert(!pkt.empty(), "Skip response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Skip PacketView parse OK");
	Assert(view.Opcode() == kOpcodeCinematicSkipResponse, "Opcode is CinematicSkipResponse");
	Assert(view.RequestId() == 99u, "Skip RequestId preserved");
	auto parsed = ParseCinematicSkipResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->allowed == true, "Skip payload decodes allowed");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestPlayNotificationRoundTrip();
	TestPlayNotificationBoundary();
	TestPlayNotificationRejectsShort();
	TestPlayNotificationPacket();

	TestAckRequestRoundTrip();
	TestAckRequestSkipped();
	TestAckRequestRejectsShort();
	TestAckResponseOk();
	TestAckResponseError();
	TestAckResponsePacket();

	TestSkipRequestRoundTrip();
	TestSkipRequestRejectsShort();
	TestSkipResponseAllowed();
	TestSkipResponseDenied();
	TestSkipResponseRejectsShort();
	TestSkipResponsePacket();

	std::cerr << (s_failCount == 0 ? "[OK] all cinematic payload tests passed\n"
	                                : "[FAIL] some cinematic tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
