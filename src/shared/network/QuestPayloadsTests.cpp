/**
 * CMANGOS.23 (Phase 5.23 step 3+4) — Round-trip tests for QUEST_*_REQUEST /
 * QUEST_*_RESPONSE / QUEST_STATE_UPDATE payloads. Pure encoding tests
 * (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 *
 * Test policy : symetrique aux MailPayloadsTests.cpp — pas de framework de
 * test externe, juste un Assert local + main() qui appelle chaque suite.
 */

#include "src/shared/network/QuestPayloads.h"
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
// QUEST_ACCEPT
// -----------------------------------------------------------------------------

static void TestAcceptRequestRoundTrip()
{
	auto buf = BuildQuestAcceptRequestPayload(42u);
	Assert(buf.size() == 4u, "Accept request is 4 bytes");
	auto parsed = ParseQuestAcceptRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Accept request parses OK");
	if (parsed)
		Assert(parsed->questId == 42u, "Accept request questId round-trips");
}

static void TestAcceptRequestRejectsShort()
{
	auto parsed = ParseQuestAcceptRequestPayload(nullptr, 4u);
	Assert(!parsed.has_value(), "Accept request rejects nullptr");
	uint8_t shortBuf[3]{};
	auto parsed2 = ParseQuestAcceptRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Accept request rejects 3 bytes");
}

static void TestAcceptResponseRoundTrip()
{
	auto buf = BuildQuestAcceptResponsePayload(0u, 42u, 2u /*Accepted*/);
	Assert(buf.size() == 6u, "Accept response is 6 bytes");
	auto parsed = ParseQuestAcceptResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Accept response parses OK");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Accept response error 0");
		Assert(parsed->questId == 42u, "Accept response questId");
		Assert(parsed->newStatus == 2u, "Accept response status");
	}
}

static void TestAcceptResponseError()
{
	auto buf = BuildQuestAcceptResponsePayload(
		static_cast<uint8_t>(QuestOpErrorCode::WrongStatus), 42u, 0u);
	auto parsed = ParseQuestAcceptResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Accept WrongStatus decodes");
}

static void TestAcceptResponsePacket()
{
	auto pkt = BuildQuestAcceptResponsePacket(0u, 42u, 2u, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Accept response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeQuestAcceptResponse, "Packet opcode is QuestAcceptResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseQuestAcceptResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->questId == 42u, "Accept response payload decodes");
}

// -----------------------------------------------------------------------------
// QUEST_COMPLETE
// -----------------------------------------------------------------------------

static void TestCompleteRequestRoundTrip()
{
	auto buf = BuildQuestCompleteRequestPayload(7u);
	auto parsed = ParseQuestCompleteRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->questId == 7u, "Complete request round-trips");
}

static void TestCompleteResponseRoundTrip()
{
	auto buf = BuildQuestCompleteResponsePayload(0u, 7u, 3u /*Completed*/);
	auto parsed = ParseQuestCompleteResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->newStatus == 3u, "Complete response round-trips");
}

// -----------------------------------------------------------------------------
// QUEST_REWARD
// -----------------------------------------------------------------------------

static void TestRewardRequestRoundTrip()
{
	auto buf = BuildQuestRewardRequestPayload(99u);
	auto parsed = ParseQuestRewardRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->questId == 99u, "Reward request round-trips");
}

static void TestRewardResponseRoundTrip()
{
	auto buf = BuildQuestRewardResponsePayload(
		static_cast<uint8_t>(QuestOpErrorCode::NotImplementedYet), 99u, 3u);
	auto parsed = ParseQuestRewardResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 7u, "Reward NotImplementedYet decodes");
}

// -----------------------------------------------------------------------------
// QUEST_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildQuestListRequestPayload();
	Assert(buf.empty(), "List request payload empty");
	auto parsed = ParseQuestListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildQuestListResponsePayload(0u, {});
	auto parsed = ParseQuestListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response empty parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "List response empty error 0");
		Assert(parsed->quests.empty(), "List response empty has 0 quests");
	}
}

static void TestListResponseUnauthorized()
{
	auto buf = BuildQuestListResponsePayload(
		static_cast<uint8_t>(QuestOpErrorCode::Unauthorized), {});
	auto parsed = ParseQuestListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "List response Unauthorized");
}

static void TestListResponseMultiple()
{
	std::vector<QuestStateEntry> quests;
	quests.push_back({1u, 2u}); // Accepted
	quests.push_back({2u, 3u}); // Completed
	quests.push_back({100u, 4u}); // Rewarded
	auto buf = BuildQuestListResponsePayload(0u, quests);
	auto parsed = ParseQuestListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response multiple parses");
	if (parsed && parsed->quests.size() == 3u)
	{
		Assert(parsed->quests[0].questId == 1u && parsed->quests[0].status == 2u, "entry[0]");
		Assert(parsed->quests[1].questId == 2u && parsed->quests[1].status == 3u, "entry[1]");
		Assert(parsed->quests[2].questId == 100u && parsed->quests[2].status == 4u, "entry[2]");
	}
	else
	{
		Assert(false, "List response should have 3 entries");
	}
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseQuestListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List response rejects nullptr");
	uint8_t one[1]{0};
	auto parsed2 = ParseQuestListResponsePayload(one, 0u);
	Assert(!parsed2.has_value(), "List response rejects size 0");
}

// -----------------------------------------------------------------------------
// QUEST_STATE_UPDATE (push)
// -----------------------------------------------------------------------------

static void TestStateUpdateRoundTrip()
{
	auto buf = BuildQuestStateUpdatePayload(123u, 5u /*Failed*/);
	Assert(buf.size() == 5u, "StateUpdate is 5 bytes");
	auto parsed = ParseQuestStateUpdatePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "StateUpdate parses");
	if (parsed)
	{
		Assert(parsed->questId == 123u, "StateUpdate questId");
		Assert(parsed->newStatus == 5u, "StateUpdate status Failed");
	}
}

static void TestStateUpdatePacket()
{
	auto pkt = BuildQuestStateUpdatePacket(123u, 5u, 0xCAFEull);
	Assert(!pkt.empty(), "StateUpdate packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "StateUpdate PacketView parse OK");
	Assert(view.Opcode() == kOpcodeQuestStateUpdate, "Opcode is QuestStateUpdate");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseQuestStateUpdatePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->questId == 123u, "StateUpdate payload decodes");
}

// -----------------------------------------------------------------------------
// Boundary
// -----------------------------------------------------------------------------

static void TestBoundaryQuestId()
{
	auto buf = BuildQuestAcceptRequestPayload(0xFFFFFFFFu);
	auto parsed = ParseQuestAcceptRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->questId == 0xFFFFFFFFu, "questId max round-trips");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestAcceptRequestRoundTrip();
	TestAcceptRequestRejectsShort();
	TestAcceptResponseRoundTrip();
	TestAcceptResponseError();
	TestAcceptResponsePacket();

	TestCompleteRequestRoundTrip();
	TestCompleteResponseRoundTrip();

	TestRewardRequestRoundTrip();
	TestRewardResponseRoundTrip();

	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseUnauthorized();
	TestListResponseMultiple();
	TestListResponseRejectsShort();

	TestStateUpdateRoundTrip();
	TestStateUpdatePacket();

	TestBoundaryQuestId();

	std::cerr << (s_failCount == 0 ? "[OK] all quest payload tests passed\n" : "[FAIL] some quest tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
