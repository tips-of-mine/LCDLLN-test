/**
 * Phase 3.6.5 — Round-trip tests for CHARACTER_SAVE_POSITION request/response payloads.
 * Pure encoding tests — no DB / no network. Returns 0 on success, non-zero on first failure.
 */

#include "engine/network/CharacterPayloads.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/core/Log.h"

#include <cstdlib>
#include <iostream>

namespace
{
	static int s_failCount = 0;
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

static void TestRequestRoundTrip()
{
	auto buf = BuildCharacterSavePositionRequestPayload(987654321ull, 12.5f, 87.25f, -34.0f, 90.0f, -5.5f);
	Assert(!buf.empty(), "Build request not empty");
	Assert(buf.size() == 28u, "Build request emits exactly 28 bytes (uint64 + 5 floats)");
	auto parsed = ParseCharacterSavePositionRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Parse request OK");
	if (parsed)
	{
		Assert(parsed->characterId == 987654321ull, "characterId round-trips");
		Assert(parsed->x == 12.5f, "x round-trips");
		Assert(parsed->y == 87.25f, "y round-trips");
		Assert(parsed->z == -34.0f, "z round-trips");
		Assert(parsed->yawDeg == 90.0f, "yawDeg round-trips");
		Assert(parsed->pitchDeg == -5.5f, "pitchDeg round-trips");
	}
}

static void TestRequestRejectsTooShort()
{
	uint8_t shortBuf[27]{};
	auto parsed = ParseCharacterSavePositionRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Parse rejects payload < 28 bytes");
}

static void TestRequestRejectsNullPayload()
{
	auto parsed = ParseCharacterSavePositionRequestPayload(nullptr, 28u);
	Assert(!parsed.has_value(), "Parse rejects nullptr payload");
}

static void TestSuccessResponseRoundTrip()
{
	auto pkt = BuildCharacterSavePositionResponsePacket(1u, 33u, 0xC0FFEEFFull);
	Assert(!pkt.empty(), "Build success response packet");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeCharacterSavePositionResponse, "Opcode is CHARACTER_SAVE_POSITION_RESPONSE");
	Assert(view.RequestId() == 33u, "RequestId preserved");
	Assert(view.SessionId() == 0xC0FFEEFFull, "SessionId preserved");
	auto parsed = ParseCharacterSavePositionResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->success == 1u, "success=1 round-trips");
}

static void TestErrorResponseRoundTrip()
{
	auto pkt = BuildCharacterSavePositionResponsePacket(0u, 7u, 42u);
	Assert(!pkt.empty(), "Build error response packet");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	auto parsed = ParseCharacterSavePositionResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->success == 0u, "success=0 round-trips");
}

static void TestZeroPositionRoundTrip()
{
	// Edge case : position (0, 0, 0) is valid (origin spawn).
	auto buf = BuildCharacterSavePositionRequestPayload(1ull, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
	auto parsed = ParseCharacterSavePositionRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Parse zero position OK");
	if (parsed)
	{
		Assert(parsed->x == 0.0f, "x zero preserved");
		Assert(parsed->yawDeg == 0.0f, "yawDeg zero preserved");
	}
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	TestRequestRoundTrip();
	TestRequestRejectsTooShort();
	TestRequestRejectsNullPayload();
	TestSuccessResponseRoundTrip();
	TestErrorResponseRoundTrip();
	TestZeroPositionRoundTrip();

	engine::core::Log::Shutdown();
	return s_failCount == 0 ? 0 : 1;
}
