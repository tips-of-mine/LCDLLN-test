// Tests du cycle jour/nuit existant + extension lunaire. Pas de framework,
// asserts simples. Utilise pour valider le comportement de DayNightCycle
// avant et apres l'ajout des phases lunaires (Phase 5 Lunar). Le binaire
// est enregistre dans CMakeLists.txt comme cible CTest `daynight_cycle_tests`.

#include "src/client/render/DayNightCycle.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using engine::render::DayNightCycle;

namespace
{
	/// Compare deux floats avec une tolerance \p eps (defaut 0.05f, le cycle
	/// utilise des keyframes lerpees donc les comparaisons restent floues).
	bool NearlyEqual(float a, float b, float eps = 0.05f)
	{
		return std::fabs(a - b) < eps;
	}

	/// A midi (12h), le cycle doit etre en jour avec timeOfDay ~ 12.
	void TestInitNoonState()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		p.initialTimeOfDay = 12.0f;
		p.timeScale = 60.0f;
		cycle.Init(p);

		const auto& s = cycle.GetState();
		assert(NearlyEqual(s.timeOfDay, 12.0f));
		assert(s.isDaytime);
		std::puts("[OK] TestInitNoonState");
	}

	/// A minuit (0h), le cycle doit etre en nuit (isDaytime=false).
	void TestInitMidnightState()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		p.initialTimeOfDay = 0.0f;
		cycle.Init(p);

		const auto& s = cycle.GetState();
		assert(!s.isDaytime);
		std::puts("[OK] TestInitMidnightState");
	}

	/// Au lever (6h) le soleil est proche de l'horizon, a midi (12h) il est
	/// haut, au coucher (18h) il est proche de l'horizon.
	void TestSunriseSunsetTransitions()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.SetTime(6.0f);
		const auto& dawn = cycle.GetState();
		// Au lever du soleil l'elevation est proche de 0 (pas a son zenith).
		assert(std::fabs(dawn.lightDir[1]) < 0.5f);

		cycle.SetTime(12.0f);
		const auto& noon = cycle.GetState();
		assert(noon.lightDir[1] > 0.7f);

		cycle.SetTime(18.0f);
		const auto& dusk = cycle.GetState();
		assert(std::fabs(dusk.lightDir[1]) < 0.5f);
		std::puts("[OK] TestSunriseSunsetTransitions");
	}

	/// SetTime(25h) doit produire un timeOfDay valide dans [0..24).
	void TestSetTimeWraps()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.SetTime(25.0f);
		const auto& s = cycle.GetState();
		assert(s.timeOfDay >= 0.0f && s.timeOfDay < 24.0f);
		std::puts("[OK] TestSetTimeWraps");
	}

	/// SetTimeScale clampe le timeScale a une valeur min positive.
	void TestSetTimeScaleClamp()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.SetTimeScale(0.0f);
		assert(cycle.GetTimeScale() >= 0.1f);
		cycle.SetTimeScale(-100.0f);
		assert(cycle.GetTimeScale() >= 0.1f);
		std::puts("[OK] TestSetTimeScaleClamp");
	}

	/// 60 secondes reelles avec timeScale=60 doit avancer de 1h en jeu.
	void TestAdvanceHour()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		p.initialTimeOfDay = 8.0f;
		p.timeScale = 60.0f; // 1 game hour = 60 real seconds
		cycle.Init(p);

		// 60 secondes reelles -> 1h en jeu -> 9h.
		cycle.Advance(60.0f);
		const auto& s = cycle.GetState();
		assert(NearlyEqual(s.timeOfDay, 9.0f, 0.1f));
		std::puts("[OK] TestAdvanceHour");
	}

	/// Apres Init, moonPhase=0 et moonIllumination=0 (defaut).
	void TestLunarPhaseDefault()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);
		const auto& s = cycle.GetState();
		assert(s.moonPhase == 0);
		assert(NearlyEqual(s.moonIllumination, 0.0f));
		std::puts("[OK] TestLunarPhaseDefault");
	}

	/// OnLunarPhaseChange(7, 1.0) doit bien set les valeurs.
	void TestLunarPhaseUpdate()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		cycle.OnLunarPhaseChange(7, 1.0f);
		const auto& s = cycle.GetState();
		assert(s.moonPhase == 7);
		assert(NearlyEqual(s.moonIllumination, 1.0f));
		std::puts("[OK] TestLunarPhaseUpdate");
	}

	/// OnLunarPhaseChange clampe l'illumination et ignore les phases >= 16.
	void TestLunarPhaseClamp()
	{
		DayNightCycle cycle;
		DayNightCycle::Params p;
		cycle.Init(p);

		// Phase invalide >= 16 ignoree.
		cycle.OnLunarPhaseChange(16, 0.5f);
		const auto& s = cycle.GetState();
		assert(s.moonPhase == 0);

		// Illumination negative clampee a 0.
		cycle.OnLunarPhaseChange(3, -1.0f);
		assert(NearlyEqual(cycle.GetState().moonIllumination, 0.0f));

		// Illumination > 1 clampee a 1.
		cycle.OnLunarPhaseChange(7, 2.5f);
		assert(NearlyEqual(cycle.GetState().moonIllumination, 1.0f));

		std::puts("[OK] TestLunarPhaseClamp");
	}
}

int main()
{
	TestInitNoonState();
	TestInitMidnightState();
	TestSunriseSunsetTransitions();
	TestSetTimeWraps();
	TestSetTimeScaleClamp();
	TestAdvanceHour();
	TestLunarPhaseDefault();
	TestLunarPhaseUpdate();
	TestLunarPhaseClamp();
	std::puts("[ALL OK] DayNightCycleTests");
	return 0;
}
