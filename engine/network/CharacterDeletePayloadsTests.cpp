/**
 * Phase 3.10 — Round-trip tests for CHARACTER_DELETE request/response payloads.
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
	auto buf = BuildCharacterDeleteRequestPayload(424242ull);
	Assert(!buf.empty(), "Build request not empty");
	Assert(buf.size() == 8u, "Build request emits exactly 8 bytes (uint64)");
	auto parsed = ParseCharacterDeleteRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Parse request OK");
	Assert(parsed && parsed->characterId == 424242ull, "Request characterId round-trips");
}

static void TestRequestRejectsTooShort()
{
	uint8_t shortBuf[7] = { 0, 0, 0, 0, 0, 0, 0 };
	auto parsed = ParseCharacterDeleteRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Parse rejects payload < 8 bytes");
}

static void TestRequestRejectsNullPayload()
{
	auto parsed = ParseCharacterDeleteRequestPayload(nullptr, 8u);
	Assert(!parsed.has_value(), "Parse rejects nullptr payload");
}

static void TestSuccessResponseRoundTrip()
{
	auto pkt = BuildCharacterDeleteResponsePacket(1u, 17u, 0xCAFEBABEull);
	Assert(!pkt.empty(), "Build success response packet");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeCharacterDeleteResponse, "Opcode is CHARACTER_DELETE_RESPONSE");
	Assert(view.RequestId() == 17u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEBABEull, "SessionId preserved");
	auto parsed = ParseCharacterDeleteResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value(), "Parse response OK");
	Assert(parsed && parsed->success == 1u, "success=1 round-trips");
}

static void TestErrorResponseRoundTrip()
{
	auto pkt = BuildCharacterDeleteResponsePacket(0u, 5u, 99u);
	Assert(!pkt.empty(), "Build error response packet");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	auto parsed = ParseCharacterDeleteResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value(), "Parse error response OK");
	Assert(parsed && parsed->success == 0u, "success=0 round-trips");
}

static void TestResponseRejectsEmptyPayload()
{
	uint8_t empty[1] = { 0 };
	auto parsed = ParseCharacterDeleteResponsePayload(empty, 0u);
	Assert(!parsed.has_value(), "Parse rejects empty payload");
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
	TestResponseRejectsEmptyPayload();

	engine::core::Log::Shutdown();
	return s_failCount == 0 ? 0 : 1;
}
