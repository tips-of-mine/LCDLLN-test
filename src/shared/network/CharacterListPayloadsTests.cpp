/**
 * Phase 1 — Round-trip tests for CHARACTER_LIST request/response payloads.
 * Pure encoding tests — no DB / no network. Returns 0 on success, non-zero on first failure.
 */

#include "src/shared/network/CharacterPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"
#include "src/shared/core/Log.h"

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
	auto buf = BuildCharacterListRequestPayload(42u);
	Assert(!buf.empty(), "Build request not empty");
	auto parsed = ParseCharacterListRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Parse request OK");
	Assert(parsed && parsed->serverId == 42u, "Request serverId round-trips");
}

static void TestRequestRejectsTooShort()
{
	uint8_t shortBuf[3] = { 0, 0, 0 };
	auto parsed = ParseCharacterListRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Parse rejects payload < 4 bytes");
}

static void TestEmptyResponseRoundTrip()
{
	std::vector<CharacterListEntry> entries;
	auto pkt = BuildCharacterListResponsePacket(1u, entries, 7u, 12345u);
	Assert(!pkt.empty(), "Build empty response packet");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	Assert(view.Opcode() == kOpcodeCharacterListResponse, "Opcode is CHARACTER_LIST_RESPONSE");
	Assert(view.RequestId() == 7u, "RequestId preserved");
	Assert(view.SessionId() == 12345u, "SessionId preserved");
	auto parsed = ParseCharacterListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->success == 1u && parsed->entries.empty(),
		"Empty response parses OK with 0 entries");
}

static void TestErrorResponseSkipsEntries()
{
	std::vector<CharacterListEntry> entries;
	CharacterListEntry e;
	e.character_id = 999u;
	e.name = "ignored";
	entries.push_back(e);
	// success=0 — handler builds an error response with no body. The build path
	// honors that by writing only the success byte.
	auto pkt = BuildCharacterListResponsePacket(0u, entries, 11u, 88u);
	Assert(!pkt.empty(), "Build error response");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	auto parsed = ParseCharacterListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->success == 0u && parsed->entries.empty(),
		"Error response yields success=0 with no entries");
}

static void TestPopulatedResponseRoundTrip()
{
	std::vector<CharacterListEntry> entries;
	{
		CharacterListEntry a;
		a.character_id     = 1001u;
		a.slot             = 0u;
		a.name             = "Alyx";
		a.race_id          = 2u;
		a.class_id         = 5u;
		a.level            = 12u;
		a.force_rename     = 0u;
		a.last_seen_unix   = 1700000000ull;
		a.total_play_secs  = 3600ull;
		a.spawn_x          = 12.5f;
		a.spawn_y          = 87.25f;
		a.spawn_z          = -34.0f;
		a.spawn_yaw_deg    = 90.0f;
		a.spawn_pitch_deg  = -5.5f;
		a.race_str         = "humains";
		a.class_str        = "warrior";
		a.gender           = "female";
		a.skin_color_idx   = 1u;
		entries.push_back(a);
	}
	{
		CharacterListEntry b;
		b.character_id     = 1002u;
		b.slot             = 3u;
		b.name             = "Brynn";
		b.race_id          = 4u;
		b.class_id         = 9u;
		b.level            = 1u;
		b.force_rename     = 1u;
		b.last_seen_unix   = 0ull;
		b.total_play_secs  = 0ull;
		b.spawn_x          = 0.0f;
		b.spawn_y          = 100.0f;
		b.spawn_z          = 0.0f;
		b.spawn_yaw_deg    = 0.0f;
		b.spawn_pitch_deg  = -10.0f;
		// Pre-migration row simulation: empty race/class strings.
		entries.push_back(b);
	}

	auto pkt = BuildCharacterListResponsePacket(1u, entries, 99u, 0xDEADBEEFull);
	Assert(!pkt.empty(), "Build populated response packet");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK");
	auto parsed = ParseCharacterListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value(), "Parse populated response");
	Assert(parsed && parsed->success == 1u, "success=1");
	Assert(parsed && parsed->entries.size() == 2u, "Two entries");

	if (parsed && parsed->entries.size() == 2u)
	{
		const auto& a = parsed->entries[0];
		Assert(a.character_id == 1001u, "entry0 id");
		Assert(a.slot == 0u, "entry0 slot");
		Assert(a.name == "Alyx", "entry0 name");
		Assert(a.race_id == 2u, "entry0 race_id");
		Assert(a.class_id == 5u, "entry0 class_id");
		Assert(a.level == 12u, "entry0 level");
		Assert(a.force_rename == 0u, "entry0 force_rename");
		Assert(a.last_seen_unix == 1700000000ull, "entry0 last_seen_unix");
		Assert(a.total_play_secs == 3600ull, "entry0 total_play_secs");
		Assert(a.spawn_x == 12.5f, "entry0 spawn_x");
		Assert(a.spawn_y == 87.25f, "entry0 spawn_y");
		Assert(a.spawn_z == -34.0f, "entry0 spawn_z");
		Assert(a.spawn_yaw_deg == 90.0f, "entry0 spawn_yaw_deg");
		Assert(a.spawn_pitch_deg == -5.5f, "entry0 spawn_pitch_deg");
		Assert(a.race_str == "humains", "entry0 race_str");
		Assert(a.class_str == "warrior", "entry0 class_str");
		Assert(a.gender == "female", "entry0 gender");
		Assert(a.skin_color_idx == 1u, "entry0 skin_color_idx");

		const auto& b = parsed->entries[1];
		Assert(b.character_id == 1002u, "entry1 id");
		Assert(b.slot == 3u, "entry1 slot");
		Assert(b.name == "Brynn", "entry1 name");
		Assert(b.force_rename == 1u, "entry1 force_rename true");
		Assert(b.last_seen_unix == 0ull, "entry1 last_seen_unix zero");
		Assert(b.spawn_x == 0.0f, "entry1 spawn_x zero");
		Assert(b.spawn_y == 100.0f, "entry1 spawn_y default");
		Assert(b.spawn_pitch_deg == -10.0f, "entry1 spawn_pitch_deg default");
		Assert(b.race_str.empty(), "entry1 race_str empty (pre-migration)");
		Assert(b.class_str.empty(), "entry1 class_str empty (pre-migration)");
		Assert(b.skin_color_idx == 0u, "entry1 skin_color_idx default 0");
	}
}

