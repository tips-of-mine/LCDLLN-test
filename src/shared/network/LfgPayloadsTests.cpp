/**
 * CMANGOS.33 (Phase 5.33 step 3+4) — Round-trip tests for LFG_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/LfgPayloads.h"
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
// LFG_QUEUE
// -----------------------------------------------------------------------------

static void TestQueueRequestRoundTrip()
{
	auto buf = BuildLfgQueueRequestPayload(1u, 42u);
	Assert(buf.size() == 5u, "Queue request payload size 5");
	auto parsed = ParseLfgQueueRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Queue request parses");
	if (parsed)
	{
		Assert(parsed->role == 1u, "Queue role 1");
		Assert(parsed->dungeonId == 42u, "Queue dungeonId 42");
	}
}

static void TestQueueRequestBoundary()
{
	auto buf = BuildLfgQueueRequestPayload(2u, 0xFFFFFFFFu);
	auto parsed = ParseLfgQueueRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->role == 2u && parsed->dungeonId == 0xFFFFFFFFu,
		"Queue boundary: role 2 + dungeonId max");
}

static void TestQueueRequestRejectsShort()
{
	auto parsed = ParseLfgQueueRequestPayload(nullptr, 5u);
	Assert(!parsed.has_value(), "Queue request rejects nullptr");
	uint8_t shortBuf[4]{};
	auto parsed2 = ParseLfgQueueRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Queue request rejects 4 bytes");
}

static void TestQueueResponseOk()
{
	auto buf = BuildLfgQueueResponsePayload(0u, 60u);
	Assert(buf.size() == 5u, "Queue response Ok size 5");
	auto parsed = ParseLfgQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->estimatedWaitSec == 60u,
		"Queue response Ok parses");
}

static void TestQueueResponseError()
{
	auto buf = BuildLfgQueueResponsePayload(
		static_cast<uint8_t>(LfgErrorCode::AlreadyQueued), 0u);
	Assert(buf.size() == 1u, "Queue response error has only 1 byte");
	auto parsed = ParseLfgQueueResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Queue response AlreadyQueued");
}

static void TestQueueResponsePacket()
{
	auto pkt = BuildLfgQueueResponsePacket(0u, 120u, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "Queue response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Queue PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLfgQueueResponse, "Opcode is LfgQueueResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseLfgQueueResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->estimatedWaitSec == 120u,
		"Queue response payload decodes");
}

// -----------------------------------------------------------------------------
// LFG_LEAVE
// -----------------------------------------------------------------------------

static void TestLeaveRequestRoundTrip()
{
	auto buf = BuildLfgLeaveRequestPayload();
	Assert(buf.empty(), "Leave request payload empty");
	auto parsed = ParseLfgLeaveRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Leave request accepts empty payload");
}

static void TestLeaveResponseOk()
{
	auto buf = BuildLfgLeaveResponsePayload(0u);
	Assert(buf.size() == 1u, "Leave response Ok size 1");
	auto parsed = ParseLfgLeaveResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Leave response Ok parses");
}

static void TestLeaveResponseNotInQueue()
{
	auto buf = BuildLfgLeaveResponsePayload(
		static_cast<uint8_t>(LfgErrorCode::NotInQueue));
	auto parsed = ParseLfgLeaveResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Leave response NotInQueue");
}

static void TestLeaveResponsePacket()
{
	auto pkt = BuildLfgLeaveResponsePacket(0u, 99u, 0xBEEFull);
	Assert(!pkt.empty(), "Leave response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Leave PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLfgLeaveResponse, "Opcode is LfgLeaveResponse");
	Assert(view.RequestId() == 99u, "Leave RequestId preserved");
}

// -----------------------------------------------------------------------------
// LFG_STATUS
// -----------------------------------------------------------------------------

static void TestStatusRequestRoundTrip()
{
	auto buf = BuildLfgStatusRequestPayload();
	Assert(buf.empty(), "Status request payload empty");
	auto parsed = ParseLfgStatusRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Status request accepts empty payload");
}

static void TestStatusResponseInQueue()
{
	auto buf = BuildLfgStatusResponsePayload(0u, true, 1u, 42u, 30u);
	Assert(buf.size() == 11u, "Status response size 11");
	auto parsed = ParseLfgStatusResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Status response in-queue parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Status error 0");
		Assert(parsed->inQueue == true, "Status inQueue true");
		Assert(parsed->role == 1u, "Status role 1");
		Assert(parsed->dungeonId == 42u, "Status dungeonId 42");
		Assert(parsed->elapsedSec == 30u, "Status elapsedSec 30");
	}
}

static void TestStatusResponseNotInQueue()
{
	auto buf = BuildLfgStatusResponsePayload(0u, false, 0u, 0u, 0u);
	auto parsed = ParseLfgStatusResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->inQueue == false, "Status not in queue");
}

static void TestStatusResponseUnauthorized()
{
	auto buf = BuildLfgStatusResponsePayload(
		static_cast<uint8_t>(LfgErrorCode::Unauthorized), false, 0u, 0u, 0u);
	auto parsed = ParseLfgStatusResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "Status Unauthorized");
}

static void TestStatusResponseRejectsShort()
{
	auto parsed = ParseLfgStatusResponsePayload(nullptr, 11u);
	Assert(!parsed.has_value(), "Status response rejects nullptr");
	uint8_t shortBuf[10]{};
	auto parsed2 = ParseLfgStatusResponsePayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Status response rejects 10 bytes");
}

static void TestStatusResponsePacket()
{
	auto pkt = BuildLfgStatusResponsePacket(0u, true, 2u, 7u, 15u, 100u, 0xFEEDull);
	Assert(!pkt.empty(), "Status response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Status PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLfgStatusResponse, "Opcode is LfgStatusResponse");
	auto parsed = ParseLfgStatusResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->role == 2u && parsed->dungeonId == 7u,
		"Status payload decodes");
}

// -----------------------------------------------------------------------------
// LFG_MATCH_PROPOSAL_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestMatchProposalEmpty()
{
	auto buf = BuildLfgMatchProposalNotificationPayload(123ull, 42u, {});
	auto parsed = ParseLfgMatchProposalNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Match proposal empty parses");
	if (parsed)
	{
		Assert(parsed->proposalId == 123ull, "Proposal id 123");
		Assert(parsed->dungeonId == 42u, "Proposal dungeonId 42");
		Assert(parsed->members.empty(), "Proposal members empty");
	}
}

static void TestMatchProposalFullGroup()
{
	std::vector<LfgMatchMember> members;
	members.push_back({1001ull, 0u}); // Tank
	members.push_back({1002ull, 1u}); // Healer
	members.push_back({1003ull, 2u}); // Damage
	members.push_back({1004ull, 2u}); // Damage
	members.push_back({1005ull, 2u}); // Damage
	auto buf = BuildLfgMatchProposalNotificationPayload(0xDEADBEEFull, 99u, members);
	auto parsed = ParseLfgMatchProposalNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Match proposal full group parses");
	if (parsed && parsed->members.size() == 5u)
	{
		Assert(parsed->proposalId == 0xDEADBEEFull, "Proposal id big");
		Assert(parsed->dungeonId == 99u, "Proposal dungeon 99");
		Assert(parsed->members[0].accountId == 1001ull && parsed->members[0].role == 0u,
			"Tank ok");
		Assert(parsed->members[1].accountId == 1002ull && parsed->members[1].role == 1u,
			"Healer ok");
		Assert(parsed->members[2].role == 2u, "Damage 1 ok");
		Assert(parsed->members[3].role == 2u, "Damage 2 ok");
		Assert(parsed->members[4].role == 2u, "Damage 3 ok");
	}
	else
	{
		Assert(false, "Match proposal should have 5 members");
	}
}

static void TestMatchProposalRejectsShort()
{
	auto parsed = ParseLfgMatchProposalNotificationPayload(nullptr, 14u);
	Assert(!parsed.has_value(), "Match proposal rejects nullptr");
	uint8_t shortBuf[13]{};
	auto parsed2 = ParseLfgMatchProposalNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Match proposal rejects 13 bytes");
}

static void TestMatchProposalPacket()
{
	std::vector<LfgMatchMember> members;
	members.push_back({42ull, 0u});
	auto pkt = BuildLfgMatchProposalNotificationPacket(7ull, 1u, members, 0xC0DEull);
	Assert(!pkt.empty(), "Match proposal packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Match proposal PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLfgMatchProposalNotification, "Opcode is LfgMatchProposalNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xC0DEull, "SessionId preserved");
	auto parsed = ParseLfgMatchProposalNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->proposalId == 7ull
		&& parsed->members.size() == 1u && parsed->members[0].accountId == 42ull,
		"Match proposal payload decodes");
}

// -----------------------------------------------------------------------------
// LFG_MATCH_ACCEPT request (request only — no dedicated response opcode in V1)
// -----------------------------------------------------------------------------

static void TestMatchAcceptRequestRoundTrip()
{
	auto buf = BuildLfgMatchAcceptRequestPayload(0xCAFEull, true);
	Assert(buf.size() == 9u, "MatchAccept request size 9");
	auto parsed = ParseLfgMatchAcceptRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "MatchAccept request parses");
	if (parsed)
	{
		Assert(parsed->proposalId == 0xCAFEull, "Proposal id preserved");
		Assert(parsed->accept == true, "accept true");
	}
}

static void TestMatchAcceptRequestReject()
{
	auto buf = BuildLfgMatchAcceptRequestPayload(0xBEEFull, false);
	auto parsed = ParseLfgMatchAcceptRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->accept == false, "MatchAccept reject");
}

static void TestMatchAcceptRequestRejectsShort()
{
	auto parsed = ParseLfgMatchAcceptRequestPayload(nullptr, 9u);
	Assert(!parsed.has_value(), "MatchAccept rejects nullptr");
	uint8_t shortBuf[8]{};
	auto parsed2 = ParseLfgMatchAcceptRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "MatchAccept rejects 8 bytes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestQueueRequestRoundTrip();
	TestQueueRequestBoundary();
	TestQueueRequestRejectsShort();
	TestQueueResponseOk();
	TestQueueResponseError();
	TestQueueResponsePacket();

	TestLeaveRequestRoundTrip();
	TestLeaveResponseOk();
	TestLeaveResponseNotInQueue();
	TestLeaveResponsePacket();

	TestStatusRequestRoundTrip();
	TestStatusResponseInQueue();
	TestStatusResponseNotInQueue();
	TestStatusResponseUnauthorized();
	TestStatusResponseRejectsShort();
	TestStatusResponsePacket();

	TestMatchProposalEmpty();
	TestMatchProposalFullGroup();
	TestMatchProposalRejectsShort();
	TestMatchProposalPacket();

	TestMatchAcceptRequestRoundTrip();
	TestMatchAcceptRequestReject();
	TestMatchAcceptRequestRejectsShort();

	std::cerr << (s_failCount == 0 ? "[OK] all lfg payload tests passed\n"
	                                : "[FAIL] some lfg tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
