/**
 * CMANGOS.42 (Phase 4.42 step 3+4) — Round-trip tests for WEATHER_*
 * payloads. Pure encoding tests (no DB / no network).
 *
 * Returns 0 on success, 1 on first failure.
 */

#include "src/shared/network/WeatherPayloads.h"
#include "src/shared/network/PacketView.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <cstdint>
#include <cstdlib>
#include <cmath>
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

	bool FloatNear(float a, float b, float eps = 1e-5f)
	{
		const float d = a - b;
		return (d < eps) && (d > -eps);
	}
}

using namespace engine::network;

// -----------------------------------------------------------------------------
// WEATHER_LIST
// -----------------------------------------------------------------------------

static void TestListRequestRoundTrip()
{
	auto buf = BuildWeatherListRequestPayload();
	Assert(buf.empty(), "Weather List request payload empty");
	auto parsed = ParseWeatherListRequestPayload(nullptr, 0u);
	Assert(parsed.has_value(), "Weather List request accepts empty payload");
}

static void TestListResponseEmpty()
{
	auto buf = BuildWeatherListResponsePayload(0u, {});
	auto parsed = ParseWeatherListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->zones.empty(),
		"Weather List response empty parses");
}

static void TestListResponseSingleZone()
{
	WeatherZoneSummary z;
	z.zoneId    = 1u;
	z.name      = "Stormwind Plains";
	z.kind      = 1u; // Rain
	z.intensity = 0.5f;
	auto buf = BuildWeatherListResponsePayload(0u, {z});
	auto parsed = ParseWeatherListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Weather List single zone parses");
	if (parsed && parsed->zones.size() == 1u)
	{
		Assert(parsed->zones[0].zoneId == 1u, "Zone 1 id");
		Assert(parsed->zones[0].name == "Stormwind Plains", "Zone 1 name");
		Assert(parsed->zones[0].kind == 1u, "Zone 1 kind Rain");
		Assert(FloatNear(parsed->zones[0].intensity, 0.5f), "Zone 1 intensity 0.5");
	}
	else
	{
		Assert(false, "List should have 1 zone");
	}
}

static void TestListResponseFullThreeZones()
{
	std::vector<WeatherZoneSummary> zones;

	WeatherZoneSummary z1;
	z1.zoneId    = 1u;
	z1.name      = "Stormwind Plains";
	z1.kind      = 0u; // Clear
	z1.intensity = 0.0f;
	zones.push_back(z1);

	WeatherZoneSummary z2;
	z2.zoneId    = 2u;
	z2.name      = "Frozen Tundra";
	z2.kind      = 2u; // Snow
	z2.intensity = 0.75f;
	zones.push_back(z2);

	WeatherZoneSummary z3;
	z3.zoneId    = 3u;
	z3.name      = "Tanaris Desert";
	z3.kind      = 4u; // Sandstorm
	z3.intensity = 1.0f;
	zones.push_back(z3);

	auto buf = BuildWeatherListResponsePayload(0u, zones);
	auto parsed = ParseWeatherListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Weather List 3 zones parses");
	if (parsed && parsed->zones.size() == 3u)
	{
		Assert(parsed->zones[0].name == "Stormwind Plains", "Z[0] name");
		Assert(parsed->zones[0].kind == 0u, "Z[0] Clear");
		Assert(FloatNear(parsed->zones[0].intensity, 0.0f), "Z[0] intensity 0");
		Assert(parsed->zones[1].name == "Frozen Tundra", "Z[1] name");
		Assert(parsed->zones[1].kind == 2u, "Z[1] Snow");
		Assert(FloatNear(parsed->zones[1].intensity, 0.75f), "Z[1] intensity 0.75");
		Assert(parsed->zones[2].name == "Tanaris Desert", "Z[2] name");
		Assert(parsed->zones[2].kind == 4u, "Z[2] Sandstorm");
		Assert(FloatNear(parsed->zones[2].intensity, 1.0f), "Z[2] intensity 1.0");
	}
	else
	{
		Assert(false, "List should have 3 zones");
	}
}

