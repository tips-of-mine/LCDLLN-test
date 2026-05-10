/**
 * CMANGOS.39 (Phase 4.39 step 3+4) — Round-trip tests for SKILL_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/SkillPayloads.h"
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
// SKILLS_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildSkillsListRequestPayload();
	Assert(buf.empty(), "List request payload empty");
	auto parsed = ParseSkillsListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildSkillsListResponsePayload(0u, {});
	auto parsed = ParseSkillsListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response empty parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "List response empty error 0");
		Assert(parsed->skills.empty(), "List response empty has 0 entries");
	}
}

static void TestListResponseUnauthorized()
{
	auto buf = BuildSkillsListResponsePayload(
		static_cast<uint8_t>(SkillErrorCode::Unauthorized), {});
	auto parsed = ParseSkillsListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 6u, "List response Unauthorized");
}

static void TestListResponseSingle()
{
	std::vector<SkillBookEntry> entries;
	entries.push_back({1u, 25u, 75u, 0u}); // Cooking partial
	auto buf = BuildSkillsListResponsePayload(0u, entries);
	auto parsed = ParseSkillsListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response single parses");
	if (parsed && parsed->skills.size() == 1u)
	{
		Assert(parsed->skills[0].skillId == 1u, "single skillId");
		Assert(parsed->skills[0].value == 25u,  "single value");
		Assert(parsed->skills[0].cap == 75u,    "single cap");
		Assert(parsed->skills[0].bonus == 0u,   "single bonus");
	}
	else
	{
		Assert(false, "List response should have 1 entry");
	}
}

static void TestListResponseMultiple()
{
	std::vector<SkillBookEntry> entries;
	entries.push_back({1u, 1u,   75u,  0u});   // Cooking starter
	entries.push_back({2u, 50u,  75u, 10u});   // Herbalism with bonus
	entries.push_back({3u, 75u,  75u,  0u});   // Mining at cap
	entries.push_back({4u, 0u,   75u,  0u});   // FirstAid not yet started
	entries.push_back({5u, 30u,  75u,  5u});   // Lockpicking
	auto buf = BuildSkillsListResponsePayload(0u, entries);
	auto parsed = ParseSkillsListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List response multiple parses");
	if (parsed && parsed->skills.size() == 5u)
	{
		Assert(parsed->skills[0].skillId == 1u && parsed->skills[0].value == 1u
			&& parsed->skills[0].cap == 75u && parsed->skills[0].bonus == 0u,
			"entry[0] Cooking starter");
		Assert(parsed->skills[1].skillId == 2u && parsed->skills[1].value == 50u
			&& parsed->skills[1].cap == 75u && parsed->skills[1].bonus == 10u,
			"entry[1] Herbalism with bonus");
		Assert(parsed->skills[2].skillId == 3u && parsed->skills[2].value == 75u
			&& parsed->skills[2].cap == 75u, "entry[2] Mining at cap");
		Assert(parsed->skills[3].skillId == 4u && parsed->skills[3].value == 0u,
			"entry[3] FirstAid not started");
		Assert(parsed->skills[4].skillId == 5u && parsed->skills[4].bonus == 5u,
			"entry[4] Lockpicking with bonus");
	}
	else
	{
		Assert(false, "List response should have 5 entries");
	}
}

static void TestListResponseSkillIdZero()
{
	// Edge case : skillId = 0 (interpretable cote serveur comme UnknownSkill,
	// mais le wire doit le serialiser fidelement).
	std::vector<SkillBookEntry> entries;
	entries.push_back({0u, 0u, 0u, 0u});
	auto buf = BuildSkillsListResponsePayload(0u, entries);
	auto parsed = ParseSkillsListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List with skillId=0 parses");
	if (parsed && parsed->skills.size() == 1u)
	{
		Assert(parsed->skills[0].skillId == 0u, "skillId=0 preserved on wire");
	}
}

static void TestListResponseValueGtCapBitwise()
{
	// Le serveur clamp normalement value <= cap, mais si pour une raison
	// quelconque la valeur arrive > cap (corruption ou bug futur), le wire
	// doit la transmettre fidelement (le clamp sera applique cote presenter ou UI).
	std::vector<SkillBookEntry> entries;
	entries.push_back({10u, 100u, 75u, 0u}); // value > cap (anormal)
	auto buf = BuildSkillsListResponsePayload(0u, entries);
	auto parsed = ParseSkillsListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "List value > cap still parses");
	if (parsed && parsed->skills.size() == 1u)
	{
		Assert(parsed->skills[0].value == 100u, "value=100 preserved bitwise");
		Assert(parsed->skills[0].cap == 75u,    "cap=75 preserved bitwise");
	}
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseSkillsListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List response rejects nullptr");
	uint8_t one[1]{0};
	auto parsed2 = ParseSkillsListResponsePayload(one, 0u);
	Assert(!parsed2.has_value(), "List response rejects size 0");
}

static void TestListResponsePacket()
{
	std::vector<SkillBookEntry> entries;
	entries.push_back({1u, 25u, 75u, 0u});
	auto pkt = BuildSkillsListResponsePacket(0u, entries, 12345u, 0xCAFEull);
	Assert(!pkt.empty(), "List response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeSkillsListResponse, "Packet opcode is SkillsListResponse");
	Assert(view.RequestId() == 12345u, "RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "SessionId preserved");
	auto parsed = ParseSkillsListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->skills.size() == 1u
		&& parsed->skills[0].skillId == 1u, "List response payload decodes");
}

// -----------------------------------------------------------------------------
// SKILL_LEARN
// -----------------------------------------------------------------------------

static void TestLearnRequestRoundTrip()
{
	auto buf = BuildSkillLearnRequestPayload(3u);
	Assert(buf.size() == 2u, "Learn request payload is 2 bytes");
	auto parsed = ParseSkillLearnRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Learn request parses");
	if (parsed)
	{
		Assert(parsed->skillId == 3u, "Learn skillId preserved");
	}
}

static void TestLearnRequestRejectsShort()
{
	auto parsed = ParseSkillLearnRequestPayload(nullptr, 0u);
	Assert(!parsed.has_value(), "Learn request rejects nullptr");
	uint8_t shortBuf[1]{0};
	auto parsed2 = ParseSkillLearnRequestPayload(shortBuf, 1u);
	Assert(!parsed2.has_value(), "Learn request rejects 1 byte");
}

static void TestLearnResponseRoundTrip()
{
	auto buf = BuildSkillLearnResponsePayload(0u, 75u);
	Assert(buf.size() == 3u, "Learn response payload is 3 bytes");
	auto parsed = ParseSkillLearnResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Learn response parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Learn response error 0");
		Assert(parsed->initialCap == 75u, "Learn initialCap preserved");
	}
}

static void TestLearnResponseAlreadyLearned()
{
	auto buf = BuildSkillLearnResponsePayload(
		static_cast<uint8_t>(SkillErrorCode::AlreadyLearned), 0u);
	auto parsed = ParseSkillLearnResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 8u, "Learn AlreadyLearned");
}

static void TestLearnResponsePacket()
{
	auto pkt = BuildSkillLearnResponsePacket(0u, 75u, 999u, 0xBEEFull);
	Assert(!pkt.empty(), "Learn response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Learn PacketView parse OK");
	Assert(view.Opcode() == kOpcodeSkillLearnResponse, "Opcode is SkillLearnResponse");
	Assert(view.RequestId() == 999u, "Learn RequestId preserved");
}

// -----------------------------------------------------------------------------
// SKILL_USE
// -----------------------------------------------------------------------------

static void TestUseRequestRoundTrip()
{
	auto buf = BuildSkillUseRequestPayload(5u, 0xABCDEFu);
	Assert(buf.size() == 10u, "Use request payload is 10 bytes");
	auto parsed = ParseSkillUseRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Use request parses");
	if (parsed)
	{
		Assert(parsed->skillId == 5u, "Use skillId preserved");
		Assert(parsed->targetEntityId == 0xABCDEFull, "Use targetEntityId preserved");
	}
}

static void TestUseRequestNoTarget()
{
	auto buf = BuildSkillUseRequestPayload(2u, 0u);
	auto parsed = ParseSkillUseRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->targetEntityId == 0ull,
		"Use request targetEntityId=0 (no target)");
}

static void TestUseRequestRejectsShort()
{
	auto parsed = ParseSkillUseRequestPayload(nullptr, 0u);
	Assert(!parsed.has_value(), "Use request rejects nullptr");
	uint8_t shortBuf[9]{};
	auto parsed2 = ParseSkillUseRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Use request rejects 9 bytes");
}

static void TestUseResponseRoundTrip()
{
	auto buf = BuildSkillUseResponsePayload(0u,
		static_cast<uint8_t>(SkillUseResult::Success), 1u);
	Assert(buf.size() == 4u, "Use response payload is 4 bytes");
	auto parsed = ParseSkillUseResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Use response parses");
	if (parsed)
	{
		Assert(parsed->error == 0u, "Use response error 0");
		Assert(parsed->result == 0u, "Use response Success");
		Assert(parsed->deltaValue == 1u, "Use deltaValue preserved");
	}
}

static void TestUseResponseCriticalSuccess()
{
	auto buf = BuildSkillUseResponsePayload(0u,
		static_cast<uint8_t>(SkillUseResult::CriticalSuccess), 5u);
	auto parsed = ParseSkillUseResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Use Crit parses");
	if (parsed)
	{
		Assert(parsed->result == 2u, "Use result CriticalSuccess");
		Assert(parsed->deltaValue == 5u, "Use Crit gain 5");
	}
}

static void TestUseResponseFailNoGain()
{
	auto buf = BuildSkillUseResponsePayload(0u,
		static_cast<uint8_t>(SkillUseResult::Fail), 0u);
	auto parsed = ParseSkillUseResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Use Fail parses");
	if (parsed)
	{
		Assert(parsed->result == 1u, "Use result Fail");
		Assert(parsed->deltaValue == 0u, "Use Fail no gain");
	}
}

static void TestUseResponseSkillNotLearned()
{
	auto buf = BuildSkillUseResponsePayload(
		static_cast<uint8_t>(SkillErrorCode::SkillNotLearned), 0u, 0u);
	auto parsed = ParseSkillUseResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 9u, "Use SkillNotLearned");
}

static void TestUseResponsePacket()
{
	auto pkt = BuildSkillUseResponsePacket(0u, 0u, 1u, 777u, 0xDEADBEEFull);
	Assert(!pkt.empty(), "Use response packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Use PacketView parse OK");
	Assert(view.Opcode() == kOpcodeSkillUseResponse, "Opcode is SkillUseResponse");
	Assert(view.RequestId() == 777u, "Use RequestId preserved");
}

// -----------------------------------------------------------------------------
// SKILL_UPGRADE_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestUpgradeNotificationRoundTrip()
{
	auto buf = BuildSkillUpgradeNotificationPayload(1u, 26u, 75u, 1);
	Assert(buf.size() == 8u, "Upgrade notification is 8 bytes");
	auto parsed = ParseSkillUpgradeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Upgrade notification parses");
	if (parsed)
	{
		Assert(parsed->skillId == 1u, "Upgrade skillId");
		Assert(parsed->newValue == 26u, "Upgrade newValue");
		Assert(parsed->newCap == 75u, "Upgrade newCap");
		Assert(parsed->delta == 1, "Upgrade delta");
	}
}

static void TestUpgradeNotificationZeroDelta()
{
	// Use case : Learn (cap initial pousse via upgrade notif avec delta=0).
	auto buf = BuildSkillUpgradeNotificationPayload(5u, 0u, 75u, 0);
	auto parsed = ParseSkillUpgradeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Upgrade with zero delta parses");
	if (parsed)
	{
		Assert(parsed->skillId == 5u, "Upgrade skillId on Learn");
		Assert(parsed->delta == 0, "Upgrade zero delta on Learn");
		Assert(parsed->newCap == 75u, "Upgrade newCap on Learn");
	}
}

static void TestUpgradeNotificationNegativeDelta()
{
	// Edge : delta peut etre negatif (cas futur : decay, debuff equipement
	// retire). Verifie que int16 round-trip preserve le signe.
	auto buf = BuildSkillUpgradeNotificationPayload(2u, 40u, 75u, -10);
	auto parsed = ParseSkillUpgradeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Upgrade with negative delta parses");
	if (parsed)
	{
		Assert(parsed->newValue == 40u, "Negative delta newValue");
		Assert(parsed->delta == -10, "Negative delta preserved");
	}
}

static void TestUpgradeNotificationBoundary()
{
	// Boundaries : skillId max 0xFFFF, value max 0xFFFF, delta max +/- 32767.
	auto buf = BuildSkillUpgradeNotificationPayload(0xFFFFu, 0xFFFFu, 0xFFFFu, 32767);
	auto parsed = ParseSkillUpgradeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Upgrade boundary parses");
	if (parsed)
	{
		Assert(parsed->skillId == 0xFFFFu, "skillId max");
		Assert(parsed->newValue == 0xFFFFu, "newValue max");
		Assert(parsed->newCap == 0xFFFFu, "newCap max");
		Assert(parsed->delta == 32767, "delta max positive");
	}

	auto buf2 = BuildSkillUpgradeNotificationPayload(1u, 0u, 1u, -32768);
	auto parsed2 = ParseSkillUpgradeNotificationPayload(buf2.data(), buf2.size());
	Assert(parsed2.has_value(), "Upgrade min delta parses");
	if (parsed2)
	{
		Assert(parsed2->delta == -32768, "delta min negative");
	}
}

static void TestUpgradeNotificationRejectsShort()
{
	auto parsed = ParseSkillUpgradeNotificationPayload(nullptr, 8u);
	Assert(!parsed.has_value(), "Upgrade rejects nullptr");
	uint8_t shortBuf[7]{};
	auto parsed2 = ParseSkillUpgradeNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Upgrade rejects 7 bytes");
}

static void TestUpgradeNotificationPacket()
{
	auto pkt = BuildSkillUpgradeNotificationPacket(1u, 26u, 75u, 1, 0xBEEFull);
	Assert(!pkt.empty(), "Upgrade notification packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "Upgrade PacketView parse OK");
	Assert(view.Opcode() == kOpcodeSkillUpgradeNotification, "Opcode is SkillUpgradeNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xBEEFull, "SessionId preserved");
	auto parsed = ParseSkillUpgradeNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->skillId == 1u && parsed->delta == 1,
		"Upgrade payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseUnauthorized();
	TestListResponseSingle();
	TestListResponseMultiple();
	TestListResponseSkillIdZero();
	TestListResponseValueGtCapBitwise();
	TestListResponseRejectsShort();
	TestListResponsePacket();

	TestLearnRequestRoundTrip();
	TestLearnRequestRejectsShort();
	TestLearnResponseRoundTrip();
	TestLearnResponseAlreadyLearned();
	TestLearnResponsePacket();

	TestUseRequestRoundTrip();
	TestUseRequestNoTarget();
	TestUseRequestRejectsShort();
	TestUseResponseRoundTrip();
	TestUseResponseCriticalSuccess();
	TestUseResponseFailNoGain();
	TestUseResponseSkillNotLearned();
	TestUseResponsePacket();

	TestUpgradeNotificationRoundTrip();
	TestUpgradeNotificationZeroDelta();
	TestUpgradeNotificationNegativeDelta();
	TestUpgradeNotificationBoundary();
	TestUpgradeNotificationRejectsShort();
	TestUpgradeNotificationPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all skill payload tests passed\n"
	                                : "[FAIL] some skill tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
