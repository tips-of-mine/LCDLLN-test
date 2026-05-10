// Tests du calcul de phase lunaire : round-trip phase <-> time, edge cases,
// illumination sinusoidale, wrap de cycle. Pas de framework, asserts simples.
// Le binaire est enregistre dans CMakeLists.txt comme cible CTest
// `lunar_calendar_tests`.

#include "src/shardd/world/LunarCalendar.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using engine::server::world::LunarCalendar;
using engine::server::world::LunarPhaseInfo;
using engine::server::world::kDefaultLunarCycleMs;
using engine::server::world::kLunarPhaseCount;

namespace
{
	/// Compare deux floats avec une tolerance \p eps.
	bool NearlyEqual(float a, float b, float eps = 0.001f)
	{
		return std::fabs(a - b) < eps;
	}

	/// A t=0 (debut de cycle), phase doit etre 0 (NewMoon) avec illumination ~0.
	void TestPhase0AtCycleStart()
	{
		auto info = LunarCalendar::Compute(0, 0, kDefaultLunarCycleMs);
		assert(info.phase == 0);
		assert(NearlyEqual(info.illumination, 0.0f));
		std::puts("[OK] TestPhase0AtCycleStart");
	}

	/// A la moitie du cycle, on doit etre a la phase 7 ou 8 (FullMoon ou
	/// FullMoonSetting selon arrondi). On accepte les deux.
	void TestPhase7AtHalfCycle()
	{
		const uint64_t halfCycle = kDefaultLunarCycleMs / 2;
		auto info = LunarCalendar::Compute(halfCycle, 0, kDefaultLunarCycleMs);
		assert(info.phase == 8 || info.phase == 7);
		std::puts("[OK] TestPhase7AtHalfCycle");
	}

	/// Apres un cycle complet, on doit retomber sur la phase 0.
	void TestPhase0AfterFullCycle()
	{
		auto info = LunarCalendar::Compute(kDefaultLunarCycleMs, 0, kDefaultLunarCycleMs);
		assert(info.phase == 0);
		std::puts("[OK] TestPhase0AfterFullCycle");
	}

	/// Pour chaque phase 0..15, le calcul a midPhaseMs doit retourner
	/// exactement cette phase.
	void TestEachPhaseTransition()
	{
		const uint64_t phaseDurationMs = kDefaultLunarCycleMs / kLunarPhaseCount;
		for (uint8_t expectedPhase = 0; expectedPhase < kLunarPhaseCount; ++expectedPhase)
		{
			const uint64_t midPhaseMs = static_cast<uint64_t>(expectedPhase) * phaseDurationMs + phaseDurationMs / 2;
			auto info = LunarCalendar::Compute(midPhaseMs, 0, kDefaultLunarCycleMs);
			if (info.phase != expectedPhase)
			{
				std::printf("[FAIL] expected phase %u got %u at midPhaseMs=%llu\n",
					expectedPhase, info.phase, static_cast<unsigned long long>(midPhaseMs));
				assert(info.phase == expectedPhase);
			}
		}
		std::puts("[OK] TestEachPhaseTransition");
	}

	/// L'illumination est maximale (1.0) a la phase 7 (FullMoon).
	void TestIlluminationMaxAtFullMoon()
	{
		float illum7 = LunarCalendar::ComputeIllumination(7);
		assert(NearlyEqual(illum7, 1.0f));
		std::puts("[OK] TestIlluminationMaxAtFullMoon");
	}

	/// L'illumination est minimale (proche de 0) a la phase 0 (NewMoon).
	void TestIlluminationMinAtNewMoon()
	{
		float illum0 = LunarCalendar::ComputeIllumination(0);
		assert(illum0 < 0.05f);
		std::puts("[OK] TestIlluminationMinAtNewMoon");
	}

	/// L'illumination est symetrique autour de la phase 7 : 6 == 8.
	void TestIlluminationSymmetric()
	{
		float illum6 = LunarCalendar::ComputeIllumination(6);
		float illum8 = LunarCalendar::ComputeIllumination(8);
		assert(NearlyEqual(illum6, illum8));
		std::puts("[OK] TestIlluminationSymmetric");
	}

	/// Apres 100 cycles, la phase doit toujours etre dans [0, 15].
	void TestNoInvalidPhase()
	{
		for (uint64_t k = 0; k < 100; ++k)
		{
			const uint64_t t = k * kDefaultLunarCycleMs + 12345;
			auto info = LunarCalendar::Compute(t, 0, kDefaultLunarCycleMs);
			assert(info.phase < kLunarPhaseCount);
		}
		std::puts("[OK] TestNoInvalidPhase");
	}

	/// Si realNow < cycleStart (clamp anti-underflow), phase = 0.
	void TestRealNowBeforeCycleStart()
	{
		auto info = LunarCalendar::Compute(0, 1000, kDefaultLunarCycleMs);
		assert(info.phase == 0);
		std::puts("[OK] TestRealNowBeforeCycleStart");
	}

	/// cycleDurationMs == 0 retourne {0, 0} (no-op).
	void TestZeroCycleDuration()
	{
		auto info = LunarCalendar::Compute(12345, 0, 0);
		assert(info.phase == 0);
		assert(NearlyEqual(info.illumination, 0.0f));
		std::puts("[OK] TestZeroCycleDuration");
	}

	/// NextChangeTsMs avance correctement entre phases.
	void TestNextChangeTsAdvances()
	{
		const uint64_t phaseDurationMs = kDefaultLunarCycleMs / kLunarPhaseCount;
		uint64_t nowMs = 0;
		uint64_t next = LunarCalendar::NextChangeTsMs(nowMs, 0, kDefaultLunarCycleMs);
		assert(next == phaseDurationMs);
		nowMs = phaseDurationMs / 2;
		next = LunarCalendar::NextChangeTsMs(nowMs, 0, kDefaultLunarCycleMs);
		assert(next == phaseDurationMs);
		std::puts("[OK] TestNextChangeTsAdvances");
	}
}

int main()
{
	TestPhase0AtCycleStart();
	TestPhase7AtHalfCycle();
	TestPhase0AfterFullCycle();
	TestEachPhaseTransition();
	TestIlluminationMaxAtFullMoon();
	TestIlluminationMinAtNewMoon();
	TestIlluminationSymmetric();
	TestNoInvalidPhase();
	TestRealNowBeforeCycleStart();
	TestZeroCycleDuration();
	TestNextChangeTsAdvances();
	std::puts("[ALL OK] LunarCalendarTests");
	return 0;
}