static void TestListResponseError()
{
	auto buf = BuildWeatherListResponsePayload(
		static_cast<uint8_t>(WeatherErrorCode::Unauthorized), {});
	Assert(buf.size() == 1u, "Weather List error has only 1 byte");
	auto parsed = ParseWeatherListResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "List Unauthorized");
}

static void TestListResponseRejectsShort()
{
	auto parsed = ParseWeatherListResponsePayload(nullptr, 1u);
	Assert(!parsed.has_value(), "List rejects nullptr");
}

static void TestListResponsePacket()
{
	WeatherZoneSummary z;
	z.zoneId    = 42u;
	z.name      = "Test Zone";
	z.kind      = 3u;  // Storm
	z.intensity = 0.8f;
	auto pkt = BuildWeatherListResponsePacket(0u, {z}, 999u, 0xCAFEull);
	Assert(!pkt.empty(), "List packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"List PacketView parse OK");
	Assert(view.Opcode() == kOpcodeWeatherListResponse, "Opcode is ListResponse");
	Assert(view.RequestId() == 999u, "List RequestId preserved");
	Assert(view.SessionId() == 0xCAFEull, "List SessionId preserved");
	auto parsed = ParseWeatherListResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->zones.size() == 1u
		&& parsed->zones[0].zoneId == 42u && parsed->zones[0].name == "Test Zone"
		&& parsed->zones[0].kind == 3u && FloatNear(parsed->zones[0].intensity, 0.8f),
		"List payload decodes");
}

// -----------------------------------------------------------------------------
// WEATHER_SUBSCRIBE
// -----------------------------------------------------------------------------

static void TestSubscribeRequestRoundTrip()
{
	auto buf = BuildWeatherSubscribeRequestPayload(1u);
	Assert(buf.size() == 4u, "Subscribe request payload size 4");
	auto parsed = ParseWeatherSubscribeRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->zoneId == 1u, "Subscribe request parses zoneId 1");
}

static void TestSubscribeRequestBoundary()
{
	auto bufMax = BuildWeatherSubscribeRequestPayload(0xFFFFFFFFu);
	auto parsedMax = ParseWeatherSubscribeRequestPayload(bufMax.data(), bufMax.size());
	Assert(parsedMax.has_value() && parsedMax->zoneId == 0xFFFFFFFFu,
		"Subscribe boundary zoneId max");
}

static void TestSubscribeRequestRejectsShort()
{
	auto parsed = ParseWeatherSubscribeRequestPayload(nullptr, 4u);
	Assert(!parsed.has_value(), "Subscribe request rejects nullptr");
	uint8_t shortBuf[3]{};
	auto parsed2 = ParseWeatherSubscribeRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Subscribe request rejects 3 bytes");
}

static void TestSubscribeResponseOk()
{
	auto buf = BuildWeatherSubscribeResponsePayload(0u, 1u, 0.5f);
	Assert(buf.size() == 6u, "Subscribe response Ok size 6 (1+1+4)");
	auto parsed = ParseWeatherSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Subscribe Ok parses error");
	if (parsed)
	{
		Assert(parsed->currentKind == 1u, "Subscribe Ok currentKind");
		Assert(FloatNear(parsed->currentIntensity, 0.5f), "Subscribe Ok currentIntensity");
	}
}

static void TestSubscribeResponseOkAllKinds()
{
	for (uint8_t k = 0; k <= 5; ++k)
	{
		auto buf = BuildWeatherSubscribeResponsePayload(0u, k, static_cast<float>(k) * 0.2f);
		auto parsed = ParseWeatherSubscribeResponsePayload(buf.data(), buf.size());
		Assert(parsed.has_value() && parsed->error == 0u && parsed->currentKind == k,
			"Subscribe Ok kind 0..5");
		if (parsed)
		{
			Assert(FloatNear(parsed->currentIntensity, static_cast<float>(k) * 0.2f),
				"Subscribe Ok intensity matches");
		}
	}
}

static void TestSubscribeResponseUnknownZone()
{
	auto buf = BuildWeatherSubscribeResponsePayload(
		static_cast<uint8_t>(WeatherErrorCode::UnknownZone), 0u, 0.0f);
	Assert(buf.size() == 1u, "Subscribe response error UnknownZone size 1 (no body)");
	auto parsed = ParseWeatherSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 1u, "Subscribe UnknownZone");
}

