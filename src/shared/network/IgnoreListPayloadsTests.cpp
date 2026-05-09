/**
 * CMANGOS.25 (Phase 3.25 step 3+4) — Round-trip tests pour IGNORE_*_REQUEST /
 * IGNORE_*_RESPONSE payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 *
 * Test policy : symétrique aux MailPayloadsTests.cpp / QuestPayloadsTests.cpp.
 */

#include "src/shared/network/IgnoreListPayloads.h"
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
// IGNORE_ADD
// -----------------------------------------------------------------------------

static void TestAddRequestRoundTrip()
{
	auto buf = BuildIgnoreAddRequestPayload(424242ull);
	Assert(buf.size() == 8u, "Add request is 8 bytes");
	auto parsed = ParseIgnoreAddRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Add request parses OK");
	if (parsed)
	{
		Assert(parsed->targetAccountId == 424242ull, "Add request targetAccountId round-trips");
	}
}

static void TestAddRequestRejectsShort()
{
	auto parsed = ParseIgnoreAddRequestPayload(nullptr, 8u);
	Assert(!parsed.has_value(), "Add request rejects nullptr");
	uint8_t shortBuf[7]{};
	auto parsed2 = ParseIgnoreAddRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Add request rejects 7 bytes");
}

static void TestAddResponseRoundTrip()
{
	auto buf = BuildIgnoreAddResponsePayload(0u, 0xDEADBEEFull);
	auto parsed = ParseIgnoreAddResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Add response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "OK error code");
		Assert(parsed->targetAccountId == 0xDEADBEEFull, "targetAccountId round-trips");
	}

	auto bufErr = BuildIgnoreAddResponsePayload(static_cast<uint8_t>(IgnoreOpErrorCode::AlreadyIgnored), 7ull);
	auto parsedErr = ParseIgnoreAddResponsePayload(bufErr.data(), bufErr.size());
	Assert(parsedErr.has_value() && parsedErr->error == 1u, "AlreadyIgnored decodes");
	Assert(parsedErr && parsedErr->targetAccountId == 7ull, "Echo target on error");
}

static void TestAddResponsePacket()
{
	auto pkt = BuildIgnoreAddResponsePacket(0u, 99ull, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Add response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeIgnoreAddResponse, "Packet opcode is IgnoreAddResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseIgnoreAddResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->targetAccountId == 99ull, "Payload decodes from packet");
}

// -----------------------------------------------------------------------------
// IGNORE_REMOVE
// -----------------------------------------------------------------------------

static void TestRemoveRequestRoundTrip()
{
	auto buf = BuildIgnoreRemoveRequestPayload(123ull);
	Assert(buf.size() == 8u, "Remove request is 8 bytes");
	auto parsed = ParseIgnoreRemoveRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->targetAccountId == 123ull, "Remove request round-trips");
}

static void TestRemoveRequestRejectsShort()
{
	uint8_t shortBuf[7]{};
	auto parsed = ParseIgnoreRemoveRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Remove request rejects 7 bytes");
}

static void TestRemoveResponseRoundTrip()
{
	auto buf = BuildIgnoreRemoveResponsePayload(0u, 5ull);
	auto parsed = ParseIgnoreRemoveResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Remove response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Remove OK");
		Assert(parsed->targetAccountId == 5ull, "Remove echo target");
	}

	auto bufErr = BuildIgnoreRemoveResponsePayload(
		static_cast<uint8_t>(IgnoreOpErrorCode::NotIgnored), 5ull);
	auto parsedErr = ParseIgnoreRemoveResponsePayload(bufErr.data(), bufErr.size());
	Assert(parsedErr.has_value() && parsedErr->error == 2u, "NotIgnored decodes");
}

// -----------------------------------------------------------------------------
// IGNORE_LIST
// -----------------------------------------------------------------------------

static void TestListRequest()
{
	auto buf = BuildIgnoreListRequestPayload();
	Assert(buf.empty(), "List request payload is empty");
	auto parsed = ParseIgnoreListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildIgnoreListResponsePayload(0u, {});
	auto parsed = ParseIgnoreListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response (empty) parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Empty list is OK");
		Assert(parsed->ignoredAccountIds.empty(), "Empty list has no entries");
	}
}

static void TestListResponseUnauthorized()
{
	// Quand error != 0, on ne sérialise pas le count (économie de bande passante).
	auto buf = BuildIgnoreListResponsePayload(static_cast<uint8_t>(IgnoreOpErrorCode::Unauthorized), {});
	auto parsed = ParseIgnoreListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "Unauthorized decodes");
	Assert(parsed && parsed->ignoredAccountIds.empty(), "No entries on error");
}

static void TestListResponseMultiple()
{
	std::vector<uint64_t> ids;
	ids.push_back(100ull);
	ids.push_back(200ull);
	ids.push_back(0xFFFFFFFFFFFFFFFFull);

	auto buf = BuildIgnoreListResponsePayload(0u, ids);
	auto parsed = ParseIgnoreListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response (3 entries) parses OK");
	if (parsed && parsed->ignoredAccountIds.size() == 3u)
	{
		Assert(parsed->ignoredAccountIds[0] == 100ull, "id[0]");
		Assert(parsed->ignoredAccountIds[1] == 200ull, "id[1]");
		Assert(parsed->ignoredAccountIds[2] == 0xFFFFFFFFFFFFFFFFull, "id[2] uint64 max");
	}
	else
	{
		Assert(false, "List response should have 3 entries");
	}
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseIgnoreListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List response rejects nullptr");
	uint8_t one[1]{0};
	auto parsed2 = ParseIgnoreListResponsePayload(one, 0u);
	Assert(!parsed2.has_value(), "List response rejects size 0");
}

static void TestListResponsePacket()
{
	std::vector<uint64_t> ids;
	ids.push_back(42ull);
	auto pkt = BuildIgnoreListResponsePacket(0u, ids, 999u, 0xBEEFull);
	Assert(!pkt.empty(), "List response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeIgnoreListResponse, "Packet opcode is IgnoreListResponse");
	Assert(view.RequestId() == 999u, "RequestId preserved");
	auto parsed = ParseIgnoreListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->ignoredAccountIds.size() == 1u, "Payload decodes");
	Assert(parsed && parsed->ignoredAccountIds[0] == 42ull, "List entry round-trips");
}

// -----------------------------------------------------------------------------
// Boundary
// -----------------------------------------------------------------------------

static void TestBoundaryValues()
{
	auto buf = BuildIgnoreAddRequestPayload(0xFFFFFFFFFFFFFFFFull);
	auto parsed = ParseIgnoreAddRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Boundary uint64 parses");
	if (parsed)
	{
		Assert(parsed->targetAccountId == 0xFFFFFFFFFFFFFFFFull, "uint64 max target");
	}
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	// IGNORE_ADD
	TestAddRequestRoundTrip();
	TestAddRequestRejectsShort();
	TestAddResponseRoundTrip();
	TestAddResponsePacket();

	// IGNORE_REMOVE
	TestRemoveRequestRoundTrip();
	TestRemoveRequestRejectsShort();
	TestRemoveResponseRoundTrip();

	// IGNORE_LIST
	TestListRequest();
	TestListResponseEmpty();
	TestListResponseUnauthorized();
	TestListResponseMultiple();
	TestListResponseRejectsShort();
	TestListResponsePacket();

	// Edge cases
	TestBoundaryValues();

	std::cerr << (s_failCount == 0 ? "[OK] all ignore_list payload tests passed\n"
	                                : "[FAIL] some ignore_list tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