static void TestWorldClockPiggyback()
{
	std::vector<CharacterListEntry> entries;
	CharacterListEntry a;
	a.character_id = 1u; a.name = "Test"; a.spawn_y = 100.0f;
	entries.push_back(a);

	engine::network::worldclock::WorldClockStateResponse wc;
	wc.status = engine::network::worldclock::WorldClockStatus::Ok;
	wc.serverTimeUnixMs = 1700000000000ull;
	wc.epochRefUnixMs   = 1690000000000ull;
	wc.timeScaleRealMinPerDay = 90.0f;
	wc.offsetGameSec    = 1234.5;
	wc.paused           = 1u;
	wc.pausedAtGameSec  = 42.0;
	wc.lunarPeriodGameSec = 229000.0;

	auto pkt = BuildCharacterListResponsePacket(1u, entries, 5u, 77u, true, wc);
	Assert(!pkt.empty(), "Build response with worldclock");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok, "PacketView parse OK (wc)");
	auto parsed = ParseCharacterListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->success == 1u, "parse success (wc)");
	Assert(parsed && parsed->entries.size() == 1u, "one entry (wc)");
	Assert(parsed && parsed->hasWorldClock, "hasWorldClock true");
	Assert(parsed && parsed->worldClock.serverTimeUnixMs == 1700000000000ull, "wc serverTime round-trips");
	Assert(parsed && parsed->worldClock.timeScaleRealMinPerDay == 90.0f, "wc timeScale round-trips");
	Assert(parsed && parsed->worldClock.paused == 1u, "wc paused round-trips");
	Assert(parsed && parsed->worldClock.lunarPeriodGameSec == 229000.0, "wc lunarPeriod round-trips");

	// Sans horloge : hasWorldClock == false.
	auto pkt2 = BuildCharacterListResponsePacket(1u, entries, 5u, 77u);
	PacketView view2;
	PacketView::Parse(pkt2.data(), pkt2.size(), view2);
	auto parsed2 = ParseCharacterListResponsePayload(view2.Payload(), view2.PayloadSize());
	Assert(parsed2 && !parsed2->hasWorldClock, "no worldclock when omitted");
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	logSettings.console = false;
	engine::core::Log::Init(logSettings);

	TestRequestRoundTrip();
	TestRequestRejectsTooShort();
	TestEmptyResponseRoundTrip();
	TestErrorResponseSkipsEntries();
	TestPopulatedResponseRoundTrip();
	TestWorldClockPiggyback();

	engine::core::Log::Shutdown();
	return s_failCount == 0 ? 0 : 1;
}
