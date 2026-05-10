/**
 * CMANGOS.17 (Phase 3.17 step 3+4 Loot) - Round-trip tests for LOOT_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/LootPayloads.h"
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
// LOOT_ROLL_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestRollNotificationRoundTrip()
{
	auto buf = BuildLootRollNotificationPayload(42ull, 1u, "Iron Ore", 5u, 30u);
	auto parsed = ParseLootRollNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "RollNotification round-trip parses");
	if (parsed)
	{
		Assert(parsed->rollId == 42ull, "RollNotification rollId=42");
		Assert(parsed->itemTemplateId == 1u, "RollNotification itemTemplateId=1");
		Assert(parsed->itemName == "Iron Ore", "RollNotification itemName");
		Assert(parsed->count == 5u, "RollNotification count=5");
		Assert(parsed->durationSec == 30u, "RollNotification durationSec=30");
	}
}

static void TestRollNotificationEmptyName()
{
	auto buf = BuildLootRollNotificationPayload(1ull, 0u, "", 0u, 0u);
	auto parsed = ParseLootRollNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "RollNotification empty name parses");
	if (parsed)
	{
		Assert(parsed->itemName.empty(), "RollNotification empty itemName");
		Assert(parsed->count == 0u, "RollNotification count=0");
	}
}

static void TestRollNotificationLongName()
{
	std::string longName(500u, 'X');
	auto buf = BuildLootRollNotificationPayload(0xCAFEBABEull, 0xFFFFFFFFu, longName, 0xFFFFFFFFu, 0xFFFFFFFFu);
	auto parsed = ParseLootRollNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "RollNotification long name parses");
	if (parsed)
	{
		Assert(parsed->rollId == 0xCAFEBABEull, "RollNotification rollId max");
		Assert(parsed->itemName.size() == 500u, "RollNotification long itemName");
		Assert(parsed->count == 0xFFFFFFFFu, "RollNotification count u32 max");
		Assert(parsed->durationSec == 0xFFFFFFFFu, "RollNotification duration u32 max");
	}
}

static void TestRollNotificationRejectsShort()
{
	auto parsed = ParseLootRollNotificationPayload(nullptr, 0u);
	Assert(!parsed.has_value(), "RollNotification rejects nullptr");
	uint8_t shortBuf[10]{};
	auto parsed2 = ParseLootRollNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "RollNotification rejects 10 bytes");
}

static void TestRollNotificationPacket()
{
	auto pkt = BuildLootRollNotificationPacket(7ull, 3u, "Mageweave", 1u, 30u, 0xDEADBEEFull);
	Assert(!pkt.empty(), "RollNotification packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"RollNotification PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLootRollNotification, "Opcode is RollNotification");
	Assert(view.RequestId() == 0u, "RollNotification RequestId is 0 (push)");
	Assert(view.SessionId() == 0xDEADBEEFull, "RollNotification SessionId preserved");
	auto parsed = ParseLootRollNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->rollId == 7ull && parsed->itemName == "Mageweave",
		"RollNotification packet payload decodes");
}

// -----------------------------------------------------------------------------
// LOOT_ROLL_CHOICE - Request
// -----------------------------------------------------------------------------

static void TestChoiceRequestPass()
{
	auto buf = BuildLootRollChoiceRequestPayload(42ull, 0u);
	auto parsed = ParseLootRollChoiceRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->rollId == 42ull && parsed->choice == 0u,
		"ChoiceRequest Pass round-trip");
}

static void TestChoiceRequestGreed()
{
	auto buf = BuildLootRollChoiceRequestPayload(7ull, 1u);
	auto parsed = ParseLootRollChoiceRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->choice == 1u, "ChoiceRequest Greed round-trip");
}

static void TestChoiceRequestNeed()
{
	auto buf = BuildLootRollChoiceRequestPayload(99ull, 2u);
	auto parsed = ParseLootRollChoiceRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->choice == 2u, "ChoiceRequest Need round-trip");
}

static void TestChoiceRequestInvalid255()
{
	// Le wire accepte 255, c'est le handler serveur qui rejettera (InvalidChoice).
	auto buf = BuildLootRollChoiceRequestPayload(1ull, 255u);
	auto parsed = ParseLootRollChoiceRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->choice == 255u,
		"ChoiceRequest accepts choice=255 at wire level");
}

static void TestChoiceRequestRejectsShort()
{
	auto parsed = ParseLootRollChoiceRequestPayload(nullptr, 0u);
	Assert(!parsed.has_value(), "ChoiceRequest rejects nullptr");
	uint8_t shortBuf[8]{};
	auto parsed2 = ParseLootRollChoiceRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "ChoiceRequest rejects 8 bytes");
}

// -----------------------------------------------------------------------------
// LOOT_ROLL_CHOICE - Response
// -----------------------------------------------------------------------------

static void TestChoiceResponseOk()
{
	auto buf = BuildLootRollChoiceResponsePayload(0u);
	Assert(buf.size() == 1u, "ChoiceResponse Ok 1 byte");
	auto parsed = ParseLootRollChoiceResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 0u, "ChoiceResponse Ok parses");
}

static void TestChoiceResponseInvalidChoice()
{
	auto buf = BuildLootRollChoiceResponsePayload(
		static_cast<uint8_t>(LootResponseStatus::InvalidChoice));
	auto parsed = ParseLootRollChoiceResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 2u, "ChoiceResponse InvalidChoice");
}

static void TestChoiceResponseRollNotFound()
{
	auto buf = BuildLootRollChoiceResponsePayload(
		static_cast<uint8_t>(LootResponseStatus::RollNotFound));
	auto parsed = ParseLootRollChoiceResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 3u, "ChoiceResponse RollNotFound");
}

static void TestChoiceResponseRollEnded()
{
	auto buf = BuildLootRollChoiceResponsePayload(
		static_cast<uint8_t>(LootResponseStatus::RollEnded));
	auto parsed = ParseLootRollChoiceResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 4u, "ChoiceResponse RollEnded");
}

static void TestChoiceResponseUnauthorized()
{
	auto buf = BuildLootRollChoiceResponsePayload(
		static_cast<uint8_t>(LootResponseStatus::Unauthorized));
	auto parsed = ParseLootRollChoiceResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 1u, "ChoiceResponse Unauthorized");
}

static void TestChoiceResponsePacket()
{
	auto pkt = BuildLootRollChoiceResponsePacket(0u, 1234u, 0xCAFEull);
	Assert(!pkt.empty(), "ChoiceResponse packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"ChoiceResponse PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLootRollChoiceResponse, "Opcode is ChoiceResponse");
	Assert(view.RequestId() == 1234u, "ChoiceResponse RequestId preserved");
	auto parsed = ParseLootRollChoiceResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->status == 0u, "ChoiceResponse packet payload decodes");
}

// -----------------------------------------------------------------------------
// LOOT_ROLL_RESULT_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestRollResultNeedWinner()
{
	auto buf = BuildLootRollResultNotificationPayload(
		42ull, "Aragorn", 2u /*Need*/, 87u, 1u, "Iron Ore", 5u);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "RollResult Need winner parses");
	if (parsed)
	{
		Assert(parsed->rollId == 42ull, "RollResult rollId=42");
		Assert(parsed->winnerName == "Aragorn", "RollResult winnerName");
		Assert(parsed->winnerChoice == 2u, "RollResult winnerChoice=Need");
		Assert(parsed->winnerRoll == 87u, "RollResult winnerRoll=87");
		Assert(parsed->itemTemplateId == 1u, "RollResult itemTemplateId=1");
		Assert(parsed->itemName == "Iron Ore", "RollResult itemName");
		Assert(parsed->count == 5u, "RollResult count=5");
	}
}

