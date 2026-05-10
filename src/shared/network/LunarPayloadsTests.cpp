// Round-trip tests pour les payloads lunaires + edge cases + reject-short.
// Pas de framework, asserts simples. Le binaire est enregistre dans
// CMakeLists.txt comme cible CTest `lunar_payloads_tests`.

#include "src/shared/network/LunarPayloads.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace engine::network::lunar;

namespace
{
	/// Compare deux floats avec une tolerance \p eps (defaut 1e-5f).
	bool NearlyEqual(float a, float b, float eps = 1e-5f)
	{
		return std::fabs(a - b) < eps;
	}

	/// Round-trip d'un payload vide (StateRequest).
	void TestStateRequestRoundTrip()
	{
		std::vector<uint8_t> buf;
		BuildLunarStateRequestPayload(buf);
		assert(buf.empty());
		LunarStateRequest parsed;
		assert(ParseLunarStateRequestPayload(buf.data(), buf.size(), parsed));
		std::puts("[OK] TestStateRequestRoundTrip");
	}

	/// Round-trip standard d'une StateResponse Ok avec valeurs realistes.
	void TestStateResponseRoundTrip()
	{
		LunarStateResponse src;
		src.status = LunarStatus::Ok;
		src.phase = 7;
		src.illumination = 1.0f;
		src.cycleStartMs = 1767225600000ull; // 2026-01-01 UTC
		src.cycleDurationMs = 1209600000ull; // 14 jours

		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == src.status);
		assert(dst.phase == src.phase);
		assert(NearlyEqual(dst.illumination, src.illumination));
		assert(dst.cycleStartMs == src.cycleStartMs);
		assert(dst.cycleDurationMs == src.cycleDurationMs);
		std::puts("[OK] TestStateResponseRoundTrip");
	}

	/// Round-trip d'une StateResponse Unauthorized (status != Ok).
	void TestStateResponseUnauthorized()
	{
		LunarStateResponse src;
		src.status = LunarStatus::Unauthorized;
		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.status == LunarStatus::Unauthorized);
		std::puts("[OK] TestStateResponseUnauthorized");
	}

	/// Round-trip standard d'une PhaseChangeNotification avec valeurs realistes.
	void TestPhaseChangeRoundTrip()
	{
		LunarPhaseChangeNotification src;
		src.newPhase = 8;
		src.newIllumination = 0.94f;
		src.nextChangeTsMs = 1767246600000ull;

		std::vector<uint8_t> buf;
		BuildLunarPhaseChangeNotificationPayload(src, buf);

		LunarPhaseChangeNotification dst;
		assert(ParseLunarPhaseChangeNotificationPayload(buf.data(), buf.size(), dst));
		assert(dst.newPhase == src.newPhase);
		assert(NearlyEqual(dst.newIllumination, src.newIllumination));
		assert(dst.nextChangeTsMs == src.nextChangeTsMs);
		std::puts("[OK] TestPhaseChangeRoundTrip");
	}

	/// Round-trip pour chacune des 16 phases (couverture exhaustive).
	void TestPhaseChangeAllPhases()
	{
		for (uint8_t p = 0; p < 16; ++p)
		{
			LunarPhaseChangeNotification src;
			src.newPhase = p;
			src.newIllumination = static_cast<float>(p) / 15.0f;
			src.nextChangeTsMs = 100000ull * static_cast<uint64_t>(p + 1);

			std::vector<uint8_t> buf;
			BuildLunarPhaseChangeNotificationPayload(src, buf);

			LunarPhaseChangeNotification dst;
			assert(ParseLunarPhaseChangeNotificationPayload(buf.data(), buf.size(), dst));
			assert(dst.newPhase == p);
		}
		std::puts("[OK] TestPhaseChangeAllPhases");
	}

	/// Reject-short : payload tronque avant la fin doit etre rejete.
	void TestStateResponseRejectShort()
	{
		std::vector<uint8_t> buf = { 0x00, 0x07, 0x00, 0x00, 0x80 }; // tronque
		LunarStateResponse dst;
		assert(!ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestStateResponseRejectShort");
	}

	/// Reject-extra : un octet en trop a la fin du payload doit etre rejete.
	void TestStateResponseRejectExtra()
	{
		LunarStateResponse src;
		src.phase = 5;
		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);
		buf.push_back(0xAA); // octet en trop

		LunarStateResponse dst;
		assert(!ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestStateResponseRejectExtra");
	}

	/// Reject-short pour PhaseChangeNotification.
	void TestPhaseChangeRejectShort()
	{
		std::vector<uint8_t> buf = { 0x07, 0x00 }; // tronque
		LunarPhaseChangeNotification dst;
		assert(!ParseLunarPhaseChangeNotificationPayload(buf.data(), buf.size(), dst));
		std::puts("[OK] TestPhaseChangeRejectShort");
	}

	/// Edge case : valeurs maximales (UINT64_MAX, illumination 1.0, phase 15).
	void TestEdgeCaseMaxValues()
	{
		LunarStateResponse src;
		src.phase = 15;
		src.illumination = 1.0f;
		src.cycleStartMs = UINT64_MAX;
		src.cycleDurationMs = UINT64_MAX;

		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.phase == 15);
		assert(dst.cycleStartMs == UINT64_MAX);
		std::puts("[OK] TestEdgeCaseMaxValues");
	}

	/// Edge case : tous les champs a 0.
	void TestEdgeCaseZero()
	{
		LunarStateResponse src;
		src.phase = 0;
		src.illumination = 0.0f;
		src.cycleStartMs = 0;
		src.cycleDurationMs = 0;

		std::vector<uint8_t> buf;
		BuildLunarStateResponsePayload(src, buf);

		LunarStateResponse dst;
		assert(ParseLunarStateResponsePayload(buf.data(), buf.size(), dst));
		assert(dst.phase == 0);
		std::puts("[OK] TestEdgeCaseZero");
	}
}

int main()
{
	TestStateRequestRoundTrip();
	TestStateResponseRoundTrip();
	TestStateResponseUnauthorized();
	TestPhaseChangeRoundTrip();
	TestPhaseChangeAllPhases();
	TestStateResponseRejectShort();
	TestStateResponseRejectExtra();
	TestPhaseChangeRejectShort();
	TestEdgeCaseMaxValues();
	TestEdgeCaseZero();
	std::puts("[ALL OK] LunarPayloadsTests");
	return 0;
}