static void TestSubscribeResponseUnauthorized()
{
	auto buf = BuildWeatherSubscribeResponsePayload(
		static_cast<uint8_t>(WeatherErrorCode::Unauthorized), 0u, 0.0f);
	auto parsed = ParseWeatherSubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 3u, "Subscribe Unauthorized");
}

static void TestSubscribeResponsePacket()
{
	auto pkt = BuildWeatherSubscribeResponsePacket(0u, 5u, 0.3f, 12345u, 0xBEEFull);
	Assert(!pkt.empty(), "Subscribe packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Subscribe PacketView parse OK");
	Assert(view.Opcode() == kOpcodeWeatherSubscribeResponse, "Opcode is SubscribeResponse");
	Assert(view.RequestId() == 12345u, "Subscribe RequestId preserved");
	auto parsed = ParseWeatherSubscribeResponsePayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->error == 0u && parsed->currentKind == 5u
		&& FloatNear(parsed->currentIntensity, 0.3f),
		"Subscribe Ok packet body decodes");
}

// -----------------------------------------------------------------------------
// WEATHER_UNSUBSCRIBE
// -----------------------------------------------------------------------------

static void TestUnsubscribeRequestRoundTrip()
{
	auto buf = BuildWeatherUnsubscribeRequestPayload(2u);
	Assert(buf.size() == 4u, "Unsubscribe request payload size 4");
	auto parsed = ParseWeatherUnsubscribeRequestPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->zoneId == 2u, "Unsubscribe request parses zoneId 2");
}

static void TestUnsubscribeRequestRejectsShort()
{
	uint8_t shortBuf[3]{};
	auto parsed = ParseWeatherUnsubscribeRequestPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed.has_value(), "Unsubscribe request rejects 3 bytes");
}

static void TestUnsubscribeResponseOk()
{
	auto buf = BuildWeatherUnsubscribeResponsePayload(0u);
	Assert(buf.size() == 1u, "Unsubscribe response Ok size 1");
	auto parsed = ParseWeatherUnsubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 0u, "Unsubscribe Ok parses");
}

static void TestUnsubscribeResponseNotSubscribed()
{
	auto buf = BuildWeatherUnsubscribeResponsePayload(
		static_cast<uint8_t>(WeatherErrorCode::NotSubscribed));
	auto parsed = ParseWeatherUnsubscribeResponsePayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->error == 2u, "Unsubscribe NotSubscribed");
}

static void TestUnsubscribeResponsePacket()
{
	auto pkt = BuildWeatherUnsubscribeResponsePacket(0u, 99u, 0xFEEDull);
	Assert(!pkt.empty(), "Unsubscribe packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Unsubscribe PacketView parse OK");
	Assert(view.Opcode() == kOpcodeWeatherUnsubscribeResponse, "Opcode is UnsubscribeResponse");
	Assert(view.RequestId() == 99u, "Unsubscribe RequestId preserved");
}

// -----------------------------------------------------------------------------
// WEATHER_UPDATE_NOTIFICATION (push)
// -----------------------------------------------------------------------------

static void TestUpdateNotificationRoundTrip()
{
	auto buf = BuildWeatherUpdateNotificationPayload(1u, 1u, 0.5f);
	Assert(buf.size() == 9u, "Update payload size 9");
	auto parsed = ParseWeatherUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value(), "Update parses");
	if (parsed)
	{
		Assert(parsed->zoneId == 1u, "Update zoneId");
		Assert(parsed->kind == 1u, "Update kind Rain");
		Assert(FloatNear(parsed->intensity, 0.5f), "Update intensity 0.5");
	}
}

static void TestUpdateNotificationZeroIntensity()
{
	auto buf = BuildWeatherUpdateNotificationPayload(2u, 0u, 0.0f);
	auto parsed = ParseWeatherUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && FloatNear(parsed->intensity, 0.0f) && parsed->kind == 0u,
		"Update Clear intensity 0");
}