static void TestRollResultGreedWinner()
{
	auto buf = BuildLootRollResultNotificationPayload(
		7ull, "Legolas", 1u /*Greed*/, 50u, 4u, "Health Potion", 1u);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->winnerChoice == 1u && parsed->winnerRoll == 50u,
		"RollResult Greed winner parses");
}

static void TestRollResultAllPass()
{
	auto buf = BuildLootRollResultNotificationPayload(
		99ull, "" /*personne*/, 0u /*Pass*/, 0u, 5u, "Mana Potion", 2u);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "RollResult all Pass parses");
	if (parsed)
	{
		Assert(parsed->winnerName.empty(), "RollResult empty winnerName when all Pass");
		Assert(parsed->winnerChoice == 0u, "RollResult winnerChoice=Pass");
		Assert(parsed->winnerRoll == 0u, "RollResult winnerRoll=0");
	}
}

static void TestRollResultRollMin()
{
	auto buf = BuildLootRollResultNotificationPayload(
		1ull, "X", 2u, 0u /*roll min*/, 1u, "Iron", 1u);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->winnerRoll == 0u, "RollResult roll=0 parses");
}

static void TestRollResultRollMax()
{
	auto buf = BuildLootRollResultNotificationPayload(
		2ull, "Y", 2u, 100u /*roll max*/, 1u, "Iron", 1u);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->winnerRoll == 100u, "RollResult roll=100 parses");
}

static void TestRollResultLongName()
{
	std::string longName(300u, 'A');
	auto buf = BuildLootRollResultNotificationPayload(
		1ull, longName, 2u, 50u, 1u, "Item", 1u);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->winnerName.size() == 300u,
		"RollResult long winnerName parses");
}

static void TestRollResultCountMax()
{
	auto buf = BuildLootRollResultNotificationPayload(
		1ull, "X", 1u, 50u, 0xFFFFFFFFu, "Y", 0xFFFFFFFFu);
	auto parsed = ParseLootRollResultNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->count == 0xFFFFFFFFu
		&& parsed->itemTemplateId == 0xFFFFFFFFu,
		"RollResult u32 max count + itemTemplateId");
}

static void TestRollResultRejectsShort()
{
	auto parsed = ParseLootRollResultNotificationPayload(nullptr, 0u);
	Assert(!parsed.has_value(), "RollResult rejects nullptr");
	uint8_t shortBuf[15]{};
	auto parsed2 = ParseLootRollResultNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "RollResult rejects 15 bytes");
}

