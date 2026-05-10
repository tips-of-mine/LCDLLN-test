/**
 * CMANGOS.31 (Phase 5.31 step 3+4) — Round-trip tests for GAME_EVENT_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/GameEventPayloads.h"
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
// GAME_EVENT_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildGameEventListRequestPayload();
	Assert(buf.empty(), "GameEvent List request payload empty");
	auto parsed = ParseGameEventListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "GameEvent List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildGameEventListResponsePayload(0u, {});
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->events.empty(),
		"GameEvent List response empty parses");
}

static void TestListResponseSingleEvent()
{
	GameEventSummary ev;
	ev.eventId    = 1u;
	ev.name       = "Halloween";
	ev.state      = 1u; // Active
	ev.startTsMs  = 1791849600000ull; // 2026-10-15
	ev.durationMs = 1209600000ull;    // 14d
	ev.recurMs    = 31536000000ull;   // 365d
	auto buf = BuildGameEventListResponsePayload(0u, {ev});
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "GameEvent List single event parses");
	if (parsed && parsed->events.size() == 1u)
	{
		Assert(parsed->events[0].eventId == 1u, "Event 1 id");
		Assert(parsed->events[0].name == "Halloween", "Event 1 name");
		Assert(parsed->events[0].state == 1u, "Event 1 state Active");
		Assert(parsed->events[0].startTsMs == 1791849600000ull, "Event 1 startTsMs");
		Assert(parsed->events[0].durationMs == 1209600000ull, "Event 1 durationMs");
		Assert(parsed->events[0].recurMs == 31536000000ull, "Event 1 recurMs");
	}
	else
	{
		Assert(false, "List should have 1 event");
	}
}

static void TestListResponseFullFourEvents()
{
	std::vector<GameEventSummary> events;

	GameEventSummary e1;
	e1.eventId    = 1u;
	e1.name       = "Halloween";
	e1.state      = 0u;
	e1.startTsMs  = 1791849600000ull;
	e1.durationMs = 1209600000ull;
	e1.recurMs    = 31536000000ull;
	events.push_back(e1);

	GameEventSummary e2;
	e2.eventId    = 2u;
	e2.name       = "Winter Veil";
	e2.state      = 1u;
	e2.startTsMs  = 1797206400000ull;
	e2.durationMs = 1814400000ull; // 21d
	e2.recurMs    = 31536000000ull;
	events.push_back(e2);

	GameEventSummary e3;
	e3.eventId    = 3u;
	e3.name       = "Lunar Festival";
	e3.state      = 0u;
	e3.startTsMs  = 1769817600000ull;
	e3.durationMs = 1209600000ull;
	e3.recurMs    = 31536000000ull;
	events.push_back(e3);

	GameEventSummary e4;
	e4.eventId    = 4u;
	e4.name       = "Midsummer Fire Festival";
	e4.state      = 1u;
	e4.startTsMs  = 1781913600000ull;
	e4.durationMs = 1209600000ull;
	e4.recurMs    = 31536000000ull;
	events.push_back(e4);

	auto buf = BuildGameEventListResponsePayload(0u, events);
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "GameEvent List 4 events parses");
	if (parsed && parsed->events.size() == 4u)
	{
		Assert(parsed->events[0].name == "Halloween", "E[0] name");
		Assert(parsed->events[0].state == 0u, "E[0] Inactive");
		Assert(parsed->events[1].name == "Winter Veil", "E[1] name");
		Assert(parsed->events[1].state == 1u, "E[1] Active");
		Assert(parsed->events[1].durationMs == 1814400000ull, "E[1] duration 21d");
		Assert(parsed->events[2].name == "Lunar Festival", "E[2] name");
		Assert(parsed->events[3].name == "Midsummer Fire Festival", "E[3] name");
		Assert(parsed->events[3].state == 1u, "E[3] Active");
	}
	else
	{
		Assert(false, "List should have 4 events");
	}
}

static void TestListResponseError()
{
	auto buf = BuildGameEventListResponsePayload(
		static_cast<uint8_t>(GameEventErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "GameEvent List error has only 1 byte");
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "List Unauthorized");
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseGameEventListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List rejects nullptr");
}

static void TestListResponseEmptyName()
{
	GameEventSummary ev;
	ev.eventId    = 99u;
	ev.name       = "";
	ev.state      = 0u;
	ev.startTsMs  = 0u;
	ev.durationMs = 0u;
	ev.recurMs    = 0u;
	auto buf = BuildGameEventListResponsePayload(0u, {ev});
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->events.size() == 1u
		&& parsed->events[0].name.empty() && parsed->events[0].eventId == 99u,
		"List accepts empty name");
}

static void TestListResponseLongName()
{
	GameEventSummary ev;
	ev.eventId    = 7u;
	ev.name       = std::string(200u, 'A');
	ev.state      = 1u;
	ev.startTsMs  = 12345u;
	ev.durationMs = 67890u;
	ev.recurMs    = 0u;
	auto buf = BuildGameEventListResponsePayload(0u, {ev});
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->events.size() == 1u
		&& parsed->events[0].name.size() == 200u,
		"List accepts long name (200 chars)");
}

static void TestListResponseRecurZero()
{
	GameEventSummary ev;
	ev.eventId    = 11u;
	ev.name       = "OneShot";
	ev.state      = 0u;
	ev.startTsMs  = 1000u;
	ev.durationMs = 500u;
	ev.recurMs    = 0u;
	auto buf = BuildGameEventListResponsePayload(0u, {ev});
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->events.size() == 1u
		&& parsed->events[0].recurMs == 0u,
		"List accepts recurMs=0 (one-shot)");
}

static void TestListResponseRecurMaxUint64()
{
	GameEventSummary ev;
	ev.eventId    = 12u;
	ev.name       = "Forever";
	ev.state      = 1u;
	ev.startTsMs  = 0xFFFFFFFFFFFFFFFFull;
	ev.durationMs = 0xFFFFFFFFFFFFFFFFull;
	ev.recurMs    = 0xFFFFFFFFFFFFFFFFull;
	auto buf = BuildGameEventListResponsePayload(0u, {ev});
	auto parsed = ParseGameEventListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->events.size() == 1u
		&& parsed->events[0].startTsMs == 0xFFFFFFFFFFFFFFFFull
		&& parsed->events[0].recurMs == 0xFFFFFFFFFFFFFFFFull,
		"List accepts uint64 max boundary");
}

static void TestListResponsePacket()
{
	GameEventSummary ev;
	ev.eventId    = 42u;
	ev.name       = "Test Event";
	ev.state      = 1u;
	ev.startTsMs  = 12345u;
	ev.durationMs = 67890u;
	ev.recurMs    = 99999u;
	auto pkt = BuildGameEventListResponsePacket(0u, {ev}, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "List packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"List PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGameEventListResponse, "Opcode is ListResponse");
	Assert(view.RequestId() == 999u, "List RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "List SessionId preserved");
	auto parsed = ParseGameEventListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->events.size() == 1u
		&& parsed->events[0].eventId == 42u && parsed->events[0].name == "Test Event"
		&& parsed->events[0].state == 1u && parsed->events[0].startTsMs == 12345u
		&& parsed->events[0].durationMs == 67890u && parsed->events[0].recurMs == 99999u,
		"List payload decodes");
}

// -----------------------------------------------------------------------------
// GAME_EVENT_SUBSCRIBE
// -----------------------------------------------------------------------------

static void TestSubscribeRequestRoundTrip()
{
	auto buf = BuildGameEventSubscribeRequestPayload();
	Assert(buf.empty(), "Subscribe request payload empty");
	auto parsed = ParseGameEventSubscribeRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Subscribe request accepts empty payload");
}

static void TestSubscribeResponseOk()
{
	auto buf = BuildGameEventSubscribeResponsePayload(0u);
	Assert(buf.size() == 1u, "Subscribe response Ok size 1");
	auto parsed = ParseGameEventSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Subscribe Ok parses");
}

static void TestSubscribeResponseAlreadySubscribed()
{
	auto buf = BuildGameEventSubscribeResponsePayload(
		static_cast<uint8_t>(GameEventErrorCode::AlreadySubscribed));
	auto parsed = ParseGameEventSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Subscribe AlreadySubscribed");
}

static void TestSubscribeResponseUnauthorized()
{
	auto buf = BuildGameEventSubscribeResponsePayload(
		static_cast<uint8_t>(GameEventErrorCode::Unauthorized));
	auto parsed = ParseGameEventSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "Subscribe Unauthorized");
}

static void TestSubscribeResponseRejectsShort()
{
	auto parsed = ParseGameEventSubscribeResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "Subscribe response rejects nullptr");
}

static void TestSubscribeResponsePacket()
{
	auto pkt = BuildGameEventSubscribeResponsePacket(0u, 12345u, 0xBEEFull);
	Assert(!pkt.empty(), "Subscribe packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Subscribe PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGameEventSubscribeResponse, "Opcode is SubscribeResponse");
	Assert(view.RequestId() == 12345u, "Subscribe RequestId preserved");
	auto parsed = ParseGameEventSubscribeResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->error == 0u,
		"Subscribe Ok packet body decodes");
}

// -----------------------------------------------------------------------------
// GAME_EVENT_UNSUBSCRIBE
// -----------------------------------------------------------------------------

static void TestUnsubscribeRequestRoundTrip()
{
	auto buf = BuildGameEventUnsubscribeRequestPayload();
	Assert(buf.empty(), "Unsubscribe request payload empty");
	auto parsed = ParseGameEventUnsubscribeRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Unsubscribe request accepts empty payload");
}

static void TestUnsubscribeResponseOk()
{
	auto buf = BuildGameEventUnsubscribeResponsePayload(0u);
	Assert(buf.size() == 1u, "Unsubscribe response Ok size 1");
	auto parsed = ParseGameEventUnsubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Unsubscribe Ok parses");
}

static void TestUnsubscribeResponseNotSubscribed()
{
	auto buf = BuildGameEventUnsubscribeResponsePayload(
		static_cast<uint8_t>(GameEventErrorCode::NotSubscribed));
	auto parsed = ParseGameEventUnsubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Unsubscribe NotSubscribed");
}

static void TestUnsubscribeResponsePacket()
{
	auto pkt = BuildGameEventUnsubscribeResponsePacket(0u, 99u, 0xFEEDull);
	Assert(!pkt.empty(), "Unsubscribe packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Unsubscribe PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGameEventUnsubscribeResponse, "Opcode is UnsubscribeResponse");
	Assert(view.RequestId() == 99u, "Unsubscribe RequestId preserved");
}

// -----------------------------------------------------------------------------
// GAME_EVENT_STATE_CHANGE_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestStateChangeRoundTrip()
{
	auto buf = BuildGameEventStateChangeNotificationPayload(1u, 1u, 1791849600000ull);
	Assert(buf.size() == 13u, "StateChange payload size 13");
	auto parsed = ParseGameEventStateChangeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "StateChange parses");
	if (parsed)
	{
		Assert(parsed->eventId == 1u, "StateChange eventId");
		Assert(parsed->newState == 1u, "StateChange newState Active");
		Assert(parsed->untilTsMs == 1791849600000ull, "StateChange untilTsMs");
	}
}

static void TestStateChangeInactiveZeroUntil()
{
	// State Inactive + untilTsMs=0 : event termine, pas de prochaine bascule.
	auto buf = BuildGameEventStateChangeNotificationPayload(99u, 0u, 0u);
	auto parsed = ParseGameEventStateChangeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->eventId == 99u
		&& parsed->newState == 0u && parsed->untilTsMs == 0u,
		"StateChange Inactive untilTsMs=0");
}

static void TestStateChangeUntilMaxUint64()
{
	auto buf = BuildGameEventStateChangeNotificationPayload(123u, 1u, 0xFFFFFFFFFFFFFFFFull);
	auto parsed = ParseGameEventStateChangeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->untilTsMs == 0xFFFFFFFFFFFFFFFFull,
		"StateChange untilTsMs UINT64_MAX");
}

static void TestStateChangeStateByteIsRaw()
{
	// Le wire ne valide pas la valeur de state ; le parser doit accepter
	// une valeur "invalide" (e.g. 99u) — la validation est cote presenter.
	auto buf = BuildGameEventStateChangeNotificationPayload(1u, 99u, 0u);
	auto parsed = ParseGameEventStateChangeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->newState == 99u,
		"StateChange accepts raw uint8 newState 99 (validation presenter-side)");
}

static void TestStateChangeEventIdMax()
{
	auto buf = BuildGameEventStateChangeNotificationPayload(0xFFFFFFFFu, 1u, 12345u);
	auto parsed = ParseGameEventStateChangeNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->eventId == 0xFFFFFFFFu,
		"StateChange eventId UINT32_MAX");
}

static void TestStateChangeRejectsShort()
{
	auto parsed = ParseGameEventStateChangeNotificationPayload(nullptr, 13u);
	Assert(!parsed.has_value(), "StateChange rejects nullptr");
	uint8_t shortBuf[12]{};
	auto parsed2 = ParseGameEventStateChangeNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "StateChange rejects 12 bytes");
}

static void TestStateChangePacket()
{
	auto pkt = BuildGameEventStateChangeNotificationPacket(1u, 1u, 1791849600000ull, 0xFADEull);
	Assert(!pkt.empty(), "StateChange packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"StateChange PacketView parse OK");
	Assert(view.Opcode() == kOpcodeGameEventStateChangeNotification, "Opcode is StateChange");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xFADEull, "StateChange SessionId preserved");
	auto parsed = ParseGameEventStateChangeNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->eventId == 1u && parsed->newState == 1u
		&& parsed->untilTsMs == 1791849600000ull,
		"StateChange payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseSingleEvent();
	TestListResponseFullFourEvents();
	TestListResponseError();
	TestListResponseRejectsShort();
	TestListResponseEmptyName();
	TestListResponseLongName();
	TestListResponseRecurZero();
	TestListResponseRecurMaxUint64();
	TestListResponsePacket();

	TestSubscribeRequestRoundTrip();
	TestSubscribeResponseOk();
	TestSubscribeResponseAlreadySubscribed();
	TestSubscribeResponseUnauthorized();
	TestSubscribeResponseRejectsShort();
	TestSubscribeResponsePacket();

	TestUnsubscribeRequestRoundTrip();
	TestUnsubscribeResponseOk();
	TestUnsubscribeResponseNotSubscribed();
	TestUnsubscribeResponsePacket();

	TestStateChangeRoundTrip();
	TestStateChangeInactiveZeroUntil();
	TestStateChangeUntilMaxUint64();
	TestStateChangeStateByteIsRaw();
	TestStateChangeEventIdMax();
	TestStateChangeRejectsShort();
	TestStateChangePacket();

	std::cerr << (s_failCount == 0 ? "[OK] all gameevent payload tests passed\n"
	                                : "[FAIL] some gameevent tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