static void TestUpdateNotificationFullIntensity()
{
	auto buf = BuildWeatherUpdateNotificationPayload(3u, 5u, 1.0f);
	auto parsed = ParseWeatherUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && FloatNear(parsed->intensity, 1.0f) && parsed->kind == 5u,
		"Update Fog intensity 1.0");
}

static void TestUpdateNotificationAllKinds()
{
	// Clear=0, Rain=1, Snow=2, Storm=3, Sandstorm=4, Fog=5
	for (uint8_t k = 0; k <= 5; ++k)
	{
		auto buf = BuildWeatherUpdateNotificationPayload(10u + k, k, 0.42f);
		auto parsed = ParseWeatherUpdateNotificationPayload(buf.data(), buf.size());
		Assert(parsed.has_value() && parsed->kind == k && parsed->zoneId == 10u + k,
			"Update kind round-trip 0..5");
	}
}

static void TestUpdateNotificationKindByteIsRaw()
{
	// Le wire ne valide pas la valeur de kind ; le parser doit accepter
	// une valeur "invalide" (e.g. 99u) — la validation est cote presenter.
	auto buf = BuildWeatherUpdateNotificationPayload(1u, 99u, 0.1f);
	auto parsed = ParseWeatherUpdateNotificationPayload(buf.data(), buf.size());
	Assert(parsed.has_value() && parsed->kind == 99u,
		"Update accepts raw uint8 kind 99 (validation is presenter-side)");
}

static void TestUpdateNotificationRejectsShort()
{
	auto parsed = ParseWeatherUpdateNotificationPayload(nullptr, 9u);
	Assert(!parsed.has_value(), "Update rejects nullptr");
	uint8_t shortBuf[8]{};
	auto parsed2 = ParseWeatherUpdateNotificationPayload(shortBuf, sizeof(shortBuf));
	Assert(!parsed2.has_value(), "Update rejects 8 bytes");
}

static void TestUpdateNotificationPacket()
{
	auto pkt = BuildWeatherUpdateNotificationPacket(1u, 3u, 0.6f, 0xFADEull);
	Assert(!pkt.empty(), "Update packet built");
	PacketView view;
	Assert(PacketView::Parse(pkt.data(), pkt.size(), view) == PacketParseResult::Ok,
		"Update PacketView parse OK");
	Assert(view.Opcode() == kOpcodeWeatherUpdateNotification, "Opcode is UpdateNotification");
	Assert(view.RequestId() == 0u, "Push requestId=0");
	Assert(view.SessionId() == 0xFADEull, "Update SessionId preserved");
	auto parsed = ParseWeatherUpdateNotificationPayload(view.Payload(), view.PayloadSize());
	Assert(parsed.has_value() && parsed->zoneId == 1u && parsed->kind == 3u
		&& FloatNear(parsed->intensity, 0.6f),
		"Update payload decodes");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

int main()
{
	TestListRequestRoundTrip();
	TestListResponseEmpty();
	TestListResponseSingleZone();
	TestListResponseFullThreeZones();
	TestListResponseError();
	TestListResponseRejectsShort();
	TestListResponsePacket();

	TestSubscribeRequestRoundTrip();
	TestSubscribeRequestBoundary();
	TestSubscribeRequestRejectsShort();
	TestSubscribeResponseOk();
	TestSubscribeResponseOkAllKinds();
	TestSubscribeResponseUnknownZone();
	TestSubscribeResponseUnauthorized();
	TestSubscribeResponsePacket();

	TestUnsubscribeRequestRoundTrip();
	TestUnsubscribeRequestRejectsShort();
	TestUnsubscribeResponseOk();
	TestUnsubscribeResponseNotSubscribed();
	TestUnsubscribeResponsePacket();

	TestUpdateNotificationRoundTrip();
	TestUpdateNotificationZeroIntensity();
	TestUpdateNotificationFullIntensity();
	TestUpdateNotificationAllKinds();
	TestUpdateNotificationKindByteIsRaw();
	TestUpdateNotificationRejectsShort();
	TestUpdateNotificationPacket();

	std::cerr << (s_failCount == 0 ? "[OK] all weather payload tests passed\n"
	                                : "[FAIL] some weather tests failed\n");
	return s_failCount == 0 ? 0 : 1;
}