static void TestRollResultPacket()
{
	auto pkt = BuildLootRollResultNotificationPacket(
		42ull, "Boss", 2u, 75u, 1u, "Mageweave", 3u, 0xBEEFull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"RollResult PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLootRollResultNotification,
		"Opcode is RollResultNotification");
	Assert(view.RequestId() == 0u, "RollResult RequestId is 0 (push)");
	auto parsed = ParseLootRollResultNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->winnerName == "Boss" && parsed->winnerRoll == 75u,
		"RollResult packet payload decodes");
}

// -----------------------------------------------------------------------------
// LOOT_SIMULATE_ROLL - Request
// -----------------------------------------------------------------------------

static void TestSimulateRequestEmpty()
{
	auto buf = BuildLootSimulateRollRequestPayload();
	Assert(buf.empty(), "Simulate request payload empty");
	auto parsed = ParseLootSimulateRollRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Simulate request accepts empty payload");
}

// -----------------------------------------------------------------------------
// LOOT_SIMULATE_ROLL - Response
// -----------------------------------------------------------------------------

static void TestSimulateResponseOk()
{
	auto buf = BuildLootSimulateRollResponsePayload(0u, 42ull);
	auto parsed = ParseLootSimulateRollResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 0u && parsed->rollId == 42ull,
		"Simulate response Ok round-trip");
}

static void TestSimulateResponseUnauthorized()
{
	auto buf = BuildLootSimulateRollResponsePayload(
		static_cast<uint8_t>(LootResponseStatus::Unauthorized), 0ull);
	auto parsed = ParseLootSimulateRollResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->status == 1u && parsed->rollId == 0ull,
		"Simulate response Unauthorized");
}

static void TestSimulateResponseRollIdMax()
{
	auto buf = BuildLootSimulateRollResponsePayload(0u, 0xFFFFFFFFFFFFFFFFull);
	auto parsed = ParseLootSimulateRollResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->rollId == 0xFFFFFFFFFFFFFFFFull,
		"Simulate response rollId u64 max");
}

static void TestSimulateResponseRejectsShort()
{
	auto parsed = ParseLootSimulateRollResponsePayload(nullptr, 0u);
	Assert(!parsed.has_value(), "Simulate response rejects nullptr");
	uint8_t shortBuf[8]{};
	auto parsed2 = ParseLootSimulateRollResponsePayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Simulate response rejects 8 bytes");
}

static void TestSimulateResponsePacket()
{
	auto pkt = BuildLootSimulateRollResponsePacket(0u, 99ull, 555u, 0xCAFEull);
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Simulate response PacketView parse OK");
	Assert(view.Opcode() == kOpcodeLootSimulateRollResponse,
		"Opcode is SimulateRollResponse");
	Assert(view.RequestId() == 555u, "Simulate response RequestId preserved");
	auto parsed = ParseLootSimulateRollResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->rollId == 99ull, "Simulate response packet decodes");
}

// -----------------------------------------------------------------------------
// Status enum exhaustif
// -----------------------------------------------------------------------------

static void TestStatusEnumValues()
{
	Assert(static_cast<uint8_t>(LootResponseStatus::Ok) == 0u, "Status Ok=0");
	Assert(static_cast<uint8_t>(LootResponseStatus::Unauthorized) == 1u, "Status Unauthorized=1");
	Assert(static_cast<uint8_t>(LootResponseStatus::InvalidChoice) == 2u, "Status InvalidChoice=2");
	Assert(static_cast<uint8_t>(LootResponseStatus::RollNotFound) == 3u, "Status RollNotFound=3");
	Assert(static_cast<uint8_t>(LootResponseStatus::RollEnded) == 4u, "Status RollEnded=4");
}

// -----------------------------------------------------------------------------

int main()
{
	TestRollNotificationRoundTrip();
	TestRollNotificationEmptyName();
	TestRollNotificationLongName();
	TestRollNotificationRejectsShort();
	TestRollNotificationPacket();

	TestChoiceRequestPass();
	TestChoiceRequestGreed();
	TestChoiceRequestNeed();
	TestChoiceRequestInvalid255();
	TestChoiceRequestRejectsShort();

	TestChoiceResponseOk();
	TestChoiceResponseInvalidChoice();
	TestChoiceResponseRollNotFound();
	TestChoiceResponseRollEnded();
	TestChoiceResponseUnauthorized();
	TestChoiceResponsePacket();

	TestRollResultNeedWinner();
	TestRollResultGreedWinner();
	TestRollResultAllPass();
	TestRollResultRollMin();
	TestRollResultRollMax();
	TestRollResultLongName();
	TestRollResultCountMax();
	TestRollResultRejectsShort();
	TestRollResultPacket();

	TestSimulateRequestEmpty();

	TestSimulateResponseOk();
	TestSimulateResponseUnauthorized();
	TestSimulateResponseRollIdMax();
	TestSimulateResponseRejectsShort();
	TestSimulateResponsePacket();

	TestStatusEnumValues();

	std::cerr << (s_failCount == 0 ? "[OK] all loot payload tests passed\n"
	                                : "[FAIL] some loot tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
